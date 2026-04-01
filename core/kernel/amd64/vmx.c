/*
 * FILE: vmx.c
 *
 * Intel VT-x (VMX) hardware-assisted virtualization for amd64
 *
 * Provides: VMX initialization, VMCS setup, EPT management,
 *           VM-exit handling, and virtual device emulation (PIT/PIC/UART).
 *
 * www.prtos.org
 */

#ifdef CONFIG_VMX

#include <assert.h>
#include <boot.h>
#include <kthread.h>
#include <physmm.h>
#include <prtosconf.h>
#include <rsvmem.h>
#include <sched.h>
#include <stdc.h>
#include <vmmap.h>
#include <arch/asm.h>
#include <arch/io.h>
#include <arch/paging.h>
#include <arch/segments.h>
#include <arch/vmx.h>
#include <arch/apic.h>

/* Per-CPU VMXON region physical address */
static prtos_u64_t vmxon_region_phys[CONFIG_NO_CPUS];
static void *vmxon_region_virt[CONFIG_NO_CPUS];
static int vmx_enabled = 0;

/* Shared MSR bitmap (allocated via GET_MEMAZ) */
static prtos_u8_t *vmx_msr_bitmap;
static int vmx_msr_bitmap_initialized = 0;

/* Forward declarations */
static void vmx_handle_io_exit(kthread_t *k, struct vmx_state *vmx);
static void vmx_handle_cpuid_exit(struct vmx_state *vmx);
static void vmx_handle_hlt_exit(kthread_t *k, struct vmx_state *vmx);
static void vmx_check_pending_irq(struct vmx_state *vmx);
static void vpit_tick(struct vmx_state *vmx, prtos_u64_t now_us);
static void vpic_raise_irq(struct vpic_state *vpic, int irq);

/* Queue an IPI vector for delivery to a target vCPU.
 * Uses a 32-bit bitmap covering vectors 0xE0-0xFF (bit N = vector 0xE0+N).
 * Called from the sending vCPU's MTF handler. */
static void vmx_queue_ipi(struct vmx_state *target_vmx, prtos_u8_t vector) {
    if (vector >= 0xE0) {
        __sync_fetch_and_or(&target_vmx->ipi_pending_bitmap, 1U << (vector - 0xE0));
    }
    /* Vectors below 0xE0 are not typical IPIs and are silently dropped */
}

/* Global PIC injection counters for heartbeat debug */
prtos_u32_t g_pic_inj_cnt = 0, g_pic_defer_cnt = 0, g_pic_busy_cnt = 0;
static prtos_u32_t g_lapic_inj_cnt = 0;
static prtos_u32_t g_vuart_tx_cnt = 0;
static prtos_u32_t g_vuart_irq4_cnt = 0;
static prtos_u32_t g_vmentry_cnt = 0;

/* Check and inject any pending IPI into the guest.
 * Called before VMLAUNCH/VMRESUME for every vCPU. */
static void vmx_inject_pending_ipi(struct vmx_state *vmx) {
    prtos_u32_t bitmap = *(volatile prtos_u32_t *)&vmx->ipi_pending_bitmap;
    if (!bitmap)
        return;

    /* Check if guest can accept an interrupt */
    prtos_u64_t guest_rflags = vmx_vmread(VMCS_GUEST_RFLAGS);
    prtos_u32_t interruptibility = (prtos_u32_t)vmx_vmread(VMCS_GUEST_INTERRUPTIBILITY);
    prtos_u64_t entry_info = vmx_vmread(VMCS_ENTRY_INTR_INFO);

    if (!(guest_rflags & _CPU_FLAG_IF) || (interruptibility & 0x3) ||
        (entry_info & VMCS_INTR_INFO_VALID)) {
        /* Guest not ready — request interrupt-window exit */
        prtos_u64_t proc = vmx_vmread(VMCS_PROC_BASED_EXEC_CTRL);
        if (!(proc & CPU_BASED_VIRTUAL_INTR_PENDING)) {
            proc |= CPU_BASED_VIRTUAL_INTR_PENDING;
            vmx_vmwrite(VMCS_PROC_BASED_EXEC_CTRL, proc);
        }
        return;
    }

    /* Find lowest set bit (highest priority pending IPI) */
    int bit = __builtin_ctz(bitmap);
    prtos_u32_t vector = 0xE0 + bit;

    /* Atomically clear this bit */
    __sync_fetch_and_and(&vmx->ipi_pending_bitmap, ~(1U << bit));

    /* Inject it */
    vmx_vmwrite(VMCS_ENTRY_INTR_INFO,
                VMCS_INTR_INFO_VALID | VMCS_INTR_TYPE_EXT_INTR | vector);
}
static int  vpic_get_pending_vector(struct vmx_state *vmx);
static void vuart_poll_host(struct vmx_state *vmx);
static prtos_u32_t vlapic_timer_get_divisor(prtos_u32_t divide_config);
static void vlapic_timer_write(struct vmx_state *vmx, prtos_u32_t offset, prtos_u32_t value);
static void vlapic_timer_tick(struct vmx_state *vmx, prtos_u64_t now_us);
static int  vlapic_timer_inject(struct vmx_state *vmx);

/* Virtual LAPIC timer frequency — must match the host bus clock for
 * consistent calibration against PM-Timer/HPET/TSC references.
 * On modern Intel CPUs (Skylake+), the bus clock is typically 24 MHz
 * (crystal frequency). The LAPIC timer ticks at bus_clock / divisor.
 * However, the kernel calibrates the LAPIC timer against a reference
 * (PM-Timer or HPET), so our virtual rate must agree with wall-clock time.
 * We use a high value (~1GHz) to give good resolution, and the guest
 * will measure the actual rate during calibration. */
#define VLAPIC_TIMER_FREQ_HZ 1000000000ULL  /* 1 GHz for high resolution */

/* External: assembly VM entry points */
extern int vmx_guest_enter(struct vmx_state *vmx);
extern int vmx_first_launch(struct vmx_state *vmx);
extern void vmx_vmexit_handler(void);

/* External: timer support */
extern hw_time_t get_sys_clock_usec(void);

/* ================================================================
 * VMX Initialization
 * ================================================================ */

static prtos_u32_t vmx_get_revision_id(void) {
    return (prtos_u32_t)(read_msr(MSR_IA32_VMX_BASIC) & 0x7FFFFFFF);
}

static prtos_u64_t vmx_adjust_controls(prtos_u64_t ctl, prtos_u32_t msr) {
    prtos_u64_t msr_val = read_msr(msr);
    /* bits 31:0 = allowed 0-settings, bits 63:32 = allowed 1-settings */
    ctl &= (prtos_u32_t)(msr_val >> 32); /* clear bits that must be 0 */
    ctl |= (prtos_u32_t)(msr_val);       /* set bits that must be 1 */
    return ctl;
}

int vmx_init(void) {
    prtos_u32_t eax, ebx, ecx, edx;
    prtos_u64_t feature_ctl;
    prtos_u32_t rev_id;
    int cpuid = GET_CPU_ID();
    void *region;

    /* Check CPUID.1:ECX.VMX (bit 5) */
    cpu_id(1, &eax, &ebx, &ecx, &edx);
    if (!(ecx & (1 << 5))) {
        kprintf("[VMX] CPU does not support VMX\n");
        return -1;
    }

    /* Check IA32_FEATURE_CONTROL */
    feature_ctl = read_msr(MSR_IA32_FEATURE_CONTROL);
    if (feature_ctl & FEATURE_CONTROL_LOCKED) {
        if (!(feature_ctl & FEATURE_CONTROL_VMXON)) {
            kprintf("[VMX] VMX locked out by BIOS\n");
            return -1;
        }
    } else {
        /* Set enable bit and lock */
        write_msr(MSR_IA32_FEATURE_CONTROL,
                  (prtos_u32_t)(feature_ctl | FEATURE_CONTROL_LOCKED | FEATURE_CONTROL_VMXON),
                  (prtos_u32_t)((feature_ctl | FEATURE_CONTROL_LOCKED | FEATURE_CONTROL_VMXON) >> 32));
    }

    /* Check for EPT and unrestricted guest support */
    {
        prtos_u64_t proc2_msr = read_msr(MSR_IA32_VMX_PROCBASED_CTLS2);
        prtos_u32_t allowed1 = (prtos_u32_t)(proc2_msr >> 32);
        if (!(allowed1 & SECONDARY_EXEC_ENABLE_EPT)) {
            kprintf("[VMX] EPT not supported\n");
            return -1;
        }
        if (!(allowed1 & SECONDARY_EXEC_UNRESTRICTED)) {
            kprintf("[VMX] Unrestricted guest not supported\n");
            return -1;
        }
    }

    /* Allocate VMXON region for this CPU from reserved memory pool */
    GET_MEMAZ(region, PAGE_SIZE, PAGE_SIZE);
    vmxon_region_virt[cpuid] = region;
    vmxon_region_phys[cpuid] = _VIRT2PHYS((prtos_u64_t)(unsigned long)region);

    /* Write revision ID */
    rev_id = vmx_get_revision_id();
    *(prtos_u32_t *)region = rev_id;

    /* Enable VMX in CR4 */
    {
        prtos_u64_t cr4 = save_cr4();
        load_cr4(cr4 | _CR4_VMXE);
    }

    /* Ensure CR0/CR4 satisfy VMX fixed bits */
    {
        prtos_u64_t cr0_fixed0 = read_msr(MSR_IA32_VMX_CR0_FIXED0);
        prtos_u64_t cr0_fixed1 = read_msr(MSR_IA32_VMX_CR0_FIXED1);
        prtos_u64_t cr0 = save_cr0();
        cr0 |= (prtos_u32_t)cr0_fixed0;
        cr0 &= (prtos_u32_t)cr0_fixed1;
        load_cr0(cr0);

        prtos_u64_t cr4_fixed0 = read_msr(MSR_IA32_VMX_CR4_FIXED0);
        prtos_u64_t cr4_fixed1 = read_msr(MSR_IA32_VMX_CR4_FIXED1);
        prtos_u64_t cr4 = save_cr4();
        cr4 |= (prtos_u32_t)cr4_fixed0;
        cr4 &= (prtos_u32_t)cr4_fixed1;
        load_cr4(cr4);
    }

    /* Execute VMXON */
    if (vmx_vmxon(&vmxon_region_phys[cpuid])) {
        kprintf("[VMX] VMXON failed\n");
        return -1;
    }

    vmx_enabled = 1;
    kprintf("[VMX] VMX enabled on CPU %d (rev %d)\n", cpuid, rev_id);
    return 0;
}

/* Initialize VMX on a secondary (AP) CPU.
 * Called from setup_arch_local() on each AP after the BSP has verified
 * VMX support via vmx_init(). Each AP needs its own VMXON region. */
int vmx_init_ap(void) {
    void *region;
    prtos_u32_t rev_id;
    prtos_s32_t cpuid = GET_CPU_ID();

    if (!vmx_enabled) return 0;  /* VMX not enabled on BSP */

    /* Enable VMX outside SMX in IA32_FEATURE_CONTROL */
    {
        prtos_u64_t feature_ctl = read_msr(MSR_IA32_FEATURE_CONTROL);
        if (feature_ctl & FEATURE_CONTROL_LOCKED) {
            if (!(feature_ctl & FEATURE_CONTROL_VMXON))
                return -1;
        } else {
            write_msr(MSR_IA32_FEATURE_CONTROL,
                      (prtos_u32_t)(feature_ctl | FEATURE_CONTROL_LOCKED | FEATURE_CONTROL_VMXON),
                      (prtos_u32_t)((feature_ctl | FEATURE_CONTROL_LOCKED | FEATURE_CONTROL_VMXON) >> 32));
        }
    }

    /* Allocate VMXON region */
    GET_MEMAZ(region, PAGE_SIZE, PAGE_SIZE);
    vmxon_region_virt[cpuid] = region;
    vmxon_region_phys[cpuid] = _VIRT2PHYS((prtos_u64_t)(unsigned long)region);

    rev_id = vmx_get_revision_id();
    *(prtos_u32_t *)region = rev_id;

    /* Enable VMX in CR4 */
    {
        prtos_u64_t cr4 = save_cr4();
        load_cr4(cr4 | _CR4_VMXE);
    }

    /* Ensure CR0/CR4 satisfy VMX fixed bits */
    {
        prtos_u64_t cr0_fixed0 = read_msr(MSR_IA32_VMX_CR0_FIXED0);
        prtos_u64_t cr0_fixed1 = read_msr(MSR_IA32_VMX_CR0_FIXED1);
        prtos_u64_t cr0 = save_cr0();
        cr0 |= (prtos_u32_t)cr0_fixed0;
        cr0 &= (prtos_u32_t)cr0_fixed1;
        load_cr0(cr0);

        prtos_u64_t cr4_fixed0 = read_msr(MSR_IA32_VMX_CR4_FIXED0);
        prtos_u64_t cr4_fixed1 = read_msr(MSR_IA32_VMX_CR4_FIXED1);
        prtos_u64_t cr4 = save_cr4();
        cr4 |= (prtos_u32_t)cr4_fixed0;
        cr4 &= (prtos_u32_t)cr4_fixed1;
        load_cr4(cr4);
    }

    /* Execute VMXON */
    if (vmx_vmxon(&vmxon_region_phys[cpuid])) {
        kprintf("[VMX] VMXON failed on CPU %d\n", cpuid);
        return -1;
    }

    kprintf("[VMX] VMX enabled on AP CPU %d\n", cpuid);
    return 0;
}

int vmx_is_enabled(void) {
    return vmx_enabled;
}

/* ================================================================
 * EPT Setup
 * ================================================================ */

