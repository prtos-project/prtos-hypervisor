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
#include <arch/paging.h>
#include <arch/segments.h>
#include <arch/vmx.h>

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
static int  vpic_get_pending_vector(struct vmx_state *vmx);

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

int vmx_is_enabled(void) {
    return vmx_enabled;
}

/* ================================================================
 * EPT Setup
 * ================================================================ */

static void vmx_setup_ept(struct vmx_state *vmx, prtos_u64_t phys_start, prtos_u64_t size,
                          void *pml4_page, void *pdpt_page, void *pd_page) {
    prtos_u64_t *pml4 = (prtos_u64_t *)pml4_page;
    prtos_u64_t *pdpt = (prtos_u64_t *)pdpt_page;
    prtos_u64_t *pd = (prtos_u64_t *)pd_page;
    prtos_u64_t addr;

    memset(pml4, 0, PAGE_SIZE);
    vmx->ept_pml4 = pml4;

    memset(pdpt, 0, PAGE_SIZE);

    /* PML4[index] → PDPT */
    {
        int pml4_idx = (phys_start >> 39) & 0x1FF;
        pml4[pml4_idx] = _VIRT2PHYS((prtos_u64_t)(unsigned long)pdpt) | EPT_RWX;
    }

    memset(pd, 0, PAGE_SIZE);

    /* PDPT[index] → PD */
    {
        int pdpt_idx = (phys_start >> 30) & 0x1FF;
        pdpt[pdpt_idx] = _VIRT2PHYS((prtos_u64_t)(unsigned long)pd) | EPT_RWX;
    }

    /* Map partition memory using 2MB pages: GPA = HPA (identity map) */
    for (addr = phys_start; addr < phys_start + size; addr += LPAGE_SIZE) {
        int pd_idx = (addr >> 21) & 0x1FF;
        pd[pd_idx] = (addr & ~(LPAGE_SIZE - 1)) | EPT_RWX | EPT_MEM_TYPE_WB | EPT_IGNORE_PAT | EPT_LARGE_PAGE;
    }

    /* Set EPTP: WB memory type, 4-level walk, PML4 physical address */
    vmx->eptp = _VIRT2PHYS((prtos_u64_t)(unsigned long)pml4) | EPTP_WB | EPTP_WALK_4;
}

/* ================================================================
 * VMCS Setup
 * ================================================================ */

