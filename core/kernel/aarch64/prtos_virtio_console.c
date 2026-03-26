/*
 * prtos_virtio_console.c — Virtio-console MMIO device emulation for PRTOS.
 *
 * Provides a virtio-mmio console device at IPA 0x0a000000 for the Linux
 * partition.  The backend reads from a shared-memory ring buffer written
 * by FreeRTOS (partition 1) and delivers the data to Linux via the
 * standard virtio receiveq mechanism.
 *
 * Implements the legacy (version 1) virtio-mmio transport.
 *   - Queue 0 (receiveq): hypervisor → Linux (FreeRTOS status data)
 *   - Queue 1 (transmitq): Linux → hypervisor (ignored / consumed)
 *
 * Uses SPI 34 (INTID 66) edge-triggered to signal data available.
 */

#include <kthread.h>
#include <processor.h>
#include <sched.h>
#include <stdc.h>
#include "prtos_vgic.h"

/* ---- Forward declarations ---- */
extern void *prtos_ipa_to_va(prtos_u64_t ipa);
extern partition_t *partition_table;

/* ---- Shared ring buffer protocol (must match user/bail/.../shared_ring.h) ---- */
#define SHARED_RING_MAGIC       0x52494E47U
#define SHARED_RING_DATA_SIZE   4096
#define SHARED_RING_HDR_SIZE    64

struct shared_ring {
    volatile prtos_u32_t magic;
    volatile prtos_u32_t write_idx;
    volatile prtos_u32_t read_idx;
    prtos_u32_t          _reserved[13];
    volatile prtos_u8_t  data[SHARED_RING_DATA_SIZE];
};

/* ---- MMIO base and size ---- */
#define VIRTIO_CONSOLE_MMIO_BASE   0x0a000000ULL
#define VIRTIO_CONSOLE_MMIO_SIZE   0x200ULL

/* ---- Shared memory IPA (same for both partitions) ---- */
#define SHARED_MEM_IPA             0x30000000ULL

/* ---- SPI for interrupt injection ---- */
#define VIRTIO_CONSOLE_SPI         34   /* INTID = 66, spis[] index = 34 */

/* ---- Virtio MMIO register offsets (legacy, version 1) ---- */
#define VIRTIO_MMIO_MAGIC_VALUE         0x000
#define VIRTIO_MMIO_VERSION             0x004
#define VIRTIO_MMIO_DEVICE_ID           0x008
#define VIRTIO_MMIO_VENDOR_ID           0x00c
#define VIRTIO_MMIO_HOST_FEATURES       0x010
#define VIRTIO_MMIO_HOST_FEATURES_SEL   0x014
#define VIRTIO_MMIO_GUEST_FEATURES      0x020
#define VIRTIO_MMIO_GUEST_FEATURES_SEL  0x024
#define VIRTIO_MMIO_GUEST_PAGE_SIZE     0x028
#define VIRTIO_MMIO_QUEUE_SEL           0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX       0x034
#define VIRTIO_MMIO_QUEUE_NUM           0x038
#define VIRTIO_MMIO_QUEUE_ALIGN         0x03c
#define VIRTIO_MMIO_QUEUE_PFN           0x040
#define VIRTIO_MMIO_QUEUE_READY         0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY        0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS    0x060
#define VIRTIO_MMIO_INTERRUPT_ACK       0x064
#define VIRTIO_MMIO_STATUS              0x070

/* Virtio device status bits */
#define VIRTIO_STATUS_ACK               1
#define VIRTIO_STATUS_DRIVER            2
#define VIRTIO_STATUS_DRIVER_OK         4
#define VIRTIO_STATUS_FEATURES_OK       8

/* Virtio vring descriptor flags */
#define VRING_DESC_F_NEXT       1
#define VRING_DESC_F_WRITE      2

/* Queue config */
#define VIRTIO_CONSOLE_QUEUE_SIZE  64   /* max descriptors per queue */
#define NUM_QUEUES                 2    /* receiveq + transmitq */

