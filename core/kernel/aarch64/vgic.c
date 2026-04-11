/*
 * FILE: vgic.c
 *
 * Standalone GICv3 virtual distributor/redistributor emulation for PRTOS
 * hw-virt partitions running unmodified Linux.
 *
 * http://www.prtos.org/
 */

#include <kthread.h>
#include <processor.h>
#include <sched.h>
#include <stdc.h>
#include <arch/layout.h>

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
#define GICD_IGROUPR        0x0080
#define GICD_ISENABLER      0x0100
#define GICD_ICENABLER      0x0180
#define GICD_ISPENDR        0x0200
#define GICD_ICPENDR        0x0280
#define GICD_ISACTIVER      0x0300
#define GICD_ICACTIVER      0x0380
#define GICD_IPRIORITYR     0x0400
#define GICD_ICFGR          0x0C00
#define GICD_IGRPMODR       0x0D00
#define GICD_ITARGETSR      0x0800
#define GICD_IROUTER        0x6000
#define GICD_PIDR2          0xFFE8

/* ------------------------------------------------------------------ */
/* GICC register offsets                                               */
/* ------------------------------------------------------------------ */
#define GICC_CTLR           0x0000
#define GICC_PMR            0x0004
#define GICC_BPR            0x0008
#define GICC_IAR            0x000C
#define GICC_EOIR           0x0010
#define GICC_RPR            0x0014
#define GICC_HPIR           0x0018
#define GICC_IIDR           0x00FC

/* ------------------------------------------------------------------ */
/* GICR register offsets                                               */
/* ------------------------------------------------------------------ */
#define GICR_CTLR           0x0000
#define GICR_IIDR           0x0004
#define GICR_TYPER          0x0008
#define GICR_STATUSR        0x0010
#define GICR_WAKER          0x0014
#define GICR_PIDR2          0xFFE8

#define GICR_SGI_OFFSET     0x10000
#define GICR_IGROUPR0       (GICR_SGI_OFFSET + 0x0080)
#define GICR_ISENABLER0     (GICR_SGI_OFFSET + 0x0100)
#define GICR_ICENABLER0     (GICR_SGI_OFFSET + 0x0180)
#define GICR_ISPENDR0       (GICR_SGI_OFFSET + 0x0200)
#define GICR_ICPENDR0       (GICR_SGI_OFFSET + 0x0280)
#define GICR_ISACTIVER0     (GICR_SGI_OFFSET + 0x0300)
#define GICR_ICACTIVER0     (GICR_SGI_OFFSET + 0x0380)
#define GICR_IPRIORITYR0    (GICR_SGI_OFFSET + 0x0400)
#define GICR_ICFGR0         (GICR_SGI_OFFSET + 0x0C00)
#define GICR_ICFGR1         (GICR_SGI_OFFSET + 0x0C04)
#define GICR_IGRPMODR0      (GICR_SGI_OFFSET + 0x0D00)

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
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
    vgic->gicd_ctlr = 0;

    for (i = 0; i < PRTOS_VGIC_NR_SPIS; i++) {
        vgic->spis[i].group = 1;
        vgic->spis[i].priority = 0xA0;
    }
    for (v = 0; v < (int)num_vcpus; v++) {
        vgic->vcpu[v].gicr_waker = 0x6;
        for (i = 0; i < PRTOS_VGIC_NR_SGIS; i++) {
            vgic->vcpu[v].sgis[i].group = 1;
            vgic->vcpu[v].sgis[i].priority = 0xA0;
            vgic->vcpu[v].sgis[i].enabled = 1;
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
        *val = vgic->gicd_ctlr | (1U << 4);
        return 0;
    case GICD_TYPER:
        *val = ((vgic->num_irqs / 32 - 1) & 0x1F) |
               (((vgic->num_vcpus - 1) & 0x7) << 5) |
               (9U << 19);
        return 0;
    case GICD_IIDR:
        *val = 0x0200043B;
        return 0;
    case GICD_STATUSR:
        *val = 0;
        return 0;
    case GICD_PIDR2:
        *val = 0x30;
        return 0;
    }

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

    if (offset >= GICD_IROUTER && offset < GICD_IROUTER + PRTOS_VGIC_NR_SPIS * 8) {
        int spi_idx = (int)((offset - GICD_IROUTER) / 8);
        if (spi_idx < PRTOS_VGIC_NR_SPIS)
            *val = vgic->spis[spi_idx].target_vcpu;
        else
            *val = 0;
        return 0;
    }

    if (offset >= GICD_ITARGETSR && offset < GICD_ITARGETSR + PRTOS_VGIC_NR_IRQS) {
        int base_irq = (int)(offset - GICD_ITARGETSR);
        reg = 0;
        for (idx = 0; idx < size && (base_irq + idx) < (int)vgic->num_irqs; idx++) {
            struct prtos_vgic_irq *irq = vgic_get_irq(vgic, 0, base_irq + idx);
            prtos_u8_t mask = 0;
            if (irq) mask = (prtos_u8_t)(1U << irq->target_vcpu);
            reg |= ((prtos_u32_t)mask << (idx * 8));
        }
        *val = reg;
        return 0;
    }

    if ((offset >= GICD_ISACTIVER && offset < GICD_ISACTIVER + (PRTOS_VGIC_NR_IRQS / 8)) ||
        (offset >= GICD_ICACTIVER && offset < GICD_ICACTIVER + (PRTOS_VGIC_NR_IRQS / 8))) {
        *val = 0;
        return 0;
    }

    if (offset >= GICD_IGRPMODR && offset < GICD_IGRPMODR + (PRTOS_VGIC_NR_IRQS / 8)) {
        *val = 0;
        return 0;
    }

    *val = 0;
    return 0;
}

