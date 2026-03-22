/*
 * FILE: prtos_vgic.c
 *
 * Standalone GICv3 virtual distributor/redistributor emulation for PRTOS
 * hw-virt partitions running unmodified Linux.
 *
 * Emulates GICD and GICR MMIO registers via stage-2 data abort traps.
 * Uses GICv3 ICH_LR registers for hardware virtual interrupt injection.
 *
 * www.prtos.org
 */

#include <kthread.h>
#include <processor.h>
#include <sched.h>
#include <stdc.h>

#include "prtos_vgic.h"

/* ------------------------------------------------------------------ */
/* GICD register offsets                                               */
/* ------------------------------------------------------------------ */
#define GICD_CTLR           0x0000
#define GICD_TYPER          0x0004
#define GICD_IIDR           0x0008
#define GICD_STATUSR        0x0010
#define GICD_SETSPI_NSR     0x0040
#define GICD_CLRSPI_NSR     0x0048
#define GICD_IGROUPR        0x0080  /* +4 per 32 IRQs */
#define GICD_ISENABLER      0x0100
#define GICD_ICENABLER      0x0180
#define GICD_ISPENDR        0x0200
#define GICD_ICPENDR        0x0280
#define GICD_ISACTIVER      0x0300
#define GICD_ICACTIVER      0x0380
#define GICD_IPRIORITYR     0x0400  /* byte-accessible */
#define GICD_ICFGR          0x0C00  /* 2 bits per IRQ */
#define GICD_IGRPMODR       0x0D00
#define GICD_IROUTER        0x6000  /* 64-bit per SPI */
#define GICD_PIDR2          0xFFE8

/* ------------------------------------------------------------------ */
/* GICR register offsets within RD_base frame                          */
/* ------------------------------------------------------------------ */
#define GICR_CTLR           0x0000
#define GICR_IIDR           0x0004
#define GICR_TYPER          0x0008  /* 64-bit */
#define GICR_STATUSR        0x0010
#define GICR_WAKER          0x0014
#define GICR_PIDR2          0xFFE8

/* SGI_base is at offset 0x10000 within the GICR frame */
#define GICR_SGI_OFFSET     0x10000
#define GICR_IGROUPR0       (GICR_SGI_OFFSET + 0x0080)
#define GICR_ISENABLER0     (GICR_SGI_OFFSET + 0x0100)
#define GICR_ICENABLER0     (GICR_SGI_OFFSET + 0x0180)
#define GICR_ISPENDR0       (GICR_SGI_OFFSET + 0x0200)
#define GICR_ICPENDR0       (GICR_SGI_OFFSET + 0x0280)
#define GICR_ISACTIVER0     (GICR_SGI_OFFSET + 0x0300)
#define GICR_ICACTIVER0     (GICR_SGI_OFFSET + 0x0380)
#define GICR_IPRIORITYR0    (GICR_SGI_OFFSET + 0x0400)  /* 32 bytes for 32 IRQs */
#define GICR_ICFGR0         (GICR_SGI_OFFSET + 0x0C00)
#define GICR_ICFGR1         (GICR_SGI_OFFSET + 0x0C04)
#define GICR_IGRPMODR0      (GICR_SGI_OFFSET + 0x0D00)

/*
 * Direct guest register access — avoids Xen's get_user_reg / set_user_reg
 * which contain BUG_ON(!guest_mode(regs)) checks that fail for PRTOS idle-
 * domain partitions.  The cpu_user_regs layout has x0..x30 contiguous.
 */

/* ------------------------------------------------------------------ */
/* Helpers: access per-interrupt state by INTID                        */
/* ------------------------------------------------------------------ */
static struct prtos_vgic_irq *vgic_get_irq(struct prtos_vgic_state *vgic,
                                            int vcpu_id, int intid) {
    if (intid < 16)
        return &vgic->vcpu[vcpu_id].sgis[intid];
    if (intid < 32)
        return &vgic->vcpu[vcpu_id].ppis[intid - 16];
    if (intid < (int)(32 + PRTOS_VGIC_NR_SPIS))
        return &vgic->spis[intid - 32];
    return 0;
}

/* ------------------------------------------------------------------ */
/* ICH_LR write helpers                                                */
/* ------------------------------------------------------------------ */
static inline void write_ich_lr(int idx, prtos_u64_t val) {
    switch (idx) {
    case 0: asm volatile("msr S3_4_C12_C12_0, %0" :: "r"(val)); break;
    case 1: asm volatile("msr S3_4_C12_C12_1, %0" :: "r"(val)); break;
    case 2: asm volatile("msr S3_4_C12_C12_2, %0" :: "r"(val)); break;
    case 3: asm volatile("msr S3_4_C12_C12_3, %0" :: "r"(val)); break;
    }
}