/* ---- Vring structures (as laid out in guest memory) ---- */
struct vring_desc {
    prtos_u64_t addr;
    prtos_u32_t len;
    prtos_u16_t flags;
    prtos_u16_t next;
};

struct vring_avail {
    prtos_u16_t flags;
    prtos_u16_t idx;
    prtos_u16_t ring[];
};

struct vring_used_elem {
    prtos_u32_t id;
    prtos_u32_t len;
};

struct vring_used {
    prtos_u16_t flags;
    prtos_u16_t idx;
    struct vring_used_elem ring[];
};

/* ---- Per-queue state ---- */
struct virtio_queue {
    prtos_u32_t num;        /* negotiated queue size */
    prtos_u32_t align;      /* alignment for used ring */
    prtos_u64_t pfn;        /* QueuePFN (IPA / page_size) */
    prtos_u32_t ready;

    /* Cached pointers into guest memory (set when QueuePFN is written) */
    volatile struct vring_desc  *desc;
    volatile struct vring_avail *avail;
    volatile struct vring_used  *used;

    prtos_u16_t last_avail_idx;  /* device-side shadow of avail->idx */
};

/* ---- Device state ---- */
static struct {
    prtos_u32_t status;             /* VIRTIO_STATUS_* */
    prtos_u32_t host_features_sel;
    prtos_u32_t guest_features;
    prtos_u32_t guest_features_sel;
    prtos_u32_t guest_page_size;
    prtos_u32_t queue_sel;
    prtos_u32_t int_status;
    struct virtio_queue queues[NUM_QUEUES];
    int initialized;
} vcon;

/* ---- Register access helpers (same pattern as prtos_vgic.c) ---- */
static inline prtos_u64_t vc_read_reg(struct cpu_user_regs *regs, int reg) {
    if (reg >= 31 || reg < 0) return 0;
    return (&regs->x0)[reg];
}

static inline void vc_write_reg(struct cpu_user_regs *regs, int reg,
                                prtos_u64_t val) {
    if (reg >= 0 && reg < 31) (&regs->x0)[reg] = val;
}

/* ---- Helpers ---- */

/*
 * Compute vring component addresses given QueuePFN and queue params.
 * Legacy layout: desc at base, avail after desc, used after avail (aligned).
 */
static void vcon_queue_setup(struct virtio_queue *q)
{
    if (q->pfn == 0 || q->num == 0 || vcon.guest_page_size == 0) {
        q->desc  = 0;
        q->avail = 0;
        q->used  = 0;
        return;
    }

    prtos_u64_t base_ipa = q->pfn * vcon.guest_page_size;
    prtos_u8_t *base = (prtos_u8_t *)prtos_ipa_to_va(base_ipa);

    /* desc table: N * 16 bytes */
    q->desc = (volatile struct vring_desc *)base;

    /* avail ring: right after desc table */
    prtos_u64_t avail_off = q->num * sizeof(struct vring_desc);
    q->avail = (volatile struct vring_avail *)(base + avail_off);

    /* used ring: aligned to q->align after avail ring */
    prtos_u64_t avail_size = sizeof(prtos_u16_t) * (3 + q->num);
    prtos_u64_t used_off = avail_off + avail_size;
    prtos_u32_t align = q->align ? q->align : 4096;
    used_off = (used_off + align - 1) & ~((prtos_u64_t)align - 1);
    q->used = (volatile struct vring_used *)(base + used_off);

    q->last_avail_idx = 0;
}

