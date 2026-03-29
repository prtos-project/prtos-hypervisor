/*
 * FILE: processor.c
 *
 * Processor (amd64)
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

#define MAX_CPU_ID 16

struct local_id local_id_table[CONFIG_NO_CPUS];
prtos_u32_t cpu_features;
void (*Idle)(void);

struct x86_desc gdt_table[(CONFIG_PARTITION_NO_GDT_ENTRIES + PRTOS_GDT_ENTRIES) * CONFIG_NO_CPUS];
extern struct x86_desc early_gdt_table[];

void _reset(prtos_address_t addr) {
    prtos_address_t *pd_table;
    prtos_address_t page;
    extern prtos_address_t prtos_reserve_one_phys_page[];
    extern void _reset2(prtos_address_t);

    load_hyp_page_table();
    page = ((prtos_address_t)_reset2) & PAGE_MASK;
    pd_table = (prtos_address_t *)_PHYS2VIRT(save_cr3());

    /* For amd64 we need to set up a PD entry for the reset code. 
     * Use large page mapping for simplicity. */
    pd_table[VADDR_TO_PDE_INDEX(page)] = (page & LPAGE_MASK) | _PG_ARCH_PRESENT | _PG_ARCH_PSE | _PG_ARCH_RW;
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
    prtos_u64_t cr0, cr4;
    cr0 = save_cr0();
    cr0 |= _CR0_PE | _CR0_PG | _CR0_WP;
    load_cr0(cr0);
    cr4 = save_cr4();
    cr4 |= _CR4_PAE | _CR4_PGE | _CR4_PSE;
    load_cr4(cr4);
}