static int gicd_mmio_write(struct prtos_vgic_state *vgic, prtos_u64_t offset,
                           int size, prtos_u64_t val) {
    int idx;

    switch (offset) {
    case GICD_CTLR:
        vgic->gicd_ctlr = (prtos_u32_t)val & 0x3;
        return 0;
    case GICD_STATUSR:
    case GICD_SETSPI_NSR:
    case GICD_CLRSPI_NSR:
        return 0;
    }

    if (offset >= GICD_IGROUPR && offset < GICD_IGROUPR + (PRTOS_VGIC_NR_IRQS / 8)) {
        int base_irq = (int)((offset - GICD_IGROUPR) * 8);
        for (idx = 0; idx < 32 && (base_irq + idx) < (int)vgic->num_irqs; idx++) {
            struct prtos_vgic_irq *irq = vgic_get_irq(vgic, 0, base_irq + idx);
            if (irq) irq->group = (val >> idx) & 1;
        }
        return 0;
    }

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

    if (offset >= GICD_IPRIORITYR && offset < GICD_IPRIORITYR + PRTOS_VGIC_NR_IRQS) {
        int base_irq = (int)(offset - GICD_IPRIORITYR);
        for (idx = 0; idx < size && (base_irq + idx) < (int)vgic->num_irqs; idx++) {
            struct prtos_vgic_irq *irq = vgic_get_irq(vgic, 0, base_irq + idx);
            if (irq) irq->priority = (prtos_u8_t)((val >> (idx * 8)) & 0xFF);
        }
        return 0;
    }

    if (offset >= GICD_ICFGR && offset < GICD_ICFGR + (PRTOS_VGIC_NR_IRQS / 4)) {
        int base_irq = (int)((offset - GICD_ICFGR) * 4);
        for (idx = 0; idx < 16 && (base_irq + idx) < (int)vgic->num_irqs; idx++) {
            struct prtos_vgic_irq *irq = vgic_get_irq(vgic, 0, base_irq + idx);
            if (irq) irq->config = (prtos_u8_t)((val >> (idx * 2)) & 0x3);
        }
        return 0;
    }

    if (offset >= GICD_IROUTER && offset < GICD_IROUTER + PRTOS_VGIC_NR_SPIS * 8) {
        int spi_idx = (int)((offset - GICD_IROUTER) / 8);
        if (spi_idx < PRTOS_VGIC_NR_SPIS)
            vgic->spis[spi_idx].target_vcpu = (prtos_u8_t)(val & 0xFF);
        return 0;
    }

    if (offset >= GICD_ITARGETSR && offset < GICD_ITARGETSR + PRTOS_VGIC_NR_IRQS) {
        int base_irq = (int)(offset - GICD_ITARGETSR);
        for (idx = 0; idx < size && (base_irq + idx) < (int)vgic->num_irqs; idx++) {
            struct prtos_vgic_irq *irq = vgic_get_irq(vgic, 0, base_irq + idx);
            if (irq) {
                prtos_u8_t mask = (prtos_u8_t)((val >> (idx * 8)) & 0xFF);
                prtos_u8_t vcpu = 0;
                if (mask) { while (!(mask & 1)) { mask >>= 1; vcpu++; } }
                irq->target_vcpu = vcpu;
            }
        }
        return 0;
    }

    if ((offset >= GICD_ISACTIVER && offset < GICD_ISACTIVER + (PRTOS_VGIC_NR_IRQS / 8)) ||
        (offset >= GICD_ICACTIVER && offset < GICD_ICACTIVER + (PRTOS_VGIC_NR_IRQS / 8)) ||
        (offset >= GICD_IGRPMODR && offset < GICD_IGRPMODR + (PRTOS_VGIC_NR_IRQS / 8)))
        return 0;

    return 0;
}

