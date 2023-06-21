/*
 * FILE: processor.c
 *
 * Processor
 *
 * www.prtos.org
 */

#include <assert.h>
#include <boot.h>
#include <processor.h>
#include <physmm.h>
#include <sched.h>
#include <smp.h>
#include <spinlock.h>
#include <stdc.h>
#include <prtosconf.h>
#include <arch/prtos_def.h>
#include <arch/segments.h>
#include <arch/io.h>

#define MAX_CPU_ID 16 /*Should be customized for each processor */

struct local_id local_id_table[CONFIG_NO_CPUS];
prtos_u32_t cpu_features;
void (*Idle)(void);

struct x86_desc gdt_table[(CONFIG_PARTITION_NO_GDT_ENTRIES + PRTOS_GDT_ENTRIES) * CONFIG_NO_CPUS];
extern struct x86_desc early_gdt_table[];

void _reset(prtos_address_t addr) {
    prtos_address_t *page_table, *page_directory_table;
    prtos_address_t page;
    extern prtos_address_t prtos_reserve_one_phys_page[];
    extern void _reset2(prtos_address_t);

    load_hyp_page_table();
    page = ((prtos_address_t)_reset2) & PAGE_MASK;                 // To get the linear addr where _reset function places
    page_directory_table = (prtos_u32_t *)_PHYS2VIRT(save_cr3());  // To get the linear addr of the page directory able

    // Make the page where _reset2 places point to the page table which is defined by prtos_reserve_one_phys_page array
    page_directory_table[VADDR_TO_PDE_INDEX(page)] = (_VIRT2PHYS((prtos_address_t)prtos_reserve_one_phys_page) & PAGE_MASK) | _PG_ARCH_RW | _PG_ARCH_PRESENT;

    // To get the linear addr of the page table which is defined by prtos_reserve_one_phys_page array
    page_table = (prtos_address_t *)_PHYS2VIRT(page_directory_table[VADDR_TO_PDE_INDEX(page)] & PAGE_MASK);

    // Map the linear addre where _reset function places to the phys addr where _reset function places by levaraging the page table
    // define by prtos_reserve_one_phys_page array
    page_table[VADDR_TO_PTE_INDEX(page)] = page | _PG_ARCH_RW | _PG_ARCH_PRESENT;
    flush_tlb();
    _reset2(addr);
}

void __VBOOT setup_cpu(void) {}

#ifdef CONFIG_SMP
void __VBOOT setup_cpu_idtable(prtos_u32_t num_of_cpus) {
    prtos_u32_t e;

    for (e = 0; e < num_of_cpus; e++) {
        local_id_table[e].id = e;
    }
}
#endif

void __VBOOT setup_cr(void) {
    load_cr0(_CR0_PE | _CR0_PG);
    load_cr4(_CR4_PSE | _CR4_PGE);
}