static void vmx_setup_ept(struct vmx_partition_shared *shared,
                          struct prtos_conf_memory_area *mem_areas,
                          prtos_s32_t num_areas,
                          void *pml4_page, void *pdpt_page, void *pd_page) {
    prtos_u64_t *pml4 = (prtos_u64_t *)pml4_page;
    prtos_u64_t *pdpt = (prtos_u64_t *)pdpt_page;
    prtos_u64_t *pd = (prtos_u64_t *)pd_page;
    prtos_u64_t addr;
    prtos_s32_t i;

    /* Determine if this partition needs the full 1GB identity map + LAPIC/IOAPIC.
     * Linux partitions need this; FreeRTOS/bare-metal typically don't.
     * Heuristic: if total memory >= 128MB, enable full mapping. */
    prtos_u64_t total_mem = 0;
    for (i = 0; i < num_areas; i++)
        total_mem += mem_areas[i].size;
    int needs_full_map = (total_mem >= 0x8000000ULL);  /* 128MB */

    memset(pml4, 0, PAGE_SIZE);
    shared->ept_pml4 = pml4;

    memset(pdpt, 0, PAGE_SIZE);
    memset(pd, 0, PAGE_SIZE);

    /* PML4[0] → PDPT (all areas assumed in first 512GB) */
    pml4[0] = _VIRT2PHYS((prtos_u64_t)(unsigned long)pdpt) | EPT_RWX;

    /* PDPT[0] → PD for first 1GB */
    pdpt[0] = _VIRT2PHYS((prtos_u64_t)(unsigned long)pd) | EPT_RWX;

    /* Map all partition memory areas using 2MB pages: GPA = HPA (identity map) */
    for (i = 0; i < num_areas; i++) {
        prtos_u64_t phys_start = mem_areas[i].start_addr;
        prtos_u64_t size = mem_areas[i].size;
        for (addr = phys_start; addr < phys_start + size; addr += LPAGE_SIZE) {
            int pd_idx = (addr >> 21) & 0x1FF;
            pd[pd_idx] = (addr & ~(LPAGE_SIZE - 1)) | EPT_RWX | EPT_MEM_TYPE_WB | EPT_IGNORE_PAT | EPT_LARGE_PAGE;
        }
    }

    if (needs_full_map) {
        prtos_u64_t *pd_high;

        /* Allocate PD for 3-4GB range to map LAPIC (0xFEE00000) and IOAPIC (0xFEC00000) */
        GET_MEMAZ(pd_high, PAGE_SIZE, PAGE_SIZE);
        memset(pd_high, 0, PAGE_SIZE);
        pdpt[3] = _VIRT2PHYS((prtos_u64_t)(unsigned long)pd_high) | EPT_RWX;

        /* Map the entire first 1GB (all 512 PD entries) as identity mapped. */
        for (i = 0; i < 512; i++) {
            if (!pd[i]) {
                pd[i] = ((prtos_u64_t)i << 21) | EPT_RWX | EPT_MEM_TYPE_WB | EPT_IGNORE_PAT | EPT_LARGE_PAGE;
            }
        }

        /* Identity-map all 2MB pages in 3-4GB range as UC (PCI MMIO, IOAPIC, etc.)
         * LAPIC page and IOAPIC pages are overwritten below with specific entries. */
        for (i = 0; i < 512; i++) {
            prtos_u64_t gpa = 0xC0000000ULL + ((prtos_u64_t)i << 21);
            pd_high[i] = gpa | EPT_RWX | EPT_MEM_TYPE_UC | EPT_IGNORE_PAT | EPT_LARGE_PAGE;
        }

        /* Map IOAPIC (0xFEC00000) as 2MB large page (identity, UC) — already done above */

        /* Map LAPIC 2MB region via a 4KB page table so we can redirect
         * the LAPIC page (GPA 0xFEE00000) to the APIC-access page.
         * The VMX APIC-access page feature requires that the EPT-translated
         * HPA matches VMCS_APIC_ACCESS_ADDR for exits to fire. */
        {
            void *apic_page;
            prtos_u64_t *lapic_pt;
            int lapic_pd_idx = (0xFEE00000 >> 21) & 0x1FF;
            prtos_u64_t lapic_2mb_base = 0xFEE00000ULL & ~(LPAGE_SIZE - 1);

            /* Allocate APIC-access page */
            GET_MEMAZ(apic_page, PAGE_SIZE, PAGE_SIZE);
            memset(apic_page, 0, PAGE_SIZE);
            shared->apic_access_page = apic_page;
            shared->apic_access_page_phys = _VIRT2PHYS((prtos_u64_t)(unsigned long)apic_page);

            /* Allocate PT for the 2MB region containing the LAPIC */
            GET_MEMAZ(lapic_pt, PAGE_SIZE, PAGE_SIZE);
            memset(lapic_pt, 0, PAGE_SIZE);

            /* Fill PT: identity-map all 512 4KB pages as UC */
            for (i = 0; i < 512; i++) {
                lapic_pt[i] = (lapic_2mb_base + ((prtos_u64_t)i << 12)) |
                              EPT_RWX | EPT_MEM_TYPE_UC | EPT_IGNORE_PAT;
            }

            /* Override the LAPIC 4KB entry to point to the APIC-access page */
            {
                int lapic_pt_idx = (0xFEE00000 >> 12) & 0x1FF;
                lapic_pt[lapic_pt_idx] = shared->apic_access_page_phys |
                                         EPT_RWX | EPT_MEM_TYPE_UC | EPT_IGNORE_PAT;
            }

            /* PD entry points to the PT (not a large page) */
            pd_high[lapic_pd_idx] = _VIRT2PHYS((prtos_u64_t)(unsigned long)lapic_pt) | EPT_RWX;
        }
    }

    /* Set EPTP: WB memory type, 4-level walk, PML4 physical address */
    shared->eptp = _VIRT2PHYS((prtos_u64_t)(unsigned long)pml4) | EPTP_WB | EPTP_WALK_4;
}

/* ================================================================
 * VMCS Setup helpers
 * ================================================================ */

/* Initialize VMCS execution, exit, entry controls and host state.
 * Called for both primary (vCPU 0) and secondary vCPUs. */
static void vmx_init_vmcs_controls(struct vmx_state *vmx, kthread_t *k) {
    /* ---- VM-execution controls ---- */
    {
        prtos_u64_t pin_based = PIN_BASED_EXT_INTR_MASK | PIN_BASED_NMI_EXITING | PIN_BASED_VMX_PREEMPTION;
        pin_based = vmx_adjust_controls(pin_based, MSR_IA32_VMX_TRUE_PINBASED);
        vmx_vmwrite(VMCS_PIN_BASED_EXEC_CTRL, pin_based);
    }

    {
        prtos_u64_t proc_based = CPU_BASED_HLT_EXITING |
                                  CPU_BASED_UNCOND_IO_EXITING |
                                  CPU_BASED_USE_MSR_BITMAPS |
                                  CPU_BASED_ACTIVATE_SEC_CTLS;
        proc_based = vmx_adjust_controls(proc_based, MSR_IA32_VMX_TRUE_PROCBASED);
        vmx_vmwrite(VMCS_PROC_BASED_EXEC_CTRL, proc_based);
    }

    {
        prtos_u64_t proc2 = SECONDARY_EXEC_ENABLE_EPT | SECONDARY_EXEC_ENABLE_RDTSCP | SECONDARY_EXEC_UNRESTRICTED;
        /* Enable APIC-access page if the partition has an APIC-access page allocated
         * (i.e., it's a multi-vCPU partition needing INIT/SIPI emulation) */
        if (vmx->shared && vmx->shared->apic_access_page) {
            proc2 |= SECONDARY_EXEC_VIRT_APIC_ACCESS;
        }
        proc2 = vmx_adjust_controls(proc2, MSR_IA32_VMX_PROCBASED_CTLS2);
        vmx_vmwrite(VMCS_SECONDARY_EXEC_CTRL, proc2);
    }

    /* Exception bitmap: don't trap any exceptions (let guest handle them) */
    vmx_vmwrite(VMCS_EXCEPTION_BITMAP, 0);

    /* APIC-access address */
    if (vmx->shared && vmx->shared->apic_access_page) {
        vmx_vmwrite(VMCS_APIC_ACCESS_ADDR, vmx->shared->apic_access_page_phys);
    }

    /* MSR bitmap */
    if (!vmx_msr_bitmap_initialized) {
        GET_MEMAZ(vmx_msr_bitmap, PAGE_SIZE, PAGE_SIZE);
        {
            /* Trap RDMSR/WRMSR for IA32_EFER (0xC0000080) */
            int efer_byte_off = 0x80 / 8;
            int efer_bit = 0x80 % 8;
            vmx_msr_bitmap[1024 + efer_byte_off] |= (1 << efer_bit);
            vmx_msr_bitmap[3072 + efer_byte_off] |= (1 << efer_bit);

            /* Trap RDMSR/WRMSR for IA32_TSC_DEADLINE (0x6E0) —
             * we hide TSC-deadline timer in CPUID, but guard against stale writes */
            {
                int tsc_dl_byte = 0x6E0 / 8;
                int tsc_dl_bit = 0x6E0 % 8;
                vmx_msr_bitmap[tsc_dl_byte] |= (1 << tsc_dl_bit);        /* RDMSR low */
                vmx_msr_bitmap[2048 + tsc_dl_byte] |= (1 << tsc_dl_bit); /* WRMSR low */
            }
        }
        vmx_msr_bitmap_initialized = 1;
    }
    vmx_vmwrite(VMCS_MSR_BITMAP, _VIRT2PHYS((prtos_u64_t)(unsigned long)vmx_msr_bitmap));

    /* EPT pointer */
    vmx_vmwrite(VMCS_EPT_POINTER, vmx->eptp);

    /* ---- VM-exit controls ---- */
    {
        prtos_u64_t exit_ctls = VM_EXIT_HOST_ADDR_SPACE_SIZE | VM_EXIT_ACK_INTR_ON_EXIT |
                                VM_EXIT_SAVE_PREEMPT_TIMER | VM_EXIT_SAVE_IA32_EFER | VM_EXIT_LOAD_IA32_EFER;
        exit_ctls = vmx_adjust_controls(exit_ctls, MSR_IA32_VMX_TRUE_EXIT);
        vmx_vmwrite(VMCS_EXIT_CONTROLS, exit_ctls);
    }

    /* ---- VM-entry controls ---- */
    {
        prtos_u64_t entry_ctls = VM_ENTRY_LOAD_IA32_EFER;
        entry_ctls = vmx_adjust_controls(entry_ctls, MSR_IA32_VMX_TRUE_ENTRY);
        vmx_vmwrite(VMCS_ENTRY_CONTROLS, entry_ctls);
    }

    /* ---- VMCS link pointer ---- */
    vmx_vmwrite(VMCS_GUEST_VMCS_LINK_PTR, 0xFFFFFFFFFFFFFFFFULL);

    /* CR0/CR4 guest/host masks and read shadows */
    vmx_vmwrite(0x6000, _CR0_NE | _CR0_PG);
    vmx_vmwrite(0x6002, _CR4_VMXE);
    vmx_vmwrite(0x6004, _CR0_PE | _CR0_ET);
    vmx_vmwrite(0x6006, 0);

    /* ---- Host state: 64-bit kernel mode ---- */
    vmx_vmwrite(VMCS_HOST_CR0, save_cr0());
    vmx_vmwrite(VMCS_HOST_CR3, save_cr3());
    vmx_vmwrite(VMCS_HOST_CR4, save_cr4());

    vmx_vmwrite(VMCS_HOST_CS_SEL, CS_SEL);
    vmx_vmwrite(VMCS_HOST_SS_SEL, DS_SEL);
    vmx_vmwrite(VMCS_HOST_DS_SEL, DS_SEL);
    vmx_vmwrite(VMCS_HOST_ES_SEL, DS_SEL);
    vmx_vmwrite(VMCS_HOST_FS_SEL, 0);
    vmx_vmwrite(VMCS_HOST_GS_SEL, PERCPU_SEL);
    vmx_vmwrite(VMCS_HOST_TR_SEL, TSS_SEL);

    vmx_vmwrite(VMCS_HOST_IA32_EFER, read_msr(MSR_IA32_EFER));
    vmx_vmwrite(0x2C04, 0);
    vmx_vmwrite(0x2C00, read_msr(0x277));
    vmx_vmwrite(0x4C00, 0);
    vmx_vmwrite(0x6C10, 0);
    vmx_vmwrite(0x6C12, 0);
    vmx_vmwrite(0x6C06, 0);

    vmx_vmwrite(VMCS_HOST_RIP, (prtos_u64_t)(unsigned long)vmx_vmexit_handler);

    /* VMX preemption timer */
    {
        prtos_u64_t vmx_misc = read_msr(0x485);
        prtos_u32_t shift = vmx_misc & 0x1F;
        prtos_u64_t tsc_freq = (prtos_u64_t)cpu_khz * 1000ULL;
        vmx->preempt_timer_val = (prtos_u32_t)((tsc_freq / 1000) >> shift);
        if (vmx->preempt_timer_val == 0) vmx->preempt_timer_val = 1;
        vmx_vmwrite(VMCS_VMX_PREEMPT_TIMER_VALUE, vmx->preempt_timer_val);
    }
}

/* Allocate and initialize a VMCS for a vCPU */
static int vmx_alloc_vmcs(struct vmx_state *vmx) {
    void *vmcs_region;
    prtos_u32_t rev_id;

    GET_MEMAZ(vmcs_region, PAGE_SIZE, PAGE_SIZE);
    vmx->vmcs_virt = vmcs_region;
    vmx->vmcs_phys = _VIRT2PHYS((prtos_u64_t)(unsigned long)vmcs_region);

    rev_id = vmx_get_revision_id();
    *(prtos_u32_t *)vmcs_region = rev_id;

    if (vmx_vmclear(&vmx->vmcs_phys)) {
        kprintf("[VMX] VMCLEAR failed\n");
        return -1;
    }
    if (vmx_vmptrld(&vmx->vmcs_phys)) {
        kprintf("[VMX] VMPTRLD failed\n");
        return -1;
    }
    return 0;
}

/* Set VMCS guest state for 32-bit protected mode entry (vCPU 0 / BSP) */
static void vmx_set_guest_state_bsp(struct vmx_state *vmx, prtos_u64_t entry_point,
                                     prtos_u64_t part_phys_start, prtos_u64_t part_size) {
    /* Guest CR0: PE=1, PG=0 */
    vmx_vmwrite(VMCS_GUEST_CR0, _CR0_PE | _CR0_ET | _CR0_NE);
    vmx_vmwrite(VMCS_GUEST_CR3, 0);

    {
        prtos_u64_t guest_cr4 = 0;
        prtos_u64_t cr4_fixed0 = read_msr(MSR_IA32_VMX_CR4_FIXED0);
        guest_cr4 |= (prtos_u32_t)cr4_fixed0;
        vmx_vmwrite(VMCS_GUEST_CR4, guest_cr4);
    }
    vmx_vmwrite(VMCS_GUEST_DR7, 0x400);

    vmx_vmwrite(VMCS_GUEST_RIP, entry_point);
    vmx_vmwrite(VMCS_GUEST_RSP, part_phys_start + part_size - 16);
    vmx_vmwrite(VMCS_GUEST_RFLAGS, 0x2);

    /* Flat 32-bit segment descriptors */
    vmx_vmwrite(VMCS_GUEST_CS_SEL, 0x08);
    vmx_vmwrite(VMCS_GUEST_CS_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_CS_LIMIT, 0xFFFFFFFF);
    vmx_vmwrite(VMCS_GUEST_CS_ACCESS, VMX_AR_CODE32);

    vmx_vmwrite(VMCS_GUEST_DS_SEL, 0x10);
    vmx_vmwrite(VMCS_GUEST_DS_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_DS_LIMIT, 0xFFFFFFFF);
    vmx_vmwrite(VMCS_GUEST_DS_ACCESS, VMX_AR_DATA32);

    vmx_vmwrite(VMCS_GUEST_ES_SEL, 0x10);
    vmx_vmwrite(VMCS_GUEST_ES_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_ES_LIMIT, 0xFFFFFFFF);
    vmx_vmwrite(VMCS_GUEST_ES_ACCESS, VMX_AR_DATA32);

    vmx_vmwrite(VMCS_GUEST_SS_SEL, 0x10);
    vmx_vmwrite(VMCS_GUEST_SS_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_SS_LIMIT, 0xFFFFFFFF);
    vmx_vmwrite(VMCS_GUEST_SS_ACCESS, VMX_AR_DATA32);

    vmx_vmwrite(VMCS_GUEST_FS_SEL, 0);
    vmx_vmwrite(VMCS_GUEST_FS_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_FS_LIMIT, 0);
    vmx_vmwrite(VMCS_GUEST_FS_ACCESS, VMX_AR_UNUSABLE);

    vmx_vmwrite(VMCS_GUEST_GS_SEL, 0);
    vmx_vmwrite(VMCS_GUEST_GS_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_GS_LIMIT, 0);
    vmx_vmwrite(VMCS_GUEST_GS_ACCESS, VMX_AR_UNUSABLE);

    vmx_vmwrite(VMCS_GUEST_LDTR_SEL, 0);
    vmx_vmwrite(VMCS_GUEST_LDTR_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_LDTR_LIMIT, 0);
    vmx_vmwrite(VMCS_GUEST_LDTR_ACCESS, VMX_AR_UNUSABLE);

    vmx_vmwrite(VMCS_GUEST_TR_SEL, 0);
    vmx_vmwrite(VMCS_GUEST_TR_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_TR_LIMIT, 0xFF);
    vmx_vmwrite(VMCS_GUEST_TR_ACCESS, VMX_AR_TSS32_BUSY);

    vmx_vmwrite(VMCS_GUEST_GDTR_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_GDTR_LIMIT, 0);
    vmx_vmwrite(VMCS_GUEST_IDTR_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_IDTR_LIMIT, 0);

    vmx_vmwrite(VMCS_GUEST_IA32_EFER, 0);
    vmx_vmwrite(VMCS_GUEST_ACTIVITY_STATE, 0);  /* active */
    vmx_vmwrite(VMCS_GUEST_INTERRUPTIBILITY, 0);
    vmx_vmwrite(VMCS_GUEST_SYSENTER_CS, 0);
    vmx_vmwrite(VMCS_GUEST_SYSENTER_ESP, 0);
    vmx_vmwrite(VMCS_GUEST_SYSENTER_EIP, 0);
    vmx_vmwrite(0x2808, 0);
    vmx_vmwrite(0x2804, read_msr(0x277));
    vmx_vmwrite(0x2802, 0);
}