static inline prtos_u64_t read_ich_lr(int idx) {
    prtos_u64_t val;
    switch (idx) {
    case 0: asm volatile("mrs %0, S3_4_C12_C12_0" : "=r"(val)); break;
    case 1: asm volatile("mrs %0, S3_4_C12_C12_1" : "=r"(val)); break;
    case 2: asm volatile("mrs %0, S3_4_C12_C12_2" : "=r"(val)); break;
    case 3: asm volatile("mrs %0, S3_4_C12_C12_3" : "=r"(val)); break;
    default: val = 0; break;
    }
    return val;
}

/* ------------------------------------------------------------------ */
/* Initialization                                                      */
/* ------------------------------------------------------------------ */
void prtos_vgic_init(struct prtos_vgic_state *vgic, prtos_u32_t num_vcpus) {
    int i, v;
    memset(vgic, 0, sizeof(*vgic));
    vgic->num_vcpus = num_vcpus;
    vgic->num_irqs = PRTOS_VGIC_NR_IRQS;
    vgic->gicd_ctlr = 0;  /* disabled until guest enables */

    /* Default all IRQs to group 1, priority 0xA0, disabled */
    for (i = 0; i < PRTOS_VGIC_NR_SPIS; i++) {
        vgic->spis[i].group = 1;
        vgic->spis[i].priority = 0xA0;
    }
    for (v = 0; v < (int)num_vcpus; v++) {
        vgic->vcpu[v].gicr_waker = 0x6;  /* ChildrenAsleep=1, ProcessorSleep=1 */
        for (i = 0; i < PRTOS_VGIC_NR_SGIS; i++) {
            vgic->vcpu[v].sgis[i].group = 1;
            vgic->vcpu[v].sgis[i].priority = 0xA0;
            vgic->vcpu[v].sgis[i].enabled = 1;  /* SGIs always enabled */
        }
        for (i = 0; i < PRTOS_VGIC_NR_PPIS; i++) {
            vgic->vcpu[v].ppis[i].group = 1;
            vgic->vcpu[v].ppis[i].priority = 0xA0;
        }
    }
}

