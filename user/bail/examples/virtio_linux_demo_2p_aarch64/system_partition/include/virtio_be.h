/*
 * virtio_be.h - Shared header for PRTOS Virtio Backend (System Partition).
 *
 * AArch64 version: Shared memory addresses for PRTOS hw-virt on AArch64.
 *
 * Shared Memory Regions (5 separate areas, mapped by both partitions):
 *   Virtio_Net0:  1MB   @ IPA 0x20000000  (virtio-net bridge)
 *   Virtio_Net1:  1MB   @ IPA 0x20100000  (virtio-net NAT)
 *   Virtio_Net2:  1MB   @ IPA 0x20200000  (virtio-net p2p)
 *   Virtio_Blk:   2MB   @ IPA 0x20300000  (virtio-blk file-backed)
 *   Virtio_Con:   256KB @ IPA 0x20500000  (virtio-console)
 *
 * Doorbell IPVI (Guest -> System via IPVI):
 *   net0 -> IPVI 0,  net1 -> IPVI 1,  net2 -> IPVI 2
 *   blk  -> IPVI 3,  con  -> IPVI 4
 * Completion doorbell (System -> Guest): IPVI 5
 */

#ifndef _VIRTIO_BE_H_
#define _VIRTIO_BE_H_

#include <stdint.h>

/* ============================================================================
 * Shared Memory Base Addresses (IPA, identity-mapped via stage-2)
 * ============================================================================ */

#define VIRTIO_NET0_BASE        0x20000000UL
#define VIRTIO_NET0_SIZE        0x00100000UL    /* 1MB */

#define VIRTIO_NET1_BASE        0x20100000UL
#define VIRTIO_NET1_SIZE        0x00100000UL    /* 1MB */

#define VIRTIO_NET2_BASE        0x20200000UL
#define VIRTIO_NET2_SIZE        0x00100000UL    /* 1MB */

#define VIRTIO_BLK_BASE         0x20300000UL
#define VIRTIO_BLK_SIZE         0x00200000UL    /* 2MB */

#define VIRTIO_CON_BASE         0x20500000UL
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
    uint32_t len;
    uint8_t  data[VIRTIO_NET_PKT_MAX_SIZE];
} __attribute__((packed));

struct virtio_net_shm {
    uint32_t magic;
    uint32_t version;
    uint32_t device_status;
    uint32_t backend_ready;
    uint32_t frontend_ready;
    volatile uint32_t tx_head;
    volatile uint32_t tx_tail;
    volatile uint32_t rx_head;
    volatile uint32_t rx_tail;
    uint32_t num_slots;
    uint32_t mode;
    volatile uint32_t doorbell_count;
    uint32_t reserved[4];
    struct virtio_net_pkt_slot tx_slots[VIRTIO_NET_PKT_SLOTS];
    struct virtio_net_pkt_slot rx_slots[VIRTIO_NET_PKT_SLOTS];
} __attribute__((packed));

/* ============================================================================
 * Virtio-Blk Shared Memory Layout (2MB)
 * ============================================================================ */

#define VIRTIO_BLK_MAGIC            0x424C4B30UL /* "BLK0" */
#define VIRTIO_BLK_REQ_SLOTS        16
#define VIRTIO_BLK_SECTOR_SIZE      512

#define VIRTIO_BLK_T_IN            0
#define VIRTIO_BLK_T_OUT           1
#define VIRTIO_BLK_T_FLUSH         4
#define VIRTIO_BLK_T_GET_ID        8

#define VIRTIO_BLK_S_OK            0
#define VIRTIO_BLK_S_IOERR         1
#define VIRTIO_BLK_S_UNSUPP        2

struct virtio_blk_req {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
    uint32_t len;
    uint32_t status;
    uint8_t  data[4096];
} __attribute__((packed));

struct virtio_blk_shm {
    uint32_t magic;
    uint32_t version;
    uint32_t device_status;
    uint32_t backend_ready;
    uint32_t frontend_ready;
    volatile uint32_t req_head;
    volatile uint32_t req_tail;
    volatile uint32_t resp_head;
    volatile uint32_t resp_tail;
    uint64_t capacity_sectors;
    uint32_t num_slots;
    volatile uint32_t doorbell_count;
    uint32_t reserved[2];
    struct virtio_blk_req slots[VIRTIO_BLK_REQ_SLOTS];
} __attribute__((packed));

/* ============================================================================
 * Virtio-Console Shared Memory Layout (256KB)
 * ============================================================================ */

#define VIRTIO_CONSOLE_MAGIC        0x434F4E53UL /* "CONS" */
#define VIRTIO_CONSOLE_BUF_SIZE     4096

struct virtio_console_shm {
    uint32_t magic;
    uint32_t version;
    uint32_t device_status;
    uint32_t backend_ready;
    uint32_t frontend_ready;
    volatile uint32_t tx_head;
    volatile uint32_t tx_tail;
    volatile uint32_t rx_head;
    volatile uint32_t rx_tail;
    uint32_t buf_size;
    volatile uint32_t doorbell_count;
    uint32_t reserved;
    char tx_buf[VIRTIO_CONSOLE_BUF_SIZE];
    char rx_buf[VIRTIO_CONSOLE_BUF_SIZE];
} __attribute__((packed));

/* ============================================================================
 * Net instance descriptor (runtime state in backend)
 * ============================================================================ */

struct virtio_net_instance {
    int id;
    int mode;
    int backend_fd;
    struct virtio_net_shm *shm;
    unsigned long phys_base;
    unsigned long phys_size;
};

/* ============================================================================
 * Backend function prototypes
 * ============================================================================ */

void virtio_console_init(struct virtio_console_shm *con);
void virtio_console_process(struct virtio_console_shm *con);
void virtio_console_cleanup(void);

int  virtio_net_init(struct virtio_net_instance *inst);
void virtio_net_process(struct virtio_net_instance *inst);
void virtio_net_cleanup(struct virtio_net_instance *inst);

void virtio_blk_init(struct virtio_blk_shm *blk, const char *backing_file);
void virtio_blk_process(struct virtio_blk_shm *blk);

void doorbell_init(void);
void doorbell_signal_guest(void);

int  manager_init(void);
int  manager_get_guest_status(void);

#endif /* _VIRTIO_BE_H_ */