/* Inject SPI to Linux partition (partition 0) */
static void vcon_inject_irq(void)
{
    kthread_t *k = partition_table[0].kthread[0];
    if (!k || !k->ctrl.g || !k->ctrl.g->karch.vgic)
        return;

    struct prtos_vgic_state *vgic = k->ctrl.g->karch.vgic;
    int spi_idx = VIRTIO_CONSOLE_SPI;

    /* Set pending + enabled so vgic_flush_lrs delivers it */
    if (!vgic->spis[spi_idx].enabled)
        vgic->spis[spi_idx].enabled = 1;
    vgic->spis[spi_idx].pending = 1;
    vgic->spis[spi_idx].config = 2;   /* edge-triggered */
    vgic->spis[spi_idx].group = 1;    /* group 1 */
    vgic->spis[spi_idx].priority = 0xa0;
    vgic->spis[spi_idx].target_vcpu = 0;
}

/*
 * Cache maintenance helpers.
 *
 * The hypervisor accesses guest memory via the EL2 directmap, while the
 * guest accesses via EL1 stage-1 + stage-2 page tables.  These may have
 * differing cacheability attributes, so explicit cache invalidation
 * (before reads) and cleaning (after writes) is required.
 */
#define CACHE_LINE_SIZE 64

static inline void invalidate_dcache_range(prtos_u64_t start, prtos_u64_t size)
{
    prtos_u64_t addr = start & ~(CACHE_LINE_SIZE - 1);
    prtos_u64_t end  = start + size;
    for (; addr < end; addr += CACHE_LINE_SIZE)
        __asm__ volatile("dc ivac, %0" :: "r"(addr) : "memory");
    __asm__ volatile("dsb ish" ::: "memory");
}

static inline void clean_dcache_range(prtos_u64_t start, prtos_u64_t size)
{
    prtos_u64_t addr = start & ~(CACHE_LINE_SIZE - 1);
    prtos_u64_t end  = start + size;
    for (; addr < end; addr += CACHE_LINE_SIZE)
        __asm__ volatile("dc cvac, %0" :: "r"(addr) : "memory");
    __asm__ volatile("dsb ish" ::: "memory");
}

/*
 * Try to push data from the shared-memory ring into the receiveq.
 * Called when the guest writes to QueueNotify or when polling from
 * the hypervisor's schedule path.
 */
static void vcon_try_push_data(void)
{
    struct virtio_queue *q = &vcon.queues[0];  /* receiveq */
    if (!q->desc || !q->avail || !q->used)
        return;

    /* Get shared ring pointer (FreeRTOS partition, IPA 0x30000000) */
    volatile struct shared_ring *ring =
        (volatile struct shared_ring *)prtos_ipa_to_va(SHARED_MEM_IPA);

    /* Invalidate ring header + avail idx to check for work (fast path) */
    invalidate_dcache_range((prtos_u64_t)ring, SHARED_RING_HDR_SIZE);
    invalidate_dcache_range((prtos_u64_t)q->avail, sizeof(prtos_u16_t) * 2);

    if (ring->magic != SHARED_RING_MAGIC)
        return;
    if (ring->write_idx == ring->read_idx)
        return;  /* No data from FreeRTOS */
    if (q->last_avail_idx == q->avail->idx)
        return;  /* No guest buffers available */

    /* Work to do — invalidate full guest vring structures */
    invalidate_dcache_range((prtos_u64_t)ring->data, SHARED_RING_DATA_SIZE);
    invalidate_dcache_range((prtos_u64_t)q->desc,
                           q->num * sizeof(struct vring_desc));
    invalidate_dcache_range((prtos_u64_t)q->avail,
                           sizeof(prtos_u16_t) * (3 + q->num));

    int pushed = 0;

    while (1) {
        prtos_u32_t w = ring->write_idx;
        prtos_u32_t r = ring->read_idx;
        if (w == r)
            break;

        prtos_u16_t avail_idx = q->avail->idx;
        if (q->last_avail_idx == avail_idx)
            break;

        prtos_u16_t desc_idx = q->avail->ring[q->last_avail_idx % q->num];
        q->last_avail_idx++;

        prtos_u64_t buf_ipa = q->desc[desc_idx].addr;
        prtos_u32_t buf_len = q->desc[desc_idx].len;

        prtos_u8_t *guest_buf = (prtos_u8_t *)prtos_ipa_to_va(buf_ipa);
        prtos_u32_t to_copy = (w - r);
        if (to_copy > buf_len)
            to_copy = buf_len;

        {
            prtos_u32_t i;
            for (i = 0; i < to_copy; i++)
                guest_buf[i] = ring->data[(r + i) & (SHARED_RING_DATA_SIZE - 1)];
        }

        /* Flush writes to guest buffer */
        clean_dcache_range((prtos_u64_t)guest_buf, to_copy);

        /* Update shared ring read pointer */
        ring->read_idx = r + to_copy;
        clean_dcache_range((prtos_u64_t)&ring->read_idx, sizeof(prtos_u32_t));

        /* Add to used ring */
        prtos_u16_t used_idx = q->used->idx % q->num;
        q->used->ring[used_idx].id  = desc_idx;
        q->used->ring[used_idx].len = to_copy;
        __asm__ volatile("dmb ish" ::: "memory");
        q->used->idx++;
        clean_dcache_range((prtos_u64_t)q->used,
            sizeof(prtos_u16_t) * 3 + sizeof(struct vring_used_elem) * q->num);

        pushed = 1;
    }

    if (pushed) {
        vcon.int_status |= 1;   /* VIRTIO_MMIO_INT_VRING */
        vcon_inject_irq();
    }
}