/* ------------------------------------------------------------------ */
/* GICD MMIO emulation                                                 */
/* ------------------------------------------------------------------ */
static int gicd_mmio_read(struct prtos_vgic_state *vgic, prtos_u64_t offset,
                          int size, prtos_u64_t *val) {
    prtos_u32_t reg;
    int idx;

    switch (offset) {
    case GICD_CTLR:
        /* ARE_NS = bit 4, EnableGrp1NS = bit 1 */
        *val = vgic->gicd_ctlr | (1U << 4);  /* ARE always set */
        return 0;
    case GICD_TYPER:
        /* ITLinesNumber = (num_irqs/32 - 1), CPUNumber = num_vcpus - 1 */
        *val = ((vgic->num_irqs / 32 - 1) & 0x1F) |
               (((vgic->num_vcpus - 1) & 0x7) << 5) |
               (9U << 19);  /* IDbits = 9 (10-bit INTID range) */
        return 0;
    case GICD_IIDR:
        *val = 0x0200043B;  /* ARM, rev 2 */
        return 0;
    case GICD_STATUSR:
        *val = 0;
        return 0;
    case GICD_PIDR2:
        *val = 0x30;  /* ArchRev = 3 (GICv3) */
        return 0;
    }

    /* GICD_IGROUPR: 1 bit per IRQ, 32 IRQs per register */
    if (offset >= GICD_IGROUPR && offset < GICD_IGROUPR + (PRTOS_VGIC_NR_IRQS / 8)) {
        int base_irq = (int)((offset - GICD_IGROUPR) * 8);
        reg = 0;
        for (idx = 0; idx < 32 && (base_irq + idx) < (int)vgic->num_irqs; idx++) {
            struct prtos_vgic_irq *irq = vgic_get_irq(vgic, 0, base_irq + idx);
            if (irq && irq->group) reg |= (1U << idx);
        }
        *val = reg;
        return 0;
    }

    /* GICD_ISENABLER / GICD_ICENABLER */
    if ((offset >= GICD_ISENABLER && offset < GICD_ISENABLER + (PRTOS_VGIC_NR_IRQS / 8)) ||
        (offset >= GICD_ICENABLER && offset < GICD_ICENABLER + (PRTOS_VGIC_NR_IRQS / 8))) {
        prtos_u64_t base_off = (offset >= GICD_ICENABLER) ?
                               (offset - GICD_ICENABLER) : (offset - GICD_ISENABLER);
        int base_irq = (int)(base_off * 8);
        reg = 0;
        for (idx = 0; idx < 32 && (base_irq + idx) < (int)vgic->num_irqs; idx++) {
            struct prtos_vgic_irq *irq = vgic_get_irq(vgic, 0, base_irq + idx);
            if (irq && irq->enabled) reg |= (1U << idx);
        }
        *val = reg;
        return 0;
    }

    /* GICD_ISPENDR / GICD_ICPENDR */
    if ((offset >= GICD_ISPENDR && offset < GICD_ISPENDR + (PRTOS_VGIC_NR_IRQS / 8)) ||
        (offset >= GICD_ICPENDR && offset < GICD_ICPENDR + (PRTOS_VGIC_NR_IRQS / 8))) {
        prtos_u64_t base_off = (offset >= GICD_ICPENDR) ?
                               (offset - GICD_ICPENDR) : (offset - GICD_ISPENDR);
        int base_irq = (int)(base_off * 8);
        reg = 0;
        for (idx = 0; idx < 32 && (base_irq + idx) < (int)vgic->num_irqs; idx++) {
            struct prtos_vgic_irq *irq = vgic_get_irq(vgic, 0, base_irq + idx);
            if (irq && irq->pending) reg |= (1U << idx);
        }
        *val = reg;
        return 0;
    }

    /* GICD_IPRIORITYR: byte-accessible, 1 byte per IRQ */
    if (offset >= GICD_IPRIORITYR && offset < GICD_IPRIORITYR + PRTOS_VGIC_NR_IRQS) {
        int base_irq = (int)(offset - GICD_IPRIORITYR);
        reg = 0;
        for (idx = 0; idx < size && (base_irq + idx) < (int)vgic->num_irqs; idx++) {
            struct prtos_vgic_irq *irq = vgic_get_irq(vgic, 0, base_irq + idx);
            if (irq) reg |= ((prtos_u32_t)irq->priority << (idx * 8));
        }
        *val = reg;
        return 0;
    }

    /* GICD_ICFGR: 2 bits per IRQ, 16 IRQs per register */
    if (offset >= GICD_ICFGR && offset < GICD_ICFGR + (PRTOS_VGIC_NR_IRQS / 4)) {
        int base_irq = (int)((offset - GICD_ICFGR) * 4);
        reg = 0;
        for (idx = 0; idx < 16 && (base_irq + idx) < (int)vgic->num_irqs; idx++) {
            struct prtos_vgic_irq *irq = vgic_get_irq(vgic, 0, base_irq + idx);
            if (irq) reg |= ((prtos_u32_t)(irq->config & 0x3) << (idx * 2));
        }
        *val = reg;
        return 0;
    }

    /* GICD_IROUTER: 64-bit per SPI (INTID 32+) */
    if (offset >= GICD_IROUTER && offset < GICD_IROUTER + PRTOS_VGIC_NR_SPIS * 8) {
        int spi_idx = (int)((offset - GICD_IROUTER) / 8);
        if (spi_idx < PRTOS_VGIC_NR_SPIS)
            *val = vgic->spis[spi_idx].target_vcpu;
        else
            *val = 0;
        return 0;
    }

    /* GICD_ISACTIVER / GICD_ICACTIVER */
    if ((offset >= GICD_ISACTIVER && offset < GICD_ISACTIVER + (PRTOS_VGIC_NR_IRQS / 8)) ||
        (offset >= GICD_ICACTIVER && offset < GICD_ICACTIVER + (PRTOS_VGIC_NR_IRQS / 8))) {
        *val = 0;  /* simplified: no active tracking */
        return 0;
    }

    /* GICD_IGRPMODR */
    if (offset >= GICD_IGRPMODR && offset < GICD_IGRPMODR + (PRTOS_VGIC_NR_IRQS / 8)) {
        *val = 0;
        return 0;
    }

    /* Unhandled — return RAZ */
    *val = 0;
    return 0;
}

