/*
 * virtio_be.h - Shared header for PRTOS Virtio Backend (System Partition).
 *
 * Defines the shared memory layout and data structures for Virtio device
 * virtualization between the System Partition (Backend) and Guest Partition
 * (Frontend) on the PRTOS Hypervisor.
 *
 * Shared Memory Region: 8MB at GPA 0x16000000
 *
 * Layout:
 *   Offset 0x000000: Control block (4KB)    - Device status, feature negotiation
 *   Offset 0x001000: Virtio-Console (64KB)  - Character ring buffer
 *   Offset 0x011000: Virtio-Net (1MB)       - Packet slot ring buffers (RX/TX)
 *   Offset 0x111000: Virtio-Blk (1MB)       - Block request/response ring
 *   Offset 0x211000: Reserved (~6MB)
 *
 */

#ifndef _VIRTIO_BE_H_
#define _VIRTIO_BE_H_

#include <stdint.h>

/* ============================================================================
 * Shared Memory Configuration
 * ============================================================================ */

#define VIRTIO_SHMEM_BASE       0x16000000UL
#define VIRTIO_SHMEM_SIZE       0x00800000UL    /* 8MB */

/* ============================================================================
 * Control Block (offset 0x000000, 4KB)
 * Written by Backend (System Partition) during init,
 * read/updated by Frontend (Guest Partition) during negotiation.
 * ============================================================================ */

#define VIRTIO_CTRL_OFFSET      0x000000
#define VIRTIO_CTRL_SIZE        0x001000

#define VIRTIO_PRTOS_MAGIC      0x50525456UL    /* "PRTV" */

/* Device status bits (Virtio spec section 2.1) */
#define VIRTIO_STATUS_ACK           0x01
#define VIRTIO_STATUS_DRIVER        0x02
#define VIRTIO_STATUS_DRIVER_OK     0x04
#define VIRTIO_STATUS_FEATURES_OK   0x08
#define VIRTIO_STATUS_FAILED        0x80

/* Device type IDs */
#define VIRTIO_DEV_NET          1
#define VIRTIO_DEV_BLK          2
#define VIRTIO_DEV_CONSOLE      3

#define VIRTIO_NUM_DEVICES      3

struct virtio_dev_info {
    uint32_t device_id;         /* VIRTIO_DEV_xxx */
    uint32_t status;            /* VIRTIO_STATUS_xxx bits */
    uint32_t features;          /* Negotiated feature bits */
    uint32_t queue_offset;      /* Offset from SHMEM_BASE to device region */
    uint32_t queue_size;        /* Total size of device region */
    uint32_t num_queues;        /* Number of virtqueues for this device */
    uint32_t reserved[2];
} __attribute__((packed));

struct virtio_ctrl_block {
    uint32_t magic;             /* VIRTIO_PRTOS_MAGIC */
    uint32_t version;           /* Protocol version (1) */
    uint32_t backend_status;    /* Backend ready flags */
    uint32_t frontend_status;   /* Frontend ready flags */
    struct virtio_dev_info devices[VIRTIO_NUM_DEVICES];
    uint32_t doorbell_guest_to_sys;     /* Counter: Guest increments */
    uint32_t doorbell_sys_to_guest;     /* Counter: Backend increments */
} __attribute__((packed));

/* ============================================================================
 * Virtio-Console (offset 0x001000, 64KB)
 *
 * Simple character ring buffer for console I/O between partitions.
 * Guest writes characters to tx_buf, Backend reads and prints to stdout.
 * Backend writes to rx_buf, Guest reads for input.
 * ============================================================================ */

#define VIRTIO_CONSOLE_OFFSET       0x001000
#define VIRTIO_CONSOLE_SIZE         0x010000     /* 64KB */
#define VIRTIO_CONSOLE_MAGIC        0x434F4E53UL /* "CONS" */
#define VIRTIO_CONSOLE_BUF_SIZE     4096

struct virtio_console_ring {
    uint32_t magic;             /* VIRTIO_CONSOLE_MAGIC */
    volatile uint32_t tx_head;  /* Guest writes here (Guest -> Backend) */
    volatile uint32_t tx_tail;  /* Backend reads from here */
    volatile uint32_t rx_head;  /* Backend writes here (Backend -> Guest) */
    volatile uint32_t rx_tail;  /* Guest reads from here */
    uint32_t buf_size;          /* Size of each ring buffer */
    uint32_t reserved[2];
    char tx_buf[VIRTIO_CONSOLE_BUF_SIZE];   /* Guest -> Backend */
    char rx_buf[VIRTIO_CONSOLE_BUF_SIZE];   /* Backend -> Guest */
} __attribute__((packed));

/* ============================================================================
 * Virtio-Net (offset 0x011000, 1MB)
 *
 * Packet slot ring buffers for network I/O.
 * Two directions: TX (Guest -> Backend -> physical NIC) and
 *                 RX (physical NIC -> Backend -> Guest)
 * Each slot holds one Ethernet frame up to 1536 bytes.
 * ============================================================================ */

