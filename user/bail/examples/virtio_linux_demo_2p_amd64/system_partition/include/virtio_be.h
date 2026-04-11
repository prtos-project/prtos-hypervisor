/*
 * virtio_be.h - Shared header for PRTOS Virtio Backend (System Partition).
 *
 * Defines the shared memory layout and data structures for Virtio device
 * virtualization between the System Partition (Backend) and Guest Partition
 * (Frontend) on the PRTOS Hypervisor.
 *
 * Shared Memory Regions (5 separate areas, mapped by both partitions):
 *   Virtio_Net0:  1MB   @ GPA 0x16000000  (virtio-net bridge)
 *   Virtio_Net1:  1MB   @ GPA 0x16100000  (virtio-net NAT)
 *   Virtio_Net2:  1MB   @ GPA 0x16200000  (virtio-net p2p)
 *   Virtio_Blk:   2MB   @ GPA 0x16300000  (virtio-blk file-backed)
 *   Virtio_Con:   256KB @ GPA 0x16500000  (virtio-console)
 *
 * Doorbell IRQ (Guest -> System via IPVI):
 *   net0 -> IPVI 0 (vector 32)
 *   net1 -> IPVI 1 (vector 33)
 *   net2 -> IPVI 2 (vector 34)
 *   blk  -> IPVI 3 (vector 35)
 *   con  -> IPVI 4 (vector 36)
 *
 * Completion doorbell (System -> Guest):
 *   IPVI 5
 */

#ifndef _VIRTIO_BE_H_
#define _VIRTIO_BE_H_

#include <stdint.h>

/* ============================================================================
 * Shared Memory Base Addresses (GPA, identity-mapped via EPT)
 * ============================================================================ */

#define VIRTIO_NET0_BASE        0x16000000UL
#define VIRTIO_NET0_SIZE        0x00100000UL    /* 1MB */

#define VIRTIO_NET1_BASE        0x16100000UL
#define VIRTIO_NET1_SIZE        0x00100000UL    /* 1MB */

#define VIRTIO_NET2_BASE        0x16200000UL
#define VIRTIO_NET2_SIZE        0x00100000UL    /* 1MB */

#define VIRTIO_BLK_BASE         0x16300000UL
#define VIRTIO_BLK_SIZE         0x00200000UL    /* 2MB */

#define VIRTIO_CON_BASE         0x16500000UL
#define VIRTIO_CON_SIZE         0x00040000UL    /* 256KB */

#define VIRTIO_NUM_NET          3
#define VIRTIO_NUM_DEVICES      5       /* 3 net + 1 blk + 1 console */

/* ============================================================================
 * IPVI Doorbell Vector Numbers (Guest -> System)
 * ============================================================================ */

#define IPVI_NET0               0       /* virtio-net bridge */
#define IPVI_NET1               1       /* virtio-net NAT */
#define IPVI_NET2               2       /* virtio-net p2p */
#define IPVI_BLK                3       /* virtio-blk */
#define IPVI_CON                4       /* virtio-console */
#define IPVI_SYS_TO_GUEST       5       /* completion doorbell */

/* ============================================================================
 * Common Virtio Constants
 * ============================================================================ */

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

/* ============================================================================
 * Virtio-Net Shared Memory Layout (1MB per instance)
 *
 * Each net instance has its own 1MB region with independent control block,
 * ring buffers, and packet slots.
 *
 * Layout within each 1MB region:
 *   Offset 0x000: Control header (256 bytes)
 *   Offset 0x100: TX packet slots (Guest -> Backend)
 *   Offset ~half: RX packet slots (Backend -> Guest)
 * ============================================================================ */

#define VIRTIO_NET_MAGIC            0x4E455430UL /* "NET0" */
#define VIRTIO_NET_PKT_SLOTS        64
#define VIRTIO_NET_PKT_MAX_SIZE     1536

/* Net backend mode */
#define VIRTIO_NET_MODE_BRIDGE      0
#define VIRTIO_NET_MODE_NAT         1
#define VIRTIO_NET_MODE_P2P         2
#define VIRTIO_NET_MODE_LOOPBACK    3

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

struct virtio_net_shm {
    /* Control header */
    uint32_t magic;                     /* VIRTIO_NET_MAGIC */
    uint32_t version;                   /* Protocol version (1) */
    uint32_t device_status;             /* VIRTIO_STATUS_xxx bits */
    uint32_t backend_ready;             /* Backend has initialized this region */
    uint32_t frontend_ready;            /* Frontend has connected */
    volatile uint32_t tx_head;          /* Guest writes (packets to send) */
    volatile uint32_t tx_tail;          /* Backend reads */
    volatile uint32_t rx_head;          /* Backend writes (received packets) */
    volatile uint32_t rx_tail;          /* Guest reads */
    uint32_t num_slots;
    uint32_t mode;                      /* VIRTIO_NET_MODE_xxx */
    volatile uint32_t doorbell_count;   /* Guest increments on new TX */
    uint32_t reserved[4];
    /* Packet slot arrays */
    struct virtio_net_pkt_slot tx_slots[VIRTIO_NET_PKT_SLOTS];
    struct virtio_net_pkt_slot rx_slots[VIRTIO_NET_PKT_SLOTS];
} __attribute__((packed));