static int gicd_mmio_write(struct prtos_vgic_state *vgic, prtos_u64_t offset,
                           int size, prtos_u64_t val) {
    int idx;

    switch (offset) {
    case GICD_CTLR:
        vgic->gicd_ctlr = (prtos_u32_t)val & 0x3;  /* EnableGrp0, EnableGrp1NS */
        return 0;
    case GICD_STATUSR:
    case GICD_SETSPI_NSR:
    case GICD_CLRSPI_NSR:
        return 0;  /* WI */
    }

    /* GICD_IGROUPR */
    if (offset >= GICD_IGROUPR && offset < GICD_IGROUPR + (PRTOS_VGIC_NR_IRQS / 8)) {
        int base_irq = (int)((offset - GICD_IGROUPR) * 8);
        for (idx = 0; idx < 32 && (base_irq + idx) < (int)vgic->num_irqs; idx++) {
            struct prtos_vgic_irq *irq = vgic_get_irq(vgic, 0, base_irq + idx);
            if (irq) irq->group = (val >> idx) & 1;
        }
        return 0;
    }

    /* GICD_ISENABLER: set-enable (write-1-to-set) */
    if (offset >= GICD_ISENABLER && offset < GICD_ISENABLER + (PRTOS_VGIC_NR_IRQS / 8)) {
        int base_irq = (int)((offset - GICD_ISENABLER) * 8);
        for (idx = 0; idx < 32 && (base_irq + idx) < (int)vgic->num_irqs; idx++) {
            if (val & (1U << idx)) {
                struct prtos_vgic_irq *irq = vgic_get_irq(vgic, 0, base_irq + idx);
                if (irq) irq->enabled = 1;
            }
        }
        return 0;
    }

    /* GICD_ICENABLER: clear-enable (write-1-to-clear) */
    if (offset >= GICD_ICENABLER && offset < GICD_ICENABLER + (PRTOS_VGIC_NR_IRQS / 8)) {
        int base_irq = (int)((offset - GICD_ICENABLER) * 8);
        for (idx = 0; idx < 32 && (base_irq + idx) < (int)vgic->num_irqs; idx++) {
            if (val & (1U << idx)) {
                struct prtos_vgic_irq *irq = vgic_get_irq(vgic, 0, base_irq + idx);
                if (irq) irq->enabled = 0;
            }
        }
        return 0;
    }

    /* GICD_ISPENDR */
    if (offset >= GICD_ISPENDR && offset < GICD_ISPENDR + (PRTOS_VGIC_NR_IRQS / 8)) {
        int base_irq = (int)((offset - GICD_ISPENDR) * 8);
        for (idx = 0; idx < 32 && (base_irq + idx) < (int)vgic->num_irqs; idx++) {
            if (val & (1U << idx)) {
                struct prtos_vgic_irq *irq = vgic_get_irq(vgic, 0, base_irq + idx);
                if (irq) irq->pending = 1;
            }
        }
        return 0;
    }

    /* GICD_ICPENDR */
    if (offset >= GICD_ICPENDR && offset < GICD_ICPENDR + (PRTOS_VGIC_NR_IRQS / 8)) {
        int base_irq = (int)((offset - GICD_ICPENDR) * 8);
        for (idx = 0; idx < 32 && (base_irq + idx) < (int)vgic->num_irqs; idx++) {
            if (val & (1U << idx)) {
                struct prtos_vgic_irq *irq = vgic_get_irq(vgic, 0, base_irq + idx);
                if (irq) irq->pending = 0;
            }
        }
        return 0;
    }

    /* GICD_IPRIORITYR */
    if (offset >= GICD_IPRIORITYR && offset < GICD_IPRIORITYR + PRTOS_VGIC_NR_IRQS) {
        int base_irq = (int)(offset - GICD_IPRIORITYR);
        for (idx = 0; idx < size && (base_irq + idx) < (int)vgic->num_irqs; idx++) {
            struct prtos_vgic_irq *irq = vgic_get_irq(vgic, 0, base_irq + idx);
            if (irq) irq->priority = (prtos_u8_t)((val >> (idx * 8)) & 0xFF);
        }
        return 0;
    }

    /* GICD_ICFGR */
    if (offset >= GICD_ICFGR && offset < GICD_ICFGR + (PRTOS_VGIC_NR_IRQS / 4)) {
        int base_irq = (int)((offset - GICD_ICFGR) * 4);
        for (idx = 0; idx < 16 && (base_irq + idx) < (int)vgic->num_irqs; idx++) {
            struct prtos_vgic_irq *irq = vgic_get_irq(vgic, 0, base_irq + idx);
            if (irq) irq->config = (prtos_u8_t)((val >> (idx * 2)) & 0x3);
        }
        return 0;
    }

    /* GICD_IROUTER */
    if (offset >= GICD_IROUTER && offset < GICD_IROUTER + PRTOS_VGIC_NR_SPIS * 8) {
        int spi_idx = (int)((offset - GICD_IROUTER) / 8);
        if (spi_idx < PRTOS_VGIC_NR_SPIS)
            vgic->spis[spi_idx].target_vcpu = (prtos_u8_t)(val & 0xFF);
        return 0;
    }

    /* GICD_ISACTIVER / GICD_ICACTIVER / GICD_IGRPMODR: WI (simplified) */
    if ((offset >= GICD_ISACTIVER && offset < GICD_ISACTIVER + (PRTOS_VGIC_NR_IRQS / 8)) ||
        (offset >= GICD_ICACTIVER && offset < GICD_ICACTIVER + (PRTOS_VGIC_NR_IRQS / 8)) ||
        (offset >= GICD_IGRPMODR && offset < GICD_IGRPMODR + (PRTOS_VGIC_NR_IRQS / 8)))
        return 0;

    return 0;  /* Unhandled — WI */
}