/* Set VMCS guest state for secondary vCPU (wait-for-SIPI) */
static void vmx_set_guest_state_ap(struct vmx_state *vmx) {
    /* Guest CR0: PE=1 (VMX requirement with unrestricted guest) */
    vmx_vmwrite(VMCS_GUEST_CR0, _CR0_PE | _CR0_ET | _CR0_NE);
    vmx_vmwrite(VMCS_GUEST_CR3, 0);

    {
        prtos_u64_t guest_cr4 = 0;
        prtos_u64_t cr4_fixed0 = read_msr(MSR_IA32_VMX_CR4_FIXED0);
        guest_cr4 |= (prtos_u32_t)cr4_fixed0;
        vmx_vmwrite(VMCS_GUEST_CR4, guest_cr4);
    }
    vmx_vmwrite(VMCS_GUEST_DR7, 0x400);

    /* AP starts at address 0 (will be set by SIPI vector later) */
    vmx_vmwrite(VMCS_GUEST_RIP, 0);
    vmx_vmwrite(VMCS_GUEST_RSP, 0);
    vmx_vmwrite(VMCS_GUEST_RFLAGS, 0x2);

    /* Flat 32-bit segments (will be reconfigured on SIPI) */
    vmx_vmwrite(VMCS_GUEST_CS_SEL, 0);
    vmx_vmwrite(VMCS_GUEST_CS_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_CS_LIMIT, 0xFFFF);
    vmx_vmwrite(VMCS_GUEST_CS_ACCESS, VMX_AR_CODE32);

    vmx_vmwrite(VMCS_GUEST_DS_SEL, 0);
    vmx_vmwrite(VMCS_GUEST_DS_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_DS_LIMIT, 0xFFFF);
    vmx_vmwrite(VMCS_GUEST_DS_ACCESS, VMX_AR_DATA32);

    vmx_vmwrite(VMCS_GUEST_ES_SEL, 0);
    vmx_vmwrite(VMCS_GUEST_ES_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_ES_LIMIT, 0xFFFF);
    vmx_vmwrite(VMCS_GUEST_ES_ACCESS, VMX_AR_DATA32);

    vmx_vmwrite(VMCS_GUEST_SS_SEL, 0);
    vmx_vmwrite(VMCS_GUEST_SS_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_SS_LIMIT, 0xFFFF);
    vmx_vmwrite(VMCS_GUEST_SS_ACCESS, VMX_AR_DATA32);

    vmx_vmwrite(VMCS_GUEST_FS_SEL, 0);
    vmx_vmwrite(VMCS_GUEST_FS_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_FS_LIMIT, 0);
    vmx_vmwrite(VMCS_GUEST_FS_ACCESS, VMX_AR_UNUSABLE);

    vmx_vmwrite(VMCS_GUEST_GS_SEL, 0);
    vmx_vmwrite(VMCS_GUEST_GS_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_GS_LIMIT, 0);
    vmx_vmwrite(VMCS_GUEST_GS_ACCESS, VMX_AR_UNUSABLE);

    vmx_vmwrite(VMCS_GUEST_LDTR_SEL, 0);
    vmx_vmwrite(VMCS_GUEST_LDTR_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_LDTR_LIMIT, 0);
    vmx_vmwrite(VMCS_GUEST_LDTR_ACCESS, VMX_AR_UNUSABLE);

    vmx_vmwrite(VMCS_GUEST_TR_SEL, 0);
    vmx_vmwrite(VMCS_GUEST_TR_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_TR_LIMIT, 0xFF);
    vmx_vmwrite(VMCS_GUEST_TR_ACCESS, VMX_AR_TSS32_BUSY);

    vmx_vmwrite(VMCS_GUEST_GDTR_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_GDTR_LIMIT, 0);
    vmx_vmwrite(VMCS_GUEST_IDTR_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_IDTR_LIMIT, 0);

    vmx_vmwrite(VMCS_GUEST_IA32_EFER, 0);
    vmx_vmwrite(VMCS_GUEST_ACTIVITY_STATE, 3);  /* wait-for-SIPI */
    vmx_vmwrite(VMCS_GUEST_INTERRUPTIBILITY, 0);
    vmx_vmwrite(VMCS_GUEST_SYSENTER_CS, 0);
    vmx_vmwrite(VMCS_GUEST_SYSENTER_ESP, 0);
    vmx_vmwrite(VMCS_GUEST_SYSENTER_EIP, 0);
    vmx_vmwrite(0x2808, 0);
    vmx_vmwrite(0x2804, read_msr(0x277));
    vmx_vmwrite(0x2802, 0);
}

int vmx_setup_partition(void *kthread_ptr) {
    kthread_t *k = (kthread_t *)kthread_ptr;
    partition_t *p = get_partition(k);
    struct vmx_state *vmx;
    struct vmx_partition_shared *shared;
    struct prtos_conf_memory_area *mem_areas;
    prtos_s32_t num_areas;
    prtos_u64_t part_phys_start, part_size, entry_point;
    void *ept_pml4, *ept_pdpt, *ept_pd;

    if (!vmx_enabled) return -1;

    /* Allocate shared partition state */
    GET_MEMAZ(shared, sizeof(struct vmx_partition_shared), ALIGNMENT);
    shared->num_vcpus = p->cfg->num_of_vcpus;
    memset(shared->vcpu_vmx, 0, sizeof(shared->vcpu_vmx));

    /* Allocate per-vCPU VMX state */
    GET_MEMAZ(vmx, sizeof(struct vmx_state), ALIGNMENT);
    k->ctrl.g->karch.vmx = vmx;
    vmx->shared = shared;
    vmx->vcpu_id = 0;
    vmx->wait_for_sipi = 0;
    vmx->sipi_received = 0;
    vmx->kthread_ptr = (void *)k;
    shared->vcpu_vmx[0] = vmx;

    /* Get partition physical memory info */
    mem_areas = &prtos_conf_phys_mem_area_table[p->cfg->physical_memory_areas_offset];
    num_areas = p->cfg->num_of_physical_memory_areas;
    part_phys_start = mem_areas[0].start_addr;
    part_size = mem_areas[0].size;

    /* Add identity mapping in hypervisor PD for partition memory */
    {
        extern prtos_address_t _page_tables[];
        prtos_u64_t *pd = (prtos_u64_t *)((unsigned long)_page_tables + 2 * PAGE_SIZE);
        prtos_u64_t addr;
        prtos_s32_t i;
        for (i = 0; i < num_areas; i++) {
            for (addr = mem_areas[i].start_addr; addr < mem_areas[i].start_addr + mem_areas[i].size; addr += LPAGE_SIZE) {
                prtos_u32_t idx = addr >> PD_SHIFT;
                if (idx < 512 && !pd[idx])
                    pd[idx] = addr | _PG_ARCH_PRESENT | _PG_ARCH_RW | _PG_ARCH_PSE;
            }
        }
        load_cr3(save_cr3());
    }

    kprintf("[VMX] EPT setup: num_areas=%d\n", num_areas);
    {
        prtos_s32_t ai;
        for (ai = 0; ai < num_areas; ai++) {
            kprintf("[VMX]   area[%d]: start=0x%llx size=0x%llx\n", ai,
                    (unsigned long long)mem_areas[ai].start_addr,
                    (unsigned long long)mem_areas[ai].size);
        }
    }

    /* Allocate EPT pages */
    GET_MEMAZ(ept_pml4, PAGE_SIZE, PAGE_SIZE);
    GET_MEMAZ(ept_pdpt, PAGE_SIZE, PAGE_SIZE);
    GET_MEMAZ(ept_pd, PAGE_SIZE, PAGE_SIZE);

    /* Setup EPT (stored in shared state) */
    vmx_setup_ept(shared, mem_areas, num_areas, ept_pml4, ept_pdpt, ept_pd);

    /* Copy EPT pointers to per-vCPU state for fast access */
    vmx->eptp = shared->eptp;
    vmx->ept_pml4 = shared->ept_pml4;

    /* Allocate and initialize VMCS */
    if (vmx_alloc_vmcs(vmx) < 0) return -1;

    /* Initialize VMCS controls and host state */
    vmx_init_vmcs_controls(vmx, k);

    /* Set BSP guest state */
    entry_point = prtos_conf_boot_partition_table[p->cfg->id].entry_point;
    p->image_start = (prtos_address_t)part_phys_start;
    vmx_set_guest_state_bsp(vmx, entry_point, part_phys_start, part_size);

    /* Initialize virtual devices in shared state */
    shared->vpit.active = 0;
    shared->vpic.base_vector[0] = 0x20;
    shared->vpic.base_vector[1] = 0x28;
    shared->vpic.imr[0] = 0xFF;
    shared->vpic.imr[1] = 0xFF;
    vmx->launched = 0;
    vmx->ipi_pending_bitmap = 0;

    /* Copy virtual device state to vCPU 0's local copy (for backward compat) */

    kprintf("[VMX] Partition %d vCPU 0 (BSP): VMCS at phys 0x%llx, EPT phys 0x%llx, entry 0x%llx\n",
            p->cfg->id, vmx->vmcs_phys, vmx->eptp & ~0xFFFULL, entry_point);

    return 0;
}

/* Setup a secondary vCPU (AP) with its own VMCS in wait-for-SIPI state */
int vmx_setup_secondary_vcpu(void *kthread_ptr, void *vcpu0_kthread_ptr) {
    kthread_t *k = (kthread_t *)kthread_ptr;
    kthread_t *k0 = (kthread_t *)vcpu0_kthread_ptr;
    struct vmx_state *vmx;
    struct vmx_state *vmx0 = k0->ctrl.g->karch.vmx;
    struct vmx_partition_shared *shared = vmx0->shared;
    partition_t *p = get_partition(k);

    if (!vmx_enabled || !vmx0) return -1;

    /* Allocate per-vCPU VMX state */
    GET_MEMAZ(vmx, sizeof(struct vmx_state), ALIGNMENT);
    k->ctrl.g->karch.vmx = vmx;
    vmx->shared = shared;
    vmx->vcpu_id = (prtos_u8_t)KID2VCPUID(k->ctrl.g->id);
    vmx->wait_for_sipi = 1;  /* AP starts in wait-for-SIPI */
    vmx->sipi_received = 0;
    vmx->sipi_vector = 0;
    vmx->kthread_ptr = (void *)k;

    /* Register in shared state */
    if (vmx->vcpu_id < CONFIG_MAX_NO_VCPUS)
        shared->vcpu_vmx[vmx->vcpu_id] = vmx;

    /* Share EPT */
    vmx->eptp = shared->eptp;
    vmx->ept_pml4 = shared->ept_pml4;

    /* Copy virtual device pointers (shared devices accessed via shared ptr) */

    /* Allocate and initialize own VMCS */
    if (vmx_alloc_vmcs(vmx) < 0) return -1;

    /* Initialize VMCS controls and host state */
    vmx_init_vmcs_controls(vmx, k);

    /* Set AP guest state (wait-for-SIPI) */
    vmx_set_guest_state_ap(vmx);

    vmx->launched = 0;
    vmx->ipi_pending_bitmap = 0;

    kprintf("[VMX] Partition %d vCPU %d (AP): VMCS at phys 0x%llx, wait-for-SIPI\n",
            p->cfg->id, vmx->vcpu_id, vmx->vmcs_phys);

    return 0;
}

/* ================================================================
 * Guest Page Table Walker (for instruction fetch during LAPIC emulation)
 * ================================================================ */

/* Walk guest page tables to translate guest virtual address to guest physical.
 * With identity-mapped EPT (first 1GB), the result can be used as a host pointer.
 * Returns (prtos_u64_t)-1 on failure. */
static prtos_u64_t guest_virt_to_phys(prtos_u64_t vaddr) {
    prtos_u64_t cr0 = vmx_vmread(VMCS_GUEST_CR0);

    if (!(cr0 & _CR0_PG)) {
        /* Paging disabled: virtual = physical */
        return vaddr;
    }

    prtos_u64_t cr3 = vmx_vmread(VMCS_GUEST_CR3);
    prtos_u64_t efer = vmx_vmread(VMCS_GUEST_IA32_EFER);

    if (efer & (1ULL << 10)) {
        /* IA-32e mode (long mode): 4-level page tables */
        prtos_u64_t *pml4 = (prtos_u64_t *)(unsigned long)(cr3 & 0x000FFFFFFFFFF000ULL);
        prtos_u64_t pml4e = pml4[(vaddr >> 39) & 0x1FF];
        if (!(pml4e & 1)) return (prtos_u64_t)-1;

        prtos_u64_t *pdpt = (prtos_u64_t *)(unsigned long)(pml4e & 0x000FFFFFFFFFF000ULL);
        prtos_u64_t pdpte = pdpt[(vaddr >> 30) & 0x1FF];
        if (!(pdpte & 1)) return (prtos_u64_t)-1;
        if (pdpte & 0x80) /* 1GB page */
            return (pdpte & 0x000FFFFFC0000000ULL) | (vaddr & 0x3FFFFFFF);

        prtos_u64_t *pd = (prtos_u64_t *)(unsigned long)(pdpte & 0x000FFFFFFFFFF000ULL);
        prtos_u64_t pde = pd[(vaddr >> 21) & 0x1FF];
        if (!(pde & 1)) return (prtos_u64_t)-1;
        if (pde & 0x80) /* 2MB page */
            return (pde & 0x000FFFFFFFE00000ULL) | (vaddr & 0x1FFFFF);

        prtos_u64_t *pt = (prtos_u64_t *)(unsigned long)(pde & 0x000FFFFFFFFFF000ULL);
        prtos_u64_t pte = pt[(vaddr >> 12) & 0x1FF];
        if (!(pte & 1)) return (prtos_u64_t)-1;
        return (pte & 0x000FFFFFFFFFF000ULL) | (vaddr & 0xFFF);
    }

    /* 32-bit paging */
    if (vmx_vmread(VMCS_GUEST_CR4) & (1ULL << 5)) {
        /* PAE: 3-level page tables */
        prtos_u64_t *pdpt = (prtos_u64_t *)(unsigned long)(cr3 & 0xFFFFFFE0ULL);
        prtos_u64_t pdpte = pdpt[(vaddr >> 30) & 0x3];
        if (!(pdpte & 1)) return (prtos_u64_t)-1;

        prtos_u64_t *pd = (prtos_u64_t *)(unsigned long)(pdpte & 0x000FFFFFFFFFF000ULL);
        prtos_u64_t pde = pd[(vaddr >> 21) & 0x1FF];
        if (!(pde & 1)) return (prtos_u64_t)-1;
        if (pde & 0x80) /* 2MB page */
            return (pde & 0x000FFFFFFFE00000ULL) | (vaddr & 0x1FFFFF);

        prtos_u64_t *pt = (prtos_u64_t *)(unsigned long)(pde & 0x000FFFFFFFFFF000ULL);
        prtos_u64_t pte = pt[(vaddr >> 12) & 0x1FF];
        if (!(pte & 1)) return (prtos_u64_t)-1;
        return (pte & 0x000FFFFFFFFFF000ULL) | (vaddr & 0xFFF);
    } else {
        /* Non-PAE: 2-level page tables */
        prtos_u32_t *pd = (prtos_u32_t *)(unsigned long)(cr3 & 0xFFFFF000ULL);
        prtos_u32_t pde = pd[(vaddr >> 22) & 0x3FF];
        if (!(pde & 1)) return (prtos_u64_t)-1;
        if (pde & 0x80) /* 4MB page */
            return ((prtos_u64_t)(pde & 0xFFC00000)) | (vaddr & 0x3FFFFF);

        prtos_u32_t *pt = (prtos_u32_t *)(unsigned long)(pde & 0xFFFFF000);
        prtos_u32_t pte = pt[(vaddr >> 12) & 0x3FF];
        if (!(pte & 1)) return (prtos_u64_t)-1;
        return ((prtos_u64_t)(pte & 0xFFFFF000)) | (vaddr & 0xFFF);
    }
}

