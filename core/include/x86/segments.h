/*
 * FILE: segments.h
 *
 * i386 segmentation
 *
 * www.prtos.org
 */

#ifndef _PRTOS_ARCH_SEGMENTS_H_
#define _PRTOS_ARCH_SEGMENTS_H_

#if (CONFIG_PARTITION_NO_GDT_ENTRIES < 3)
#error Number of GDT entries must be at least 3
#endif

#define EARLY_PRTOS_GDT_ENTRIES 3

// PRTOS GDT's number of entries
#define PRTOS_GDT_ENTRIES 12

#define EARLY_CS_SEL (1 << 3)  // PRTOS's code segment (Ring 0)
#define EARLY_DS_SEL (2 << 3)  // PRTOS's data segment (Ring 0)

// Segment selectors
#define PRTOS_HYPERCALL_CALLGATE_SEL ((1 + CONFIG_PARTITION_NO_GDT_ENTRIES) << 3)
#define PRTOS_USER_HYPERCALL_CALLGATE_SEL ((2 + CONFIG_PARTITION_NO_GDT_ENTRIES) << 3)
#define PRTOS_IRET_CALLGATE_SEL ((3 + CONFIG_PARTITION_NO_GDT_ENTRIES) << 3)

#define CS_SEL ((5 + CONFIG_PARTITION_NO_GDT_ENTRIES) << 3)              // PRTOS's code segment (Ring 0)
#define DS_SEL ((6 + CONFIG_PARTITION_NO_GDT_ENTRIES) << 3)              // PRTOS's data segment (Ring 0)
#define GUEST_CS_SEL (((7 + CONFIG_PARTITION_NO_GDT_ENTRIES) << 3) + 1)  // Guest's code segment (Ring 1)
#define GUEST_DS_SEL (((8 + CONFIG_PARTITION_NO_GDT_ENTRIES) << 3) + 1)  // Guest's data segment (Ring 1)

#define PERCPU_SEL ((9 + CONFIG_PARTITION_NO_GDT_ENTRIES) << 3)
#define TSS_SEL ((10 + CONFIG_PARTITION_NO_GDT_ENTRIES) << 3)
#define PCT_SEL (((11 + CONFIG_PARTITION_NO_GDT_ENTRIES) << 3) + 1)  // Guest's PCT access segment (Ring 1)

#ifndef __ASSEMBLY__

struct x86_desc {
    union {
        struct {
            prtos_u32_t seg_limit15_0 : 16, base15_0 : 16, base23_16 : 8, access : 8, seg_limit19_16 : 4, granularity : 4, base31_24 : 8;
        };
        struct {
            prtos_u32_t offset15_0 : 16, seg_selector : 16, word_count : 8,
                _access : 8,  // <-- Matches with above
                offset31_16 : 16;
        };
        prtos_u64_t x;
        struct {
            prtos_u32_t high;
            prtos_u32_t low;
#define X86DESC_LOW_P_POS 15
#define X86DESC_LOW_P (0x1 << X86DESC_LOW_P_POS)
#define X86DESC_LOW_DPL_POS 13
#define X86DESC_LOW_DPL_MASK (0x3 << X86DESC_LOW_DPL_POS)
#define X86DESC_LOW_S_POS 12
#define X86DESC_LOW_S (0x1 << X86DESC_LOW_S_POS)
#define X86DESC_LOW_TYPE_POS 8
#define X86DESC_LOW_TYPE_MASK (0xf << X86DESC_LOW_TYPE_POS)
        };
    };
} __PACKED;

struct x86_seg_by_field {
    prtos_u32_t base;
    prtos_u32_t seg_limit;
    prtos_u32_t g : 1, d_b : 1, l : 1, avl : 1, p : 1, dpl : 2, s : 1, type : 4;
} __PACKED;

struct x86_gate {
    union {
        struct {
            prtos_u32_t offset15_0 : 16, seg_selector : 16, word_count : 8, access : 8, offset31_16 : 16;
        };
        struct {
            prtos_u64_t l;
        };
        struct {
            prtos_u32_t high0;
            prtos_u32_t low0;
#define X86GATE_LOW0_P_POS 15
#define X86GATE_LOW0_P (0x1 << X86GATE_LOW0_P_POS)
        };
    };
} __PACKED;