/* ------------------------------------------------------------------ */
/* GICR MMIO emulation                                                 */
/* ------------------------------------------------------------------ */
static int gicr_mmio_read(struct prtos_vgic_state *vgic, int vcpu_id,
                          prtos_u64_t offset, int size, prtos_u64_t *val) {
    struct prtos_vgic_vcpu *vc = &vgic->vcpu[vcpu_id];
    int idx;

    /* RD_base registers (offset 0x0000 - 0xFFFF) */
    if (offset < GICR_SGI_OFFSET) {
        switch (offset) {
        case GICR_CTLR:
            *val = vc->gicr_ctlr;
            return 0;
        case GICR_IIDR:
            *val = 0x0200043B;  /* ARM, rev 2 */
            return 0;
        case GICR_TYPER:
            /*
             * GICR_TYPER (64-bit):
             *   [7:0]   Processor_Number = vcpu_id
             *   [4]     Last = 1 if this is the last redistributor
             *   [39:32] Aff0 = vcpu_id  (must match MPIDR.Aff0 for Linux to
             *           find the correct redistributor for each CPU)
             */
            *val = ((prtos_u64_t)vcpu_id) |
                   ((vcpu_id == (int)vgic->num_vcpus - 1) ? (1ULL << 4) : 0) |
                   ((prtos_u64_t)vcpu_id << 32);
            return 0;
        case GICR_TYPER + 4:
            /* Upper 32 bits of TYPER: Aff0 = vcpu_id */
            *val = (prtos_u64_t)vcpu_id;
            return 0;
        case GICR_STATUSR:
            *val = 0;
            return 0;
        case GICR_WAKER:
            *val = vc->gicr_waker;
            return 0;
        case GICR_PIDR2:
            *val = 0x30;  /* ArchRev = 3 */
            return 0;
        default:
            *val = 0;
            return 0;
        }
    }

    /* SGI_base registers (offset 0x10000+) */
    prtos_u64_t sgi_off = offset - GICR_SGI_OFFSET;

    if (offset == GICR_IGROUPR0) {
        prtos_u32_t reg = 0;
        for (idx = 0; idx < 16; idx++)
            if (vc->sgis[idx].group) reg |= (1U << idx);
        for (idx = 0; idx < 16; idx++)
            if (vc->ppis[idx].group) reg |= (1U << (idx + 16));
        *val = reg;
        return 0;
    }

    if (offset == GICR_ISENABLER0 || offset == GICR_ICENABLER0) {
        prtos_u32_t reg = 0;
        for (idx = 0; idx < 16; idx++)
            if (vc->sgis[idx].enabled) reg |= (1U << idx);
        for (idx = 0; idx < 16; idx++)
            if (vc->ppis[idx].enabled) reg |= (1U << (idx + 16));
        *val = reg;
        return 0;
    }

    if (offset == GICR_ISPENDR0 || offset == GICR_ICPENDR0) {
        prtos_u32_t reg = 0;
        for (idx = 0; idx < 16; idx++)
            if (vc->sgis[idx].pending) reg |= (1U << idx);
        for (idx = 0; idx < 16; idx++)
            if (vc->ppis[idx].pending) reg |= (1U << (idx + 16));
        *val = reg;
        return 0;
    }

    /* GICR_IPRIORITYR0: 32 bytes covering INTID 0-31 */
    if (offset >= GICR_IPRIORITYR0 && offset < GICR_IPRIORITYR0 + 32) {
        int base_irq = (int)(offset - GICR_IPRIORITYR0);
        prtos_u32_t reg = 0;
        for (idx = 0; idx < size && (base_irq + idx) < 32; idx++) {
            struct prtos_vgic_irq *irq;
            int intid = base_irq + idx;
            if (intid < 16) irq = &vc->sgis[intid];
            else irq = &vc->ppis[intid - 16];
            reg |= ((prtos_u32_t)irq->priority << (idx * 8));
        }
        *val = reg;
        return 0;
    }

    if (offset == GICR_ICFGR0) {
        /* SGI config: all edge-triggered (fixed) */
        *val = 0xAAAAAAAA;
        return 0;
    }
    if (offset == GICR_ICFGR1) {
        prtos_u32_t reg = 0;
        for (idx = 0; idx < 16; idx++)
            reg |= ((prtos_u32_t)(vc->ppis[idx].config & 0x3) << (idx * 2));
        *val = reg;
        return 0;
    }

    if (offset == GICR_IGRPMODR0) {
        *val = 0;
        return 0;
    }

    if (offset == GICR_ISACTIVER0 || offset == GICR_ICACTIVER0) {
        *val = 0;
        return 0;
    }

    *val = 0;
    return 0;
}

