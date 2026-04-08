/*
 * FILE: vmx.h
 *
 * Intel VT-x (VMX) definitions for amd64 hardware-assisted virtualization
 *
 * http://www.prtos.org/
 */

#ifndef _PRTOS_ARCH_VMX_H_
#define _PRTOS_ARCH_VMX_H_

#ifdef CONFIG_VMX

#include <arch/processor.h>

/* ---- MSRs ---- */
#define MSR_IA32_VMX_BASIC            0x480
#define MSR_IA32_VMX_PINBASED_CTLS    0x481
#define MSR_IA32_VMX_PROCBASED_CTLS   0x482
#define MSR_IA32_VMX_EXIT_CTLS        0x483
#define MSR_IA32_VMX_ENTRY_CTLS       0x484
#define MSR_IA32_VMX_MISC             0x485
#define MSR_IA32_VMX_CR0_FIXED0       0x486
#define MSR_IA32_VMX_CR0_FIXED1       0x487
#define MSR_IA32_VMX_CR4_FIXED0       0x488
#define MSR_IA32_VMX_CR4_FIXED1       0x489
#define MSR_IA32_VMX_PROCBASED_CTLS2  0x48B
#define MSR_IA32_VMX_EPT_VPID_CAP     0x48C
#define MSR_IA32_VMX_TRUE_PINBASED    0x48D
#define MSR_IA32_VMX_TRUE_PROCBASED   0x48E
#define MSR_IA32_VMX_TRUE_EXIT        0x48F
#define MSR_IA32_VMX_TRUE_ENTRY       0x490

#define MSR_IA32_FEATURE_CONTROL      0x3A
#define FEATURE_CONTROL_LOCKED        (1ULL << 0)
#define FEATURE_CONTROL_VMXON         (1ULL << 2)

/* ---- CR4 VMX enable ---- */
#define _CR4_VMXE  (1UL << 13)

/* ---- Pin-based VM-execution controls ---- */
#define PIN_BASED_EXT_INTR_MASK       (1U << 0)
#define PIN_BASED_NMI_EXITING         (1U << 3)
#define PIN_BASED_VMX_PREEMPTION      (1U << 6)

/* ---- Primary proc-based VM-execution controls ---- */
#define CPU_BASED_VIRTUAL_INTR_PENDING (1U << 2)
#define CPU_BASED_HLT_EXITING         (1U << 7)
#define CPU_BASED_CR8_LOAD_EXITING    (1U << 19)
#define CPU_BASED_CR8_STORE_EXITING   (1U << 20)
#define CPU_BASED_UNCOND_IO_EXITING   (1U << 24)
#define CPU_BASED_USE_IO_BITMAPS      (1U << 25)
#define CPU_BASED_USE_MSR_BITMAPS     (1U << 28)
#define CPU_BASED_MONITOR_TRAP_FLAG   (1U << 27)
#define CPU_BASED_ACTIVATE_SEC_CTLS   (1U << 31)

/* ---- Secondary proc-based VM-execution controls ---- */
#define SECONDARY_EXEC_VIRT_APIC_ACCESS (1U << 0)
#define SECONDARY_EXEC_ENABLE_EPT     (1U << 1)
#define SECONDARY_EXEC_ENABLE_RDTSCP  (1U << 3)
#define SECONDARY_EXEC_UNRESTRICTED   (1U << 7)

/* ---- VM-exit controls ---- */
#define VM_EXIT_HOST_ADDR_SPACE_SIZE  (1U << 9)
#define VM_EXIT_ACK_INTR_ON_EXIT     (1U << 15)
#define VM_EXIT_SAVE_IA32_EFER       (1U << 20)
#define VM_EXIT_LOAD_IA32_EFER       (1U << 21)
#define VM_EXIT_SAVE_PREEMPT_TIMER   (1U << 22)

/* ---- VM-entry controls ---- */
#define VM_ENTRY_LOAD_DEBUG_CTLS      (1U << 2)
#define VM_ENTRY_IA32E_MODE           (1U << 9)
#define VM_ENTRY_LOAD_IA32_EFER       (1U << 15)