/* ------------------------------------------------------------------ */
/* GICR MMIO emulation                                                 */
/* ------------------------------------------------------------------ */
static int gicr_mmio_read(struct prtos_vgic_state *vgic, int vcpu_id,
                          prtos_u64_t offset, int size, prtos_u64_t *val) {
    struct prtos_vgic_vcpu *vc = &vgic->vcpu[vcpu_id];
    int idx;

    if (offset < GICR_SGI_OFFSET) {
        switch (offset) {
        case GICR_CTLR:
            *val = vc->gicr_ctlr;
            return 0;
        case GICR_IIDR:
            *val = 0x0200043B;
            return 0;
        case GICR_TYPER:
        {
            prtos_u8_t pcpu = vgic->vcpu_to_pcpu[vcpu_id];
            *val = ((prtos_u64_t)pcpu << 8) |
                   ((vcpu_id == (int)vgic->num_vcpus - 1) ? (1ULL << 4) : 0) |
                   ((prtos_u64_t)pcpu << 32);
            return 0;
        }
        case GICR_TYPER + 4:
        {
            prtos_u8_t pcpu = vgic->vcpu_to_pcpu[vcpu_id];
            *val = (prtos_u64_t)pcpu;
            return 0;
        }
        case GICR_STATUSR:
            *val = 0;
            return 0;
        case GICR_WAKER:
            *val = vc->gicr_waker;
            return 0;
        case GICR_PIDR2:
            *val = 0x30;
            return 0;
        default:
            *val = 0;
            return 0;
        }
    }

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

    if (offset < GICR_SGI_OFFSET) {
        switch (offset) {
        case GICR_CTLR:
            vc->gicr_ctlr = (prtos_u32_t)val;
            return 0;
        case GICR_WAKER:
            if (!(val & (1U << 1)))
                vc->gicr_waker &= ~(1U << 2);
            else
                vc->gicr_waker |= (1U << 2);
            vc->gicr_waker = (vc->gicr_waker & ~(1U << 1)) | ((prtos_u32_t)val & (1U << 1));
            return 0;
        default:
            return 0;
        }
    }

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

    return 0;
}

/* ------------------------------------------------------------------ */
/* GICC MMIO emulation                                                 */
/* ------------------------------------------------------------------ */
static inline prtos_u64_t read_ich_vmcr(void) {
    prtos_u64_t val;
    asm volatile("mrs %0, S3_4_C12_C11_7" : "=r"(val));
    return val;
}

static inline void write_ich_vmcr(prtos_u64_t val) {
    asm volatile("msr S3_4_C12_C11_7, %0" :: "r"(val));
    asm volatile("isb" ::: "memory");
}

static prtos_u32_t gicc_iar_read(void) {
    int best_lr = -1;
    prtos_u8_t best_prio = 0xFF;
    prtos_u32_t best_intid = 1023;
    int i;

    for (i = 0; i < PRTOS_VGIC_NR_LRS; i++) {
        prtos_u64_t lr = read_ich_lr(i);
        if (lr & ICH_LR_STATE_PENDING) {
            prtos_u8_t prio = (prtos_u8_t)((lr >> ICH_LR_PRIORITY_SHIFT) & 0xFF);
            if (prio < best_prio) {
                best_prio = prio;
                best_lr = i;
                best_intid = (prtos_u32_t)(lr & ICH_LR_VIRTUAL_MASK);
            }
        }
    }

    if (best_lr >= 0) {
        prtos_u64_t lr = read_ich_lr(best_lr);
        lr = (lr & ~ICH_LR_STATE_PENDING) | ICH_LR_STATE_ACTIVE;
        write_ich_lr(best_lr, lr);
    }

    return best_intid;
}

