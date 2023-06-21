/*
 * FILE: guest.h
 *
 * Guest shared info
 *
 * www.prtos.org
 */

#ifndef _PRTOS_GUEST_H_
#define _PRTOS_GUEST_H_

#define PRTOS_HYPERVISOR_ID 0xff
#define UNCHECKED_ID 0xff
#define KTHREAD_MAGIC 0xa5a55a5a

/* prtos interrupt list*/
#define PRTOS_VT_HW_FIRST (0)
#define PRTOS_VT_HW_LAST (31)
#define PRTOS_VT_HW_MAX (32)

#define PRTOS_VT_HW_INTERNAL_BUS_TRAP_NR (1 + PRTOS_VT_HW_FIRST)
#define PRTOS_VT_HW_UART2_TRAP_NR (2 + PRTOS_VT_HW_FIRST)
#define PRTOS_VT_HW_UART1_TRAP_NR (3 + PRTOS_VT_HW_FIRST)
#define PRTOS_VT_HW_IO_IRQ0_TRAP_NR (4 + PRTOS_VT_HW_FIRST)
#define PRTOS_VT_HW_IO_IRQ1_TRAP_NR (5 + PRTOS_VT_HW_FIRST)
#define PRTOS_VT_HW_IO_IRQ2_TRAP_NR (6 + PRTOS_VT_HW_FIRST)
#define PRTOS_VT_HW_IO_IRQ3_TRAP_NR (7 + PRTOS_VT_HW_FIRST)
#define PRTOS_VT_HW_TIMER1_TRAP_NR (8 + PRTOS_VT_HW_FIRST)
#define PRTOS_VT_HW_TIMER2_TRAP_NR (9 + PRTOS_VT_HW_FIRST)
#define PRTOS_VT_HW_DSU_TRAP_NR (11 + PRTOS_VT_HW_FIRST)
#define PRTOS_VT_HW_PCI_TRAP_NR (14 + PRTOS_VT_HW_FIRST)

#define PRTOS_VT_EXT_FIRST (0)
#define PRTOS_VT_EXT_LAST (31)
#define PRTOS_VT_EXT_MAX (32)

#define PRTOS_VT_EXT_HW_TIMER (0 + PRTOS_VT_EXT_FIRST)
#define PRTOS_VT_EXT_EXEC_TIMER (1 + PRTOS_VT_EXT_FIRST)
#define PRTOS_VT_EXT_WATCHDOG_TIMER (2 + PRTOS_VT_EXT_FIRST)
#define PRTOS_VT_EXT_SHUTDOWN (3 + PRTOS_VT_EXT_FIRST)
#define PRTOS_VT_EXT_SAMPLING_PORT (4 + PRTOS_VT_EXT_FIRST)
#define PRTOS_VT_EXT_QUEUING_PORT (5 + PRTOS_VT_EXT_FIRST)

#define PRTOS_VT_EXT_CYCLIC_SLOT_START (8 + PRTOS_VT_EXT_FIRST)

#define PRTOS_VT_EXT_MEM_PROTECT (16 + PRTOS_VT_EXT_FIRST)

/* Inter-Partition Virtual Interrupts */
#define PRTOS_MAX_IPVI CONFIG_PRTOS_MAX_IPVI
#define PRTOS_VT_EXT_IPVI0 (24 + PRTOS_VT_EXT_FIRST)
#define PRTOS_VT_EXT_IPVI1 (25 + PRTOS_VT_EXT_FIRST)
#define PRTOS_VT_EXT_IPVI2 (26 + PRTOS_VT_EXT_FIRST)
#define PRTOS_VT_EXT_IPVI3 (27 + PRTOS_VT_EXT_FIRST)
#define PRTOS_VT_EXT_IPVI4 (28 + PRTOS_VT_EXT_FIRST)
#define PRTOS_VT_EXT_IPVI5 (29 + PRTOS_VT_EXT_FIRST)
#define PRTOS_VT_EXT_IPVI6 (30 + PRTOS_VT_EXT_FIRST)
#define PRTOS_VT_EXT_IPVI7 (31 + PRTOS_VT_EXT_FIRST)

#define PRTOS_EXT_TRAPS ((1 << (PRTOS_VT_EXT_MEM_PROTECT - PRTOS_VT_EXT_FIRST)))

#ifndef __ASSEMBLY__

#include __PRTOS_INCFLD(arch/atomic.h)
#include __PRTOS_INCFLD(arch/guest.h)
#include __PRTOS_INCFLD(arch/irqs.h)
#include __PRTOS_INCFLD(prtosconf.h)
#include __PRTOS_INCFLD(objects/hm.h)