static int gicr_mmio_write(struct prtos_vgic_state *vgic, int vcpu_id,
                           prtos_u64_t offset, int size, prtos_u64_t val) {
    struct prtos_vgic_vcpu *vc = &vgic->vcpu[vcpu_id];
    int idx;

    /* RD_base registers */
    if (offset < GICR_SGI_OFFSET) {
        switch (offset) {
        case GICR_CTLR:
            vc->gicr_ctlr = (prtos_u32_t)val;
            return 0;
        case GICR_WAKER:
            /* Clear ProcessorSleep when guest writes ProcessorSleep=0 */
            if (!(val & (1U << 1)))
                vc->gicr_waker &= ~(1U << 2);  /* ChildrenAsleep = 0 */
            else
                vc->gicr_waker |= (1U << 2);
            vc->gicr_waker = (vc->gicr_waker & ~(1U << 1)) | ((prtos_u32_t)val & (1U << 1));
            return 0;
        default:
            return 0;
        }
    }

    /* SGI_base registers */
    if (offset == GICR_IGROUPR0) {
        for (idx = 0; idx < 16; idx++)
            vc->sgis[idx].group = (val >> idx) & 1;
        for (idx = 0; idx < 16; idx++)
            vc->ppis[idx].group = (val >> (idx + 16)) & 1;
        return 0;
    }

    if (offset == GICR_ISENABLER0) {
        for (idx = 0; idx < 16; idx++)
            if (val & (1U << idx)) vc->sgis[idx].enabled = 1;
        for (idx = 0; idx < 16; idx++)
            if (val & (1U << (idx + 16))) vc->ppis[idx].enabled = 1;
        return 0;
    }

    if (offset == GICR_ICENABLER0) {
        for (idx = 0; idx < 16; idx++)
            if (val & (1U << idx)) vc->sgis[idx].enabled = 0;
        for (idx = 0; idx < 16; idx++)
            if (val & (1U << (idx + 16))) vc->ppis[idx].enabled = 0;
        return 0;
    }

    if (offset == GICR_ISPENDR0) {
        for (idx = 0; idx < 16; idx++)
            if (val & (1U << idx)) vc->sgis[idx].pending = 1;
        for (idx = 0; idx < 16; idx++)
            if (val & (1U << (idx + 16))) vc->ppis[idx].pending = 1;
        return 0;
    }

    if (offset == GICR_ICPENDR0) {
        for (idx = 0; idx < 16; idx++)
            if (val & (1U << idx)) vc->sgis[idx].pending = 0;
        for (idx = 0; idx < 16; idx++)
            if (val & (1U << (idx + 16))) vc->ppis[idx].pending = 0;
        return 0;
    }

    /* GICR_IPRIORITYR0 */
    if (offset >= GICR_IPRIORITYR0 && offset < GICR_IPRIORITYR0 + 32) {
        int base_irq = (int)(offset - GICR_IPRIORITYR0);
        for (idx = 0; idx < size && (base_irq + idx) < 32; idx++) {
            struct prtos_vgic_irq *irq;
            int intid = base_irq + idx;
            if (intid < 16) irq = &vc->sgis[intid];
            else irq = &vc->ppis[intid - 16];
            irq->priority = (prtos_u8_t)((val >> (idx * 8)) & 0xFF);
        }
        return 0;
    }

    if (offset == GICR_ICFGR1) {
        for (idx = 0; idx < 16; idx++)
            vc->ppis[idx].config = (prtos_u8_t)((val >> (idx * 2)) & 0x3);
        return 0;
    }

    return 0;  /* WI for unhandled */
}

/* ------------------------------------------------------------------ */
/* Top-level MMIO dispatch                                             */
/* ------------------------------------------------------------------ */
/* Read guest register, treating reg 31 (xzr) as zero */
static inline prtos_u64_t mmio_read_reg(struct cpu_user_regs *regs, int reg) {
    if (reg >= 31 || reg < 0) return 0;
    return (&regs->x0)[reg];
}

/* Write guest register, ignoring writes to reg 31 (xzr) */
static inline void mmio_write_reg(struct cpu_user_regs *regs, int reg,
                                  prtos_u64_t val) {
    if (reg >= 0 && reg < 31) (&regs->x0)[reg] = val;
}