/* ================================================================
 * x86 Instruction Decoder for LAPIC MMIO Emulation
 * ================================================================ */

/* Minimal x86 instruction decoder for MOV instructions accessing LAPIC MMIO.
 * Handles: 0x8B (MOV r, r/m), 0x89 (MOV r/m, r), 0xC7 (MOV r/m, imm32),
 *          0x0F B6/B7 (MOVZX).
 * Returns 1 on successful decode, 0 on failure. */
static int lapic_decode_insn(const prtos_u8_t *insn, int is_64bit,
                             int *p_is_write, int *p_reg_idx,
                             prtos_u32_t *p_imm_val, int *p_insn_len) {
    int i = 0;
    int rex = 0;

    /* Skip legacy prefixes */
    for (;;) {
        prtos_u8_t b = insn[i];
        if (b == 0x66 || b == 0x67 || b == 0xF0 || b == 0xF2 || b == 0xF3 ||
            b == 0x2E || b == 0x3E || b == 0x26 || b == 0x36 ||
            b == 0x64 || b == 0x65) {
            i++;
            continue;
        }
        break;
    }

    /* REX prefix (0x40-0x4F) in 64-bit mode */
    if (is_64bit && (insn[i] & 0xF0) == 0x40) {
        rex = insn[i];
        i++;
    }

    prtos_u8_t opcode = insn[i++];
    prtos_u8_t modrm;
    int mod, reg, rm;

    if (opcode == 0x8B || opcode == 0x89 || opcode == 0xC7) {
        modrm = insn[i++];
        mod = (modrm >> 6) & 3;
        reg = (modrm >> 3) & 7;
        rm = modrm & 7;
        if (rex & 0x04) reg |= 8; /* REX.R */
        if (rex & 0x01) rm |= 8;  /* REX.B */

        /* Skip SIB + displacement based on ModR/M and SIB */
        if (mod == 0) {
            if ((rm & 7) == 4) {
                /* SIB byte present — check base for disp32 */
                prtos_u8_t sib = insn[i++];
                if ((sib & 7) == 5) i += 4; /* base=5 → disp32, no base reg */
            } else if ((rm & 7) == 5) {
                i += 4; /* RIP-relative disp32 */
            }
        } else if (mod == 1) {
            if ((rm & 7) == 4) i++; /* SIB byte */
            i += 1; /* disp8 */
        } else if (mod == 2) {
            if ((rm & 7) == 4) i++; /* SIB byte */
            i += 4; /* disp32 */
        }

        if (opcode == 0x8B) {
            *p_is_write = 0;
            *p_reg_idx = reg;
        } else if (opcode == 0x89) {
            *p_is_write = 1;
            *p_reg_idx = reg;
        } else { /* 0xC7: MOV r/m, imm32 */
            *p_is_write = 1;
            *p_reg_idx = -1;
            *p_imm_val = *(const prtos_u32_t *)(insn + i);
            i += 4;
        }

        *p_insn_len = i;
        return 1;
    }

    if (opcode == 0x0F) {
        prtos_u8_t opcode2 = insn[i++];
        if (opcode2 == 0xB6 || opcode2 == 0xB7) {
            /* MOVZX r, r/m8 or MOVZX r, r/m16 */
            modrm = insn[i++];
            mod = (modrm >> 6) & 3;
            reg = (modrm >> 3) & 7;
            rm = modrm & 7;
            if (rex & 0x04) reg |= 8;
            if (rex & 0x01) rm |= 8;

            if (mod == 0) {
                if ((rm & 7) == 4) {
                    prtos_u8_t sib = insn[i++];
                    if ((sib & 7) == 5) i += 4;
                } else if ((rm & 7) == 5) {
                    i += 4;
                }
            } else if (mod == 1) {
                if ((rm & 7) == 4) i++;
                i += 1;
            } else if (mod == 2) {
                if ((rm & 7) == 4) i++;
                i += 4;
            }

            *p_is_write = 0;
            *p_reg_idx = reg;
            *p_insn_len = i;
            return 1;
        }
    }

    return 0; /* Unsupported instruction */
}

/* ================================================================
 * LAPIC Register Emulation
 * ================================================================ */

static prtos_u64_t vmx_get_guest_reg(struct vmx_state *vmx, int reg_idx) {
    if (reg_idx == VMX_REG_RSP)
        return vmx_vmread(VMCS_GUEST_RSP);
    return vmx->guest_regs[reg_idx];
}

static void vmx_set_guest_reg(struct vmx_state *vmx, int reg_idx, prtos_u64_t val) {
    if (reg_idx == VMX_REG_RSP)
        vmx_vmwrite(VMCS_GUEST_RSP, val);
    else
        vmx->guest_regs[reg_idx] = val;
}

/* Compute LAPIC register value for a guest read. */
static prtos_u32_t vmx_lapic_read_reg(struct vmx_state *vmx, prtos_u32_t offset) {
    switch (offset) {
    case 0x020: /* APIC ID */
        return (prtos_u32_t)vmx->vcpu_id << 24;
    case 0x030: /* APIC Version */
        return lapic_read(0x030);
    case 0x320: /* LVT Timer */
        return vmx->vlapic_timer.lvt_timer;
    case 0x380: /* Initial Count */
        return vmx->vlapic_timer.initial_count;
    case 0x390: /* Current Count */
        if (vmx->vlapic_timer.active && vmx->vlapic_timer.period_us > 0) {
            prtos_u64_t now = get_sys_clock_usec();
            if (now < vmx->vlapic_timer.next_fire_us) {
                prtos_u64_t remaining_us = vmx->vlapic_timer.next_fire_us - now;
                prtos_u32_t divisor = vlapic_timer_get_divisor(vmx->vlapic_timer.divide_config);
                return (prtos_u32_t)((remaining_us * VLAPIC_TIMER_FREQ_HZ) /
                                     (divisor * 1000000ULL));
            }
        }
        return 0;
    case 0x3E0: /* Divide Configuration */
        return vmx->vlapic_timer.divide_config;
    case 0x300: /* ICR Low — return idle/complete status */
        return 0;
    case 0x310: /* ICR High */
        return 0;
    default:
        return lapic_read(offset);
    }
}

/* Handle a LAPIC register write from the guest. */
static void vmx_lapic_write_reg(struct vmx_state *vmx, prtos_u32_t offset, prtos_u32_t value) {
    switch (offset) {
    case 0x300: { /* ICR Low — dispatch INIT/SIPI/IPI */
        volatile prtos_u32_t *page = (volatile prtos_u32_t *)vmx->shared->apic_access_page;
        prtos_u32_t icr_high = page ? page[0x310 / 4] : 0;
        prtos_u32_t delivery_mode = (value >> 8) & 0x7;
        prtos_u32_t vector = value & 0xFF;
        prtos_u32_t dest_apic_id = (icr_high >> 24) & 0xFF;

        if (delivery_mode == 5) {
            /* INIT IPI */
            if (dest_apic_id < vmx->shared->num_vcpus &&
                vmx->shared->vcpu_vmx[dest_apic_id]) {
                struct vmx_state *ap_vmx = vmx->shared->vcpu_vmx[dest_apic_id];
                ap_vmx->wait_for_sipi = 1;
                ap_vmx->sipi_received = 0;
            }
        } else if (delivery_mode == 6) {
            /* Startup IPI (SIPI) */
            if (dest_apic_id < vmx->shared->num_vcpus &&
                vmx->shared->vcpu_vmx[dest_apic_id]) {
                struct vmx_state *ap_vmx = vmx->shared->vcpu_vmx[dest_apic_id];
                if (ap_vmx->wait_for_sipi) {
                    kthread_t *target_k = (kthread_t *)ap_vmx->kthread_ptr;
                    ap_vmx->sipi_vector = (prtos_u8_t)vector;
                    ap_vmx->sipi_received = 1;
                    ap_vmx->wait_for_sipi = 0;

                    {
                        partition_control_table_t *pct = target_k->ctrl.g->part_ctrl_table;
                        pct->hw_irqs_mask |= ~0;
                        pct->ext_irqs_to_mask |= ~0;
                        init_part_ctrl_table_irqs(&pct->iflags);
                    }

                    setup_kstack(target_k, start_up_guest, 0);
                    __asm__ __volatile__("mfence" ::: "memory");

                    set_kthread_flags(target_k, KTHREAD_READY_F);
                    clear_kthread_flags(target_k, KTHREAD_HALTED_F);
                    __asm__ __volatile__("mfence" ::: "memory");

                    {
                        prtos_s32_t part_id = KID2PARTID(target_k->ctrl.g->id);
                        prtos_s32_t vcpu_idx = KID2VCPUID(target_k->ctrl.g->id);
                        prtos_s32_t ncpus = prtos_conf_table.hpv.num_of_cpus;
                        prtos_s32_t tbl_idx = (part_id * ncpus) + vcpu_idx;
                        prtos_u8_t target_cpu = prtos_conf_vcpu_table[tbl_idx].cpu;
                        if (target_cpu != GET_CPU_ID())
                            CROSS_CPU_SCHED_NOTIFY(target_cpu);
                    }
                }
            }
        }

        /* Fixed or lowest-priority IPI */
        if (delivery_mode == 0 || delivery_mode == 1) {
            prtos_u32_t shorthand = (value >> 18) & 0x3;
            prtos_u32_t i;
            switch (shorthand) {
            case 0:
                if (dest_apic_id < vmx->shared->num_vcpus &&
                    vmx->shared->vcpu_vmx[dest_apic_id])
                    vmx_queue_ipi(vmx->shared->vcpu_vmx[dest_apic_id], (prtos_u8_t)vector);
                break;
            case 1:
                vmx_queue_ipi(vmx, (prtos_u8_t)vector);
                break;
            case 2:
                for (i = 0; i < vmx->shared->num_vcpus; i++)
                    if (i != vmx->vcpu_id && vmx->shared->vcpu_vmx[i])
                        vmx_queue_ipi(vmx->shared->vcpu_vmx[i], (prtos_u8_t)vector);
                break;
            case 3:
                for (i = 0; i < vmx->shared->num_vcpus; i++)
                    if (vmx->shared->vcpu_vmx[i])
                        vmx_queue_ipi(vmx->shared->vcpu_vmx[i], (prtos_u8_t)vector);
                break;
            }
        }
        break;
    }
    case 0x310: /* ICR High — store for next ICR Low write */
        if (vmx->shared->apic_access_page) {
            volatile prtos_u32_t *page = (volatile prtos_u32_t *)vmx->shared->apic_access_page;
            page[0x310 / 4] = value;
        }
        break;
    case 0x320: /* LVT Timer */
    case 0x380: /* Initial Count */
    case 0x3E0: /* Divide Configuration */
        vlapic_timer_write(vmx, offset, value);
        break;
    case 0x0B0: /* EOI — virtual for injected interrupts.  Write to real LAPIC
                 * as well to maintain ISR consistency for anything the
                 * physical LAPIC might have pending. */
        lapic_write(offset, value);
        break;
    case 0x0F0: /* SVR — protect BSP's LAPIC enable */
        if (vmx->vcpu_id != 0)
            lapic_write(offset, value);
        break;
    case 0x020: /* APIC ID (read-only) */
    case 0x030: /* APIC Version (read-only) */
        break;
    default:
        lapic_write(offset, value);
        break;
    }
}

/* ================================================================
 * VM Run Loop
 * ================================================================ */

/* Handle APIC-access VM exit.
 * Decodes the x86 instruction at guest RIP and emulates the LAPIC
 * register access directly (single VM exit). Falls back to MTF-based
 * passthrough if instruction decoding fails. */
static void vmx_handle_apic_access(struct vmx_state *vmx, prtos_u64_t qual) {
    prtos_u32_t offset = (prtos_u32_t)(qual & 0xFFF);
    prtos_u32_t access_type = (prtos_u32_t)((qual >> 12) & 0xF);

    /* Only handle linear read (0) and linear write (1) */
    if (access_type > 1) {
        kprintf("[VMX] vCPU %d: APIC-access type=%d offset=0x%x (unexpected)\n",
                vmx->vcpu_id, access_type, offset);
        return;
    }

    /* Translate guest RIP to physical address for instruction fetch */
    prtos_u64_t guest_rip = vmx_vmread(VMCS_GUEST_RIP);
    prtos_u64_t rip_phys = guest_virt_to_phys(guest_rip);

    if (rip_phys == (prtos_u64_t)-1 || rip_phys >= 0x40000000ULL) {
        /* Cannot translate or address outside identity-mapped EPT range */
        goto fallback_mtf;
    }

    /* Read instruction bytes from guest memory (EPT identity-mapped) */
    const prtos_u8_t *insn = (const prtos_u8_t *)(unsigned long)rip_phys;

    /* Determine if guest is in 64-bit mode */
    prtos_u64_t cs_access = vmx_vmread(VMCS_GUEST_CS_ACCESS);
    int is_64bit = (cs_access & VMX_AR_L) ? 1 : 0;

    int is_write, reg_idx, insn_len;
    prtos_u32_t imm_val = 0;

    if (!lapic_decode_insn(insn, is_64bit, &is_write, &reg_idx, &imm_val, &insn_len)) {
        goto fallback_mtf;
    }

    if (is_write) {
        prtos_u32_t write_val;
        if (reg_idx >= 0)
            write_val = (prtos_u32_t)vmx_get_guest_reg(vmx, reg_idx);
        else
            write_val = imm_val;
        vmx_lapic_write_reg(vmx, offset, write_val);
    } else {
        prtos_u32_t read_val = vmx_lapic_read_reg(vmx, offset);
        vmx_set_guest_reg(vmx, reg_idx, (prtos_u64_t)read_val);
    }

    /* Advance guest RIP past the instruction */
    vmx_vmwrite(VMCS_GUEST_RIP, guest_rip + insn_len);
    return;

fallback_mtf:
    /* Fall back to MTF-based passthrough (2 VM exits, but handles all cases) */
    {
        static int mtf_fallback_cnt = 0;
        if (mtf_fallback_cnt < 10) {
            kprintf("[VMX] vCPU %d: LAPIC MTF fallback offset=0x%x type=%d RIP=0x%llx\n",
                    vmx->vcpu_id, offset, access_type,
                    (unsigned long long)vmx_vmread(VMCS_GUEST_RIP));
            mtf_fallback_cnt++;
        }
    }

    /* Track ICR_LOW writes for MTF handler */
    if (access_type == 1 && offset == 0x300)
        vmx->lapic_icr_pending = 1;

    while (__sync_lock_test_and_set(&vmx->shared->apic_page_lock, 1))
        __asm__ __volatile__("pause" ::: "memory");

    /* Pre-populate APIC-access page for reads */
    {
        volatile prtos_u32_t *apic_page = (volatile prtos_u32_t *)vmx->shared->apic_access_page;
        if (offset == 0x320) {
            apic_page[0x320 / 4] = vmx->vlapic_timer.lvt_timer;
        } else if (offset == 0x380) {
            apic_page[0x380 / 4] = vmx->vlapic_timer.initial_count;
        } else if (offset == 0x3E0) {
            apic_page[0x3E0 / 4] = vmx->vlapic_timer.divide_config;
        } else if (offset == 0x390) {
            apic_page[0x390 / 4] = vmx_lapic_read_reg(vmx, 0x390);
        } else if (offset == 0x020) {
            apic_page[0x020 / 4] = (prtos_u32_t)vmx->vcpu_id << 24;
        } else if (offset != 0x300 && offset != 0x310) {
            apic_page[offset / 4] = lapic_read(offset);
        }
    }

    vmx->lapic_access_offset = offset;
    vmx->lapic_access_is_write = (access_type == 1) ? 1 : 0;

    {
        prtos_u64_t proc2 = vmx_vmread(VMCS_SECONDARY_EXEC_CTRL);
        proc2 &= ~(prtos_u64_t)SECONDARY_EXEC_VIRT_APIC_ACCESS;
        vmx_vmwrite(VMCS_SECONDARY_EXEC_CTRL, proc2);

        prtos_u64_t proc = vmx_vmread(VMCS_PROC_BASED_EXEC_CTRL);
        proc |= CPU_BASED_MONITOR_TRAP_FLAG;
        vmx_vmwrite(VMCS_PROC_BASED_EXEC_CTRL, proc);
        vmx->lapic_mtf_pending = 1;
    }
}