struct x86_tss {
    prtos_u16_t back_link, _blh;
    prtos_u32_t sp0;
    prtos_u16_t ss0, _ss0h;
    prtos_u32_t sp1;
    prtos_u16_t ss1, _ss1h;
    prtos_u32_t sp2;
    prtos_u16_t ss2, _ss2h;
    prtos_u32_t cr3;
    prtos_u32_t ip;
    prtos_u32_t flags;
    prtos_u32_t ax;
    prtos_u32_t cx;
    prtos_u32_t dx;
    prtos_u32_t bx;
    prtos_u32_t sp;
    prtos_u32_t bp;
    prtos_u32_t si;
    prtos_u32_t di;
    prtos_u16_t es, _esh;
    prtos_u16_t cs, _csh;
    prtos_u16_t ss, _ssh;
    prtos_u16_t ds, _dsh;
    prtos_u16_t fs, _fsh;
    prtos_u16_t gs, _gsh;
    prtos_u16_t ldt, _ldth;
    prtos_u16_t trace_trap;
    prtos_u16_t io_git_map_offset;
} __PACKED;

struct io_tss {
    struct x86_tss t;
    prtos_u32_t io_map[2048];
};
#endif

#ifdef _PRTOS_KERNEL_
#ifndef __ASSEMBLY__

#ifdef CONFIG_SMP
#define GDT_ENTRY(cpu, e) (((PRTOS_GDT_ENTRIES + CONFIG_PARTITION_NO_GDT_ENTRIES) * (cpu)) + ((e) >> 3))
#else
#define GDT_ENTRY(cpu, e) ((e) >> 3)
#endif /*CONFIG_SMP*/

#define get_desc_base(d) (((d)->low & 0xff000000) | (((d)->low & 0xff) << 16) | ((d)->high >> 16))

#define get_desc_limit(d) \
    ((d)->low & (1 << 23)) ? (((((d)->low & 0xf0000) | ((d)->high & 0xffff)) << 12) + 0xfff) : (((d)->low & 0xf0000) | ((d)->high & 0xffff))

#define get_tss_desc(gdtr, tss_sel) ((struct x86_desc *)((gdtr)->linear_base))[tss_sel / 8]

#define tss_clear_busy(gdtr, tss_sel) get_tss_desc((gdtr), tss_sel).access &= ~(0x2);

#define TSS_IO_MAP_DISABLED (0xFFFF)

#define disable_tss_io_map(tss) (tss)->t.io_git_map_offset = TSS_IO_MAP_DISABLED

#define enable_tss_io_map(tss) (tss)->t.io_git_map_offset = ((prtos_address_t) & ((tss)->io_map) - (prtos_address_t)tss)

#define set_io_map_bit(bit, io_map)         \
    do {                                    \
        prtos_u32_t __entry, __offset;      \
        __entry = bit / 32;                 \
        __offset = bit % 32;                \
        io_map[__entry] |= (1 << __offset); \
    } while (0)

#define clear_io_map_bit(bit, io_map)        \
    do {                                     \
        prtos_u32_t __entry, __offset;       \
        __entry = bit / 32;                  \
        __offset = bit % 32;                 \
        io_map[__entry] &= ~(1 << __offset); \
    } while (0)

#define is_io_map_bit_set(bit, io_map)     \
    do {                                   \
        prtos_u32_t __entry, __offset;     \
        __entry = bit / 32;                \
        __offset = bit % 32;               \
        io_map[__entry] & (1 << __offset); \
    } while (0)

extern struct x86_desc gdt_table[];
extern struct x86_gate hyp_idt_table[IDT_ENTRIES];

static inline void load_tss_desc(struct x86_desc *desc, struct io_tss *tss) {
    desc->seg_limit15_0 = 0xffff & (sizeof(struct io_tss) - 1);
    desc->seg_limit19_16 = 0xf & ((sizeof(struct io_tss) - 1) >> 16);
    desc->base15_0 = (prtos_address_t)tss & 0xffff;
    desc->base23_16 = ((prtos_address_t)tss >> 16) & 0xff;
    desc->base31_24 = ((prtos_address_t)tss >> 24) & 0xff;
    desc->access = 0x89;
    desc->granularity = 0;
}

#endif /*__ASSEMBLY__*/
#endif /*_PRTOS_KERNEL_*/

#endif