#define VIRTIO_NET_OFFSET           0x011000
#define VIRTIO_NET_SIZE             0x100000     /* 1MB */
#define VIRTIO_NET_MAGIC            0x4E455430UL /* "NET0" */
#define VIRTIO_NET_PKT_SLOTS        64
#define VIRTIO_NET_PKT_MAX_SIZE     1536

struct virtio_net_hdr {
    uint8_t  flags;
    uint8_t  gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
    uint16_t num_buffers;
} __attribute__((packed));

struct virtio_net_pkt_slot {
    uint32_t len;                       /* Actual packet length (0 = empty) */
    uint8_t  data[VIRTIO_NET_PKT_MAX_SIZE];
} __attribute__((packed));

struct virtio_net_ring {
    uint32_t magic;                     /* VIRTIO_NET_MAGIC */
    volatile uint32_t tx_head;          /* Guest writes (packets to send) */
    volatile uint32_t tx_tail;          /* Backend reads */
    volatile uint32_t rx_head;          /* Backend writes (received packets) */
    volatile uint32_t rx_tail;          /* Guest reads */
    uint32_t num_slots;
    uint32_t reserved[2];
    struct virtio_net_pkt_slot tx_slots[VIRTIO_NET_PKT_SLOTS];
    struct virtio_net_pkt_slot rx_slots[VIRTIO_NET_PKT_SLOTS];
} __attribute__((packed));

/* ============================================================================
 * Virtio-Blk (offset 0x111000, 1MB)
 *
 * Block request/response ring for disk I/O.
 * Guest submits read/write requests with sector addresses.
 * Backend processes using pread()/pwrite() on a backing file or RAM disk.
 * ============================================================================ */

#define VIRTIO_BLK_OFFSET           0x111000
#define VIRTIO_BLK_SIZE             0x100000     /* 1MB */
#define VIRTIO_BLK_MAGIC            0x424C4B30UL /* "BLK0" */
#define VIRTIO_BLK_REQ_SLOTS        16
#define VIRTIO_BLK_SECTOR_SIZE      512

/* Block request types (Virtio spec) */
#define VIRTIO_BLK_T_IN            0    /* Read */
#define VIRTIO_BLK_T_OUT           1    /* Write */
#define VIRTIO_BLK_T_FLUSH         4    /* Flush */
#define VIRTIO_BLK_T_GET_ID        8    /* Get device ID */

/* Block request status */
#define VIRTIO_BLK_S_OK            0
#define VIRTIO_BLK_S_IOERR         1
#define VIRTIO_BLK_S_UNSUPP        2

struct virtio_blk_req {
    uint32_t type;              /* VIRTIO_BLK_T_xxx */
    uint32_t reserved;
    uint64_t sector;            /* Starting sector number */
    uint32_t len;               /* Data length in bytes */
    uint32_t status;            /* VIRTIO_BLK_S_xxx (written by backend) */
    uint8_t  data[4096];        /* Data buffer (up to one page) */
} __attribute__((packed));

struct virtio_blk_ring {
    uint32_t magic;                         /* VIRTIO_BLK_MAGIC */
    volatile uint32_t req_head;             /* Guest writes requests */
    volatile uint32_t req_tail;             /* Backend processes requests */
    volatile uint32_t resp_head;            /* Backend writes responses */
    volatile uint32_t resp_tail;            /* Guest reads responses */
    uint64_t capacity_sectors;              /* Total disk size in sectors */
    uint32_t num_slots;
    uint32_t reserved;
    struct virtio_blk_req slots[VIRTIO_BLK_REQ_SLOTS];
} __attribute__((packed));

/* ============================================================================
 * Helper macros for accessing shared memory from mapped base pointer
 * ============================================================================ */

#define VIRTIO_CTRL(base)       ((struct virtio_ctrl_block *)((char *)(base) + VIRTIO_CTRL_OFFSET))
#define VIRTIO_CONSOLE(base)    ((struct virtio_console_ring *)((char *)(base) + VIRTIO_CONSOLE_OFFSET))
#define VIRTIO_NET(base)        ((struct virtio_net_ring *)((char *)(base) + VIRTIO_NET_OFFSET))
#define VIRTIO_BLK(base)        ((struct virtio_blk_ring *)((char *)(base) + VIRTIO_BLK_OFFSET))

/* ============================================================================
 * Backend function prototypes (implemented in separate .c files)
 * ============================================================================ */

/* virtio_console.c */
void virtio_console_init(void *shmem_base);
void virtio_console_process(void *shmem_base);

/* virtio_net.c */
void virtio_net_init(void *shmem_base);
void virtio_net_process(void *shmem_base);

/* virtio_blk.c */
void virtio_blk_init(void *shmem_base, const char *backing_file);
void virtio_blk_process(void *shmem_base);

#endif /* _VIRTIO_BE_H_ */