/* Configure a secondary vCPU's VMCS after receiving SIPI.
 * Sets up real-mode entry at the SIPI vector address. */
static void vmx_apply_sipi(struct vmx_state *vmx) {
    prtos_u64_t sipi_addr = (prtos_u64_t)vmx->sipi_vector << 12;

    /* Load this vCPU's VMCS */
    vmx_vmptrld(&vmx->vmcs_phys);

    /* Set guest to real mode at SIPI vector address.
     * CS:IP = sipi_vector*0x100 : 0x0000
     * In real mode with unrestricted guest: CS.base = sipi_vector * 0x1000 */
    vmx_vmwrite(VMCS_GUEST_CS_SEL, (prtos_u64_t)vmx->sipi_vector << 8);
    vmx_vmwrite(VMCS_GUEST_CS_BASE, sipi_addr);
    vmx_vmwrite(VMCS_GUEST_CS_LIMIT, 0xFFFF);
    /* Real-mode CS: type=0xB (exec/read/accessed), S=1, DPL=0, P=1, 16-bit */
    vmx_vmwrite(VMCS_GUEST_CS_ACCESS, 0x9B);

    /* Set all data segments to real-mode defaults */
    vmx_vmwrite(VMCS_GUEST_DS_SEL, 0);
    vmx_vmwrite(VMCS_GUEST_DS_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_DS_LIMIT, 0xFFFF);
    vmx_vmwrite(VMCS_GUEST_DS_ACCESS, 0x93);

    vmx_vmwrite(VMCS_GUEST_ES_SEL, 0);
    vmx_vmwrite(VMCS_GUEST_ES_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_ES_LIMIT, 0xFFFF);
    vmx_vmwrite(VMCS_GUEST_ES_ACCESS, 0x93);

    vmx_vmwrite(VMCS_GUEST_SS_SEL, 0);
    vmx_vmwrite(VMCS_GUEST_SS_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_SS_LIMIT, 0xFFFF);
    vmx_vmwrite(VMCS_GUEST_SS_ACCESS, 0x93);

    vmx_vmwrite(VMCS_GUEST_FS_SEL, 0);
    vmx_vmwrite(VMCS_GUEST_FS_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_FS_LIMIT, 0xFFFF);
    vmx_vmwrite(VMCS_GUEST_FS_ACCESS, 0x93);

    vmx_vmwrite(VMCS_GUEST_GS_SEL, 0);
    vmx_vmwrite(VMCS_GUEST_GS_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_GS_LIMIT, 0xFFFF);
    vmx_vmwrite(VMCS_GUEST_GS_ACCESS, 0x93);

    vmx_vmwrite(VMCS_GUEST_RIP, 0);
    vmx_vmwrite(VMCS_GUEST_RSP, 0);
    vmx_vmwrite(VMCS_GUEST_RFLAGS, 0x2);

    /* CR0: real mode (PE=0), unrestricted guest allows this.
     * Apply fixed bits but clear PE and PG (unrestricted guest relaxes these). */
    {
        prtos_u64_t cr0 = _CR0_ET | _CR0_NE;
        prtos_u64_t cr0_fixed0 = read_msr(MSR_IA32_VMX_CR0_FIXED0);
        /* PE and PG are relaxed under unrestricted guest */
        cr0_fixed0 &= ~(_CR0_PE | _CR0_PG);
        cr0 |= (prtos_u32_t)cr0_fixed0;
        vmx_vmwrite(VMCS_GUEST_CR0, cr0);
    }

    /* Activity state: active (was wait-for-SIPI) */
    vmx_vmwrite(VMCS_GUEST_ACTIVITY_STATE, 0);

    vmx->wait_for_sipi = 0;
    vmx->sipi_received = 0;
    vmx->launched = 0;  /* Need VMLAUNCH since VMCS was cleared/reloaded */
}