int prtos_mmio_dispatch(struct cpu_user_regs *regs, prtos_u64_t gpa,
                        int is_write, int reg, int size) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    kthread_t *k = info->sched.current_kthread;
    if (!k->ctrl.g || !k->ctrl.g->karch.vgic)
        return -1;

    struct prtos_vgic_state *vgic = k->ctrl.g->karch.vgic;
    prtos_u64_t val = 0;
    int ret;

    /* GICD region */
    if (gpa >= PRTOS_VGIC_GICD_BASE &&
        gpa < PRTOS_VGIC_GICD_BASE + PRTOS_VGIC_GICD_SIZE) {
        prtos_u64_t offset = gpa - PRTOS_VGIC_GICD_BASE;
        if (is_write) {
            val = mmio_read_reg(regs, reg);
            ret = gicd_mmio_write(vgic, offset, size, val);
        } else {
            ret = gicd_mmio_read(vgic, offset, size, &val);
            if (ret == 0) mmio_write_reg(regs, reg, val);
        }
        return ret;
    }

    /* GICR region */
    if (gpa >= PRTOS_VGIC_GICR_BASE &&
        gpa < PRTOS_VGIC_GICR_BASE + PRTOS_VGIC_GICR_SIZE) {
        prtos_u64_t gicr_offset = gpa - PRTOS_VGIC_GICR_BASE;
        int target_vcpu = (int)(gicr_offset / PRTOS_VGIC_GICR_STRIDE);
        prtos_u64_t offset = gicr_offset % PRTOS_VGIC_GICR_STRIDE;

        if (target_vcpu >= (int)vgic->num_vcpus)
            target_vcpu = 0;  /* Safety: clamp to vCPU 0 */

        if (is_write) {
            val = mmio_read_reg(regs, reg);
            ret = gicr_mmio_write(vgic, target_vcpu, offset, size, val);
        } else {
            ret = gicr_mmio_read(vgic, target_vcpu, offset, size, &val);
            if (ret == 0) mmio_write_reg(regs, reg, val);
        }
        return ret;
    }

    return -1;  /* Not a VGIC region */
}

/* ------------------------------------------------------------------ */
/* ICH_LR flush: inject pending virtual IRQs                           */
/* ------------------------------------------------------------------ */
static void vgic_flush_lrs(struct prtos_vgic_state *vgic, int vcpu_id) {
    struct prtos_vgic_vcpu *vc = &vgic->vcpu[vcpu_id];
    int lr_idx = 0;
    int i;
    prtos_u64_t lr_val;

    /* First, reclaim any completed LRs (state == inactive) */
    for (i = 0; i < PRTOS_VGIC_NR_LRS; i++) {
        lr_val = read_ich_lr(i);
        if (!(lr_val & (ICH_LR_STATE_PENDING | ICH_LR_STATE_ACTIVE)))
            write_ich_lr(i, 0);
    }

    /* Find free LR slots and inject pending IRQs */

    /* SGIs (0-15) */
    for (i = 0; i < PRTOS_VGIC_NR_SGIS && lr_idx < PRTOS_VGIC_NR_LRS; i++) {
        if (vc->sgis[i].pending && vc->sgis[i].enabled) {
            /* Find a free LR */
            int slot = -1, s;
            for (s = 0; s < PRTOS_VGIC_NR_LRS; s++) {
                if (!(read_ich_lr(s) & (ICH_LR_STATE_PENDING | ICH_LR_STATE_ACTIVE))) {
                    slot = s;
                    break;
                }
            }
            if (slot < 0) break;

            lr_val = ICH_LR_STATE_PENDING | ICH_LR_GRP1 |
                     ((prtos_u64_t)vc->sgis[i].priority << ICH_LR_PRIORITY_SHIFT) |
                     (prtos_u64_t)i;
            write_ich_lr(slot, lr_val);
            vc->sgis[i].pending = 0;
            lr_idx++;
        }
    }

    /* PPIs (16-31) — especially timer PPI 27 (index 11) */
    for (i = 0; i < PRTOS_VGIC_NR_PPIS && lr_idx < PRTOS_VGIC_NR_LRS; i++) {
        if (vc->ppis[i].pending && vc->ppis[i].enabled) {
            int slot = -1, s;
            for (s = 0; s < PRTOS_VGIC_NR_LRS; s++) {
                if (!(read_ich_lr(s) & (ICH_LR_STATE_PENDING | ICH_LR_STATE_ACTIVE))) {
                    slot = s;
                    break;
                }
            }
            if (slot < 0) break;

            lr_val = ICH_LR_STATE_PENDING | ICH_LR_GRP1 |
                     ((prtos_u64_t)vc->ppis[i].priority << ICH_LR_PRIORITY_SHIFT) |
                     (prtos_u64_t)(i + 16);
            write_ich_lr(slot, lr_val);
            vc->ppis[i].pending = 0;
            lr_idx++;
        }
    }

    /* -----------------------------------------------------------
     * Unmask pass-through physical SPIs whose virtual delivery
     * has completed (no pending SW flag, no active/pending LR).
     * This avoids unmasking too early which would cause immediate
     * re-assertion before the guest processes the virtual IRQ.
     * ----------------------------------------------------------- */
    {
        extern void prtos_gicv3_enable_spi(int irq);
        /* Check SPI INTID 33 (UART) — index 1 in spis[] */
        if (!vgic->spis[1].pending) {
            int spi33_in_lr = 0, s;
            for (s = 0; s < PRTOS_VGIC_NR_LRS; s++) {
                prtos_u64_t lr = read_ich_lr(s);
                if ((lr & (ICH_LR_STATE_PENDING | ICH_LR_STATE_ACTIVE)) &&
                    (lr & ICH_LR_VIRTUAL_MASK) == 33) {
                    spi33_in_lr = 1;
                    break;
                }
            }
            if (!spi33_in_lr)
                prtos_gicv3_enable_spi(33);
        }
    }

    /* SPIs (32+) */
    for (i = 0; i < PRTOS_VGIC_NR_SPIS && lr_idx < PRTOS_VGIC_NR_LRS; i++) {
        if (vgic->spis[i].pending && vgic->spis[i].enabled &&
            vgic->spis[i].target_vcpu == vcpu_id) {
            int slot = -1, s;
            for (s = 0; s < PRTOS_VGIC_NR_LRS; s++) {
                if (!(read_ich_lr(s) & (ICH_LR_STATE_PENDING | ICH_LR_STATE_ACTIVE))) {
                    slot = s;
                    break;
                }
            }
            if (slot < 0) break;

            lr_val = ICH_LR_STATE_PENDING | ICH_LR_GRP1 |
                     ((prtos_u64_t)vgic->spis[i].priority << ICH_LR_PRIORITY_SHIFT) |
                     (prtos_u64_t)(i + 32);
            write_ich_lr(slot, lr_val);
            vgic->spis[i].pending = 0;
            lr_idx++;
        }
    }

    asm volatile("isb" ::: "memory");
}

