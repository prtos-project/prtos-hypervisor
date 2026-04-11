/*
 * FILE: prtos_vgic.h
 *
 * Standalone GICv3 virtual distributor/redistributor emulation for PRTOS
 * hw-virt partitions (Linux guests).
 *
 * http://www.prtos.org/
 */

#ifndef _PRTOS_VGIC_H_
#define _PRTOS_VGIC_H_

#include <arch/arch_types.h>

/* Maximum supported vCPUs and interrupt lines */
#define PRTOS_VGIC_MAX_VCPUS    4
#define PRTOS_VGIC_NR_IRQS      128  /* SGI(0-15) + PPI(16-31) + SPI(32-127) */
#define PRTOS_VGIC_NR_SGIS      16
#define PRTOS_VGIC_NR_PPIS      16
#define PRTOS_VGIC_NR_SPIS      (PRTOS_VGIC_NR_IRQS - 32)

/* GICv3 MMIO regions (QEMU virt standard addresses) */
#define PRTOS_VGIC_GICD_BASE    0x08000000ULL
#define PRTOS_VGIC_GICD_SIZE    0x10000ULL
#define PRTOS_VGIC_GICC_BASE    0x08010000ULL
#define PRTOS_VGIC_GICC_SIZE    0x10000ULL
#define PRTOS_VGIC_GICR_BASE   0x080A0000ULL
#define PRTOS_VGIC_GICR_STRIDE 0x20000ULL
#define PRTOS_VGIC_GICR_SIZE   0x200000ULL

/* GICv3 List Register constants */
#define ICH_LR_VIRTUAL_MASK     0xFFFFFFFFULL
#define ICH_LR_HW               (1ULL << 61)
#define ICH_LR_GRP1             (1ULL << 60)
#define ICH_LR_STATE_PENDING    (1ULL << 62)
#define ICH_LR_STATE_ACTIVE     (1ULL << 63)
#define ICH_LR_PRIORITY_SHIFT   48
#define ICH_LR_PRIORITY_MASK    (0xFFULL << ICH_LR_PRIORITY_SHIFT)

#define PRTOS_VGIC_NR_LRS       4

/* Per-interrupt state */
struct prtos_vgic_irq {
    prtos_u8_t enabled;
    prtos_u8_t pending;
    prtos_u8_t active;
    prtos_u8_t priority;
    prtos_u8_t config;       /* 0=level, 2=edge */
    prtos_u8_t group;        /* 0=group0, 1=group1 */
    prtos_u8_t target_vcpu;
    prtos_u8_t _pad;
};

/* Per-vCPU redistributor state */
struct prtos_vgic_vcpu {
    prtos_u32_t gicr_ctlr;
    prtos_u32_t gicr_waker;
    struct prtos_vgic_irq sgis[PRTOS_VGIC_NR_SGIS];
    struct prtos_vgic_irq ppis[PRTOS_VGIC_NR_PPIS];
};

/* Per-partition VGIC state */
struct prtos_vgic_state {
    prtos_u32_t gicd_ctlr;
    prtos_u32_t num_irqs;
    prtos_u32_t num_vcpus;
    prtos_u32_t gicc_bpr;
    prtos_u8_t vcpu_to_pcpu[PRTOS_VGIC_MAX_VCPUS]; /* physical CPU for each vCPU */
    struct prtos_vgic_irq spis[PRTOS_VGIC_NR_SPIS];
    struct prtos_vgic_vcpu vcpu[PRTOS_VGIC_MAX_VCPUS];
};

struct cpu_user_regs;

void prtos_vgic_init(struct prtos_vgic_state *vgic, prtos_u32_t num_vcpus);

int prtos_mmio_dispatch(struct cpu_user_regs *regs, prtos_u64_t gpa,
                        int is_write, int reg, int size);

void prtos_vgic_flush_lrs_current(void);

void prtos_vgic_inject_sgi(struct prtos_vgic_state *vgic,
                            prtos_u32_t target_vcpu, prtos_u32_t intid);

void prtos_vgic_inject_spi(struct prtos_vgic_state *vgic, prtos_u32_t intid);

#endif /* _PRTOS_VGIC_H_ */