/* ---- VMCS field encodings ---- */
/* 16-bit guest-state */
#define VMCS_GUEST_ES_SEL             0x0800
#define VMCS_GUEST_CS_SEL             0x0802
#define VMCS_GUEST_SS_SEL             0x0804
#define VMCS_GUEST_DS_SEL             0x0806
#define VMCS_GUEST_FS_SEL             0x0808
#define VMCS_GUEST_GS_SEL             0x080A
#define VMCS_GUEST_LDTR_SEL           0x080C
#define VMCS_GUEST_TR_SEL             0x080E

/* 16-bit host-state */
#define VMCS_HOST_ES_SEL              0x0C00
#define VMCS_HOST_CS_SEL              0x0C02
#define VMCS_HOST_SS_SEL              0x0C04
#define VMCS_HOST_DS_SEL              0x0C06
#define VMCS_HOST_FS_SEL              0x0C08
#define VMCS_HOST_GS_SEL              0x0C0A
#define VMCS_HOST_TR_SEL              0x0C0C

/* 64-bit control */
#define VMCS_IO_BITMAP_A              0x2000
#define VMCS_IO_BITMAP_B              0x2002
#define VMCS_MSR_BITMAP               0x2004
#define VMCS_EPT_POINTER              0x201A
#define VMCS_APIC_ACCESS_ADDR         0x2014

/* 64-bit guest-state */
#define VMCS_GUEST_VMCS_LINK_PTR      0x2800
#define VMCS_GUEST_IA32_EFER          0x2806

/* 64-bit host-state */
#define VMCS_HOST_IA32_EFER           0x2C02

/* 32-bit control */
#define VMCS_PIN_BASED_EXEC_CTRL      0x4000
#define VMCS_PROC_BASED_EXEC_CTRL     0x4002
#define VMCS_EXCEPTION_BITMAP         0x4004
#define VMCS_EXIT_CONTROLS            0x400C
#define VMCS_ENTRY_CONTROLS           0x4012
#define VMCS_SECONDARY_EXEC_CTRL      0x401E

/* 32-bit exit information */
#define VMCS_EXIT_REASON              0x4402
#define VMCS_EXIT_INTR_INFO           0x4404
#define VMCS_EXIT_INTR_ERROR_CODE     0x4406
#define VMCS_IDT_VECTORING_INFO       0x4408
#define VMCS_EXIT_INSTRUCTION_LEN     0x440C
#define VMCS_EXIT_QUALIFICATION       0x6400

/* 32-bit entry */
#define VMCS_ENTRY_INTR_INFO          0x4016
#define VMCS_ENTRY_EXCEPTION_ERROR    0x4018
#define VMCS_ENTRY_INSTRUCTION_LEN    0x401A

/* 32-bit guest-state */
#define VMCS_GUEST_ES_LIMIT           0x4800
#define VMCS_GUEST_CS_LIMIT           0x4802
#define VMCS_GUEST_SS_LIMIT           0x4804
#define VMCS_GUEST_DS_LIMIT           0x4806
#define VMCS_GUEST_FS_LIMIT           0x4808
#define VMCS_GUEST_GS_LIMIT           0x480A
#define VMCS_GUEST_LDTR_LIMIT         0x480C
#define VMCS_GUEST_TR_LIMIT           0x480E
#define VMCS_GUEST_GDTR_LIMIT         0x4810
#define VMCS_GUEST_IDTR_LIMIT         0x4812
#define VMCS_GUEST_ES_ACCESS          0x4814
#define VMCS_GUEST_CS_ACCESS          0x4816
#define VMCS_GUEST_SS_ACCESS          0x4818
#define VMCS_GUEST_DS_ACCESS          0x481A
#define VMCS_GUEST_FS_ACCESS          0x481C
#define VMCS_GUEST_GS_ACCESS          0x481E
#define VMCS_GUEST_LDTR_ACCESS        0x4820
#define VMCS_GUEST_TR_ACCESS          0x4822
#define VMCS_GUEST_INTERRUPTIBILITY   0x4824
#define VMCS_GUEST_ACTIVITY_STATE     0x4826
#define VMCS_GUEST_SYSENTER_CS        0x482A