int vmx_setup_partition(void *kthread_ptr) {
    kthread_t *k = (kthread_t *)kthread_ptr;
    partition_t *p = get_partition(k);
    struct vmx_state *vmx;
    void *vmcs_region;
    prtos_u32_t rev_id;
    struct prtos_conf_memory_area *mem_area;
    prtos_u64_t part_phys_start, part_size, entry_point;
    void *ept_pml4, *ept_pdpt, *ept_pd;

    if (!vmx_enabled) return -1;

    /* Allocate VMX state from reserved memory pool */
    GET_MEMAZ(vmx, sizeof(struct vmx_state), ALIGNMENT);
    k->ctrl.g->karch.vmx = vmx;

    /* Get partition physical memory info */
    mem_area = &prtos_conf_phys_mem_area_table[p->cfg->physical_memory_areas_offset];
    part_phys_start = mem_area->start_addr;
    part_size = mem_area->size;

    /* The RSW already loaded partition segments to their target physical addresses
     * via load_pef_file() with proper vaddr_to_paddr translation.
     * We just need identity mappings in the hypervisor page tables to access
     * partition memory for EPT setup. No memcpy needed. */
    {
        extern prtos_address_t _page_tables[];
        prtos_u64_t *pd = (prtos_u64_t *)((unsigned long)_page_tables + 2 * PAGE_SIZE);
        prtos_u64_t addr;

        /* Add identity mapping for partition physical area in hypervisor PD.
         * The PD covers the first 1GB (PML4[0]→PDPT[0]→PD), with each entry
         * mapping 2MB. Partition must be in the first 1GB. */
        for (addr = part_phys_start; addr < part_phys_start + part_size; addr += LPAGE_SIZE) {
            prtos_u32_t idx = addr >> PD_SHIFT;
            if (idx < 512 && !pd[idx])
                pd[idx] = addr | _PG_ARCH_PRESENT | _PG_ARCH_RW | _PG_ARCH_PSE;
        }
        /* Flush TLB */
        load_cr3(save_cr3());
    }

    /* Allocate EPT pages from reserved memory pool */
    GET_MEMAZ(ept_pml4, PAGE_SIZE, PAGE_SIZE);
    GET_MEMAZ(ept_pdpt, PAGE_SIZE, PAGE_SIZE);
    GET_MEMAZ(ept_pd, PAGE_SIZE, PAGE_SIZE);

    /* Setup EPT using allocated pages */
    vmx_setup_ept(vmx, part_phys_start, part_size, ept_pml4, ept_pdpt, ept_pd);

    /* Allocate VMCS region from reserved memory pool */
    GET_MEMAZ(vmcs_region, PAGE_SIZE, PAGE_SIZE);
    vmx->vmcs_virt = vmcs_region;
    vmx->vmcs_phys = _VIRT2PHYS((prtos_u64_t)(unsigned long)vmcs_region);

    /* Write revision ID to VMCS */
    rev_id = vmx_get_revision_id();
    *(prtos_u32_t *)vmcs_region = rev_id;

    /* VMCLEAR then VMPTRLD */
    if (vmx_vmclear(&vmx->vmcs_phys)) {
        kprintf("[VMX] VMCLEAR failed\n");
        return -1;
    }
    if (vmx_vmptrld(&vmx->vmcs_phys)) {
        kprintf("[VMX] VMPTRLD failed\n");
        return -1;
    }

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
        prtos_u64_t proc2 = SECONDARY_EXEC_ENABLE_EPT | SECONDARY_EXEC_UNRESTRICTED;
        proc2 = vmx_adjust_controls(proc2, MSR_IA32_VMX_PROCBASED_CTLS2);
        vmx_vmwrite(VMCS_SECONDARY_EXEC_CTRL, proc2);
    }

    /* Exception bitmap: don't trap any exceptions (let guest handle them) */
    vmx_vmwrite(VMCS_EXCEPTION_BITMAP, 0);

    /* MSR bitmap: allocated once from reserved memory pool, shared by all VMX partitions.
     * All zeros = pass-through, except intercept EFER (0xC0000080). */
    if (!vmx_msr_bitmap_initialized) {
        GET_MEMAZ(vmx_msr_bitmap, PAGE_SIZE, PAGE_SIZE);
        {
            /* EFER = MSR 0xC0000080 → high MSR range (0xC0000000+), offset=0x80 */
            int efer_byte_off = 0x80 / 8;  /* byte 16 within section */
            int efer_bit = 0x80 % 8;       /* bit 0 */
            /* Read bitmap for high MSRs starts at byte 1024 */
            vmx_msr_bitmap[1024 + efer_byte_off] |= (1 << efer_bit);
            /* Write bitmap for high MSRs starts at byte 3072 */
            vmx_msr_bitmap[3072 + efer_byte_off] |= (1 << efer_bit);
        }
        vmx_msr_bitmap_initialized = 1;
    }
    vmx_vmwrite(VMCS_MSR_BITMAP, _VIRT2PHYS((prtos_u64_t)(unsigned long)vmx_msr_bitmap));

    /* EPT pointer */
    vmx_vmwrite(VMCS_EPT_POINTER, vmx->eptp);

    /* ---- VM-exit controls ---- */
    {
        prtos_u64_t exit_ctls = VM_EXIT_HOST_ADDR_SPACE_SIZE | VM_EXIT_ACK_INTR_ON_EXIT | VM_EXIT_SAVE_PREEMPT_TIMER;
        exit_ctls = vmx_adjust_controls(exit_ctls, MSR_IA32_VMX_TRUE_EXIT);
        vmx_vmwrite(VMCS_EXIT_CONTROLS, exit_ctls);
    }

    /* ---- VM-entry controls ---- */
    {
        /* Guest starts in 32-bit protected mode (IA-32e mode NOT active) */
        prtos_u64_t entry_ctls = 0;
        entry_ctls = vmx_adjust_controls(entry_ctls, MSR_IA32_VMX_TRUE_ENTRY);
        vmx_vmwrite(VMCS_ENTRY_CONTROLS, entry_ctls);
    }

    /* ---- VMCS link pointer ---- */
    vmx_vmwrite(VMCS_GUEST_VMCS_LINK_PTR, 0xFFFFFFFFFFFFFFFFULL);

    /* ---- Guest state: 32-bit protected mode, paging off ---- */
    entry_point = prtos_conf_boot_partition_table[p->cfg->id].entry_point;
    p->image_start = (prtos_address_t)part_phys_start;



    /* Guest CR0: PE=1, PG=0 — 32-bit protected mode, paging OFF.
     * Unrestricted guest allows PG=0 even if CR0_FIXED0 demands it. */
    {
        prtos_u64_t guest_cr0 = _CR0_PE | _CR0_ET | _CR0_NE;
        vmx_vmwrite(VMCS_GUEST_CR0, guest_cr0);
    }

    vmx_vmwrite(VMCS_GUEST_CR3, 0);

    /* Guest CR4: just VMXE (required by CR4_FIXED0).
     * VMXE is hidden from the guest via CR4 mask/shadow. */
    {
        prtos_u64_t guest_cr4 = 0;
        prtos_u64_t cr4_fixed0 = read_msr(MSR_IA32_VMX_CR4_FIXED0);
        guest_cr4 |= (prtos_u32_t)cr4_fixed0;  /* includes VMXE */
        vmx_vmwrite(VMCS_GUEST_CR4, guest_cr4);
    }
    vmx_vmwrite(VMCS_GUEST_DR7, 0x400);

    vmx_vmwrite(VMCS_GUEST_RIP, entry_point);
    vmx_vmwrite(VMCS_GUEST_RSP, part_phys_start + part_size - 16);
    vmx_vmwrite(VMCS_GUEST_RFLAGS, 0x2);  /* reserved bit only, IF=0 */

    /* Flat 32-bit CS: base=0, limit=0xFFFFFFFF, 32-bit code */
    vmx_vmwrite(VMCS_GUEST_CS_SEL, 0x08);
    vmx_vmwrite(VMCS_GUEST_CS_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_CS_LIMIT, 0xFFFFFFFF);
    vmx_vmwrite(VMCS_GUEST_CS_ACCESS, VMX_AR_CODE32);

    /* Flat 32-bit DS/ES/SS: base=0, limit=0xFFFFFFFF, data */
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

    /* FS, GS: unusable */
    vmx_vmwrite(VMCS_GUEST_FS_SEL, 0);
    vmx_vmwrite(VMCS_GUEST_FS_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_FS_LIMIT, 0);
    vmx_vmwrite(VMCS_GUEST_FS_ACCESS, VMX_AR_UNUSABLE);

    vmx_vmwrite(VMCS_GUEST_GS_SEL, 0);
    vmx_vmwrite(VMCS_GUEST_GS_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_GS_LIMIT, 0);
    vmx_vmwrite(VMCS_GUEST_GS_ACCESS, VMX_AR_UNUSABLE);

    /* LDTR: unusable */
    vmx_vmwrite(VMCS_GUEST_LDTR_SEL, 0);
    vmx_vmwrite(VMCS_GUEST_LDTR_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_LDTR_LIMIT, 0);
    vmx_vmwrite(VMCS_GUEST_LDTR_ACCESS, VMX_AR_UNUSABLE);

    /* TR: busy 32-bit TSS (VMX requires non-null TR with type=11) */
    vmx_vmwrite(VMCS_GUEST_TR_SEL, 0);
    vmx_vmwrite(VMCS_GUEST_TR_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_TR_LIMIT, 0xFF);
    vmx_vmwrite(VMCS_GUEST_TR_ACCESS, VMX_AR_TSS32_BUSY);

    /* GDTR, IDTR: guest will set these up itself */
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
    vmx_vmwrite(0x2808, 0);  /* GUEST_IA32_PERF_GLOBAL_CTRL */
    vmx_vmwrite(0x2804, read_msr(0x277));  /* GUEST_IA32_PAT = current PAT */
    vmx_vmwrite(0x2802, 0);  /* GUEST_IA32_DEBUGCTL */

    /* CR0/CR4 guest/host masks and read shadows.
     * Mask NE+PG in CR0 (NE: VMX-required, PG: need to handle mode transition).
     * Mask VMXE in CR4 so guest can't see/clear it.
     * Shadows show what the guest expects to see. */
    vmx_vmwrite(0x6000, _CR0_NE | _CR0_PG); /* CR0_GUEST_HOST_MASK: trap NE and PG */
    vmx_vmwrite(0x6002, _CR4_VMXE);         /* CR4_GUEST_HOST_MASK: trap VMXE */
    vmx_vmwrite(0x6004, _CR0_PE | _CR0_ET); /* CR0_READ_SHADOW: PE|ET (no NE, no PG) */
    vmx_vmwrite(0x6006, 0);                  /* CR4_READ_SHADOW: guest sees CR4=0 */

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
    vmx_vmwrite(0x2C04, 0);  /* HOST_IA32_PERF_GLOBAL_CTRL */
    vmx_vmwrite(0x2C00, read_msr(0x277));  /* HOST_IA32_PAT = current PAT MSR */
    vmx_vmwrite(0x4C00, 0);  /* HOST_IA32_SYSENTER_CS */
    vmx_vmwrite(0x6C10, 0);  /* HOST_IA32_SYSENTER_ESP */
    vmx_vmwrite(0x6C12, 0);  /* HOST_IA32_SYSENTER_EIP */
    vmx_vmwrite(0x6C06, 0);  /* HOST_FS_BASE */

    /* HOST_RIP: VM-exit entry point */
    vmx_vmwrite(VMCS_HOST_RIP, (prtos_u64_t)(unsigned long)vmx_vmexit_handler);

    /* HOST_RSP, GDT, IDT, TR bases will be refreshed before each VM entry */

    /* Initialize virtual devices */
    vmx->vpit.active = 0;
    vmx->vpic.base_vector[0] = 0x20;  /* PIC1 default: IRQ 0 → vector 0x20 */
    vmx->vpic.base_vector[1] = 0x28;
    vmx->vpic.imr[0] = 0xFF;
    vmx->vpic.imr[1] = 0xFF;
    vmx->launched = 0;

    /* VMX preemption timer: ~1ms VM exits to drive virtual PIT.
     * Timer counts down at TSC rate >> shift. Shift from IA32_VMX_MISC[4:0]. */
    {
        prtos_u64_t vmx_misc = read_msr(0x485); /* IA32_VMX_MISC */
        prtos_u32_t shift = vmx_misc & 0x1F;
        prtos_u64_t tsc_freq = (prtos_u64_t)cpu_khz * 1000ULL;
        /* 1ms = tsc_freq / 1000 TSC ticks. Timer counts at tsc_freq >> shift. */
        vmx->preempt_timer_val = (prtos_u32_t)((tsc_freq / 1000) >> shift);
        if (vmx->preempt_timer_val == 0) vmx->preempt_timer_val = 1;
        vmx_vmwrite(VMCS_VMX_PREEMPT_TIMER_VALUE, vmx->preempt_timer_val);
    }

    kprintf("[VMX] Partition %d: VMCS at phys 0x%llx, EPT phys 0x%llx, entry 0x%llx\n",
            p->cfg->id, vmx->vmcs_phys, vmx->eptp & ~0xFFFULL, entry_point);

    return 0;
}