void __VBOOT setup_gdt(prtos_s32_t cpu_id) {
    extern void asm_hypercall_handle(void);
    extern void asm_iret_handle(void);
    struct x86_desc_reg gdt_desc;

    gdt_table[GDT_ENTRY(cpu_id, CS_SEL)] = early_gdt_table[EARLY_CS_SEL >> 3];
    gdt_table[GDT_ENTRY(cpu_id, DS_SEL)] = early_gdt_table[EARLY_DS_SEL >> 3];

    gdt_table[GDT_ENTRY(cpu_id, GUEST_CS_SEL)].low = (((CONFIG_PRTOS_OFFSET - 1) >> 12) & 0xf0000) | 0xc0bb00;
    gdt_table[GDT_ENTRY(cpu_id, GUEST_CS_SEL)].high = ((CONFIG_PRTOS_OFFSET - 1) >> 12) & 0xffff;
    gdt_table[GDT_ENTRY(cpu_id, GUEST_DS_SEL)].low = (((CONFIG_PRTOS_OFFSET - 1) >> 12) & 0xf0000) | 0xc0b300;
    gdt_table[GDT_ENTRY(cpu_id, GUEST_DS_SEL)].high = ((CONFIG_PRTOS_OFFSET - 1) >> 12) & 0xffff;

    gdt_table[GDT_ENTRY(cpu_id, PERCPU_SEL)].base31_24 = ((prtos_address_t)&local_id_table[cpu_id] >> 24) & 0xff;
    gdt_table[GDT_ENTRY(cpu_id, PERCPU_SEL)].granularity = 0xc;
    gdt_table[GDT_ENTRY(cpu_id, PERCPU_SEL)].segLimit19_16 = 0xf;
    gdt_table[GDT_ENTRY(cpu_id, PERCPU_SEL)].access = 0x93;
    gdt_table[GDT_ENTRY(cpu_id, PERCPU_SEL)].base23_16 = ((prtos_address_t)&local_id_table[cpu_id] >> 16) & 0xff;
    gdt_table[GDT_ENTRY(cpu_id, PERCPU_SEL)].base15_0 = (prtos_address_t)&local_id_table[cpu_id] & 0xffff;
    gdt_table[GDT_ENTRY(cpu_id, PERCPU_SEL)].segLimit15_0 = 0xffff;

    gdt_table[GDT_ENTRY(cpu_id, PRTOS_HYPERCALL_CALLGATE_SEL)].seg_selector = CS_SEL;
    gdt_table[GDT_ENTRY(cpu_id, PRTOS_HYPERCALL_CALLGATE_SEL)].offset15_0 = (prtos_address_t)asm_hypercall_handle & 0xffff;
    gdt_table[GDT_ENTRY(cpu_id, PRTOS_HYPERCALL_CALLGATE_SEL)].word_count = 1;
    gdt_table[GDT_ENTRY(cpu_id, PRTOS_HYPERCALL_CALLGATE_SEL)].access = 0x8c | (2 << 5);
    gdt_table[GDT_ENTRY(cpu_id, PRTOS_HYPERCALL_CALLGATE_SEL)].offset31_16 = ((prtos_address_t)asm_hypercall_handle & 0xffff0000) >> 16;

    gdt_table[GDT_ENTRY(cpu_id, PRTOS_IRET_CALLGATE_SEL)].seg_selector = CS_SEL;
    gdt_table[GDT_ENTRY(cpu_id, PRTOS_IRET_CALLGATE_SEL)].offset15_0 = (prtos_address_t)asm_iret_handle & 0xffff;
    gdt_table[GDT_ENTRY(cpu_id, PRTOS_IRET_CALLGATE_SEL)].word_count = 1;
    gdt_table[GDT_ENTRY(cpu_id, PRTOS_IRET_CALLGATE_SEL)].access = 0x8c | (2 << 5);
    gdt_table[GDT_ENTRY(cpu_id, PRTOS_IRET_CALLGATE_SEL)].offset31_16 = ((prtos_address_t)asm_iret_handle & 0xffff0000) >> 16;

    gdt_desc.limit = (sizeof(struct x86_desc) * (CONFIG_PARTITION_NO_GDT_ENTRIES + PRTOS_GDT_ENTRIES) * CONFIG_NO_CPUS) - 1;
    gdt_desc.linear_base = (prtos_address_t)&gdt_table[GDT_ENTRY(cpu_id, 0)];

    load_gdt(gdt_desc);
    load_seg_selector(CS_SEL, DS_SEL);
    load_gs(PERCPU_SEL);
}

prtos_u32_t __arch_get_local_id(void) {
    prtos_u32_t id;
    __asm__ __volatile__("mov %%gs:0, %0\n\t" : "=r"(id));
    return id;
}

prtos_u32_t __arch_get_local_hw_id(void) {
    prtos_u32_t hwId;
    __asm__ __volatile__("mov %%gs:4, %0\n\t" : "=r"(hwId));
    return hwId;
}

void __arch_set_local_id(prtos_u32_t id) {
    __asm__ __volatile__("movl %0, %%gs:0\n\t" ::"r"(id));
}

void __arch_set_local_hw_id(prtos_u32_t hwId) {
    __asm__ __volatile__("movl %0, %%gs:4\n\t" ::"r"(hwId));
}

local_processor_t *get_local_processor() {
    return GET_LOCAL_PROCESSOR();
}

#define CLOCK_TICK_RATE 1193180 /* Underlying HZ */
#define PIT_CH2 0x42
#define PIT_MODE 0x43
#define CALIBRATE_MULT 100
#define CALIBRATE_CYCLES CLOCK_TICK_RATE / CALIBRATE_MULT

__VBOOT prtos_u32_t calculate_cpu_freq(void) {
    prtos_u64_t c_start, c_stop, delta;

    out_byte((in_byte(0x61) & ~0x02) | 0x01, 0x61);
    out_byte(0xb0, PIT_MODE);                    // binary, mode 0, LSB/MSB, ch 2/
    out_byte(CALIBRATE_CYCLES & 0xff, PIT_CH2);  // LSB
    out_byte(CALIBRATE_CYCLES >> 8, PIT_CH2);    // MSB
    c_start = read_tsc_load_low();
    delta = read_tsc_load_low();
    in_byte(0x61);
    delta = read_tsc_load_low() - delta;
    while ((in_byte(0x61) & 0x20) == 0)
        ;
    c_stop = read_tsc_load_low();

    return (c_stop - (c_start + delta)) * CALIBRATE_MULT;
}