/* Natural-width guest-state */
#define VMCS_GUEST_CR0                0x6800
#define VMCS_GUEST_CR3                0x6802
#define VMCS_GUEST_CR4                0x6804
#define VMCS_GUEST_ES_BASE            0x6806
#define VMCS_GUEST_CS_BASE            0x6808
#define VMCS_GUEST_SS_BASE            0x680A
#define VMCS_GUEST_DS_BASE            0x680C
#define VMCS_GUEST_FS_BASE            0x680E
#define VMCS_GUEST_GS_BASE            0x6810
#define VMCS_GUEST_LDTR_BASE          0x6812
#define VMCS_GUEST_TR_BASE            0x6814
#define VMCS_GUEST_GDTR_BASE          0x6816
#define VMCS_GUEST_IDTR_BASE          0x6818
#define VMCS_GUEST_DR7                0x681A
#define VMCS_GUEST_RSP                0x681C
#define VMCS_GUEST_RIP                0x681E
#define VMCS_GUEST_RFLAGS             0x6820
#define VMCS_GUEST_SYSENTER_ESP       0x6824
#define VMCS_GUEST_SYSENTER_EIP       0x6826

/* Natural-width host-state */
#define VMCS_HOST_CR0                 0x6C00
#define VMCS_HOST_CR3                 0x6C02
#define VMCS_HOST_CR4                 0x6C04
#define VMCS_HOST_FS_BASE             0x6C06
#define VMCS_HOST_GS_BASE             0x6C08
#define VMCS_HOST_TR_BASE             0x6C0A
#define VMCS_HOST_GDTR_BASE           0x6C0C
#define VMCS_HOST_IDTR_BASE           0x6C0E
#define VMCS_HOST_SYSENTER_ESP        0x6C10
#define VMCS_HOST_SYSENTER_EIP        0x6C12
#define VMCS_HOST_RSP                 0x6C14
#define VMCS_HOST_RIP                 0x6C16

/* ---- VM-exit reasons ---- */
#define EXIT_REASON_EXCEPTION_NMI     0
#define EXIT_REASON_EXTERNAL_INTR     1
#define EXIT_REASON_TRIPLE_FAULT      2
#define EXIT_REASON_INTERRUPT_WINDOW  7
#define EXIT_REASON_CPUID             10
#define EXIT_REASON_HLT               12
#define EXIT_REASON_INVLPG            14
#define EXIT_REASON_VMCALL            18
#define EXIT_REASON_CR_ACCESS         28
#define EXIT_REASON_IO_INSTRUCTION    30
#define EXIT_REASON_MSR_READ          31
#define EXIT_REASON_MSR_WRITE         32
#define EXIT_REASON_EPT_VIOLATION     48
#define EXIT_REASON_EPT_MISCONFIG     49
#define EXIT_REASON_PREEMPTION_TIMER  52
#define EXIT_REASON_MTF               37
#define EXIT_REASON_APIC_ACCESS       44

/* ---- VMX preemption timer ---- */
#define VMCS_VMX_PREEMPT_TIMER_VALUE  0x482E

/* ---- I/O instruction exit qualification bits ---- */
#define IO_EXIT_SIZE_MASK    0x7
#define IO_EXIT_IN           (1U << 3)
#define IO_EXIT_STRING       (1U << 4)
#define IO_EXIT_REP          (1U << 5)
#define IO_EXIT_PORT_SHIFT   16

/* ---- EPT definitions ---- */
#define EPT_READ              (1ULL << 0)
#define EPT_WRITE             (1ULL << 1)
#define EPT_EXEC              (1ULL << 2)
#define EPT_MEM_TYPE_WB       (6ULL << 3)
#define EPT_MEM_TYPE_UC       (0ULL << 3)
#define EPT_IGNORE_PAT        (1ULL << 6)
#define EPT_LARGE_PAGE        (1ULL << 7)
#define EPT_RWX               (EPT_READ | EPT_WRITE | EPT_EXEC)

/* EPT pointer: WB memory type, 4-level walk */
#define EPTP_WB               (6ULL << 0)
#define EPTP_WALK_4           (3ULL << 3)