void __attribute__((noreturn)) vmx_run_guest(void *kthread_ptr) {
    kthread_t *k = (kthread_t *)kthread_ptr;
    struct vmx_state *vmx = k->ctrl.g->karch.vmx;
    int ret;

    /* If this is a secondary vCPU, wait for SIPI if not already received */
    if (vmx->vcpu_id > 0) {
        if (!vmx->sipi_received) {
            /* Spin-wait for SIPI from BSP.
             * Allow host interrupts so the scheduler can preempt us. */
            while (!(*(volatile prtos_u8_t *)&vmx->sipi_received)) {
                hw_sti();
                __asm__ __volatile__("pause" ::: "memory");
                hw_cli();
            }
        }
        /* SIPI received — configure VMCS for real-mode entry */
        vmx_apply_sipi(vmx);
    }

    /* Load this vCPU's VMCS */
    vmx_vmptrld(&vmx->vmcs_phys);

    /* Refresh host state */
    vmx_vmwrite(VMCS_HOST_CR3, save_cr3());
    vmx_vmwrite(VMCS_HOST_GDTR_BASE, (prtos_u64_t)(unsigned long)k->ctrl.g->karch.gdt_table);
    vmx_vmwrite(VMCS_HOST_IDTR_BASE, (prtos_u64_t)(unsigned long)k->ctrl.g->karch.hyp_idt_table);
    vmx_vmwrite(VMCS_HOST_TR_BASE, (prtos_u64_t)(unsigned long)&k->ctrl.g->karch.tss);
    vmx_vmwrite(VMCS_HOST_GS_BASE, (prtos_u64_t)read_msr(MSR_IA32_GS_BASE));

    for (;;) {
        /* Tick and try to inject virtual LAPIC timer interrupt (all vCPUs) */
        {
            prtos_u64_t now = get_sys_clock_usec();
            vlapic_timer_tick(vmx, now);
            vlapic_timer_inject(vmx);
        }

        /* Check if there's a pending PIC IRQ to inject (only for BSP / vCPU 0) */
        if (vmx->vcpu_id == 0)
            vmx_check_pending_irq(vmx);

        /* Check for pending inter-vCPU IPIs */
        vmx_inject_pending_ipi(vmx);

        /* Enter the guest */
        g_vmentry_cnt++;
        if (!vmx->launched) {
            vmx->launched = 1;
            ret = vmx_first_launch(vmx);
        } else {
            ret = vmx_guest_enter(vmx);
        }

        if (ret != 0) {
            prtos_u64_t err = vmx_vmread(0x4400);
            kprintf("[VMX] vCPU %d: VM entry failed! error=%lld RIP=0x%llx EFER=0x%llx CR0=0x%llx\n",
                    vmx->vcpu_id, (long long)err,
                    (unsigned long long)vmx_vmread(VMCS_GUEST_RIP),
                    (unsigned long long)vmx_vmread(VMCS_GUEST_IA32_EFER),
                    (unsigned long long)vmx_vmread(VMCS_GUEST_CR0));
            for (;;) { __asm__ __volatile__("cli; hlt"); }
        }

        /* VM exit occurred - handle it */
        {
            prtos_u32_t reason = (prtos_u32_t)vmx_vmread(VMCS_EXIT_REASON) & 0xFFFF;

            switch (reason) {
            case EXIT_REASON_EXCEPTION_NMI: {
                prtos_u64_t intr_info = vmx_vmread(0x4404);
                prtos_u32_t vector = intr_info & 0xFF;
                prtos_u64_t err_code = vmx_vmread(0x4406);
                kprintf("[VMX] vCPU %d: Exception #%d err=0x%llx RIP=0x%llx RSP=0x%llx\n",
                        vmx->vcpu_id, vector,
                        (unsigned long long)err_code,
                        (unsigned long long)vmx_vmread(VMCS_GUEST_RIP),
                        (unsigned long long)vmx_vmread(VMCS_GUEST_RSP));
                kprintf("[VMX] CS:sel=0x%llx ar=0x%llx IDTR base=0x%llx lim=0x%llx\n",
                        (unsigned long long)vmx_vmread(VMCS_GUEST_CS_SEL),
                        (unsigned long long)vmx_vmread(VMCS_GUEST_CS_ACCESS),
                        (unsigned long long)vmx_vmread(0x6816),
                        (unsigned long long)vmx_vmread(0x4812));
                kprintf("[VMX] GDTR base=0x%llx lim=0x%llx EFER=0x%llx\n",
                        (unsigned long long)vmx_vmread(0x6814),
                        (unsigned long long)vmx_vmread(0x4810),
                        (unsigned long long)vmx_vmread(VMCS_GUEST_IA32_EFER));
                for (;;) { __asm__ __volatile__("cli; hlt"); }
                break;
            }

            case EXIT_REASON_EXTERNAL_INTR:
                hw_sti();
                hw_cli();
                if (vmx->vcpu_id == 0) {
                    prtos_u64_t now = get_sys_clock_usec();
                    vpit_tick(vmx, now);
                }
                break;

            case EXIT_REASON_INTERRUPT_WINDOW:
                /* Guest just became interruptible (IF=1).
                 * vmx_check_pending_irq at the top of the loop
                 * will inject the pending interrupt on the next iteration.
                 * Disable the interrupt-window exiting. */
                {
                    prtos_u64_t proc = vmx_vmread(VMCS_PROC_BASED_EXEC_CTRL);
                    proc &= ~(prtos_u64_t)CPU_BASED_VIRTUAL_INTR_PENDING;
                    vmx_vmwrite(VMCS_PROC_BASED_EXEC_CTRL, proc);
                }
                break;

            case EXIT_REASON_IO_INSTRUCTION:
                vmx_handle_io_exit(k, vmx);
                break;

            case EXIT_REASON_HLT:
                vmx_handle_hlt_exit(k, vmx);
                break;

            case EXIT_REASON_CPUID:
                vmx_handle_cpuid_exit(vmx);
                break;

            case EXIT_REASON_PREEMPTION_TIMER: {
                prtos_u64_t now = get_sys_clock_usec();
                if (vmx->vcpu_id == 0) {
                    vpit_tick(vmx, now);
                    vuart_poll_host(vmx);

                    /* Heartbeat: show timer state every 60s */
                    {
                        extern prtos_u32_t g_pic_inj_cnt, g_pic_defer_cnt, g_pic_busy_cnt;
                        extern prtos_u32_t g_lapic_inj_cnt, g_vuart_tx_cnt, g_vuart_irq4_cnt, g_vmentry_cnt;
                        static prtos_u64_t next_hb = 60000000;
                        if (now >= next_hb) {
                            kprintf("[HB] PIC=%u LAPIC=%u TX=%u VM=%u IER=0x%x\n",
                                    g_pic_inj_cnt,
                                    g_lapic_inj_cnt,
                                    g_vuart_tx_cnt, g_vmentry_cnt,
                                    vmx->shared->vuart.ier);
                            next_hb = now + 10000000;
                        }
                    }
                }
                vmx_vmwrite(VMCS_VMX_PREEMPT_TIMER_VALUE, vmx->preempt_timer_val);
                break;
            }

            case EXIT_REASON_MTF: {
                /* MTF fallback for LAPIC accesses that couldn't be decoded.
                 * The instruction has now executed against the APIC-access
                 * page (RAM). Re-enable APIC-access page and process write. */
                if (vmx->lapic_mtf_pending) {
                    /* Handle writes via the APIC-access page values */
                    if (vmx->lapic_access_is_write) {
                        volatile prtos_u32_t *apic_page = (volatile prtos_u32_t *)vmx->shared->apic_access_page;
                        prtos_u32_t off = vmx->lapic_access_offset;
                        prtos_u32_t val = apic_page[off / 4];
                        vmx_lapic_write_reg(vmx, off, val);
                    }
                    vmx->lapic_access_is_write = 0;
                    vmx->lapic_icr_pending = 0;

                    /* Re-enable APIC-access page */
                    prtos_u64_t proc2 = vmx_vmread(VMCS_SECONDARY_EXEC_CTRL);
                    proc2 |= SECONDARY_EXEC_VIRT_APIC_ACCESS;
                    vmx_vmwrite(VMCS_SECONDARY_EXEC_CTRL, proc2);
                    /* Disable MTF */
                    prtos_u64_t proc = vmx_vmread(VMCS_PROC_BASED_EXEC_CTRL);
                    proc &= ~(prtos_u64_t)CPU_BASED_MONITOR_TRAP_FLAG;
                    vmx_vmwrite(VMCS_PROC_BASED_EXEC_CTRL, proc);
                    vmx->lapic_mtf_pending = 0;

                    __sync_lock_release(&vmx->shared->apic_page_lock);
                }
                break;
            }

            case EXIT_REASON_MSR_READ: {
                prtos_u32_t msr = (prtos_u32_t)vmx->guest_regs[VMX_REG_RCX];
                if (msr == 0xC0000080) {
                    prtos_u64_t efer = vmx_vmread(VMCS_GUEST_IA32_EFER);
                    vmx->guest_regs[VMX_REG_RAX] = efer & 0xFFFFFFFF;
                    vmx->guest_regs[VMX_REG_RDX] = efer >> 32;
                } else if (msr == 0x6E0) {
                    /* IA32_TSC_DEADLINE — we hide this feature, return 0 */
                    vmx->guest_regs[VMX_REG_RAX] = 0;
                    vmx->guest_regs[VMX_REG_RDX] = 0;
                } else {
                    prtos_u32_t lo = 0, hi = 0;
                    __asm__ __volatile__(
                        "1: rdmsr; jmp 2f\n"
                        "3: xorl %%eax,%%eax; xorl %%edx,%%edx; jmp 2f\n"
                        ASM_EXPTABLE(1b, 3b)
                        "2:\n"
                        : "=a"(lo), "=d"(hi) : "c"(msr) );
                    vmx->guest_regs[VMX_REG_RAX] = lo;
                    vmx->guest_regs[VMX_REG_RDX] = hi;
                }
                vmx_vmwrite(VMCS_GUEST_RIP,
                    vmx_vmread(VMCS_GUEST_RIP) + vmx_vmread(VMCS_EXIT_INSTRUCTION_LEN));
                break;
            }

            case EXIT_REASON_MSR_WRITE: {
                prtos_u32_t msr = (prtos_u32_t)vmx->guest_regs[VMX_REG_RCX];
                prtos_u64_t value = ((vmx->guest_regs[VMX_REG_RDX] & 0xFFFFFFFF) << 32) |
                                     (vmx->guest_regs[VMX_REG_RAX] & 0xFFFFFFFF);
                if (msr == 0xC0000080) {
                    vmx_vmwrite(VMCS_GUEST_IA32_EFER, value);
                } else if (msr == 0x6E0) {
                    /* IA32_TSC_DEADLINE — silently ignore (feature hidden) */
                } else {
                    prtos_u32_t lo = (prtos_u32_t)value;
                    prtos_u32_t hi = (prtos_u32_t)(value >> 32);
                    __asm__ __volatile__(
                        "1: wrmsr; jmp 2f\n"
                        "3: jmp 2f\n"
                        ASM_EXPTABLE(1b, 3b)
                        "2:\n"
                        :: "a"(lo), "d"(hi), "c"(msr) );
                }
                vmx_vmwrite(VMCS_GUEST_RIP,
                    vmx_vmread(VMCS_GUEST_RIP) + vmx_vmread(VMCS_EXIT_INSTRUCTION_LEN));
                break;
            }

            case EXIT_REASON_CR_ACCESS: {
                prtos_u64_t qual = vmx_vmread(VMCS_EXIT_QUALIFICATION);
                int cr_num = qual & 0xF;
                int access_type = (qual >> 4) & 3;
                int reg = (qual >> 8) & 0xF;
                prtos_u64_t reg_val;

                /* Read source register value */
                if (reg == 4)  /* RSP is in VMCS, not guest_regs */
                    reg_val = vmx_vmread(VMCS_GUEST_RSP);
                else
                    reg_val = vmx->guest_regs[reg];

                if (cr_num == 0 && access_type == 0) {
                    /* MOV to CR0: emulate with mode transition support */
                    prtos_u64_t new_cr0 = reg_val;
                    prtos_u64_t old_cr0 = vmx_vmread(VMCS_GUEST_CR0);

                    /* Force NE always on (VMX requirement) */
                    new_cr0 |= _CR0_NE;

                    /* Check for PG bit change (long mode transition) */
                    int new_pg = (new_cr0 >> 31) & 1;
                    int old_pg = (old_cr0 >> 31) & 1;

                    if (new_pg && !old_pg) {
                        /* Guest enabling paging */
                        prtos_u64_t efer = vmx_vmread(VMCS_GUEST_IA32_EFER);
                        if (efer & (1ULL << 8)) {  /* EFER.LME */
                            /* Long mode activation */
                            efer |= (1ULL << 10);  /* Set LMA */
                            vmx_vmwrite(VMCS_GUEST_IA32_EFER, efer);
                            /* Enable IA-32e mode in entry controls */
                            prtos_u64_t entry_ctl = vmx_vmread(VMCS_ENTRY_CONTROLS);
                            entry_ctl |= (1ULL << 9);
                            vmx_vmwrite(VMCS_ENTRY_CONTROLS, entry_ctl);
                        }
                    } else if (!new_pg && old_pg) {
                        /* Guest disabling paging */
                        prtos_u64_t efer = vmx_vmread(VMCS_GUEST_IA32_EFER);
                        efer &= ~(1ULL << 10);  /* Clear LMA */
                        vmx_vmwrite(VMCS_GUEST_IA32_EFER, efer);
                        prtos_u64_t entry_ctl = vmx_vmread(VMCS_ENTRY_CONTROLS);
                        entry_ctl &= ~(1ULL << 9);
                        vmx_vmwrite(VMCS_ENTRY_CONTROLS, entry_ctl);
                    }

                    vmx_vmwrite(VMCS_GUEST_CR0, new_cr0);
                    /* Update CR0 read shadow for guest reads */
                    vmx_vmwrite(0x6004, new_cr0 & ~((prtos_u64_t)_CR0_NE));
                } else if (cr_num == 0 && access_type == 1) {
                    /* MOV from CR0: return shadow value */
                    prtos_u64_t shadow = vmx_vmread(0x6004);
                    if (reg == 4)
                        vmx_vmwrite(VMCS_GUEST_RSP, shadow);
                    else
                        vmx->guest_regs[reg] = shadow;
                } else if (cr_num == 4 && access_type == 0) {
                    /* MOV to CR4: keep VMXE forced on */
                    prtos_u64_t new_cr4 = reg_val | _CR4_VMXE;
                    vmx_vmwrite(VMCS_GUEST_CR4, new_cr4);
                    vmx_vmwrite(0x6006, reg_val & ~((prtos_u64_t)_CR4_VMXE));
                } else if (cr_num == 4 && access_type == 1) {
                    /* MOV from CR4: return shadow value */
                    prtos_u64_t shadow = vmx_vmread(0x6006);
                    if (reg == 4)
                        vmx_vmwrite(VMCS_GUEST_RSP, shadow);
                    else
                        vmx->guest_regs[reg] = shadow;
                }

                vmx_vmwrite(VMCS_GUEST_RIP,
                    vmx_vmread(VMCS_GUEST_RIP) + vmx_vmread(VMCS_EXIT_INSTRUCTION_LEN));
                break;
            }

            case EXIT_REASON_TRIPLE_FAULT:
                kprintf("[VMX] Triple fault! RIP=0x%llx RSP=0x%llx CR0=0x%llx CR3=0x%llx CR4=0x%llx\n",
                        (unsigned long long)vmx_vmread(VMCS_GUEST_RIP),
                        (unsigned long long)vmx_vmread(VMCS_GUEST_RSP),
                        (unsigned long long)vmx_vmread(VMCS_GUEST_CR0),
                        (unsigned long long)vmx_vmread(VMCS_GUEST_CR3),
                        (unsigned long long)vmx_vmread(VMCS_GUEST_CR4));
                kprintf("[VMX] EFER=0x%llx CS:sel=0x%llx ar=0x%llx SS:ar=0x%llx\n",
                        (unsigned long long)vmx_vmread(0x2806),
                        (unsigned long long)vmx_vmread(VMCS_GUEST_CS_SEL),
                        (unsigned long long)vmx_vmread(VMCS_GUEST_CS_ACCESS),
                        (unsigned long long)vmx_vmread(VMCS_GUEST_SS_ACCESS));
                for (;;) { __asm__ __volatile__("cli; hlt"); }
                break;

            case EXIT_REASON_APIC_ACCESS: {
                prtos_u64_t apic_qual = vmx_vmread(VMCS_EXIT_QUALIFICATION);
                vmx_handle_apic_access(vmx, apic_qual);
                break;
            }

            case EXIT_REASON_EPT_VIOLATION: {
                prtos_u64_t ept_qual = vmx_vmread(VMCS_EXIT_QUALIFICATION);
                prtos_u64_t ept_gpa = vmx_vmread(0x2400);

                kprintf("[VMX] vCPU %d: EPT violation: qual=0x%llx, GPA=0x%llx, GLA=0x%llx\n",
                        vmx->vcpu_id,
                        (unsigned long long)ept_qual,
                        (unsigned long long)ept_gpa,
                        (unsigned long long)vmx_vmread(0x640A));
                for (;;) { __asm__ __volatile__("cli; hlt"); }
                break;
            }

            case EXIT_REASON_EPT_MISCONFIG:
                kprintf("[VMX] EPT misconfiguration at GPA=0x%llx\n",
                        (unsigned long long)vmx_vmread(0x2400));
                for (;;) { __asm__ __volatile__("cli; hlt"); }
                break;

            case 55: /* EXIT_REASON_XSETBV */ {
                /* Guest executing XSETBV to set XCR0 (AVX/SSE state).
                 * XCR0 is not directly settable via VMCS. We need host CR4.OSXSAVE=1
                 * to execute XSETBV. Enable it temporarily if needed. */
                prtos_u32_t xcr_idx = (prtos_u32_t)vmx->guest_regs[VMX_REG_RCX];
                prtos_u32_t lo = (prtos_u32_t)vmx->guest_regs[VMX_REG_RAX];
                prtos_u32_t hi = (prtos_u32_t)vmx->guest_regs[VMX_REG_RDX];
                prtos_u64_t cr4 = save_cr4();
                if (!(cr4 & (1ULL << 18))) {  /* CR4.OSXSAVE = bit 18 */
                    load_cr4(cr4 | (1ULL << 18));
                }
                __asm__ __volatile__("xsetbv" :: "c"(xcr_idx), "a"(lo), "d"(hi));
                if (!(cr4 & (1ULL << 18))) {
                    load_cr4(cr4);
                }
                vmx_vmwrite(VMCS_GUEST_RIP,
                    vmx_vmread(VMCS_GUEST_RIP) + vmx_vmread(VMCS_EXIT_INSTRUCTION_LEN));
                break;
            }

            default:
                kprintf("[VMX] Unhandled VM exit reason %d at RIP=0x%llx qual=0x%llx\n",
                        reason, (unsigned long long)vmx_vmread(VMCS_GUEST_RIP),
                        (unsigned long long)vmx_vmread(VMCS_EXIT_QUALIFICATION));
                if (reason == 33 || reason == 34) {
                    kprintf("[VMX] Entry fail: guest CR0=0x%llx CR4=0x%llx EFER=0x%llx\n",
                            (unsigned long long)vmx_vmread(VMCS_GUEST_CR0),
                            (unsigned long long)vmx_vmread(VMCS_GUEST_CR4),
                            (unsigned long long)vmx_vmread(VMCS_GUEST_IA32_EFER));
                    kprintf("[VMX] CS: sel=0x%llx ar=0x%llx SS ar=0x%llx TR ar=0x%llx\n",
                            (unsigned long long)vmx_vmread(VMCS_GUEST_CS_SEL),
                            (unsigned long long)vmx_vmread(VMCS_GUEST_CS_ACCESS),
                            (unsigned long long)vmx_vmread(VMCS_GUEST_SS_ACCESS),
                            (unsigned long long)vmx_vmread(VMCS_GUEST_TR_ACCESS));
                    kprintf("[VMX] Entry ctl=0x%llx Exit ctl=0x%llx Pin=0x%llx Proc=0x%llx\n",
                            (unsigned long long)vmx_vmread(VMCS_ENTRY_CONTROLS),
                            (unsigned long long)vmx_vmread(VMCS_EXIT_CONTROLS),
                            (unsigned long long)vmx_vmread(VMCS_PIN_BASED_EXEC_CTRL),
                            (unsigned long long)vmx_vmread(VMCS_PROC_BASED_EXEC_CTRL));
                    kprintf("[VMX] Sec=0x%llx EPTP=0x%llx link=0x%llx\n",
                            (unsigned long long)vmx_vmread(VMCS_SECONDARY_EXEC_CTRL),
                            (unsigned long long)vmx_vmread(VMCS_EPT_POINTER),
                            (unsigned long long)vmx_vmread(VMCS_GUEST_VMCS_LINK_PTR));
                    kprintf("[VMX] GuestRSP=0x%llx RFLAGS=0x%llx Activity=0x%llx Interr=0x%llx\n",
                            (unsigned long long)vmx_vmread(VMCS_GUEST_RSP),
                            (unsigned long long)vmx_vmread(VMCS_GUEST_RFLAGS),
                            (unsigned long long)vmx_vmread(VMCS_GUEST_ACTIVITY_STATE),
                            (unsigned long long)vmx_vmread(VMCS_GUEST_INTERRUPTIBILITY));
                }
                for (;;) { __asm__ __volatile__("cli; hlt"); }
                break;
            }
        }
    }
}

/* ================================================================
 * Virtual PIT (8254) Emulation
 * ================================================================ */

#define PIT_FREQ_HZ 1193182ULL

static void vpit_write(struct vmx_state *vmx, prtos_u16_t port, prtos_u8_t val) {
    struct vpit_state *pit = &vmx->shared->vpit;

    if (port == 0x43) {
        /* Control word */
        prtos_u8_t channel = (val >> 6) & 0x3;
        prtos_u8_t access  = (val >> 4) & 0x3;
        prtos_u8_t mode    = (val >> 1) & 0x7;

        /* Emulate channels 0 and 2 (Linux uses ch2 for calibration) */
        if (channel != 0 && channel != 2) return;
        pit->mode = mode;
        pit->access = access;
        pit->write_lsb = 0;
        pit->read_lsb = 0;
        pit->latch_state = 0;
    } else if (port == 0x40 || port == 0x42) {
        /* Channel 0 or 2 data */
        if (pit->access == 1) {
            /* Lobyte only */
            pit->reload = val;
        } else if (pit->access == 2) {
            /* Hibyte only */
            pit->reload = (prtos_u32_t)val << 8;
        } else if (pit->access == 3) {
            /* Lo/Hi byte */
            if (!pit->write_lsb) {
                pit->reload = (pit->reload & 0xFF00) | val;
                pit->write_lsb = 1;
                return;  /* Wait for high byte */
            } else {
                pit->reload = (pit->reload & 0x00FF) | ((prtos_u32_t)val << 8);
                pit->write_lsb = 0;
            }
        }

        if (pit->reload == 0) pit->reload = 0x10000;
        pit->counter = pit->reload;
        pit->period_us = (prtos_u32_t)((prtos_u64_t)pit->reload * 1000000ULL / PIT_FREQ_HZ);
        if (pit->period_us == 0) pit->period_us = 1;
        pit->active = 1;
        pit->next_tick_us = get_sys_clock_usec() + pit->period_us;
    }
}

static prtos_u8_t vpit_read(struct vmx_state *vmx, prtos_u16_t port) {
    struct vpit_state *pit = &vmx->shared->vpit;

    if (port == 0x43) return 0;  /* control word is write-only */

    if (port == 0x40 || port == 0x42) {
        /* Read channel 0 (or 2) counter. Compute current count based on time elapsed. */
        if (pit->active && pit->reload > 0) {
            prtos_u64_t now = get_sys_clock_usec();
            /* Elapsed microseconds since last reload */
            prtos_u64_t elapsed_us;
            if (now >= pit->next_tick_us - pit->period_us)
                elapsed_us = now - (pit->next_tick_us - pit->period_us);
            else
                elapsed_us = 0;
            /* Convert elapsed us to PIT ticks: ticks = elapsed_us * PIT_FREQ_HZ / 1000000 */
            prtos_u64_t elapsed_ticks = elapsed_us * PIT_FREQ_HZ / 1000000ULL;
            prtos_u32_t cur_count;
            if (elapsed_ticks >= pit->reload)
                cur_count = 0;
            else
                cur_count = pit->reload - (prtos_u32_t)elapsed_ticks;

            /* Return low or high byte depending on access mode and latch state */
            if (pit->access == 1) {
                return (prtos_u8_t)(cur_count & 0xFF);
            } else if (pit->access == 2) {
                return (prtos_u8_t)((cur_count >> 8) & 0xFF);
            } else if (pit->access == 3) {
                /* Lo/Hi: alternate between low and high byte */
                if (!pit->read_lsb) {
                    pit->read_lsb = 1;
                    return (prtos_u8_t)(cur_count & 0xFF);
                } else {
                    pit->read_lsb = 0;
                    return (prtos_u8_t)((cur_count >> 8) & 0xFF);
                }
            }
        }
        return 0;
    }

    return 0;
}

/* ================================================================
 * Virtual LAPIC Timer Emulation
 * ================================================================ */

static prtos_u32_t vlapic_timer_get_divisor(prtos_u32_t divide_config) {
    /* DCR encodes divisor: bits 3,1,0 form a 3-bit field.
     * 0b000 = /2, 0b001 = /4, 0b010 = /8, 0b011 = /16,
     * 0b100 = /32, 0b101 = /64, 0b110 = /128, 0b111 = /1 */
    prtos_u32_t bits = ((divide_config >> 1) & 0x4) | (divide_config & 0x3);
    if (bits == 7) return 1;
    return 1U << (bits + 1);
}

