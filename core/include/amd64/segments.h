/*
 * FILE: segments.h
 *
 * amd64 segmentation
 *
 * www.prtos.org
 */

#ifndef _PRTOS_ARCH_SEGMENTS_H_
#define _PRTOS_ARCH_SEGMENTS_H_

#if (CONFIG_PARTITION_NO_GDT_ENTRIES < 3)
#error Number of GDT entries must be at least 3
#endif

#define EARLY_PRTOS_GDT_ENTRIES 4

#define PRTOS_GDT_ENTRIES 12

#define EARLY_CS_SEL (1 << 3)
#define EARLY_DS_SEL (2 << 3)
#define EARLY_CS32_SEL (3 << 3)  /* 32-bit code segment for AP 16→32 transition */

/* Segment selectors - same layout as x86 for compatibility */
#define PRTOS_HYPERCALL_CALLGATE_SEL ((1 + CONFIG_PARTITION_NO_GDT_ENTRIES) << 3)
#define PRTOS_USER_HYPERCALL_CALLGATE_SEL ((2 + CONFIG_PARTITION_NO_GDT_ENTRIES) << 3)
#define PRTOS_IRET_CALLGATE_SEL ((3 + CONFIG_PARTITION_NO_GDT_ENTRIES) << 3)

#define CS_SEL ((5 + CONFIG_PARTITION_NO_GDT_ENTRIES) << 3)
#define DS_SEL ((6 + CONFIG_PARTITION_NO_GDT_ENTRIES) << 3)
#define GUEST_CS_SEL (((7 + CONFIG_PARTITION_NO_GDT_ENTRIES) << 3) | 3)
#define GUEST_DS_SEL (((8 + CONFIG_PARTITION_NO_GDT_ENTRIES) << 3) | 3)

#define PERCPU_SEL ((9 + CONFIG_PARTITION_NO_GDT_ENTRIES) << 3)
#define TSS_SEL ((10 + CONFIG_PARTITION_NO_GDT_ENTRIES) << 3)
#define PCT_SEL (((11 + CONFIG_PARTITION_NO_GDT_ENTRIES) << 3) | 3)

#ifndef __ASSEMBLY__

/* 8-byte GDT descriptor (legacy format, used for code/data segments) */
struct x86_desc {
    union {
        struct {
            prtos_u32_t seg_limit15_0 : 16, base15_0 : 16, base23_16 : 8, access : 8, seg_limit19_16 : 4, granularity : 4, base31_24 : 8;
        };
        struct {
            prtos_u32_t offset15_0 : 16, seg_selector : 16, word_count : 8,
                _access : 8,
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

/* 16-byte IDT gate descriptor for long mode */
struct x86_gate {
    union {
        struct {
            prtos_u16_t offset15_0;
            prtos_u16_t seg_selector;
            prtos_u8_t ist : 3, reserved0 : 5;
            prtos_u8_t access;
            prtos_u16_t offset31_16;
            prtos_u32_t offset63_32;
            prtos_u32_t reserved1;
        };
        struct {
            prtos_u64_t low;
            prtos_u64_t high;
        };
    };
} __PACKED;

/* 64-bit TSS */
struct x86_tss {
    prtos_u32_t reserved0;
    prtos_u64_t rsp0;
    prtos_u64_t rsp1;
    prtos_u64_t rsp2;
    prtos_u64_t reserved1;
    prtos_u64_t ist1;
    prtos_u64_t ist2;
    prtos_u64_t ist3;
    prtos_u64_t ist4;
    prtos_u64_t ist5;
    prtos_u64_t ist6;
    prtos_u64_t ist7;
    prtos_u64_t reserved2;
    prtos_u16_t reserved3;
    prtos_u16_t io_map_base;
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
#endif

#define get_desc_base(d) (((d)->low & 0xff000000) | (((d)->low & 0xff) << 16) | ((d)->high >> 16))

#define get_desc_limit(d) \
    ((d)->low & (1 << 23)) ? (((((d)->low & 0xf0000) | ((d)->high & 0xffff)) << 12) + 0xfff) : (((d)->low & 0xf0000) | ((d)->high & 0xffff))

#define get_tss_desc(gdtr, tss_sel) ((struct x86_desc *)((unsigned long)(gdtr)->linear_base))[tss_sel / 8]

#define tss_clear_busy(gdtr, tss_sel) get_tss_desc((gdtr), tss_sel).access &= ~(0x2);

#define TSS_IO_MAP_DISABLED (0xFFFF)

#define disable_tss_io_map(tss) (tss)->t.io_map_base = TSS_IO_MAP_DISABLED

#define enable_tss_io_map(tss) (tss)->t.io_map_base = ((prtos_address_t)(unsigned long) & ((tss)->io_map) - (prtos_address_t)(unsigned long)tss)

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

extern struct x86_desc gdt_table[];
extern struct x86_gate hyp_idt_table[IDT_ENTRIES];

/* Load a 64-bit TSS descriptor (occupies 2 GDT entries = 16 bytes) */
static inline void load_tss_desc(struct x86_desc *desc, struct io_tss *tss) {
    prtos_u64_t base = (prtos_u64_t)(unsigned long)tss;
    prtos_u64_t limit = sizeof(struct io_tss) - 1;

    /* Low 8 bytes */
    desc[0].seg_limit15_0 = limit & 0xffff;
    desc[0].base15_0 = base & 0xffff;
    desc[0].base23_16 = (base >> 16) & 0xff;
    desc[0].access = 0x89;  /* Present, 64-bit TSS available */
    desc[0].seg_limit19_16 = (limit >> 16) & 0xf;
    desc[0].granularity = 0;
    desc[0].base31_24 = (base >> 24) & 0xff;

    /* High 8 bytes: base[63:32] + reserved */
    desc[1].high = (prtos_u32_t)(base >> 32);
    desc[1].low = 0;
}

#endif /*__ASSEMBLY__*/
#endif /*_PRTOS_KERNEL_*/

#endif