void prtos_vgic_flush_lrs_current(void) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    kthread_t *k = info->sched.current_kthread;
    if (!k->ctrl.g || !k->ctrl.g->karch.vgic)
        return;

    int vcpu_id = KID2VCPUID(k->ctrl.g->id);
    vgic_flush_lrs(k->ctrl.g->karch.vgic, vcpu_id);
}

/* ------------------------------------------------------------------ */
/* SGI injection (for IPI between vCPUs)                               */
/* ------------------------------------------------------------------ */
void prtos_vgic_inject_sgi(struct prtos_vgic_state *vgic,
                            prtos_u32_t target_vcpu, prtos_u32_t intid) {
    if (target_vcpu >= vgic->num_vcpus || intid >= PRTOS_VGIC_NR_SGIS)
        return;
    vgic->vcpu[target_vcpu].sgis[intid].pending = 1;
}

/* ------------------------------------------------------------------ */
/* SPI injection for pass-through level-triggered physical IRQs        */
/* ------------------------------------------------------------------ */
/*
 * Mark a physical SPI as pending in the guest's VGIC software state
 * and mask the physical interrupt at the GIC distributor to prevent
 * level-triggered re-assertion storms.  The pending SPI will be written
 * to an ICH_LR by vgic_flush_lrs() when the guest is next scheduled.
 * The physical SPI is re-enabled in prtos_vgic_flush_lrs_current().
 *
 * The caller must fully EOI+DIR the physical interrupt.
 * Returns 0 on success.
 */
int prtos_vgic_inject_hw_spi(prtos_u32_t intid) {
    extern partition_t *partition_table;
    extern void prtos_gicv3_mask_spi(int irq);

    if (intid < 32 || intid >= 32 + PRTOS_VGIC_NR_SPIS)
        return -1;

    /* Get partition 0's VGIC state (Linux guest is always partition 0) */
    kthread_t *k = partition_table[0].kthread[0];
    if (!k || !k->ctrl.g || !k->ctrl.g->karch.vgic)
        return -1;

    struct prtos_vgic_state *vgic = k->ctrl.g->karch.vgic;
    int spi_idx = intid - 32;

    /* Set pending in SW state; vgic_flush_lrs will load it into an LR */
    vgic->spis[spi_idx].pending = 1;

    /* Mask the physical SPI at GICD to prevent re-assertion from the
     * level-sensitive UART line while the character sits in the FIFO. */
    prtos_gicv3_mask_spi(intid);

    return 0;
}