static void vlapic_timer_rearm(struct vmx_state *vmx) {
    prtos_u32_t initial = vmx->vlapic_timer.initial_count;

    /* If initial count is 0, stop the timer */
    if (initial == 0) {
        vmx->vlapic_timer.active = 0;
        return;
    }

    prtos_u32_t divisor = vlapic_timer_get_divisor(vmx->vlapic_timer.divide_config);
    prtos_u64_t ticks = (prtos_u64_t)initial;
    /* period in microseconds = ticks * divisor / freq_hz * 1000000 */
    vmx->vlapic_timer.period_us = (prtos_u32_t)((ticks * divisor * 1000000ULL) / VLAPIC_TIMER_FREQ_HZ);
    if (vmx->vlapic_timer.period_us == 0) vmx->vlapic_timer.period_us = 1;

    vmx->vlapic_timer.next_fire_us = get_sys_clock_usec() + vmx->vlapic_timer.period_us;
    vmx->vlapic_timer.active = 1;
    vmx->vlapic_timer.pending = 0;
}

static void vlapic_timer_write(struct vmx_state *vmx, prtos_u32_t offset, prtos_u32_t value) {
    if (offset == 0x320) {
        /* LVT Timer register.
         * The mask bit only controls interrupt delivery, not the counter.
         * The counter runs regardless of mask state. */
        vmx->vlapic_timer.lvt_timer = value;
    } else if (offset == 0x380) {
        /* Initial Count register — writing this arms/rearms the timer */
        vmx->vlapic_timer.initial_count = value;
        vlapic_timer_rearm(vmx);
    } else if (offset == 0x3E0) {
        /* Divide Configuration register */
        vmx->vlapic_timer.divide_config = value;
        /* If timer is already active, recalculate with new divisor */
        if (vmx->vlapic_timer.active) {
            vlapic_timer_rearm(vmx);
        }
    }
}

static void vlapic_timer_tick(struct vmx_state *vmx, prtos_u64_t now_us) {
    if (!vmx->vlapic_timer.active) return;
    if (now_us < vmx->vlapic_timer.next_fire_us) return;

    prtos_u32_t lvt = vmx->vlapic_timer.lvt_timer;
    prtos_u32_t mode = (lvt >> 17) & 0x3;

    /* Timer fired */
    vmx->vlapic_timer.pending = 1;

    if (mode == 1) {
        /* Periodic mode — re-arm.
         * Just advance by one period. The context-switch hooks
         * (vmx_switch_pre/post) adjust next_fire_us to account for
         * scheduler idle time, so no special catch-up is needed here. */
        vmx->vlapic_timer.next_fire_us += vmx->vlapic_timer.period_us;
    } else {
        /* One-shot mode (mode == 0) or TSC-deadline (mode == 2, not fully supported) */
        vmx->vlapic_timer.active = 0;
    }
}

/* Try to inject a pending LAPIC timer interrupt into the guest.
 * Returns 1 if injected, 0 if not (guest not interruptible or no pending). */
static int vlapic_timer_inject(struct vmx_state *vmx) {
    if (!vmx->vlapic_timer.pending) return 0;

    prtos_u32_t lvt = vmx->vlapic_timer.lvt_timer;
    if (lvt & (1 << 16)) {
        /* Masked — drop the interrupt */
        vmx->vlapic_timer.pending = 0;
        return 0;
    }

    prtos_u32_t vector = lvt & 0xFF;
    if (vector == 0) return 0;  /* Invalid vector */

    /* Check if guest is interruptible */
    prtos_u64_t guest_rflags = vmx_vmread(VMCS_GUEST_RFLAGS);
    prtos_u32_t interruptibility = (prtos_u32_t)vmx_vmread(VMCS_GUEST_INTERRUPTIBILITY);

    if (!(guest_rflags & _CPU_FLAG_IF) || (interruptibility & 0x3)) {
        /* Guest not ready — enable interrupt-window exiting */
        prtos_u64_t proc = vmx_vmread(VMCS_PROC_BASED_EXEC_CTRL);
        if (!(proc & CPU_BASED_VIRTUAL_INTR_PENDING)) {
            proc |= CPU_BASED_VIRTUAL_INTR_PENDING;
            vmx_vmwrite(VMCS_PROC_BASED_EXEC_CTRL, proc);
        }
        return 0;
    }

    /* Check if there's already a pending interrupt injection */
    prtos_u64_t entry_info = vmx_vmread(VMCS_ENTRY_INTR_INFO);
    if (entry_info & VMCS_INTR_INFO_VALID) {
        /* Already injecting something — try again next time */
        return 0;
    }

    /* Inject the timer interrupt */
    vmx_vmwrite(VMCS_ENTRY_INTR_INFO,
                VMCS_INTR_INFO_VALID | VMCS_INTR_TYPE_EXT_INTR | (prtos_u32_t)vector);
    vmx->vlapic_timer.pending = 0;
    g_lapic_inj_cnt++;
    return 1;
}

static void vpit_tick(struct vmx_state *vmx, prtos_u64_t now_us) {
    struct vpit_state *pit = &vmx->shared->vpit;

    if (!pit->active) return;

    while (now_us >= pit->next_tick_us) {
        /* PIT fired - raise IRQ 0 */
        vpic_raise_irq(&vmx->shared->vpic, 0);
        pit->next_tick_us += pit->period_us;
    }
}

/* ================================================================
 * Virtual PIC (8259A) Emulation
 * ================================================================ */

static void vpic_write(struct vpic_state *vpic, prtos_u16_t port, prtos_u8_t val) {
    int chip = (port >= 0xA0) ? 1 : 0;
    int is_data = (port & 1);

    if (!is_data) {
        /* Command port */
        if (val & 0x10) {
            /* ICW1 */
            vpic->icw_step[chip] = 1;
            vpic->imr[chip] = 0;
            vpic->isr[chip] = 0;
            vpic->irr[chip] = 0;
        } else if ((val & 0x18) == 0x08) {
            /* OCW3: bits 4:3 = 01 */
            vpic->read_isr[chip] = (val & 0x03) == 0x03 ? 1 : 0;
        } else if ((val & 0x18) == 0x00) {
            /* OCW2: bits 4:3 = 00. Bits 7:5 = R SL EOI */
            int eoi = (val >> 5) & 1;
            int sl  = (val >> 6) & 1;
            int irq = val & 0x07;
            if (eoi) {
                if (sl) {
                    /* Specific EOI: clear ISR for specified IRQ */
                    vpic->isr[chip] &= ~(1 << irq);
                } else {
                    /* Non-specific EOI: clear highest-priority ISR bit */
                    int i;
                    for (i = 0; i < 8; i++) {
                        if (vpic->isr[chip] & (1 << i)) {
                            vpic->isr[chip] &= ~(1 << i);
                            break;
                        }
                    }
                }
            }
        }
    } else {
        /* Data port */
        if (vpic->icw_step[chip] == 1) {
            /* ICW2: base vector */
            vpic->base_vector[chip] = val & 0xF8;
            kprintf("[VPIC] chip %d base_vector set to 0x%x\n", chip, val & 0xF8);
            vpic->icw_step[chip] = 2;
        } else if (vpic->icw_step[chip] == 2) {
            /* ICW3 */
            vpic->icw_step[chip] = 3;
        } else if (vpic->icw_step[chip] == 3) {
            /* ICW4 */
            vpic->icw4[chip] = val;
            vpic->icw_step[chip] = 0;
        } else {
            /* OCW1: IMR (interrupt mask register) */
            vpic->imr[chip] = val;
        }
    }
}

static prtos_u8_t vpic_read(struct vpic_state *vpic, prtos_u16_t port) {
    int chip = (port >= 0xA0) ? 1 : 0;
    int is_data = (port & 1);

    if (is_data) {
        return vpic->imr[chip];
    } else {
        /* Command port read: depends on OCW3 state */
        if (vpic->read_isr[chip])
            return vpic->isr[chip];
        return vpic->irr[chip];
    }
}

static void vpic_raise_irq(struct vpic_state *vpic, int irq) {
    int chip = (irq >= 8) ? 1 : 0;
    int bit = irq & 7;

    vpic->irr[chip] |= (1 << bit);
}

static int vpic_get_pending_vector(struct vmx_state *vmx) {
    struct vpic_state *vpic = &vmx->shared->vpic;
    int i;

    /* Check master PIC (IRQ 0-7) */
    for (i = 0; i < 8; i++) {
        if ((vpic->irr[0] & (1 << i)) && !(vpic->imr[0] & (1 << i)) && !(vpic->isr[0] & (1 << i))) {
            /* Mark as in-service, clear request */
            vpic->isr[0] |= (1 << i);
            vpic->irr[0] &= ~(1 << i);

            if (i == 2) {
                /* Cascade: check slave PIC (IRQ 8-15) */
                int j;
                for (j = 0; j < 8; j++) {
                    if ((vpic->irr[1] & (1 << j)) && !(vpic->imr[1] & (1 << j)) && !(vpic->isr[1] & (1 << j))) {
                        vpic->isr[1] |= (1 << j);
                        vpic->irr[1] &= ~(1 << j);
                        return vpic->base_vector[1] + j;
                    }
                }
                /* No slave IRQ pending - spurious */
                vpic->isr[0] &= ~(1 << i);
                continue;
            }
            return vpic->base_vector[0] + i;
        }
    }

    return -1;  /* No pending interrupt */
}

/* ================================================================
 * Virtual UART (16550) Emulation
 * ================================================================ */

static void vuart_update_irq(struct vmx_state *vmx) {
    struct vuart_state *uart = &vmx->shared->vuart;
    int raise = 0;

    /* THRE interrupt: IER bit 1 and THR is empty (always true - we instantly consume) */
    if (uart->ier & 0x02)
        raise = 1;

    /* Data available interrupt: IER bit 0 and data in rx buffer */
    if ((uart->ier & 0x01) && (uart->rx_head != uart->rx_tail))
        raise = 1;

    if (raise) {
        vpic_raise_irq(&vmx->shared->vpic, 4);
        g_vuart_irq4_cnt++;
    }
}

static void vuart_poll_host(struct vmx_state *vmx) {
    struct vuart_state *uart = &vmx->shared->vuart;
    /* Poll host UART for incoming data */
    while (in_byte(0x3FD) & 0x01) {
        prtos_u8_t ch = in_byte(0x3F8);
        prtos_u8_t next = (uart->rx_head + 1) % sizeof(uart->rx_buf);
        if (next != uart->rx_tail) {
            uart->rx_buf[uart->rx_head] = ch;
            uart->rx_head = next;
        }
    }
    if (uart->rx_head != uart->rx_tail)
        vuart_update_irq(vmx);
}

static void vuart_write(struct vmx_state *vmx, prtos_u16_t port, prtos_u8_t val) {
    struct vuart_state *uart = &vmx->shared->vuart;
    prtos_u16_t offset = port - 0x3F8;

    if (uart->lcr & 0x80) {
        /* DLAB=1: divisor latch access */
        if (offset == 0) { uart->dll = val; return; }
        if (offset == 1) { uart->dlm = val; return; }
    }

    switch (offset) {
    case 0: /* THR: write character to hypervisor console */
        {
            extern void console_put_char(prtos_u8_t c);
            console_put_char((prtos_u8_t)val);
            g_vuart_tx_cnt++;
        }
        uart->thr_empty = 1;
        vuart_update_irq(vmx);
        break;
    case 1: uart->ier = val; vuart_update_irq(vmx); break;
    case 2: uart->fcr = val; break;
    case 3: uart->lcr = val; break;
    case 4: uart->mcr = val; break;
    case 7: uart->scratch = val; break;
    }
}

static prtos_u8_t vuart_read(struct vmx_state *vmx, prtos_u16_t port) {
    struct vuart_state *uart = &vmx->shared->vuart;
    prtos_u16_t offset = port - 0x3F8;

    if (uart->lcr & 0x80) {
        if (offset == 0) return uart->dll;
        if (offset == 1) return uart->dlm;
    }

    switch (offset) {
    case 0: {                    /* RBR: receive buffer */
        if (uart->rx_head != uart->rx_tail) {
            prtos_u8_t ch = uart->rx_buf[uart->rx_tail];
            uart->rx_tail = (uart->rx_tail + 1) % sizeof(uart->rx_buf);
            return ch;
        }
        return 0;
    }
    case 1: return uart->ier;
    case 2: {                    /* IIR: interrupt identification */
        /* Priority: 1) RX data available, 2) THRE */
        if ((uart->ier & 0x01) && (uart->rx_head != uart->rx_tail))
            return 0x04;         /* IIR: RX data available (priority 2) */
        if (uart->ier & 0x02)
            return 0x02;         /* IIR: THRE interrupt (priority 3) */
        return 0x01;             /* No interrupt pending */
    }
    case 3: return uart->lcr;
    case 4: return uart->mcr;
    case 5: {                    /* LSR */
        prtos_u8_t lsr = 0x60;  /* THR empty + transmitter empty */
        if (uart->rx_head != uart->rx_tail)
            lsr |= 0x01;        /* Data ready */
        return lsr;
    }
    case 6: return 0xB0;         /* MSR: DCD + DSR + CTS asserted */
    case 7: return uart->scratch;
    }
    return 0xFF;
}

/* ================================================================
 * VM-Exit Handlers
 * ================================================================ */

