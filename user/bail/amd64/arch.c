/*
 * FILE: arch.c
 *
 * Architecture initialization functions for amd64
 *
 * www.prtos.org
 */
#include <prtos.h>
#include <stdio.h>
#include <irqs.h>

/* 16-byte IDT gate descriptor for long mode */
typedef struct {
    prtos_u16_t offset_low;
    prtos_u16_t selector;
    prtos_u8_t ist;
    prtos_u8_t access;
    prtos_u16_t offset_mid;
    prtos_u32_t offset_high;
    prtos_u32_t reserved;
} gate_desc_t;

#define IDT_ENTRIES (256 + 32)
extern gate_desc_t part_idt_table[IDT_ENTRIES];
extern struct x86_desc_reg part_idt_desc;
extern struct x86_desc_reg part_gdt_desc;

static inline void hw_set_irq_gate(prtos_s32_t e, void *hndl, prtos_u32_t dpl) {
    prtos_u64_t offset = (prtos_u64_t)(unsigned long)hndl;
    part_idt_table[e].offset_low = offset & 0xffff;
    part_idt_table[e].selector = GUEST_CS_SEL;
    part_idt_table[e].ist = 0;
    part_idt_table[e].access = 0x8e | ((dpl & 0x3) << 5);
    part_idt_table[e].offset_mid = (offset >> 16) & 0xffff;
    part_idt_table[e].offset_high = (offset >> 32) & 0xffffffff;
    part_idt_table[e].reserved = 0;
}

static inline void hw_set_trap_gate(prtos_s32_t e, void *hndl, prtos_u32_t dpl) {
    prtos_u64_t offset = (prtos_u64_t)(unsigned long)hndl;
    part_idt_table[e].offset_low = offset & 0xffff;
    part_idt_table[e].selector = GUEST_CS_SEL;
    part_idt_table[e].ist = 0;
    part_idt_table[e].access = 0x8f | ((dpl & 0x3) << 5);
    part_idt_table[e].offset_mid = (offset >> 16) & 0xffff;
    part_idt_table[e].offset_high = (offset >> 32) & 0xffffffff;
    part_idt_table[e].reserved = 0;
}

typedef void (*vtrap_table_t)(void);
void init_arch(void) {
    extern vtrap_table_t vtrap_table[];
    long irq_nr;

    hw_set_trap_gate(0, vtrap_table[0], 3);
    hw_set_irq_gate(1, vtrap_table[1], 3);
    hw_set_irq_gate(2, vtrap_table[2], 3);
    hw_set_trap_gate(3, vtrap_table[3], 3);
    hw_set_trap_gate(4, vtrap_table[4], 3);
    hw_set_trap_gate(5, vtrap_table[5], 3);
    hw_set_trap_gate(6, vtrap_table[6], 3);
    hw_set_trap_gate(7, vtrap_table[7], 3);
    hw_set_trap_gate(8, vtrap_table[8], 3);
    hw_set_trap_gate(9, vtrap_table[9], 3);
    hw_set_trap_gate(10, vtrap_table[10], 3);
    hw_set_trap_gate(11, vtrap_table[11], 3);
    hw_set_trap_gate(12, vtrap_table[12], 3);
    hw_set_irq_gate(13, vtrap_table[13], 3);
    hw_set_irq_gate(14, vtrap_table[14], 3);
    hw_set_trap_gate(15, vtrap_table[15], 3);
    hw_set_trap_gate(16, vtrap_table[16], 3);
    hw_set_trap_gate(17, vtrap_table[17], 3);
    hw_set_trap_gate(18, vtrap_table[18], 3);
    hw_set_trap_gate(19, vtrap_table[19], 3);

    for (irq_nr = 0x20; irq_nr < IDT_ENTRIES; irq_nr++) hw_set_irq_gate(irq_nr, vtrap_table[irq_nr], 3);

    prtos_x86_load_gdt(&part_gdt_desc);
    prtos_x86_load_idtr(&part_idt_desc);
}

void part_halt(void) {
    prtos_halt_partition(PRTOS_PARTITION_SELF);
}
