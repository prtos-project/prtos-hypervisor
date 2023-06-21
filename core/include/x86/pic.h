/*
 * FILE: pic.h
 *
 * The PC's PIC
 *
 * www.prtos.org
 */

#ifndef _PRTOS_ARCH_PIC_H_
#define _PRTOS_ARCH_PIC_H_

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

// Without the use of the IO-APIC, the pic controlls interrupts
// from 0 to 15 (16 irqs)
#define PIC_IRQS 16

extern void init_pic(prtos_u8_t master_base, prtos_u8_t slave_base);

#endif