/* ---- Entry interrupt-information field ---- */
#define VMCS_INTR_INFO_VALID          (1U << 31)
#define VMCS_INTR_TYPE_EXT_INTR       (0U << 8)
#define VMCS_INTR_TYPE_NMI            (2U << 8)
#define VMCS_INTR_TYPE_HW_EXCEPTION   (3U << 8)
#define VMCS_INTR_TYPE_SW_EXCEPTION   (6U << 8)
#define VMCS_INTR_DELIVER_ERROR       (1U << 11)

/* ---- Segment access-rights (VMX format) ---- */
#define VMX_AR_TYPE_ACCESSES          (1U << 0)
#define VMX_AR_TYPE_READABLE          (1U << 1)
#define VMX_AR_TYPE_WRITABLE          (1U << 1)
#define VMX_AR_TYPE_CONFORMING        (1U << 2)
#define VMX_AR_TYPE_CODE              (1U << 3)
#define VMX_AR_S                      (1U << 4)
#define VMX_AR_DPL_SHIFT              5
#define VMX_AR_P                      (1U << 7)
#define VMX_AR_AVL                    (1U << 12)
#define VMX_AR_L                      (1U << 13)  /* 64-bit code segment */
#define VMX_AR_DB                     (1U << 14)  /* default size */
#define VMX_AR_G                      (1U << 15)  /* granularity */
#define VMX_AR_UNUSABLE               (1U << 16)

/* Flat 32-bit code segment: type=0xB (exec/read/accessed), S=1, DPL=0, P=1, DB=1, G=1 */
#define VMX_AR_CODE32  (VMX_AR_TYPE_ACCESSES | VMX_AR_TYPE_READABLE | VMX_AR_TYPE_CODE | \
                        VMX_AR_S | VMX_AR_P | VMX_AR_DB | VMX_AR_G)
/* Flat 32-bit data segment: type=0x3 (read/write/accessed), S=1, DPL=0, P=1, DB=1, G=1 */
#define VMX_AR_DATA32  (VMX_AR_TYPE_ACCESSES | VMX_AR_TYPE_WRITABLE | \
                        VMX_AR_S | VMX_AR_P | VMX_AR_DB | VMX_AR_G)
/* 16-bit TSS (busy): type=0xB, S=0, P=1 */
#define VMX_AR_TSS32_BUSY (0x0B | VMX_AR_P)

#ifndef __ASSEMBLY__

/* ---- Virtual device state ---- */

/* Virtual 8254 PIT */
struct vpit_state {
    prtos_u32_t counter;      /* programmed count value */
    prtos_u32_t reload;       /* reload value for mode 2 */
    prtos_u8_t  mode;         /* operating mode (0-5) */
    prtos_u8_t  latch_state;  /* byte selection: 0=lobyte, 1=hibyte */
    prtos_u8_t  access;       /* 1=lobyte, 2=hibyte, 3=lo+hi */
    prtos_u8_t  write_lsb;    /* 1 if LSB already written (for lo+hi access) */
    prtos_u8_t  read_lsb;     /* 1 if LSB already read (for lo+hi read) */
    prtos_u64_t next_tick_us; /* next tick time in microseconds */
    prtos_u32_t period_us;    /* period in microseconds */
    prtos_u8_t  active;       /* timer is running */
};

/* Virtual 8259A PIC (pair: master + slave) */
struct vpic_state {
    prtos_u8_t  base_vector[2]; /* ICW2: base interrupt vector */
    prtos_u8_t  imr[2];        /* interrupt mask register */
    prtos_u8_t  isr[2];        /* in-service register */
    prtos_u8_t  irr[2];        /* interrupt request register */
    prtos_u8_t  icw_step[2];   /* ICW initialization step (0=idle, 1-4) */
    prtos_u8_t  icw4[2];       /* ICW4 value */
    prtos_u8_t  elcr[2];       /* edge/level control */
    prtos_u8_t  read_isr[2];   /* OCW3: 1=read ISR, 0=read IRR */
};