static void vmx_handle_io_exit(kthread_t *k, struct vmx_state *vmx) {
    prtos_u64_t qual = vmx_vmread(VMCS_EXIT_QUALIFICATION);
    prtos_u16_t port = (prtos_u16_t)((qual >> IO_EXIT_PORT_SHIFT) & 0xFFFF);
    int is_in = (qual & IO_EXIT_IN) ? 1 : 0;
    int size = (int)(qual & IO_EXIT_SIZE_MASK) + 1;
    prtos_u32_t len = (prtos_u32_t)vmx_vmread(VMCS_EXIT_INSTRUCTION_LEN);

    (void)k;
    (void)size;

    if (is_in) {
        prtos_u32_t val32 = 0xFF;  /* default for 8-bit unhandled */
        int handled_wide = 0;  /* set if val32 has full result */

        if (port == 0x608 || port == 0x609 || port == 0x60A || port == 0x60B) {
            /* ACPI PM-Timer: 24-bit free-running counter at 3.579545 MHz.
             * Virtual implementation using wall-clock time. */
            prtos_u64_t now = get_sys_clock_usec();
            prtos_u32_t pm_count = (prtos_u32_t)((now * 3579545ULL) / 1000000ULL) & 0x00FFFFFF;
            if (port == 0x608 && size == 4) {
                val32 = pm_count;
                handled_wide = 1;
            } else {
                /* Byte access within the 32-bit PM-Timer register */
                int byte_off = port - 0x608;
                val32 = (pm_count >> (byte_off * 8)) & 0xFF;
            }
        }
        else if (port >= 0x40 && port <= 0x43)
            val32 = vpit_read(vmx, port);
        else if (port == 0x61) {
            /* Speaker/PIT gate register.
             * Bit 0: PIT channel 2 gate (written by guest)
             * Bit 5: PIT channel 2 OUT (read-only, set when counter reaches 0) */
            prtos_u8_t v = vmx->shared->port61;
            struct vpit_state *pit = &vmx->shared->vpit;
            if (pit->active && pit->reload > 0) {
                prtos_u64_t now = get_sys_clock_usec();
                prtos_u64_t elapsed_us;
                if (now >= pit->next_tick_us - pit->period_us)
                    elapsed_us = now - (pit->next_tick_us - pit->period_us);
                else
                    elapsed_us = 0;
                prtos_u64_t elapsed_ticks = elapsed_us * PIT_FREQ_HZ / 1000000ULL;
                if (elapsed_ticks >= pit->reload)
                    v |= 0x20;  /* Set OUT bit - counter expired */
                else
                    v &= ~0x20;  /* Clear OUT bit - still counting */
            }
            val32 = v;
        }
        else if (port == 0x20 || port == 0x21 || port == 0xA0 || port == 0xA1)
            val32 = vpic_read(&vmx->shared->vpic, port);
        else if (port >= 0x3F8 && port <= 0x3FF)
            val32 = vuart_read(vmx, port);
        else if (port == 0x64)
            val32 = 0;  /* keyboard status: no data */
        else if (port == 0xCF8 || (port >= 0xCFC && port <= 0xCFF)) {
            /* PCI config space — passthrough to real hardware (QEMU emulated) */
            if (size == 4) {
                __asm__ __volatile__("inl %w1, %0" : "=a"(val32) : "Nd"(port));
                handled_wide = 1;
            } else if (size == 2) {
                prtos_u16_t v16;
                __asm__ __volatile__("inw %w1, %0" : "=a"(v16) : "Nd"(port));
                val32 = v16;
            } else {
                prtos_u8_t v8;
                __asm__ __volatile__("inb %w1, %b0" : "=a"(v8) : "Nd"(port));
                val32 = v8;
            }
        }
        else
            val32 = 0xFF;

        /* Set result in guest EAX respecting access size */
        if (handled_wide || size == 4) {
            vmx->guest_regs[VMX_REG_RAX] = (vmx->guest_regs[VMX_REG_RAX] & ~0xFFFFFFFFULL) | val32;
        } else if (size == 2) {
            vmx->guest_regs[VMX_REG_RAX] = (vmx->guest_regs[VMX_REG_RAX] & ~0xFFFFULL) | (val32 & 0xFFFF);
        } else {
            vmx->guest_regs[VMX_REG_RAX] = (vmx->guest_regs[VMX_REG_RAX] & ~0xFFULL) | (val32 & 0xFF);
        }
    } else {
        prtos_u8_t val = (prtos_u8_t)(vmx->guest_regs[VMX_REG_RAX] & 0xFF);

        if (port >= 0x40 && port <= 0x43)
            vpit_write(vmx, port, val);
        else if (port == 0x61)
            vmx->shared->port61 = val;  /* Speaker/PIT gate register */
        else if (port == 0x20 || port == 0x21 || port == 0xA0 || port == 0xA1)
            vpic_write(&vmx->shared->vpic, port, val);
        else if (port >= 0x3F8 && port <= 0x3FF)
            vuart_write(vmx, port, val);
        else if (port == 0xCF8 || (port >= 0xCFC && port <= 0xCFF)) {
            /* PCI config space — passthrough to real hardware (QEMU emulated) */
            prtos_u32_t val32_out = (prtos_u32_t)(vmx->guest_regs[VMX_REG_RAX] & 0xFFFFFFFF);
            if (size == 4) {
                __asm__ __volatile__("outl %0, %w1" :: "a"(val32_out), "Nd"(port));
            } else if (size == 2) {
                __asm__ __volatile__("outw %w0, %w1" :: "a"((prtos_u16_t)val32_out), "Nd"(port));
            } else {
                __asm__ __volatile__("outb %b0, %w1" :: "a"((prtos_u8_t)val32_out), "Nd"(port));
            }
        }
        /* Ignore writes to unknown ports (keyboard, etc.) */
    }

    /* Advance guest RIP past the I/O instruction */
    vmx_vmwrite(VMCS_GUEST_RIP, vmx_vmread(VMCS_GUEST_RIP) + len);
}

static void vmx_handle_cpuid_exit(struct vmx_state *vmx) {
    prtos_u32_t eax, ebx, ecx, edx;
    prtos_u32_t leaf = (prtos_u32_t)vmx->guest_regs[VMX_REG_RAX];
    prtos_u32_t subleaf = (prtos_u32_t)vmx->guest_regs[VMX_REG_RCX];
    struct vmx_partition_shared *shared = vmx->shared;

    /* Hide KVM hypervisor signature so guest doesn't try KVM paravirt features.
     * Leaves 0x40000000-0x400000FF are KVM-specific. Return zeros. */
    if (leaf >= 0x40000000 && leaf <= 0x400000FF) {
        eax = 0; ebx = 0; ecx = 0; edx = 0;
    } else {
        eax = leaf;
        ecx = subleaf;
        __asm__ __volatile__("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                                     : "0"(eax), "2"(ecx));
    }

    /* Hide VMX capability, x2APIC, and TSC-deadline timer from guest */
    if (leaf == 1) {
        ecx &= ~(1 << 5);   /* VMX */
        ecx &= ~(1 << 21);  /* x2APIC — force xAPIC (MMIO) mode for LAPIC trap */
        ecx &= ~(1 << 24);  /* TSC-deadline timer — force LAPIC timer via MMIO */
        ecx &= ~(1 << 31);  /* hypervisor present bit - hide from guest */

        /* Report correct APIC ID for this vCPU */
        ebx = (ebx & 0x0000FFFF) | ((prtos_u32_t)vmx->vcpu_id << 24);
        /* Set max logical processors per package */
        ebx = (ebx & 0xFF00FFFF) | (shared->num_vcpus << 16);
        /* Set HTT (hyper-threading technology) bit if multiple vCPUs */
        if (shared->num_vcpus > 1)
            edx |= (1 << 28);
    }

    /* Leaf 0xB: Extended Topology Enumeration — report virtual topology */
    if (leaf == 0xB) {
        if (subleaf == 0) {
            /* SMT level: 1 thread per core */
            eax = 0;  /* bits to shift for next level */
            ebx = 1;  /* num logical processors at this level */
            ecx = (1 << 8) | (subleaf & 0xFF);  /* level type = SMT (1) */
            edx = vmx->vcpu_id;  /* x2APIC ID of current vCPU */
        } else if (subleaf == 1) {
            /* Core level: num_vcpus cores per package */
            prtos_u32_t shift = 0;
            prtos_u32_t n = shared->num_vcpus;
            while ((1U << shift) < n) shift++;
            eax = shift;
            ebx = shared->num_vcpus;
            ecx = (2 << 8) | (subleaf & 0xFF);  /* level type = Core (2) */
            edx = vmx->vcpu_id;
        } else {
            /* Invalid level */
            eax = 0; ebx = 0;
            ecx = subleaf & 0xFF;  /* level type = Invalid (0) */
            edx = vmx->vcpu_id;
        }
    }

    /* Hide INVPCID (leaf 7, subleaf 0, EBX bit 10) - not emulated */
    if (leaf == 7 && subleaf == 0) ebx &= ~(1 << 10);

    /* Hide XSAVES/XRSTORS (leaf 0xD, subleaf 1, EAX bit 3) - not enabled in VMCS */
    if (leaf == 0xD && subleaf == 1) eax &= ~(1 << 3);

    vmx->guest_regs[VMX_REG_RAX] = eax;
    vmx->guest_regs[VMX_REG_RBX] = ebx;
    vmx->guest_regs[VMX_REG_RCX] = ecx;
    vmx->guest_regs[VMX_REG_RDX] = edx;

    prtos_u32_t len = (prtos_u32_t)vmx_vmread(VMCS_EXIT_INSTRUCTION_LEN);
    vmx_vmwrite(VMCS_GUEST_RIP, vmx_vmread(VMCS_GUEST_RIP) + len);
}

static void vmx_handle_hlt_exit(kthread_t *k, struct vmx_state *vmx) {
    (void)k;
    prtos_u32_t len = (prtos_u32_t)vmx_vmread(VMCS_EXIT_INSTRUCTION_LEN);
    vmx_vmwrite(VMCS_GUEST_RIP, vmx_vmread(VMCS_GUEST_RIP) + len);

    /* Clear STI/MOV-SS blocking — HLT has completed, so the shadow
     * is no longer relevant.  Without this the pending-IRQ check
     * can loop indefinitely refusing to inject because interruptibility
     * still has bit 0 (blocking-by-STI) set. */
    vmx_vmwrite(VMCS_GUEST_INTERRUPTIBILITY, 0);

    /* Wait for an interrupt (PIT tick, LAPIC timer, or host timer).
     * Allow host interrupts so the scheduler/LAPIC timer can fire,
     * and poll the PIT/LAPIC timer for pending ticks. */
    {
        prtos_u64_t now = get_sys_clock_usec();
        vpit_tick(vmx, now);
        vlapic_timer_tick(vmx, now);

        /* If a virtual interrupt is already pending, return immediately
         * to allow fast catch-up of missed timer ticks. */
        if ((vmx->shared->vpic.irr[0] & ~vmx->shared->vpic.imr[0] & ~vmx->shared->vpic.isr[0]) ||
            vmx->vlapic_timer.pending ||
            *(volatile prtos_u32_t *)&vmx->ipi_pending_bitmap != 0)
            return;

        /* No pending interrupt — wait briefly for a real host interrupt */
        {
            prtos_u64_t deadline = now + 1000; /* 1ms max wait */
            while (get_sys_clock_usec() < deadline) {
                now = get_sys_clock_usec();
                vpit_tick(vmx, now);
                vlapic_timer_tick(vmx, now);
                if ((vmx->shared->vpic.irr[0] & ~vmx->shared->vpic.imr[0] & ~vmx->shared->vpic.isr[0]) ||
                    vmx->vlapic_timer.pending ||
                    *(volatile prtos_u32_t *)&vmx->ipi_pending_bitmap != 0)
                    break;
                hw_sti();
                __asm__ __volatile__("pause");
                hw_cli();
            }
        }
    }
}

/* ================================================================
 * Interrupt Injection
 * ================================================================ */

static void vmx_check_pending_irq(struct vmx_state *vmx) {
    int vector;

    /* Check if there's already a pending interrupt injection */
    prtos_u64_t entry_info = vmx_vmread(VMCS_ENTRY_INTR_INFO);
    if (entry_info & VMCS_INTR_INFO_VALID) {
        /* Something already being injected — check if PIC has pending, enable int-window */
        struct vpic_state *vpic = &vmx->shared->vpic;
        int i;
        int has_pending = 0;
        for (i = 0; i < 8; i++) {
            if ((vpic->irr[0] & (1 << i)) && !(vpic->imr[0] & (1 << i)) && !(vpic->isr[0] & (1 << i))) {
                has_pending = 1;
                break;
            }
        }
        if (has_pending) {
            g_pic_busy_cnt++;
            prtos_u64_t proc = vmx_vmread(VMCS_PROC_BASED_EXEC_CTRL);
            if (!(proc & CPU_BASED_VIRTUAL_INTR_PENDING)) {
                proc |= CPU_BASED_VIRTUAL_INTR_PENDING;
                vmx_vmwrite(VMCS_PROC_BASED_EXEC_CTRL, proc);
            }
        }
        return;
    }

    /* Get pending vector from virtual PIC */
    vector = vpic_get_pending_vector(vmx);
    if (vector < 0) {
        /* No pending interrupt — make sure interrupt-window exiting is off */
        prtos_u64_t proc = vmx_vmread(VMCS_PROC_BASED_EXEC_CTRL);
        if (proc & CPU_BASED_VIRTUAL_INTR_PENDING) {
            proc &= ~(prtos_u64_t)CPU_BASED_VIRTUAL_INTR_PENDING;
            vmx_vmwrite(VMCS_PROC_BASED_EXEC_CTRL, proc);
        }
        return;
    }

    /* Check if guest has interrupts enabled (RFLAGS.IF) */
    prtos_u64_t guest_rflags = vmx_vmread(VMCS_GUEST_RFLAGS);
    prtos_u32_t guest_interruptibility = (prtos_u32_t)vmx_vmread(VMCS_GUEST_INTERRUPTIBILITY);

    if (!(guest_rflags & _CPU_FLAG_IF) || (guest_interruptibility & 0x3)) {
        /* Guest not ready for interrupts - put the request back */
        int chip = (vector >= vmx->shared->vpic.base_vector[1]) ? 1 : 0;
        int irq = vector - vmx->shared->vpic.base_vector[chip];
        vpic_raise_irq(&vmx->shared->vpic, chip * 8 + irq);
        /* Clear ISR bit since we couldn't deliver */
        vmx->shared->vpic.isr[chip] &= ~(1 << irq);
        g_pic_defer_cnt++;
        /* Enable interrupt-window exiting so we get a VM exit
         * as soon as the guest re-enables interrupts (STI / IRET). */
        prtos_u64_t proc = vmx_vmread(VMCS_PROC_BASED_EXEC_CTRL);
        if (!(proc & CPU_BASED_VIRTUAL_INTR_PENDING)) {
            proc |= CPU_BASED_VIRTUAL_INTR_PENDING;
            vmx_vmwrite(VMCS_PROC_BASED_EXEC_CTRL, proc);
        }
        return;
    }

    g_pic_inj_cnt++;
    /* Guest ready — inject the interrupt */
    vmx_vmwrite(VMCS_ENTRY_INTR_INFO,
                VMCS_INTR_INFO_VALID | VMCS_INTR_TYPE_EXT_INTR | (prtos_u32_t)vector);
    /* Disable interrupt-window exiting */
    prtos_u64_t proc = vmx_vmread(VMCS_PROC_BASED_EXEC_CTRL);
    if (proc & CPU_BASED_VIRTUAL_INTR_PENDING) {
        proc &= ~(prtos_u64_t)CPU_BASED_VIRTUAL_INTR_PENDING;
        vmx_vmwrite(VMCS_PROC_BASED_EXEC_CTRL, proc);
    }
}

/* ================================================================
 * Context switch helpers
 * ================================================================ */

void vmx_switch_pre(void *old_kthread) {
    kthread_t *k = (kthread_t *)old_kthread;
    if (k && k->ctrl.g && k->ctrl.g->karch.vmx) {
        struct vmx_state *vmx = k->ctrl.g->karch.vmx;

        /* Record timestamp so vmx_switch_post can compensate the
         * virtual LAPIC timer for the time spent in the hypervisor. */
        vmx->vlapic_timer.switch_out_us = get_sys_clock_usec();

        vmx_vmclear(&vmx->vmcs_phys);
        vmx->launched = 0;  /* VMCLEAR resets VMCS launch state; need VMLAUNCH next */
    }
}

void vmx_switch_post(void *new_kthread) {
    kthread_t *k = (kthread_t *)new_kthread;
    if (k && k->ctrl.g && k->ctrl.g->karch.vmx) {
        struct vmx_state *vmx = k->ctrl.g->karch.vmx;
        vmx_vmptrld(&vmx->vmcs_phys);

        /* Advance the virtual LAPIC timer's next_fire_us by the idle
         * duration so the guest sees a consistent tick rate relative
         * to its own execution time (skip the scheduler-idle gap). */
        if (vmx->vlapic_timer.active && vmx->vlapic_timer.switch_out_us != 0) {
            prtos_u64_t now = get_sys_clock_usec();
            prtos_u64_t idle_us = now - vmx->vlapic_timer.switch_out_us;
            vmx->vlapic_timer.next_fire_us += idle_us;
            vmx->vlapic_timer.switch_out_us = 0;
        }

        /* Refresh host state */
        vmx_vmwrite(VMCS_HOST_CR3, save_cr3());
        vmx_vmwrite(VMCS_HOST_GDTR_BASE, (prtos_u64_t)(unsigned long)k->ctrl.g->karch.gdt_table);
        vmx_vmwrite(VMCS_HOST_IDTR_BASE, (prtos_u64_t)(unsigned long)k->ctrl.g->karch.hyp_idt_table);
        vmx_vmwrite(VMCS_HOST_TR_BASE, (prtos_u64_t)(unsigned long)&k->ctrl.g->karch.tss);
    }
}

#endif /* CONFIG_VMX */