/* ---- MMIO read handler ---- */
static int vcon_mmio_read(prtos_u64_t offset, int size, prtos_u64_t *val)
{
    *val = 0;
    struct virtio_queue *q;

    switch (offset) {
    case VIRTIO_MMIO_MAGIC_VALUE:
        *val = 0x74726976;  /* "virt" */
        break;
    case VIRTIO_MMIO_VERSION:
        *val = 1;           /* legacy */
        break;
    case VIRTIO_MMIO_DEVICE_ID:
        *val = 3;           /* console */
        break;
    case VIRTIO_MMIO_VENDOR_ID:
        *val = 0x554d4551;  /* "QEMU" */
        break;
    case VIRTIO_MMIO_HOST_FEATURES:
        /* Feature bit 0: no special features needed for basic console */
        *val = 0;
        break;
    case VIRTIO_MMIO_QUEUE_NUM_MAX:
        *val = VIRTIO_CONSOLE_QUEUE_SIZE;
        break;
    case VIRTIO_MMIO_QUEUE_PFN:
        q = &vcon.queues[vcon.queue_sel < NUM_QUEUES ? vcon.queue_sel : 0];
        *val = q->pfn;
        break;
    case VIRTIO_MMIO_INTERRUPT_STATUS:
        *val = vcon.int_status;
        break;
    case VIRTIO_MMIO_STATUS:
        *val = vcon.status;
        break;
    default:
        break;
    }
    return 0;
}