static void gicc_eoir_write(prtos_u32_t intid) {
    int i;
    for (i = 0; i < PRTOS_VGIC_NR_LRS; i++) {
        prtos_u64_t lr = read_ich_lr(i);
        if ((lr & ICH_LR_STATE_ACTIVE) &&
            (lr & ICH_LR_VIRTUAL_MASK) == intid) {
            lr &= ~ICH_LR_STATE_ACTIVE;
            if (!(lr & ICH_LR_STATE_PENDING))
                lr = 0;
            write_ich_lr(i, lr);

            if (intid == 27) {
                prtos_u32_t ctl;
                __asm__ __volatile__("mrs %0, CNTV_CTL_EL0" : "=r"(ctl));
                ctl &= ~(1U << 1);
                __asm__ __volatile__("msr CNTV_CTL_EL0, %0\n\tisb" : : "r"(ctl) : "memory");
            }
            break;
            break;
        }
    }
}

static int gicc_mmio_read(struct prtos_vgic_state *vgic, prtos_u64_t offset,
                          int size, prtos_u64_t *val) {
    (void)size;

    switch (offset) {
    case GICC_CTLR: {
        prtos_u64_t vmcr = read_ich_vmcr();
        *val = (vmcr >> 1) & 1;
        return 0;
    }
    case GICC_PMR: {
        prtos_u64_t vmcr = read_ich_vmcr();
        *val = (vmcr >> 24) & 0xFF;
        return 0;
    }
    case GICC_BPR:
        *val = vgic->gicc_bpr;
        return 0;
    case GICC_IAR:
        *val = gicc_iar_read();
        return 0;
    case GICC_RPR: {
        prtos_u8_t best_prio = 0xFF;
        int i;
        for (i = 0; i < PRTOS_VGIC_NR_LRS; i++) {
            prtos_u64_t lr = read_ich_lr(i);
            if (lr & ICH_LR_STATE_ACTIVE) {
                prtos_u8_t p = (prtos_u8_t)((lr >> ICH_LR_PRIORITY_SHIFT) & 0xFF);
                if (p < best_prio) best_prio = p;
            }
        }
        *val = best_prio;
        return 0;
    }
    case GICC_HPIR: {
        prtos_u8_t best_prio = 0xFF;
        prtos_u32_t best_intid = 1023;
        int i;
        for (i = 0; i < PRTOS_VGIC_NR_LRS; i++) {
            prtos_u64_t lr = read_ich_lr(i);
            if (lr & ICH_LR_STATE_PENDING) {
                prtos_u8_t p = (prtos_u8_t)((lr >> ICH_LR_PRIORITY_SHIFT) & 0xFF);
                if (p < best_prio) {
                    best_prio = p;
                    best_intid = (prtos_u32_t)(lr & ICH_LR_VIRTUAL_MASK);
                }
            }
        }
        *val = best_intid;
        return 0;
    }
    case GICC_IIDR:
        *val = 0x0200043B;
        return 0;
    default:
        *val = 0;
        return 0;
    }
}

