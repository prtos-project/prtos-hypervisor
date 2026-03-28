/*
 * FILE: smp.c
 *
 * RISC-V 64-bit SMP support using SBI HSM extension
 *
 * www.prtos.org
 */

#include <processor.h>
#include <stdc.h>

#ifdef CONFIG_SMP
#include <boot.h>
#include <smp.h>

extern void setup_cpu_idtable(prtos_u32_t num_of_cpus);
extern void _secondary_start(void);
extern prtos_u32_t _boot_hartid;  /* physical address in .boot.data */
extern prtos_u32_t _boot_hartid_va;  /* virtual alias from linker script */
extern unsigned long _secondary_start_addr;  /* stored by boot code: PA of _secondary_start */
extern unsigned long _secondary_start_addr_va;  /* virtual alias */

/*
 * SBI HSM extension: hart_start
 * Extension ID: 0x48534D, Function ID: 0
 *
 * Starts a stopped hart at the given start address.
 * The target hart enters S-mode with:
 *   a0 = hartid, a1 = opaque, satp = 0, sstatus.SIE = 0
 */
static long sbi_hart_start(unsigned long hartid, unsigned long start_addr,
                           unsigned long opaque) {
    register unsigned long a0 __asm__("a0") = hartid;
    register unsigned long a1 __asm__("a1") = start_addr;
    register unsigned long a2 __asm__("a2") = opaque;
    register unsigned long a6 __asm__("a6") = 0;          /* function ID */
    register unsigned long a7 __asm__("a7") = 0x48534D;   /* HSM extension */
    __asm__ __volatile__("ecall"
                         : "+r"(a0)
                         : "r"(a1), "r"(a2), "r"(a6), "r"(a7)
                         : "memory");
    return (long)a0;
}

/*
 * Mapping between logical CPU IDs and physical hart IDs.
 * logical_to_hart[0] = boot hartid (BSP)
 * logical_to_hart[1..N-1] = remaining harts in ascending order
 */
static prtos_u32_t logical_to_hart[CONFIG_NO_CPUS];
static prtos_u32_t hart_to_logical[CONFIG_NO_CPUS];

static void build_hart_mapping(prtos_u32_t ncpus, prtos_u32_t boot_hart) {
    prtos_u32_t logical = 1, h;

    logical_to_hart[0] = boot_hart;
    hart_to_logical[boot_hart] = 0;

    for (h = 0; h < ncpus; h++) {
        if (h != boot_hart) {
            logical_to_hart[logical] = h;
            hart_to_logical[h] = logical;
            logical++;
        }
    }
}

void __VBOOT setup_smp(void) {
    prtos_u32_t ncpus, cpu;
    long ret;

    ncpus = prtos_conf_table.hpv.num_of_cpus;
    if (ncpus > CONFIG_NO_CPUS)
        ncpus = CONFIG_NO_CPUS;
    SET_NRCPUS(ncpus);

    if (ncpus <= 1)
        return;

    build_hart_mapping(ncpus, _boot_hartid_va);
    setup_cpu_idtable(ncpus);

    for (cpu = 1; cpu < ncpus; cpu++) {
        prtos_u32_t hartid = logical_to_hart[cpu];
        kprintf("Starting secondary hart %d (logical CPU %d)\n", hartid, cpu);
        /* opaque = logical CPU ID, received as a1 by _secondary_start */
        ret = sbi_hart_start(hartid, _secondary_start_addr_va, cpu);
        if (ret != 0)
            kprintf("Failed to start hart %d: error %ld\n", hartid, ret);
    }
}

prtos_s32_t __VBOOT init_smp(void) {
    return 0;
}

/*
 * SBI IPI extension (0x735049): sbi_send_ipi
 */
static void sbi_send_ipi(unsigned long hart_mask,
                         unsigned long hart_mask_base) {
    register unsigned long a0 __asm__("a0") = hart_mask;
    register unsigned long a1 __asm__("a1") = hart_mask_base;
    register unsigned long a6 __asm__("a6") = 0;
    register unsigned long a7 __asm__("a7") = 0x735049;
    __asm__ __volatile__("ecall"
                         : "+r"(a0)
                         : "r"(a1), "r"(a6), "r"(a7)
                         : "memory");
}

void riscv_send_ipi_to(unsigned long cpu) {
    prtos_u32_t hartid = logical_to_hart[cpu];
    sbi_send_ipi(1UL << hartid, 0);
}

void riscv_send_ipi_all_others(void) {
    unsigned long self_hartid = logical_to_hart[GET_CPU_ID()];
    unsigned long mask = 0;
    prtos_u32_t i;

    for (i = 0; i < GET_NRCPUS(); i++) {
        prtos_u32_t h = logical_to_hart[i];
        if (h != self_hartid)
            mask |= (1UL << h);
    }
    if (mask)
        sbi_send_ipi(mask, 0);
}

#else
prtos_s32_t __VBOOT init_smp(void) {
    return 0;
}
#endif /* CONFIG_SMP */