/* ================================================================
 * VM Run Loop
 * ================================================================ */

void __attribute__((noreturn)) vmx_run_guest(void *kthread_ptr) {
    kthread_t *k = (kthread_t *)kthread_ptr;
    struct vmx_state *vmx = k->ctrl.g->karch.vmx;
    int ret;

    /* Load this partition's VMCS */
    vmx_vmptrld(&vmx->vmcs_phys);

    /* Refresh host state that may change between context switches */
    vmx_vmwrite(VMCS_HOST_CR3, save_cr3());
    vmx_vmwrite(VMCS_HOST_GDTR_BASE, (prtos_u64_t)(unsigned long)k->ctrl.g->karch.gdt_table);
    vmx_vmwrite(VMCS_HOST_IDTR_BASE, (prtos_u64_t)(unsigned long)k->ctrl.g->karch.hyp_idt_table);
    vmx_vmwrite(VMCS_HOST_TR_BASE, (prtos_u64_t)(unsigned long)&k->ctrl.g->karch.tss);
    vmx_vmwrite(VMCS_HOST_GS_BASE, (prtos_u64_t)read_msr(MSR_IA32_GS_BASE));

    /* Set HOST_RSP to a value that vmx_guest_enter expects.
     * vmx_guest_enter pushes callee-saved regs + vmx pointer on the kernel stack.
     * On VM exit, vmx_vmexit_handler pops them back.
     * Set HOST_RSP = current RSP (the assembly will adjust) */

    for (;;) {
        /* Check if there's a pending IRQ to inject */
        vmx_check_pending_irq(vmx);

        /* HOST_RSP is set by the assembly (vmx_guest_enter/vmx_first_launch)
         * after pushing callee-saved registers, right before VMRESUME/VMLAUNCH. */

        /* Enter the guest */
        if (!vmx->launched) {
            vmx->launched = 1;
            ret = vmx_first_launch(vmx);
        } else {
            ret = vmx_guest_enter(vmx);
        }

        if (ret != 0) {
            prtos_u64_t err = vmx_vmread(0x4400); /* VM_INSTRUCTION_ERROR */
            kprintf("[VMX] VM entry failed! error=%lld\n", (long long)err);
            /* Fatal - halt this partition */
            for (;;) { __asm__ __volatile__("cli; hlt"); }
        }

        /* VM exit occurred - handle it */
        {
            prtos_u32_t reason = (prtos_u32_t)vmx_vmread(VMCS_EXIT_REASON) & 0xFFFF;

            switch (reason) {
            case EXIT_REASON_EXCEPTION_NMI: {
                /* Exception or NMI intercepted by exception bitmap */
                prtos_u64_t intr_info = vmx_vmread(0x4404); /* VM_EXIT_INTR_INFO */
                prtos_u32_t vector = intr_info & 0xFF;
                prtos_u64_t err_code = vmx_vmread(0x4406); /* VM_EXIT_INTR_ERROR_CODE */
                kprintf("[VMX] Exception #%d err=0x%llx RIP=0x%llx RSP=0x%llx\n",
                        vector,
                        (unsigned long long)err_code,
                        (unsigned long long)vmx_vmread(VMCS_GUEST_RIP),
                        (unsigned long long)vmx_vmread(VMCS_GUEST_RSP));
                kprintf("[VMX] CS:sel=0x%llx ar=0x%llx IDTR base=0x%llx lim=0x%llx\n",
                        (unsigned long long)vmx_vmread(VMCS_GUEST_CS_SEL),
                        (unsigned long long)vmx_vmread(VMCS_GUEST_CS_ACCESS),
                        (unsigned long long)vmx_vmread(0x6816), /* GUEST_IDTR_BASE */
                        (unsigned long long)vmx_vmread(0x4812)); /* GUEST_IDTR_LIMIT */
                kprintf("[VMX] GDTR base=0x%llx lim=0x%llx EFER=0x%llx\n",
                        (unsigned long long)vmx_vmread(0x6814), /* GUEST_GDTR_BASE */
                        (unsigned long long)vmx_vmread(0x4810), /* GUEST_GDTR_LIMIT */
                        (unsigned long long)vmx_vmread(VMCS_GUEST_IA32_EFER));
                for (;;) { __asm__ __volatile__("cli; hlt"); }
                break;
            }

            case EXIT_REASON_EXTERNAL_INTR:
                /* External hardware interrupt (e.g., LAPIC timer) */
                hw_sti();
                hw_cli();
                {
                    prtos_u64_t now = get_sys_clock_usec();
                    vpit_tick(vmx, now);
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
                /* VMX preemption timer expired - check PIT and reload timer */
                prtos_u64_t now = get_sys_clock_usec();
                vpit_tick(vmx, now);
                vmx_vmwrite(VMCS_VMX_PREEMPT_TIMER_VALUE, vmx->preempt_timer_val);
                break;
            }

            case EXIT_REASON_MSR_READ: {
                prtos_u32_t msr = (prtos_u32_t)vmx->guest_regs[VMX_REG_RCX];
                if (msr == 0xC0000080) {  /* IA32_EFER */
                    prtos_u64_t efer = vmx_vmread(VMCS_GUEST_IA32_EFER);
                    vmx->guest_regs[VMX_REG_RAX] = efer & 0xFFFFFFFF;
                    vmx->guest_regs[VMX_REG_RDX] = efer >> 32;
                } else {
                    /* Pass through other MSRs */
                    prtos_u32_t lo, hi;
                    __asm__ __volatile__("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
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
                if (msr == 0xC0000080) {  /* IA32_EFER */
                    vmx_vmwrite(VMCS_GUEST_IA32_EFER, value);
                } else {
                    /* Pass through other MSRs */
                    prtos_u32_t lo = (prtos_u32_t)value;
                    prtos_u32_t hi = (prtos_u32_t)(value >> 32);
                    __asm__ __volatile__("wrmsr" :: "a"(lo), "d"(hi), "c"(msr));
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

            case EXIT_REASON_EPT_VIOLATION:
                kprintf("[VMX] EPT violation: qual=0x%llx, GPA=0x%llx, GLA=0x%llx\n",
                        (unsigned long long)vmx_vmread(VMCS_EXIT_QUALIFICATION),
                        (unsigned long long)vmx_vmread(0x2400), /* GUEST_PHYSICAL_ADDRESS */
                        (unsigned long long)vmx_vmread(0x640A)); /* GUEST_LINEAR_ADDRESS */
                for (;;) { __asm__ __volatile__("cli; hlt"); }
                break;

            case EXIT_REASON_EPT_MISCONFIG:
                kprintf("[VMX] EPT misconfiguration at GPA=0x%llx\n",
                        (unsigned long long)vmx_vmread(0x2400));
                for (;;) { __asm__ __volatile__("cli; hlt"); }
                break;

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
    struct vpit_state *pit = &vmx->vpit;

    if (port == 0x43) {
        /* Control word */
        prtos_u8_t channel = (val >> 6) & 0x3;
        prtos_u8_t access  = (val >> 4) & 0x3;
        prtos_u8_t mode    = (val >> 1) & 0x7;

        if (channel != 0) return;  /* Only emulate channel 0 */
        pit->mode = mode;
        pit->access = access;
        pit->write_lsb = 0;
        pit->latch_state = 0;
    } else if (port == 0x40) {
        /* Channel 0 data */
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
    (void)vmx;
    (void)port;
    /* Reading PIT counter is rarely used by FreeRTOS; return 0 */
    return 0;
}

static void vpit_tick(struct vmx_state *vmx, prtos_u64_t now_us) {
    struct vpit_state *pit = &vmx->vpit;

    if (!pit->active) return;

    while (now_us >= pit->next_tick_us) {
        /* PIT fired - raise IRQ 0 */
        vpic_raise_irq(&vmx->vpic, 0);
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
        } else if (val == 0x20) {
            /* Non-specific EOI (OCW2) */
            if (vpic->isr[chip]) {
                /* Clear highest-priority in-service bit */
                int i;
                for (i = 0; i < 8; i++) {
                    if (vpic->isr[chip] & (1 << i)) {
                        vpic->isr[chip] &= ~(1 << i);
                        break;
                    }
                }
            }
        }
    } else {
        /* Data port */
        if (vpic->icw_step[chip] == 1) {
            /* ICW2: base vector */
            vpic->base_vector[chip] = val & 0xF8;
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
        return vpic->irr[chip];
    }
}

static void vpic_raise_irq(struct vpic_state *vpic, int irq) {
    int chip = (irq >= 8) ? 1 : 0;
    int bit = irq & 7;

    vpic->irr[chip] |= (1 << bit);
}

static int vpic_get_pending_vector(struct vmx_state *vmx) {
    struct vpic_state *vpic = &vmx->vpic;
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

static void vuart_write(struct vmx_state *vmx, prtos_u16_t port, prtos_u8_t val) {
    struct vuart_state *uart = &vmx->vuart;
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
        }
        break;
    case 1: uart->ier = val; break;
    case 3: uart->lcr = val; break;
    case 4: uart->mcr = val; break;
    case 7: uart->scratch = val; break;
    }
}

static prtos_u8_t vuart_read(struct vmx_state *vmx, prtos_u16_t port) {
    struct vuart_state *uart = &vmx->vuart;
    prtos_u16_t offset = port - 0x3F8;

    if (uart->lcr & 0x80) {
        if (offset == 0) return uart->dll;
        if (offset == 1) return uart->dlm;
    }

    switch (offset) {
    case 0: return 0;            /* RBR: no data */
    case 1: return uart->ier;
    case 2: return 0x01;         /* IIR: no interrupt pending */
    case 3: return uart->lcr;
    case 4: return uart->mcr;
    case 5: return 0x60;         /* LSR: THR empty + transmitter empty */
    case 6: return 0;            /* MSR */
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
        prtos_u8_t val = 0xFF;

        if (port >= 0x40 && port <= 0x43)
            val = vpit_read(vmx, port);
        else if (port == 0x20 || port == 0x21 || port == 0xA0 || port == 0xA1)
            val = vpic_read(&vmx->vpic, port);
        else if (port >= 0x3F8 && port <= 0x3FF)
            val = vuart_read(vmx, port);
        else if (port == 0x64)
            val = 0;  /* keyboard status: no data */
        else if (port == 0xCF8 || port == 0xCFC)
            val = 0xFF; /* PCI: nothing here */

        /* Set result in guest EAX */
        vmx->guest_regs[VMX_REG_RAX] = (vmx->guest_regs[VMX_REG_RAX] & ~0xFFULL) | val;
    } else {
        prtos_u8_t val = (prtos_u8_t)(vmx->guest_regs[VMX_REG_RAX] & 0xFF);

        if (port >= 0x40 && port <= 0x43)
            vpit_write(vmx, port, val);
        else if (port == 0x20 || port == 0x21 || port == 0xA0 || port == 0xA1)
            vpic_write(&vmx->vpic, port, val);
        else if (port >= 0x3F8 && port <= 0x3FF)
            vuart_write(vmx, port, val);
        /* Ignore writes to unknown ports (keyboard, PCI, etc.) */
    }

    /* Advance guest RIP past the I/O instruction */
    vmx_vmwrite(VMCS_GUEST_RIP, vmx_vmread(VMCS_GUEST_RIP) + len);
}

static void vmx_handle_cpuid_exit(struct vmx_state *vmx) {
    prtos_u32_t eax, ebx, ecx, edx;
    prtos_u32_t leaf = (prtos_u32_t)vmx->guest_regs[VMX_REG_RAX];
    prtos_u32_t subleaf = (prtos_u32_t)vmx->guest_regs[VMX_REG_RCX];

    eax = leaf;
    ecx = subleaf;
    __asm__ __volatile__("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                                 : "0"(eax), "2"(ecx));

    /* Hide VMX capability from guest */
    if (leaf == 1) ecx &= ~(1 << 5);

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

    /* Wait for an interrupt (PIT tick or host timer).
     * Allow host interrupts so the scheduler/LAPIC timer can fire,
     * and poll the PIT for pending ticks. */
    {
        prtos_u64_t deadline = get_sys_clock_usec() + 2000; /* 2ms max wait */
        while (get_sys_clock_usec() < deadline) {
            prtos_u64_t now = get_sys_clock_usec();
            vpit_tick(vmx, now);
            if (vmx->vpic.irr[0] & ~vmx->vpic.imr[0] & ~vmx->vpic.isr[0])
                break;
            hw_sti();
            __asm__ __volatile__("pause");
            hw_cli();
        }
    }
}

/* ================================================================
 * Interrupt Injection
 * ================================================================ */

static void vmx_check_pending_irq(struct vmx_state *vmx) {
    int vector;

    /* NOTE: Do NOT call vpit_tick here. PIT ticks are raised only in
     * specific exit handlers (preemption timer, external interrupt, HLT).
     * Calling vpit_tick here would inject a new PIT interrupt immediately
     * after the guest's ISR sends EOI, causing nested interrupts that
     * corrupt FreeRTOS's ullPortInterruptNesting counter. */

    /* Get pending vector from virtual PIC */
    vector = vpic_get_pending_vector(vmx);
    if (vector < 0) return;

    /* Check if guest has interrupts enabled (RFLAGS.IF) */
    prtos_u64_t guest_rflags = vmx_vmread(VMCS_GUEST_RFLAGS);
    prtos_u32_t guest_interruptibility = (prtos_u32_t)vmx_vmread(VMCS_GUEST_INTERRUPTIBILITY);

    if (!(guest_rflags & _CPU_FLAG_IF) || (guest_interruptibility & 0x3)) {
        /* Guest not ready for interrupts - put the request back */
        int chip = (vector >= vmx->vpic.base_vector[1]) ? 1 : 0;
        int irq = vector - vmx->vpic.base_vector[chip];
        vpic_raise_irq(&vmx->vpic, chip * 8 + irq);
        /* Clear ISR bit since we couldn't deliver */
        vmx->vpic.isr[chip] &= ~(1 << irq);
        return;
    }

    /* Inject the interrupt via VMCS entry interrupt-information field */
    vmx_vmwrite(VMCS_ENTRY_INTR_INFO,
                VMCS_INTR_INFO_VALID | VMCS_INTR_TYPE_EXT_INTR | (prtos_u32_t)vector);
}

/* ================================================================
 * Context switch helpers
 * ================================================================ */

void vmx_switch_pre(void *old_kthread) {
    kthread_t *k = (kthread_t *)old_kthread;
    if (k && k->ctrl.g && k->ctrl.g->karch.vmx) {
        struct vmx_state *vmx = k->ctrl.g->karch.vmx;
        vmx_vmclear(&vmx->vmcs_phys);
    }
}

void vmx_switch_post(void *new_kthread) {
    kthread_t *k = (kthread_t *)new_kthread;
    if (k && k->ctrl.g && k->ctrl.g->karch.vmx) {
        struct vmx_state *vmx = k->ctrl.g->karch.vmx;
        vmx_vmptrld(&vmx->vmcs_phys);

        /* Refresh host state */
        vmx_vmwrite(VMCS_HOST_CR3, save_cr3());
        vmx_vmwrite(VMCS_HOST_GDTR_BASE, (prtos_u64_t)(unsigned long)k->ctrl.g->karch.gdt_table);
        vmx_vmwrite(VMCS_HOST_IDTR_BASE, (prtos_u64_t)(unsigned long)k->ctrl.g->karch.hyp_idt_table);
        vmx_vmwrite(VMCS_HOST_TR_BASE, (prtos_u64_t)(unsigned long)&k->ctrl.g->karch.tss);
    }
}

#endif /* CONFIG_VMX */