static int gicc_mmio_write(struct prtos_vgic_state *vgic, prtos_u64_t offset,
                           int size, prtos_u64_t val) {
    (void)vgic; (void)size;

    switch (offset) {
    case GICC_CTLR: {
        prtos_u64_t vmcr = read_ich_vmcr();
        if (val & 1)
            vmcr |= (1ULL << 1);
        else
            vmcr &= ~(1ULL << 1);
        write_ich_vmcr(vmcr);
        return 0;
    }
    case GICC_PMR: {
        prtos_u64_t vmcr = read_ich_vmcr();
        vmcr = (vmcr & ~(0xFFULL << 24)) | (((prtos_u64_t)val & 0xFF) << 24);
        write_ich_vmcr(vmcr);
        return 0;
    }
    case GICC_BPR: {
        prtos_u64_t vmcr = read_ich_vmcr();
        vgic->gicc_bpr = (prtos_u32_t)(val & 0x7);
        vmcr = (vmcr & ~(0x7ULL << 18)) | (((prtos_u64_t)val & 0x7) << 18);
        write_ich_vmcr(vmcr);
        return 0;
    }
    case GICC_EOIR:
        gicc_eoir_write((prtos_u32_t)(val & 0x3FF));
        return 0;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/* Top-level MMIO dispatch                                             */
/* ------------------------------------------------------------------ */
static inline prtos_u64_t mmio_read_reg(struct cpu_user_regs *regs, int reg) {
    if (reg >= 31 || reg < 0) return 0;
    return regs->regs[reg];
}

static inline void mmio_write_reg(struct cpu_user_regs *regs, int reg,
                                  prtos_u64_t val) {
    if (reg >= 0 && reg < 31) regs->regs[reg] = val;
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

        if (target_vcpu >= (int)vgic->num_vcpus) {
            if (!is_write) {
                prtos_u64_t raz = 0;
                if (offset == GICR_TYPER || offset == GICR_TYPER + 4) {
                    if (offset == GICR_TYPER)
                        raz = (1ULL << 4);
                    else
                        raz = 0xFF;
                }
                mmio_write_reg(regs, reg, raz);
            }
            return 0;
        }

        if (is_write) {
            val = mmio_read_reg(regs, reg);
            ret = gicr_mmio_write(vgic, target_vcpu, offset, size, val);
        } else {
            ret = gicr_mmio_read(vgic, target_vcpu, offset, size, &val);
            if (ret == 0) mmio_write_reg(regs, reg, val);
        }
        return ret;
    }

    /* GICC region */
    if (gpa >= PRTOS_VGIC_GICC_BASE &&
        gpa < PRTOS_VGIC_GICC_BASE + PRTOS_VGIC_GICC_SIZE) {
        prtos_u64_t offset = gpa - PRTOS_VGIC_GICC_BASE;
        if (is_write) {
            val = mmio_read_reg(regs, reg);
            ret = gicc_mmio_write(vgic, offset, size, val);
        } else {
            ret = gicc_mmio_read(vgic, offset, size, &val);
            if (ret == 0) mmio_write_reg(regs, reg, val);
        }
        return ret;
    }

    return -1;
}

/* ------------------------------------------------------------------ */
/* ICH_LR flush                                                        */
/* ------------------------------------------------------------------ */
static void vgic_flush_lrs(struct prtos_vgic_state *vgic, int vcpu_id) {
    struct prtos_vgic_vcpu *vc = &vgic->vcpu[vcpu_id];
    int lr_idx = 0;
    int i;
    prtos_u64_t lr_val;

    /* Reclaim completed LRs */
    for (i = 0; i < PRTOS_VGIC_NR_LRS; i++) {
        lr_val = read_ich_lr(i);
        if (!(lr_val & (ICH_LR_STATE_PENDING | ICH_LR_STATE_ACTIVE)))
            write_ich_lr(i, 0);
    }

    /* SGIs (0-15) */
    for (i = 0; i < PRTOS_VGIC_NR_SGIS && lr_idx < PRTOS_VGIC_NR_LRS; i++) {
        if (vc->sgis[i].pending && vc->sgis[i].enabled) {
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

    /* PPIs (16-31) */
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
/* SGI injection                                                       */
/* ------------------------------------------------------------------ */
void prtos_vgic_inject_sgi(struct prtos_vgic_state *vgic,
                            prtos_u32_t target_vcpu, prtos_u32_t intid) {
    if (target_vcpu >= vgic->num_vcpus || intid >= PRTOS_VGIC_NR_SGIS)
        return;
    vgic->vcpu[target_vcpu].sgis[intid].pending = 1;
}

/* ------------------------------------------------------------------ */
/* SPI injection (physical HW IRQ -> guest vGIC)                       */
/* ------------------------------------------------------------------ */
void prtos_vgic_inject_spi(struct prtos_vgic_state *vgic, prtos_u32_t intid) {
    if (intid < 32 || intid >= vgic->num_irqs)
        return;
    vgic->spis[intid - 32].pending = 1;
}