/* Virtual 16550 UART */
struct vuart_state {
    prtos_u8_t  thr;           /* transmit holding register */
    prtos_u8_t  ier;           /* interrupt enable register */
    prtos_u8_t  fcr;           /* FIFO control register */
    prtos_u8_t  lcr;           /* line control register */
    prtos_u8_t  mcr;           /* modem control register */
    prtos_u8_t  dll;           /* divisor latch low */
    prtos_u8_t  dlm;           /* divisor latch high */
    prtos_u8_t  scratch;
    prtos_u8_t  thr_empty;     /* THR empty flag for interrupt generation */
    prtos_u8_t  rx_buf[64];    /* receive buffer (ring) */
    prtos_u8_t  rx_head;       /* ring head (write pointer) */
    prtos_u8_t  rx_tail;       /* ring tail (read pointer) */
};

/* Forward declaration */
struct vmx_state;

/* VMX shared partition state (one per partition, shared across vCPUs) */
struct vmx_partition_shared {
    prtos_u64_t eptp;               /* EPT pointer (PML4 phys | flags) */
    void *ept_pml4;                 /* virtual address of EPT PML4 */

    /* Virtual devices (shared across all vCPUs of a partition) */
    struct vpit_state  vpit;
    struct vpic_state  vpic;
    struct vuart_state vuart;
    prtos_u8_t port61;         /* Speaker/PIT gate register (port 0x61) */
    prtos_u32_t partition_id;  /* PRTOS partition ID (0=System, 1=Guest, ...) */

    /* I/O bitmap for selective port pass-through (Guest partition COM2) */
    void *io_bitmap_a;              /* ports 0x0000-0x7FFF (4KB, phys-aligned) */
    void *io_bitmap_b;              /* ports 0x8000-0xFFFF (4KB, phys-aligned) */
    prtos_u64_t io_bitmap_a_phys;
    prtos_u64_t io_bitmap_b_phys;

    /* Per-vCPU VMX state pointers (for SIPI cross-vCPU wakeup) */
    prtos_u32_t num_vcpus;
    struct vmx_state *vcpu_vmx[CONFIG_MAX_NO_VCPUS];

    /* APIC-access page for trapping LAPIC MMIO (used for INIT/SIPI emulation).
     * NULL if not a Linux (SMP) partition. */
    void *apic_access_page;
    prtos_u64_t apic_access_page_phys;

    /* Spinlock protecting APIC-access page during MTF passthrough.
     * Must be held while APIC-access is disabled and the instruction
     * is single-stepped via MTF against the APIC-access page RAM. */
    volatile prtos_u32_t apic_page_lock;
};

/* VMX state per-vCPU */
struct vmx_state {
    prtos_u64_t vmcs_phys;          /* physical address of VMCS region */
    void *vmcs_virt;                /* virtual address of VMCS region */
    prtos_u64_t eptp;               /* EPT pointer (copy from shared, for fast access) */
    void *ept_pml4;                 /* virtual address of EPT PML4 (shared) */

    /* Guest GP registers (not saved in VMCS automatically) */
    prtos_u64_t guest_regs[16];     /* rax,rcx,rdx,rbx,rsp_dummy,rbp,rsi,rdi,r8-r15 */

    prtos_u8_t  launched;           /* 0 = need VMLAUNCH, 1 = use VMRESUME */

    /* VMX preemption timer reload value (~1ms) */
    prtos_u32_t preempt_timer_val;

    /* Inter-vCPU IPI delivery — bitmap for vectors 0xE0-0xFF (bit N = vector 0xE0+N) */
    volatile prtos_u32_t ipi_pending_bitmap;

    /* SMP / SIPI support */
    prtos_u8_t  wait_for_sipi;      /* 1 = secondary vCPU waiting for SIPI */
    prtos_u8_t  sipi_received;      /* 1 = SIPI was received, vector is valid */
    prtos_u8_t  sipi_vector;        /* SIPI vector page (entry = vector * 0x1000) */
    prtos_u8_t  vcpu_id;            /* vCPU index within partition */
    prtos_u8_t  lapic_mtf_pending;  /* 1 = MTF active for LAPIC passthrough */
    prtos_u8_t  lapic_icr_pending;  /* 1 = ICR_LOW write pending via MTF */
    prtos_u8_t  lapic_access_is_write; /* 1 = current MTF is for a write */
    prtos_u16_t lapic_access_offset;   /* LAPIC register offset being accessed */

    /* Pointer to shared partition state (EPT, virtual devices) */
    struct vmx_partition_shared *shared;

    /* Back-pointer to the owning kthread (for un-halting APs on SIPI) */
    void *kthread_ptr;