struct prtos_physical_mem_map {
    prtos_s8_t name[CONFIG_ID_STRING_LENGTH];
    prtos_address_t start_addr;
    prtos_address_t mapped_at;
    prtos_u_size_t size;
#define PRTOS_MEM_AREA_SHARED (1 << 0)
#define PRTOS_MEM_AREA_UNMAPPED (1 << 1)
#define PRTOS_MEM_AREA_READONLY (1 << 2)
#define PRTOS_MEM_AREA_UNCACHEABLE (1 << 3)
#define PRTOS_MEM_AREA_ROM (1 << 4)
#define PRTOS_MEM_AREA_FLAG0 (1 << 5)
#define PRTOS_MEM_AREA_FLAG1 (1 << 6)
#define PRTOS_MEM_AREA_FLAG2 (1 << 7)
#define PRTOS_MEM_AREA_FLAG3 (1 << 8)
#define PRTOS_MEM_AREA_TAGGED (1 << 9)
    prtos_u32_t flags;
};

struct cyclic_sched_info {
    prtos_u32_t num_of_slots;
    prtos_u32_t id;
    prtos_u32_t slot_duration;
};

typedef struct {
    prtos_u32_t magic;
    prtos_u32_t prtos_version;      // PRTOS version
    prtos_u32_t prtos_abi_version;  // PRTOS's abi version
    prtos_u32_t prtos_api_version;  // PRTOS's api version
    prtos_u_size_t part_ctrl_table_size;
    prtos_u32_t reset_counter;
    prtos_u32_t reset_status;
    prtos_u32_t cpu_khz;
#define PCT_GET_PARTITION_ID(pct) ((pct)->id & 0xff)
#define PCT_GET_VCPU_ID(pct) ((pct)->id >> 8)
    prtos_id_t id;
    prtos_id_t num_of_vcpus;
    prtos_u32_t flags;
    prtos_u32_t image_start;
    prtos_u32_t hw_irqs;  // Hw interrupts belonging to the partition
    prtos_s32_t num_of_physical_mem_areas;
    prtos_s32_t num_of_comm_ports;
    prtos_u8_t name[CONFIG_ID_STRING_LENGTH];
    prtos_u32_t iflags;        // As defined by the ARCH (FLAGS in X86)
    prtos_u32_t hw_irqs_pend;  // pending hw irqs
    prtos_u32_t hw_irqs_mask;  // masked hw irqs

    prtos_u32_t ext_irqs_pend;     // pending extended irqs
    prtos_u32_t ext_irqs_to_mask;  // masked extended irqs

    struct pct_arch arch;
    struct cyclic_sched_info cyclic_sched_info;
    prtos_u16_t trap_to_vector[NO_TRAPS];
    prtos_u16_t hw_irq_to_vector[CONFIG_NO_HWIRQS];
    prtos_u16_t ext_irq_to_vector[PRTOS_VT_EXT_MAX];
} partition_control_table_t;

static inline void prtos_set_bit(prtos_word_t bm[], prtos_u32_t bp, prtos_s32_t max_bits) {
    prtos_u32_t e = bp >> PRTOS_LOG2_WORD_SZ, b = bp & ((1 << PRTOS_LOG2_WORD_SZ) - 1);
    if (bp >= max_bits) return;
    bm[e] |= (1 << b);
}

static inline void prtos_clear_bit(prtos_word_t bm[], prtos_u32_t bp, prtos_s32_t max_bits) {
    prtos_u32_t e = bp >> PRTOS_LOG2_WORD_SZ, b = bp & ((1 << PRTOS_LOG2_WORD_SZ) - 1);
    if (bp >= max_bits) return;
    bm[e] &= ~(1 << b);
}

static inline void prtos_clear_bitmap(prtos_word_t bm[], prtos_s32_t max_bits) {
    prtos_u32_t e;
    for (e = 0; e < ((max_bits & ((1 << (PRTOS_LOG2_WORD_SZ)) - 1)) ? (max_bits >> PRTOS_LOG2_WORD_SZ) + 1 : (max_bits >> PRTOS_LOG2_WORD_SZ)); e++) bm[e] = 0;
}

static inline void prtos_set_bitmap(prtos_word_t bm[], prtos_s32_t max_bits) {
    prtos_u32_t e;

    for (e = 0; e < ((max_bits & ((1 << (PRTOS_LOG2_WORD_SZ)) - 1)) ? (max_bits >> PRTOS_LOG2_WORD_SZ) + 1 : (max_bits >> PRTOS_LOG2_WORD_SZ)); e++) bm[e] = ~0;
}

static inline prtos_s32_t prtos_is_bit_set(prtos_word_t bm[], prtos_s32_t bp, prtos_s32_t max_bits) {
    prtos_u32_t e = bp >> PRTOS_LOG2_WORD_SZ, b = bp & ((1 << PRTOS_LOG2_WORD_SZ) - 1);
    if (bp >= max_bits) return -1;

    return (bm[e] & (1 << b)) ? 1 : 0;
}

#endif

#endif