void __VBOOT setup_gdt(prtos_s32_t cpu_id) {
    extern void asm_hypercall_dispatch(void);
    extern void asm_iret_handle(void);
    struct x86_desc_reg gdt_desc;

    /* Code segment: L=1, D=0, G=1, P=1, DPL=0, S=1, type=0xB (exec/read/accessed) */
    gdt_table[GDT_ENTRY(cpu_id, CS_SEL)].high = 0x0000ffff;
    gdt_table[GDT_ENTRY(cpu_id, CS_SEL)].low = 0x00af9b00;

    /* Data segment: G=1, D/B=1, P=1, DPL=0, S=1, type=0x3 (read/write/accessed) */
    gdt_table[GDT_ENTRY(cpu_id, DS_SEL)].high = 0x0000ffff;
    gdt_table[GDT_ENTRY(cpu_id, DS_SEL)].low = 0x00cf9300;

    /* Guest code segment: L=1, D=0, G=1, P=1, DPL=3, S=1, type=0xB */
    gdt_table[GDT_ENTRY(cpu_id, GUEST_CS_SEL & ~0x3)].high = 0x0000ffff;
    gdt_table[GDT_ENTRY(cpu_id, GUEST_CS_SEL & ~0x3)].low = 0x00affb00;

    /* Guest data segment: G=1, D/B=1, P=1, DPL=3, S=1, type=0x3 */
    gdt_table[GDT_ENTRY(cpu_id, GUEST_DS_SEL & ~0x3)].high = 0x0000ffff;
    gdt_table[GDT_ENTRY(cpu_id, GUEST_DS_SEL & ~0x3)].low = 0x00cff300;

    /* Per-CPU segment: use GS base MSR instead of segment descriptor base */
    gdt_table[GDT_ENTRY(cpu_id, PERCPU_SEL)].base31_24 = ((prtos_address_t)(unsigned long)&local_id_table[cpu_id] >> 24) & 0xff;
    gdt_table[GDT_ENTRY(cpu_id, PERCPU_SEL)].granularity = 0xc;
    gdt_table[GDT_ENTRY(cpu_id, PERCPU_SEL)].seg_limit19_16 = 0xf;
    gdt_table[GDT_ENTRY(cpu_id, PERCPU_SEL)].access = 0x93;
    gdt_table[GDT_ENTRY(cpu_id, PERCPU_SEL)].base23_16 = ((prtos_address_t)(unsigned long)&local_id_table[cpu_id] >> 16) & 0xff;
    gdt_table[GDT_ENTRY(cpu_id, PERCPU_SEL)].base15_0 = (prtos_address_t)(unsigned long)&local_id_table[cpu_id] & 0xffff;
    gdt_table[GDT_ENTRY(cpu_id, PERCPU_SEL)].seg_limit15_0 = 0xffff;

    /* Also write the GS_BASE MSR for fast access */
    {
        prtos_u64_t gs_base = (prtos_u64_t)(unsigned long)&local_id_table[cpu_id];
        write_msr(MSR_IA32_GS_BASE, (prtos_u32_t)gs_base, (prtos_u32_t)(gs_base >> 32));
    }

    /* Callgate entries - keep for compatibility, though we use int $0x82 */
    gdt_table[GDT_ENTRY(cpu_id, PRTOS_HYPERCALL_CALLGATE_SEL)].seg_selector = CS_SEL;
    gdt_table[GDT_ENTRY(cpu_id, PRTOS_HYPERCALL_CALLGATE_SEL)].offset15_0 = (prtos_address_t)(unsigned long)asm_hypercall_dispatch & 0xffff;
    gdt_table[GDT_ENTRY(cpu_id, PRTOS_HYPERCALL_CALLGATE_SEL)].word_count = 0;
    gdt_table[GDT_ENTRY(cpu_id, PRTOS_HYPERCALL_CALLGATE_SEL)].access = 0x8c | (3 << 5);
    gdt_table[GDT_ENTRY(cpu_id, PRTOS_HYPERCALL_CALLGATE_SEL)].offset31_16 = ((prtos_address_t)(unsigned long)asm_hypercall_dispatch >> 16) & 0xffff;

    gdt_table[GDT_ENTRY(cpu_id, PRTOS_IRET_CALLGATE_SEL)].seg_selector = CS_SEL;
    gdt_table[GDT_ENTRY(cpu_id, PRTOS_IRET_CALLGATE_SEL)].offset15_0 = (prtos_address_t)(unsigned long)asm_iret_handle & 0xffff;
    gdt_table[GDT_ENTRY(cpu_id, PRTOS_IRET_CALLGATE_SEL)].word_count = 0;
    gdt_table[GDT_ENTRY(cpu_id, PRTOS_IRET_CALLGATE_SEL)].access = 0x8c | (3 << 5);
    gdt_table[GDT_ENTRY(cpu_id, PRTOS_IRET_CALLGATE_SEL)].offset31_16 = ((prtos_address_t)(unsigned long)asm_iret_handle >> 16) & 0xffff;

    gdt_desc.limit = (sizeof(struct x86_desc) * (CONFIG_PARTITION_NO_GDT_ENTRIES + PRTOS_GDT_ENTRIES) * CONFIG_NO_CPUS) - 1;
    gdt_desc.linear_base = (prtos_u64_t)(unsigned long)&gdt_table[GDT_ENTRY(cpu_id, 0)];

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
    prtos_u32_t hw_id;
    __asm__ __volatile__("mov %%gs:4, %0\n\t" : "=r"(hw_id));
    return hw_id;
}

void __arch_set_local_id(prtos_u32_t id) {
    __asm__ __volatile__("movl %0, %%gs:0\n\t" ::"r"(id));
}

void __arch_set_local_hw_id(prtos_u32_t hw_id) {
    __asm__ __volatile__("movl %0, %%gs:4\n\t" ::"r"(hw_id));
}

local_processor_t *get_local_processor() {
    return GET_LOCAL_PROCESSOR();
}

#define CLOCK_TICK_RATE 1193180
#define PIT_CH2 0x42
#define PIT_MODE 0x43
#define CALIBRATE_MULT 100
#define CALIBRATE_CYCLES CLOCK_TICK_RATE / CALIBRATE_MULT

__VBOOT prtos_u32_t calculate_cpu_freq(void) {
    prtos_u64_t c_start, c_stop, delta;

    out_byte((in_byte(0x61) & ~0x02) | 0x01, 0x61);
    out_byte(0xb0, PIT_MODE);
    out_byte(CALIBRATE_CYCLES & 0xff, PIT_CH2);
    out_byte(CALIBRATE_CYCLES >> 8, PIT_CH2);
    c_start = read_tsc_load_low();
    delta = read_tsc_load_low();
    in_byte(0x61);
    delta = read_tsc_load_low() - delta;
    while ((in_byte(0x61) & 0x20) == 0)
        ;
    c_stop = read_tsc_load_low();

    return (c_stop - (c_start + delta)) * CALIBRATE_MULT;
}
