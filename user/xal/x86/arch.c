/*
 * FILE: arch.c
 *
 * Architecture initialization functions
 *
 * www.prtos.org
 */
#include <prtos.h>
#include <stdio.h>
#include <irqs.h>

typedef struct {
    prtos_u32_t offset_low : 16,                                     /* offset 0..15 */
        selector : 16, word_count : 8, access : 8, offset_high : 16; /* offset 16..31 */
} gate_desc_t;

#define IDT_ENTRIES (256 + 32)
extern gate_desc_t part_idt_table[IDT_ENTRIES];
extern struct x86_desc_reg part_idt_desc;
extern struct x86_desc_reg part_gdt_desc;

static inline void hw_set_irq_gate(prtos_s32_t e, void *hndl, prtos_u32_t dpl) {
    part_idt_table[e].selector = GUEST_CS_SEL;
    part_idt_table[e].offset_low = (prtos_address_t)hndl & 0xffff;
    part_idt_table[e].offset_high = ((prtos_address_t)hndl >> 16) & 0xffff;
    part_idt_table[e].access = 0x8e | (dpl & 0x3) << 5;
}

static inline void hw_set_trap_gate(prtos_s32_t e, void *hndl, prtos_u32_t dpl) {
    part_idt_table[e].selector = GUEST_CS_SEL;
    part_idt_table[e].offset_low = (prtos_address_t)hndl & 0xffff;
    part_idt_table[e].offset_high = ((prtos_address_t)hndl >> 16) & 0xffff;
    part_idt_table[e].access = 0x8f | (dpl & 0x3) << 5;
}

void init_arch(void) {
    extern void (*vtrap_table[0])(void);
    long irq_nr;

    hw_set_trap_gate(0, vtrap_table[0], 1);
    hw_set_irq_gate(1, vtrap_table[1], 1);
    hw_set_irq_gate(2, vtrap_table[2], 1);
    hw_set_trap_gate(3, vtrap_table[3], 1);
    hw_set_trap_gate(4, vtrap_table[4], 1);
    hw_set_trap_gate(5, vtrap_table[5], 1);
    hw_set_trap_gate(6, vtrap_table[6], 1);
    hw_set_trap_gate(7, vtrap_table[7], 1);
    hw_set_trap_gate(8, vtrap_table[8], 1);
    hw_set_trap_gate(9, vtrap_table[9], 1);
    hw_set_trap_gate(10, vtrap_table[10], 1);
    hw_set_trap_gate(11, vtrap_table[11], 1);
    hw_set_trap_gate(12, vtrap_table[12], 1);
    hw_set_trap_gate(13, vtrap_table[13], 1);
    hw_set_irq_gate(13, vtrap_table[13], 1);
    hw_set_irq_gate(14, vtrap_table[14], 1);
    hw_set_trap_gate(15, vtrap_table[15], 1);
    hw_set_trap_gate(16, vtrap_table[16], 1);
    hw_set_trap_gate(17, vtrap_table[17], 1);
    hw_set_trap_gate(18, vtrap_table[18], 1);
    hw_set_trap_gate(19, vtrap_table[19], 1);

    /* Setting up the HW irqs */
    for (irq_nr = 0x20; irq_nr < IDT_ENTRIES; irq_nr++) hw_set_irq_gate(irq_nr, vtrap_table[irq_nr], 1);

    prtos_x86_load_gdt(&part_gdt_desc);
    prtos_x86_load_idtr(&part_idt_desc);
}

void part_halt(void) {
    prtos_halt_partition(PRTOS_PARTITION_SELF);
}