/* ---- MMIO write handler ---- */
static int vcon_mmio_write(prtos_u64_t offset, int size, prtos_u64_t val)
{
    struct virtio_queue *q;

    switch (offset) {
    case VIRTIO_MMIO_HOST_FEATURES_SEL:
        vcon.host_features_sel = (prtos_u32_t)val;
        break;
    case VIRTIO_MMIO_GUEST_FEATURES:
        vcon.guest_features = (prtos_u32_t)val;
        break;
    case VIRTIO_MMIO_GUEST_FEATURES_SEL:
        vcon.guest_features_sel = (prtos_u32_t)val;
        break;
    case VIRTIO_MMIO_GUEST_PAGE_SIZE:
        vcon.guest_page_size = (prtos_u32_t)val;
        break;
    case VIRTIO_MMIO_QUEUE_SEL:
        vcon.queue_sel = (prtos_u32_t)val;
        break;
    case VIRTIO_MMIO_QUEUE_NUM:
        if (vcon.queue_sel < NUM_QUEUES) {
            vcon.queues[vcon.queue_sel].num = (prtos_u32_t)val;
        }
        break;
    case VIRTIO_MMIO_QUEUE_ALIGN:
        if (vcon.queue_sel < NUM_QUEUES) {
            vcon.queues[vcon.queue_sel].align = (prtos_u32_t)val;
        }
        break;
    case VIRTIO_MMIO_QUEUE_PFN:
        if (vcon.queue_sel < NUM_QUEUES) {
            q = &vcon.queues[vcon.queue_sel];
            q->pfn = (prtos_u32_t)val;
            vcon_queue_setup(q);
        }
        break;
    case VIRTIO_MMIO_QUEUE_NOTIFY:
        /* Guest kicked a queue — try pushing data if it's the receiveq */
        if ((prtos_u32_t)val == 0) {
            vcon_try_push_data();
        }
        /* For transmitq (val==1): consume and discard guest writes */
        if ((prtos_u32_t)val == 1) {
            q = &vcon.queues[1];
            if (q->desc && q->avail && q->used) {
                invalidate_dcache_range((prtos_u64_t)q->avail,
                    sizeof(prtos_u16_t) * (3 + q->num));
                prtos_u16_t avail_idx = q->avail->idx;
                while (q->last_avail_idx != avail_idx) {
                    prtos_u16_t di = q->avail->ring[q->last_avail_idx % q->num];
                    q->last_avail_idx++;
                    prtos_u16_t ui = q->used->idx % q->num;
                    q->used->ring[ui].id  = di;
                    q->used->ring[ui].len = 0;
                    __asm__ volatile("dmb ish" ::: "memory");
                    q->used->idx++;
                }
                clean_dcache_range((prtos_u64_t)q->used,
                    sizeof(prtos_u16_t) * 3 + sizeof(struct vring_used_elem) * q->num);
                vcon.int_status |= 1;
                vcon_inject_irq();
            }
        }
        break;
    case VIRTIO_MMIO_INTERRUPT_ACK:
        vcon.int_status &= ~(prtos_u32_t)val;
        break;
    case VIRTIO_MMIO_STATUS:
        if ((prtos_u32_t)val == 0) {
            /* Reset device — preserve guest_page_size since it's set
             * once before the first status write sequence. */
            prtos_u32_t saved_pgsz = vcon.guest_page_size;
            memset(&vcon, 0, sizeof(vcon));
            vcon.guest_page_size = saved_pgsz;
        } else {
            vcon.status = (prtos_u32_t)val;
            if (vcon.status & VIRTIO_STATUS_DRIVER_OK) {
                vcon.initialized = 1;
            }
        }
        break;
    default:
        break;
    }
    return 0;
}

/* ---- Public API: called from prtos_mmio_dispatch() ---- */

/*
 * Handle a data abort to the virtio-console MMIO region.
 * Returns 0 if handled, -1 if address not in our range.
 */
int prtos_virtio_console_mmio(struct cpu_user_regs *regs, prtos_u64_t gpa,
                              int is_write, int reg, int size)
{
    if (gpa < VIRTIO_CONSOLE_MMIO_BASE ||
        gpa >= VIRTIO_CONSOLE_MMIO_BASE + VIRTIO_CONSOLE_MMIO_SIZE)
        return -1;

    prtos_u64_t offset = gpa - VIRTIO_CONSOLE_MMIO_BASE;
    prtos_u64_t val;

    if (is_write) {
        val = vc_read_reg(regs, reg);
        vcon_mmio_write(offset, size, val);
    } else {
        vcon_mmio_read(offset, size, &val);
        vc_write_reg(regs, reg, val);
    }
    return 0;
}

/*
 * Poll for data from FreeRTOS and push into Linux receiveq.
 * Called from the hypervisor's context-switch / schedule path.
 */
void prtos_virtio_console_poll(void)
{
    if (vcon.initialized)
        vcon_try_push_data();
}
