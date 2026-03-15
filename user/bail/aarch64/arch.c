/*
 * FILE: arch.c
 *
 * AArch64 BAIL architecture initialization
 *
 * www.prtos.org
 */
#include <prtos.h>

void init_arch(void) {
    /* AArch64: no IDT/GDT setup needed */
}

void part_halt(void) {
    prtos_halt_partition(PRTOS_PARTITION_SELF);
}
