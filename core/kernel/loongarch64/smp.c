/*
 * FILE: smp.c
 *
 * LoongArch 64-bit SMP support using IOCSR IPI mailbox
 *
 * http://www.prtos.org/
 */

#include <processor.h>
#include <stdc.h>

#ifdef CONFIG_SMP
#include <boot.h>
#include <smp.h>

extern void setup_cpu_idtable(prtos_u32_t num_of_cpus);
extern void _secondary_start(void);
extern prtos_u32_t _boot_cpuid;
extern prtos_u32_t _boot_cpuid_va;
extern unsigned long _secondary_start_addr;
extern unsigned long _secondary_start_addr_va;

/* IOCSR addresses for IPI */
#define IOCSR_IPI_STATUS  0x1000
#define IOCSR_IPI_EN      0x1004
#define IOCSR_IPI_SET     0x1008
#define IOCSR_IPI_CLEAR   0x100C
#define IOCSR_MBUF0       0x1020
#define IOCSR_IPI_SEND    0x1040
#define IOCSR_MAIL_SEND   0x1048

/*
 * LoongArch IPI send:
 * Write to IOCSR_IPI_SEND register with target CPU ID and action.
 * Format: [25:16] = cpu_id, [4:0] = action (bit number in IPI_STATUS)
 */
static void loongarch_ipi_send(unsigned long cpu, unsigned long action) {
    prtos_u32_t val = (prtos_u32_t)((cpu << 16) | action);
    __asm__ __volatile__(
        "iocsrwr.w %0, %1\n\t"
        : : "r"(val), "r"(IOCSR_IPI_SEND)
    );
}

/*
 * Write a 64-bit entry address to a remote CPU's IOCSR CORE_BUF_20 mailbox.
 *
 * Uses IOCSR_MAIL_SEND (0x1048) which writes 32 bits at a time to
 * the target CPU's mailbox. Format of the 64-bit value written:
 *   [63:32] = 32-bit data
 *   [30:27] = byte write mask (0xF = all 4 bytes)
 *   [25:16] = target CPU ID
 *   [4:2]   = offset within mailbox (0x00=BUF_20 low, 0x04=BUF_20 high)
 *
 * QEMU's slave_boot_code (at 0x1c000000) polls IOCSR CORE_BUF_20,
 * so this is the correct mechanism for waking secondary CPUs.
 */
static void __VBOOT write_remote_mailbox(unsigned long cpu, unsigned long addr) {
    prtos_u64_t val;
    unsigned long iocsr_addr = IOCSR_MAIL_SEND;

    /* Write low 32 bits to CORE_BUF_20 (offset=0x00) */
    val = ((prtos_u64_t)(addr & 0xFFFFFFFFUL) << 32) |
          (0x0UL << 27) |
          ((prtos_u64_t)cpu << 16) |
          0x00;
    __asm__ __volatile__(
        "iocsrwr.d %0, %1\n\t"
        :
        : "r"(val), "r"(iocsr_addr)
    );

    /* Write high 32 bits to CORE_BUF_20+4 (offset=0x04) */
    val = ((prtos_u64_t)((addr >> 32) & 0xFFFFFFFFUL) << 32) |
          (0x0UL << 27) |
          ((prtos_u64_t)cpu << 16) |
          0x04;
    __asm__ __volatile__(
        "iocsrwr.d %0, %1\n\t"
        :
        : "r"(val), "r"(iocsr_addr)
    );
}

void __VBOOT setup_smp(void) {
    prtos_u32_t ncpus, cpu;

    ncpus = prtos_conf_table.hpv.num_of_cpus;
    if (ncpus > CONFIG_NO_CPUS)
        ncpus = CONFIG_NO_CPUS;
    SET_NRCPUS(ncpus);

    if (ncpus <= 1)
        return;

    setup_cpu_idtable(ncpus);

    for (cpu = 1; cpu < ncpus; cpu++) {
        kprintf("Starting secondary CPU %d\n", cpu);

        /*
         * Write _secondary_start PA to target CPU's IOCSR mailbox
         * (CORE_BUF_20) via IOCSR_MAIL_SEND. QEMU's secondary CPUs
         * execute slave_boot_code which polls IOCSR 0x1020.
         */
        write_remote_mailbox(cpu, _secondary_start_addr);
        __asm__ __volatile__("dbar 0" ::: "memory");

        /* Send IPI to wake the target CPU from idle */
        loongarch_ipi_send(cpu, 0x1);
    }
}

prtos_s32_t __VBOOT init_smp(void) {
    return 0;
}

void loongarch_send_ipi_to(unsigned long cpu) {
    loongarch_ipi_send(cpu, 0x1);
}

void loongarch_send_ipi_all_others(void) {
    prtos_u32_t self = GET_CPU_ID();
    prtos_u32_t i;

    for (i = 0; i < GET_NRCPUS(); i++) {
        if (i != self)
            loongarch_ipi_send(i, 0x1);
    }
}

#else
prtos_s32_t __VBOOT init_smp(void) {
    return 0;
}
#endif /* CONFIG_SMP */