    /* Virtual devices (aliases for vCPU 0 compatibility — access via shared ptr) */
    struct vpit_state  vpit;
    struct vpic_state  vpic;
    struct vuart_state vuart;
    prtos_u8_t port61;         /* Speaker/PIT gate register (port 0x61) */

    /* Virtual LAPIC timer */
    struct {
        prtos_u32_t lvt_timer;       /* LVT Timer register (0x320) */
        prtos_u32_t initial_count;   /* Initial Count register (0x380) */
        prtos_u32_t divide_config;   /* Divide Configuration register (0x3E0) */
        prtos_u32_t current_count;   /* Current Count register (0x390) */
        prtos_u64_t next_fire_us;    /* system clock time of next fire */
        prtos_u64_t period_us;       /* timer period in microseconds */
        prtos_u64_t switch_out_us;   /* timestamp when vCPU was last switched out */
        prtos_u8_t  active;          /* 1 = timer is running */
        prtos_u8_t  pending;         /* 1 = timer interrupt pending */
    } vlapic_timer;
};

/* Register indices in guest_regs[] */
#define VMX_REG_RAX  0
#define VMX_REG_RCX  1
#define VMX_REG_RDX  2
#define VMX_REG_RBX  3
#define VMX_REG_RSP  4  /* not used - RSP is in VMCS */
#define VMX_REG_RBP  5
#define VMX_REG_RSI  6
#define VMX_REG_RDI  7
#define VMX_REG_R8   8
#define VMX_REG_R9   9
#define VMX_REG_R10  10
#define VMX_REG_R11  11
#define VMX_REG_R12  12
#define VMX_REG_R13  13
#define VMX_REG_R14  14
#define VMX_REG_R15  15

/* ---- VMX inline helpers ---- */

static inline prtos_u64_t vmx_vmread(prtos_u64_t field) {
    prtos_u64_t value;
    __asm__ __volatile__("vmread %1, %0" : "=r"(value) : "r"(field) : "cc");
    return value;
}

static inline void vmx_vmwrite(prtos_u64_t field, prtos_u64_t value) {
    __asm__ __volatile__("vmwrite %1, %0" :: "r"(field), "r"(value) : "cc", "memory");
}

static inline int vmx_vmclear(prtos_u64_t *vmcs_phys) {
    prtos_u8_t err;
    __asm__ __volatile__("vmclear %1; setna %0"
                         : "=qm"(err) : "m"(*vmcs_phys) : "cc", "memory");
    return err;
}

static inline int vmx_vmptrld(prtos_u64_t *vmcs_phys) {
    prtos_u8_t err;
    __asm__ __volatile__("vmptrld %1; setna %0"
                         : "=qm"(err) : "m"(*vmcs_phys) : "cc", "memory");
    return err;
}

static inline int vmx_vmlaunch(void) {
    prtos_u8_t err;
    __asm__ __volatile__("vmlaunch; setna %0" : "=qm"(err) :: "cc", "memory");
    return err;
}

static inline int vmx_vmresume(void) {
    prtos_u8_t err;
    __asm__ __volatile__("vmresume; setna %0" : "=qm"(err) :: "cc", "memory");
    return err;
}

static inline int vmx_vmxon(prtos_u64_t *vmxon_region_phys) {
    prtos_u8_t err;
    __asm__ __volatile__("vmxon %1; setna %0"
                         : "=qm"(err) : "m"(*vmxon_region_phys) : "cc", "memory");
    return err;
}

/* ---- Function prototypes ---- */
extern int vmx_init(void);
extern int vmx_is_enabled(void);
extern int vmx_setup_partition(void *kthread);
extern int vmx_setup_secondary_vcpu(void *kthread, void *vcpu0_kthread);
extern void vmx_run_guest(void *kthread) __attribute__((noreturn));
extern void vmx_switch_pre(void *old_kthread);
extern void vmx_switch_post(void *new_kthread);

/* Assembly entry/exit points */
extern int vmx_guest_enter(struct vmx_state *vmx);

#endif /* __ASSEMBLY__ */
#endif /* CONFIG_VMX */
#endif /* _PRTOS_ARCH_VMX_H_ */
