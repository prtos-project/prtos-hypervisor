/*
 * shared_ring.h — Lock-free single-producer/single-consumer ring buffer.
 *
 * Shared between FreeRTOS (producer, partition 1) and the PRTOS hypervisor
 * (consumer, reads on behalf of Linux virtio-console backend).
 *
 * Layout at IPA 0x30000000 (PA 0x70000000):
 *   [0x00]  magic       — 0x52494E47 ("RING") when initialized
 *   [0x04]  write_idx   — producer write index (FreeRTOS increments)
 *   [0x08]  read_idx    — consumer read index  (hypervisor increments)
 *   [0x0C]  reserved
 *   [0x40]  data[0..RING_DATA_SIZE-1] — ring buffer payload
 *
 * Protocol:
 *   Producer writes data[write_idx % RING_DATA_SIZE], then increments write_idx.
 *   Consumer reads  data[read_idx  % RING_DATA_SIZE], then increments read_idx.
 *   Buffer is empty when write_idx == read_idx.
 *   Buffer is full  when (write_idx - read_idx) == RING_DATA_SIZE.
 *
 * Both indices wrap using modular arithmetic (uint32_t); only the lower bits
 * matter after masking with (RING_DATA_SIZE - 1).
 */

#ifndef _SHARED_RING_H_
#define _SHARED_RING_H_

#define SHARED_RING_MAGIC      0x52494E47U  /* "RING" */
#define SHARED_RING_DATA_SIZE  4096         /* Must be power of 2 */
#define SHARED_RING_HDR_SIZE   64           /* Header before data area */

#ifndef __ASSEMBLER__

#include <stdint.h>

struct shared_ring {
    volatile uint32_t magic;
    volatile uint32_t write_idx;
    volatile uint32_t read_idx;
    uint32_t          _reserved[13];       /* Pad header to 64 bytes */
    volatile uint8_t  data[SHARED_RING_DATA_SIZE];
};

/* ---- Producer helpers (FreeRTOS side) ---- */

static inline void shared_ring_init(volatile struct shared_ring *r)
{
    r->write_idx = 0;
    r->read_idx  = 0;
    __asm__ volatile("dmb ish" ::: "memory");
    r->magic = SHARED_RING_MAGIC;
    __asm__ volatile("dmb ish" ::: "memory");
}

static inline uint32_t shared_ring_free_space(volatile struct shared_ring *r)
{
    uint32_t w = r->write_idx;
    uint32_t rd = r->read_idx;
    __asm__ volatile("dmb ish" ::: "memory");
    return SHARED_RING_DATA_SIZE - (w - rd);
}

/* Write a byte buffer. Returns number of bytes actually written. */
static inline uint32_t shared_ring_write(volatile struct shared_ring *r,
                                         const uint8_t *buf, uint32_t len)
{
    uint32_t w = r->write_idx;
    uint32_t rd = r->read_idx;
    __asm__ volatile("dmb ish" ::: "memory");

    uint32_t avail = SHARED_RING_DATA_SIZE - (w - rd);
    if (len > avail)
        len = avail;

    for (uint32_t i = 0; i < len; i++)
        r->data[(w + i) & (SHARED_RING_DATA_SIZE - 1)] = buf[i];

    __asm__ volatile("dmb ish" ::: "memory");
    r->write_idx = w + len;
    return len;
}

/* Write a null-terminated string (without the NUL). */
static inline uint32_t shared_ring_puts(volatile struct shared_ring *r,
                                        const char *s)
{
    uint32_t len = 0;
    while (s[len]) len++;
    return shared_ring_write(r, (const uint8_t *)s, len);
}

/* ---- Consumer helpers (hypervisor side) ---- */

static inline uint32_t shared_ring_available(volatile struct shared_ring *r)
{
    uint32_t w = r->write_idx;
    __asm__ volatile("dmb ish" ::: "memory");
    uint32_t rd = r->read_idx;
    return w - rd;
}

/* Read up to len bytes. Returns number of bytes actually read. */
static inline uint32_t shared_ring_read(volatile struct shared_ring *r,
                                        uint8_t *buf, uint32_t len)
{
    uint32_t rd = r->read_idx;
    uint32_t w  = r->write_idx;
    __asm__ volatile("dmb ish" ::: "memory");

    uint32_t avail = w - rd;
    if (len > avail)
        len = avail;

    for (uint32_t i = 0; i < len; i++)
        buf[i] = r->data[(rd + i) & (SHARED_RING_DATA_SIZE - 1)];

    __asm__ volatile("dmb ish" ::: "memory");
    r->read_idx = rd + len;
    return len;
}

#endif /* __ASSEMBLER__ */
#endif /* _SHARED_RING_H_ */