/* ============================================================================
 * Virtio-Blk Shared Memory Layout (2MB)
 *
 * Block request/response ring for disk I/O.
 * Guest submits read/write requests with sector addresses.
 * Backend processes using pread()/pwrite() on a backing file.
 *
 * Layout:
 *   Offset 0x000: Control header
 *   Offset 0x100: Request slots
 * ============================================================================ */

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

struct virtio_blk_shm {
    /* Control header */
    uint32_t magic;                         /* VIRTIO_BLK_MAGIC */
    uint32_t version;                       /* Protocol version (1) */
    uint32_t device_status;                 /* VIRTIO_STATUS_xxx bits */
    uint32_t backend_ready;
    uint32_t frontend_ready;
    volatile uint32_t req_head;             /* Guest writes requests */
    volatile uint32_t req_tail;             /* Backend processes requests */
    volatile uint32_t resp_head;            /* Backend writes responses */
    volatile uint32_t resp_tail;            /* Guest reads responses */
    uint64_t capacity_sectors;              /* Total disk size in sectors */
    uint32_t num_slots;
    volatile uint32_t doorbell_count;
    uint32_t reserved[2];
    /* Request slot array */
    struct virtio_blk_req slots[VIRTIO_BLK_REQ_SLOTS];
} __attribute__((packed));

/* ============================================================================
 * Virtio-Console Shared Memory Layout (256KB)
 *
 * Simple character ring buffer for console I/O between partitions.
 * Guest writes characters to tx_buf, Backend reads and prints to stdout.
 * Backend writes to rx_buf, Guest reads for input.
 * ============================================================================ */

#define VIRTIO_CONSOLE_MAGIC        0x434F4E53UL /* "CONS" */
#define VIRTIO_CONSOLE_BUF_SIZE     4096

struct virtio_console_shm {
    /* Control header */
    uint32_t magic;             /* VIRTIO_CONSOLE_MAGIC */
    uint32_t version;           /* Protocol version (1) */
    uint32_t device_status;     /* VIRTIO_STATUS_xxx bits */
    uint32_t backend_ready;
    uint32_t frontend_ready;
    volatile uint32_t tx_head;  /* Guest writes here (Guest -> Backend) */
    volatile uint32_t tx_tail;  /* Backend reads from here */
    volatile uint32_t rx_head;  /* Backend writes here (Backend -> Guest) */
    volatile uint32_t rx_tail;  /* Guest reads from here */
    uint32_t buf_size;          /* Size of each ring buffer */
    volatile uint32_t doorbell_count;
    uint32_t reserved;
    char tx_buf[VIRTIO_CONSOLE_BUF_SIZE];   /* Guest -> Backend */
    char rx_buf[VIRTIO_CONSOLE_BUF_SIZE];   /* Backend -> Guest */
} __attribute__((packed));

/* ============================================================================
 * Net instance descriptor (runtime state in backend)
 * ============================================================================ */

struct virtio_net_instance {
    int id;                             /* Instance number (0, 1, 2) */
    int mode;                           /* VIRTIO_NET_MODE_xxx */
    int backend_fd;                     /* TAP fd, socket fd, etc. (-1 if unused) */
    struct virtio_net_shm *shm;         /* Mapped shared memory pointer */
    unsigned long phys_base;            /* Physical base address */
    unsigned long phys_size;            /* Physical region size */
};

/* ============================================================================
 * Backend function prototypes (implemented in separate .c files)
 * ============================================================================ */

/* virtio_console.c */
void virtio_console_init(struct virtio_console_shm *con);
void virtio_console_process(struct virtio_console_shm *con);
void virtio_console_cleanup(void);

/* virtio_net.c */
int  virtio_net_init(struct virtio_net_instance *inst);
void virtio_net_process(struct virtio_net_instance *inst);
void virtio_net_cleanup(struct virtio_net_instance *inst);

/* virtio_blk.c */
void virtio_blk_init(struct virtio_blk_shm *blk, const char *backing_file);
void virtio_blk_process(struct virtio_blk_shm *blk);

/* doorbell.c */
void doorbell_init(void);
void doorbell_signal_guest(void);

/* manager_if.c */
int  manager_init(void);
int  manager_get_guest_status(void);

#endif /* _VIRTIO_BE_H_ */
