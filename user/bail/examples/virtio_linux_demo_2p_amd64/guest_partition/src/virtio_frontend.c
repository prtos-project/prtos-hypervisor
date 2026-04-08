/*
 * virtio_frontend.c - Virtio Frontend daemon for PRTOS Guest Partition.
 *
 * Bridges the PRTOS shared-memory custom Virtio protocol to standard
 * Linux device interfaces:
 *   - Virtio-Blk  -> /dev/nbd0  (via built-in NBD server)
 *   - Virtio-Console -> /dev/hvc0 (via PTY pair)
 *   - Virtio-Net  -> tap0/tap1/tap2 (via TUN/TAP)
 *
 * Runs in Guest Partition userspace. Maps shared memory via /dev/mem,
 * then creates real Linux device nodes that applications can use normally.
 *
 * Usage: virtio_frontend [--blk] [--console] [--net] [--all]
 *        Default: --all (enable all devices)
 *
 * Prerequisites:
 *   - Kernel CONFIG_BLK_DEV_NBD=y, CONFIG_TUN=y
 *   - /dev/mem accessible (need root)
 *   - nbd kernel module or built-in
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/select.h>
#include <pthread.h>
#include <linux/nbd.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <arpa/inet.h>

/* ============================================================================
 * Shared Memory Layout (must match backend's virtio_be.h)
 * ============================================================================ */

#define VIRTIO_BLK_BASE         0x16300000UL
#define VIRTIO_BLK_SIZE         0x00200000UL    /* 2MB */

#define VIRTIO_CON_BASE         0x16500000UL
#define VIRTIO_CON_SIZE         0x00040000UL    /* 256KB */

/* Magic values */
#define VIRTIO_BLK_MAGIC        0x424C4B30UL    /* "BLK0" */
#define VIRTIO_CONSOLE_MAGIC    0x434F4E53UL    /* "CONS" */

/* Status bits */
#define VIRTIO_STATUS_ACK           0x01
#define VIRTIO_STATUS_DRIVER        0x02
#define VIRTIO_STATUS_DRIVER_OK     0x04

/* Block constants */
#define VIRTIO_BLK_SECTOR_SIZE  512
#define VIRTIO_BLK_REQ_SLOTS    16
#define VIRTIO_BLK_T_IN         0       /* Read */
#define VIRTIO_BLK_T_OUT        1       /* Write */
#define VIRTIO_BLK_T_FLUSH      4
#define VIRTIO_BLK_S_OK         0
#define VIRTIO_BLK_S_IOERR      1

/* Console constants */
#define VIRTIO_CONSOLE_BUF_SIZE 4096

/* Net constants */
#define VIRTIO_NET0_BASE        0x16000000UL
#define VIRTIO_NET1_BASE        0x16100000UL
#define VIRTIO_NET2_BASE        0x16200000UL
#define VIRTIO_NET_SIZE         0x00100000UL    /* 1MB each */
#define VIRTIO_NET_MAGIC        0x4E455430UL    /* "NET0" */
#define VIRTIO_NET_PKT_SLOTS    64
#define VIRTIO_NET_PKT_MAX_SIZE 1536
#define VIRTIO_NUM_NET          3

/* ============================================================================
 * Shared Memory Structures (mirror of virtio_be.h)
 * ============================================================================ */

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
    volatile uint32_t tx_head;          /* Guest writes (packets to send) */
    volatile uint32_t tx_tail;          /* Backend reads */
    volatile uint32_t rx_head;          /* Backend writes (received packets) */
    volatile uint32_t rx_tail;          /* Guest reads */
    uint32_t num_slots;
    uint32_t mode;
    volatile uint32_t doorbell_count;
    uint32_t reserved[4];
    struct virtio_net_pkt_slot tx_slots[VIRTIO_NET_PKT_SLOTS];
    struct virtio_net_pkt_slot rx_slots[VIRTIO_NET_PKT_SLOTS];
} __attribute__((packed));

/* ============================================================================
 * Globals
 * ============================================================================ */

static volatile int running = 1;
static struct virtio_blk_shm *blk_shm = NULL;
static struct virtio_console_shm *con_shm = NULL;
static struct virtio_net_shm *net_shm[VIRTIO_NUM_NET] = {NULL, NULL, NULL};
static int net_tap_fd[VIRTIO_NUM_NET] = {-1, -1, -1};
static const unsigned long net_bases[VIRTIO_NUM_NET] = {
    VIRTIO_NET0_BASE, VIRTIO_NET1_BASE, VIRTIO_NET2_BASE
};

/* Suppress warn_unused_result for non-critical writes */
static inline void write_all(int fd, const void *buf, size_t count)
{
    ssize_t ret = write(fd, buf, count);
    (void)ret;
}

static void signal_handler(int sig)
{
    (void)sig;
    running = 0;
}

/* ============================================================================
 * Physical Memory Mapping via /dev/mem
 * ============================================================================ */

static void *map_phys(unsigned long phys_addr, unsigned long size)
{
    int fd;
    void *base;

    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("[Frontend] open /dev/mem");
        return NULL;
    }

    base = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, phys_addr);
    close(fd);

    if (base == MAP_FAILED) {
        fprintf(stderr, "[Frontend] mmap 0x%lx failed: %s\n",
                phys_addr, strerror(errno));
        return NULL;
    }
    return base;
}

/* ============================================================================
 * Virtio-Blk Frontend: Shared-Memory <-> NBD Bridge
 *
 * We act as an NBD server for the kernel NBD client.  The kernel sends
 * NBD requests over a socketpair; we translate them into shared-memory
 * ring operations and poll for completion.
 * ============================================================================ */

/*
 * Submit a block request through shared memory and wait for response.
 * Returns 0 on success, -1 on error.
 */
static int blk_do_request(uint32_t type, uint64_t sector,
                          uint8_t *buf, uint32_t len)
{
    uint32_t head, idx;
    struct virtio_blk_req *req;
    int timeout;

    __sync_synchronize();
    head = blk_shm->req_head;
    idx = head % blk_shm->num_slots;
    req = &blk_shm->slots[idx];

    /* Fill request */
    req->type = type;
    req->sector = sector;
    req->len = len;
    req->status = 0xFF; /* sentinel */
    if (type == VIRTIO_BLK_T_OUT && buf && len <= sizeof(req->data))
        memcpy(req->data, buf, len);

    __sync_synchronize();
    blk_shm->req_head = (head + 1) % blk_shm->num_slots;
    blk_shm->doorbell_count++;
    __sync_synchronize();

    /* Poll for backend completion */
    timeout = 500000; /* 500ms at 1us intervals */
    while (timeout-- > 0) {
        __sync_synchronize();
        if (req->status != 0xFF)
            break;
        usleep(1);
    }

    if (req->status == 0xFF) {
        fprintf(stderr, "[Frontend] Blk request timeout (type=%u sector=%lu)\n",
                type, (unsigned long)sector);
        return -1;
    }

    if (req->status != VIRTIO_BLK_S_OK) {
        fprintf(stderr, "[Frontend] Blk request error %u (type=%u sector=%lu)\n",
                req->status, type, (unsigned long)sector);
        return -1;
    }

    /* Copy read data back */
    if (type == VIRTIO_BLK_T_IN && buf && len <= sizeof(req->data))
        memcpy(buf, req->data, len);

    return 0;
}

/* NBD protocol constants */
#define NBD_REQUEST_MAGIC   0x25609513
#define NBD_REPLY_MAGIC     0x67446698

struct nbd_request_pkt {
    uint32_t magic;
    uint32_t type;
    char handle[8];
    uint64_t from;
    uint32_t len;
} __attribute__((packed));

struct nbd_reply_pkt {
    uint32_t magic;
    uint32_t error;
    char handle[8];
} __attribute__((packed));

/*
 * NBD server thread: reads requests from the kernel NBD client socket,
 * translates to shared-memory block operations, sends replies.
 */
static void *nbd_server_thread(void *arg)
{
    int sock = *(int *)arg;
    struct nbd_request_pkt nbd_req;
    struct nbd_reply_pkt nbd_reply;
    uint8_t *data_buf;
    ssize_t n;

    data_buf = malloc(128 * 1024);
    if (!data_buf) {
        perror("[Frontend] malloc");
        return NULL;
    }

    printf("[Frontend] NBD server thread started\n");

    while (running) {
        /* Read NBD request header */
        n = read(sock, &nbd_req, sizeof(nbd_req));
        if (n <= 0) {
            if (n == 0 || !running)
                break;
            perror("[Frontend] NBD read");
            break;
        }
        if (n != sizeof(nbd_req)) {
            fprintf(stderr, "[Frontend] NBD short read: %zd\n", n);
            break;
        }

        if (ntohl(nbd_req.magic) != NBD_REQUEST_MAGIC) {
            fprintf(stderr, "[Frontend] NBD bad magic: 0x%x\n",
                    ntohl(nbd_req.magic));
            break;
        }

        uint32_t type = ntohl(nbd_req.type);
        uint64_t from = __builtin_bswap64(nbd_req.from);
        uint32_t len = ntohl(nbd_req.len);
        uint64_t sector = from / VIRTIO_BLK_SECTOR_SIZE;
        int err = 0;

        /* Prepare reply */
        nbd_reply.magic = htonl(NBD_REPLY_MAGIC);
        memcpy(nbd_reply.handle, nbd_req.handle, 8);
        nbd_reply.error = 0;

        switch (type) {
        case NBD_CMD_READ:
            /* Process in chunks of 4096 bytes (max per slot) */
            {
                uint32_t off = 0;
                while (off < len) {
                    uint32_t chunk = len - off;
                    if (chunk > 4096) chunk = 4096;
                    uint64_t s = sector + (off / VIRTIO_BLK_SECTOR_SIZE);
                    if (blk_do_request(VIRTIO_BLK_T_IN, s, data_buf + off, chunk) < 0) {
                        err = 1;
                        break;
                    }
                    off += chunk;
                }
            }
            nbd_reply.error = htonl(err ? 5 : 0); /* EIO */
            write_all(sock, &nbd_reply, sizeof(nbd_reply));
            if (!err)
                write_all(sock, data_buf, len);
            break;

        case NBD_CMD_WRITE:
            /* Read data from socket first */
            {
                uint32_t got = 0;
                while (got < len) {
                    n = read(sock, data_buf + got, len - got);
                    if (n <= 0) { err = 1; break; }
                    got += n;
                }
            }
            if (!err) {
                uint32_t off = 0;
                while (off < len) {
                    uint32_t chunk = len - off;
                    if (chunk > 4096) chunk = 4096;
                    uint64_t s = sector + (off / VIRTIO_BLK_SECTOR_SIZE);
                    if (blk_do_request(VIRTIO_BLK_T_OUT, s, data_buf + off, chunk) < 0) {
                        err = 1;
                        break;
                    }
                    off += chunk;
                }
            }
            nbd_reply.error = htonl(err ? 5 : 0);
            write_all(sock, &nbd_reply, sizeof(nbd_reply));
            break;

        case NBD_CMD_FLUSH:
            blk_do_request(VIRTIO_BLK_T_FLUSH, 0, NULL, 0);
            write_all(sock, &nbd_reply, sizeof(nbd_reply));
            break;

        case NBD_CMD_DISC:
            printf("[Frontend] NBD disconnect\n");
            write_all(sock, &nbd_reply, sizeof(nbd_reply));
            goto done;

        default:
            fprintf(stderr, "[Frontend] NBD unknown type %u\n", type);
            nbd_reply.error = htonl(22); /* EINVAL */
            write_all(sock, &nbd_reply, sizeof(nbd_reply));
            break;
        }
    }

done:
    free(data_buf);
    printf("[Frontend] NBD server thread exiting\n");
    return NULL;
}

/*
 * Set up the NBD device (/dev/nbd0):
 *   1. Create a socketpair
 *   2. Configure /dev/nbd0 with one end
 *   3. Start NBD server thread on the other end
 *   4. Tell kernel to start processing (NBD_DO_IT blocks)
 */
static int nbd_fd = -1;
static int nbd_sock[2] = {-1, -1};
static pthread_t nbd_thread;

static int setup_nbd_device(void)
{
    uint64_t disk_bytes;

    if (!blk_shm || blk_shm->magic != VIRTIO_BLK_MAGIC) {
        fprintf(stderr, "[Frontend] Blk shared memory not ready\n");
        return -1;
    }

    /* Wait for backend ready */
    {
        int tries = 50;
        while (tries-- > 0 && !blk_shm->backend_ready) {
            usleep(100000);
            __sync_synchronize();
        }
        if (!blk_shm->backend_ready) {
            fprintf(stderr, "[Frontend] Blk backend not ready\n");
            return -1;
        }
    }

    disk_bytes = blk_shm->capacity_sectors * VIRTIO_BLK_SECTOR_SIZE;
    printf("[Frontend] Blk: capacity %lu sectors (%lu MB)\n",
           (unsigned long)blk_shm->capacity_sectors,
           (unsigned long)(disk_bytes / (1024 * 1024)));

    /* Signal frontend connected */
    blk_shm->frontend_ready = 1;
    blk_shm->device_status = VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER |
                              VIRTIO_STATUS_DRIVER_OK;
    __sync_synchronize();

    /* Create socketpair for NBD communication */
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, nbd_sock) < 0) {
        perror("[Frontend] socketpair");
        return -1;
    }

    /* Open NBD device */
    nbd_fd = open("/dev/nbd0", O_RDWR);
    if (nbd_fd < 0) {
        perror("[Frontend] open /dev/nbd0");
        close(nbd_sock[0]);
        close(nbd_sock[1]);
        return -1;
    }

    /* Configure NBD device */
    if (ioctl(nbd_fd, NBD_SET_SIZE, (unsigned long)disk_bytes) < 0) {
        perror("[Frontend] NBD_SET_SIZE");
        goto err;
    }
    if (ioctl(nbd_fd, NBD_CLEAR_SOCK) < 0) {
        perror("[Frontend] NBD_CLEAR_SOCK");
        goto err;
    }
    if (ioctl(nbd_fd, NBD_SET_SOCK, nbd_sock[0]) < 0) {
        perror("[Frontend] NBD_SET_SOCK");
        goto err;
    }

    /* Start NBD server thread */
    if (pthread_create(&nbd_thread, NULL, nbd_server_thread, &nbd_sock[1]) != 0) {
        perror("[Frontend] pthread_create");
        goto err;
    }

    printf("[Frontend] NBD device /dev/nbd0 configured (%lu MB)\n",
           (unsigned long)(disk_bytes / (1024 * 1024)));

    /* NBD_DO_IT blocks until disconnect - run in a forked process */
    {
        pid_t pid = fork();
        if (pid == 0) {
            /* Child: this blocks until NBD is disconnected */
            ioctl(nbd_fd, NBD_DO_IT);
            ioctl(nbd_fd, NBD_CLEAR_QUE);
            ioctl(nbd_fd, NBD_CLEAR_SOCK);
            _exit(0);
        } else if (pid < 0) {
            perror("[Frontend] fork");
            goto err;
        }
        /* Parent continues */
    }

    printf("[Frontend] /dev/nbd0 active as Virtio-Blk frontend\n");
    return 0;

err:
    close(nbd_fd);
    close(nbd_sock[0]);
    close(nbd_sock[1]);
    nbd_fd = -1;
    return -1;
}

/* ============================================================================
 * Virtio-Console Frontend: Shared-Memory <-> PTY Bridge
 *
 * Creates a PTY pair. The slave end becomes /dev/hvc0 (via symlink).
 * We bridge master-side reads/writes to the shared memory ring buffers.
 * ============================================================================ */

static int pty_master = -1;

static int setup_console_device(void)
{
    char slave_name[256];

    if (!con_shm || con_shm->magic != VIRTIO_CONSOLE_MAGIC) {
        fprintf(stderr, "[Frontend] Console shared memory not ready\n");
        return -1;
    }

    /* Wait for backend ready */
    {
        int tries = 50;
        while (tries-- > 0 && !con_shm->backend_ready) {
            usleep(100000);
            __sync_synchronize();
        }
        if (!con_shm->backend_ready) {
            fprintf(stderr, "[Frontend] Console backend not ready\n");
            return -1;
        }
    }

    /* Signal frontend connected */
    con_shm->frontend_ready = 1;
    con_shm->device_status = VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER |
                              VIRTIO_STATUS_DRIVER_OK;
    __sync_synchronize();

    /* Create PTY pair */
    pty_master = posix_openpt(O_RDWR | O_NOCTTY);
    if (pty_master < 0) {
        perror("[Frontend] posix_openpt");
        return -1;
    }
    if (grantpt(pty_master) < 0 || unlockpt(pty_master) < 0) {
        perror("[Frontend] grantpt/unlockpt");
        close(pty_master);
        pty_master = -1;
        return -1;
    }
    if (ptsname_r(pty_master, slave_name, sizeof(slave_name)) != 0) {
        perror("[Frontend] ptsname_r");
        close(pty_master);
        pty_master = -1;
        return -1;
    }

    /* Create /dev/hvc0 symlink */
    unlink("/dev/hvc0");
    if (symlink(slave_name, "/dev/hvc0") < 0) {
        /* If symlink fails, try mknod approach */
        fprintf(stderr, "[Frontend] symlink /dev/hvc0 -> %s failed: %s\n",
                slave_name, strerror(errno));
        fprintf(stderr, "[Frontend] Console available at %s instead\n", slave_name);
    } else {
        printf("[Frontend] /dev/hvc0 -> %s\n", slave_name);
    }

    /* Set non-blocking on master */
    int flags = fcntl(pty_master, F_GETFL, 0);
    fcntl(pty_master, F_SETFL, flags | O_NONBLOCK);

    printf("[Frontend] Virtio-Console frontend ready (PTY: %s)\n", slave_name);
    return 0;
}

/*
 * Process console: bridge PTY master <-> shared memory ring buffers.
 * Called from the main poll loop.
 */
static void console_process(void)
{
    char buf[256];
    ssize_t n;
    uint32_t head, tail, buf_size;

    if (pty_master < 0 || !con_shm)
        return;

    buf_size = con_shm->buf_size;

    /* PTY master -> shared memory TX (Guest -> Backend) */
    n = read(pty_master, buf, sizeof(buf));
    if (n > 0) {
        __sync_synchronize();
        head = con_shm->tx_head;
        for (ssize_t i = 0; i < n; i++) {
            uint32_t next_head = (head + 1) % buf_size;
            if (next_head == con_shm->tx_tail)
                break;  /* Ring full */
            con_shm->tx_buf[head] = buf[i];
            head = next_head;
        }
        con_shm->tx_head = head;
        con_shm->doorbell_count++;
        __sync_synchronize();
    }

    /* Shared memory RX (Backend -> Guest) -> PTY master */
    __sync_synchronize();
    head = con_shm->rx_head;
    tail = con_shm->rx_tail;
    while (tail != head) {
        char c = con_shm->rx_buf[tail % buf_size];
        write_all(pty_master, &c, 1);
        tail = (tail + 1) % buf_size;
    }
    if (con_shm->rx_tail != tail) {
        con_shm->rx_tail = tail;
        __sync_synchronize();
    }
}

/* ============================================================================
 * Virtio-Net Frontend: Shared-Memory <-> TUN/TAP Bridge
 *
 * Creates TAP devices (tap0/tap1/tap2) and bridges them to the
 * shared memory net rings.  The backend (System Partition) has the
 * other half of the shared memory mapping with its own TAP device.
 *
 * Guest TAP → tx_slots (guest writes) → backend reads → backend TAP
 * Backend TAP → rx_slots (backend writes) → guest reads → guest TAP
 * ============================================================================ */

static int open_tap(const char *dev_name)
{
    struct ifreq ifr;
    int fd;

    /* Ensure /dev/net/tun exists */
    if (access("/dev/net/tun", F_OK) != 0) {
        mkdir("/dev/net", 0755);
        if (mknod("/dev/net/tun", S_IFCHR | 0666, makedev(10, 200)) < 0 &&
            errno != EEXIST) {
            perror("[Frontend] mknod /dev/net/tun");
            return -1;
        }
    }

    fd = open("/dev/net/tun", O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        perror("[Frontend] open /dev/net/tun");
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    if (dev_name)
        strncpy(ifr.ifr_name, dev_name, IFNAMSIZ - 1);

    if (ioctl(fd, TUNSETIFF, &ifr) < 0) {
        perror("[Frontend] TUNSETIFF");
        close(fd);
        return -1;
    }

    printf("[Frontend] TAP device %s opened (fd=%d)\n", ifr.ifr_name, fd);
    return fd;
}

static int setup_net_devices(void)
{
    int count = 0;

    for (int i = 0; i < VIRTIO_NUM_NET; i++) {
        if (!net_shm[i] || net_shm[i]->magic != VIRTIO_NET_MAGIC)
            continue;

        /* Wait for backend ready */
        int tries = 50;
        while (tries-- > 0 && !net_shm[i]->backend_ready) {
            usleep(100000);
            __sync_synchronize();
        }
        if (!net_shm[i]->backend_ready) {
            fprintf(stderr, "[Frontend] Net%d backend not ready\n", i);
            continue;
        }

        char tap_name[16];
        snprintf(tap_name, sizeof(tap_name), "tap%d", i);
        net_tap_fd[i] = open_tap(tap_name);
        if (net_tap_fd[i] < 0) {
            fprintf(stderr, "[Frontend] Net%d: failed to open TAP\n", i);
            continue;
        }

        /* Signal frontend connected */
        net_shm[i]->frontend_ready = 1;
        net_shm[i]->device_status = VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER |
                                     VIRTIO_STATUS_DRIVER_OK;
        __sync_synchronize();

        printf("[Frontend] Virtio-Net%d frontend ready (TAP: %s)\n", i, tap_name);
        count++;
    }

    return count;
}

/*
 * Process net: bridge TAP <-> shared memory packet rings.
 * Called from the main poll loop.
 */
static void net_process(int idx)
{
    struct virtio_net_shm *net = net_shm[idx];
    int tap_fd = net_tap_fd[idx];
    uint8_t pkt_buf[VIRTIO_NET_PKT_MAX_SIZE];

    if (!net || tap_fd < 0)
        return;

    /* TAP -> shared memory TX (Guest sends to Backend) */
    ssize_t n = read(tap_fd, pkt_buf, sizeof(pkt_buf));
    if (n > 0) {
        __sync_synchronize();
        uint32_t head = net->tx_head;
        uint32_t next = (head + 1) % net->num_slots;
        if (next != net->tx_tail) {
            struct virtio_net_pkt_slot *slot = &net->tx_slots[head];
            memcpy(slot->data, pkt_buf, n);
            slot->len = (uint32_t)n;
            __sync_synchronize();
            net->tx_head = next;
            net->doorbell_count++;
            __sync_synchronize();
        }
    }

    /* Shared memory RX (Backend sends to Guest) -> TAP */
    __sync_synchronize();
    uint32_t rx_head = net->rx_head;
    uint32_t rx_tail = net->rx_tail;
    while (rx_tail != rx_head) {
        uint32_t idx2 = rx_tail % net->num_slots;
        struct virtio_net_pkt_slot *slot = &net->rx_slots[idx2];
        if (slot->len > 0 && slot->len <= VIRTIO_NET_PKT_MAX_SIZE) {
            write_all(tap_fd, slot->data, slot->len);
            slot->len = 0;
        }
        rx_tail = (rx_tail + 1) % net->num_slots;
    }
    if (net->rx_tail != rx_tail) {
        net->rx_tail = rx_tail;
        __sync_synchronize();
    }
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(int argc, char *argv[])
{
    int enable_blk = 0, enable_console = 0, enable_net = 0;
    int i;

    /* Parse args */
    if (argc <= 1) {
        /* Default: all devices */
        enable_blk = enable_console = enable_net = 1;
    } else {
        for (i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--blk") == 0)
                enable_blk = 1;
            else if (strcmp(argv[i], "--console") == 0)
                enable_console = 1;
            else if (strcmp(argv[i], "--net") == 0)
                enable_net = 1;
            else if (strcmp(argv[i], "--all") == 0)
                enable_blk = enable_console = enable_net = 1;
            else {
                fprintf(stderr, "Usage: %s [--blk] [--console] [--net] [--all]\n", argv[0]);
                return 1;
            }
        }
    }

    printf("=== PRTOS Virtio Frontend Daemon ===\n");
    printf("Guest Partition - Virtio Device Frontend\n");

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Map shared memory regions */
    if (enable_blk) {
        blk_shm = (struct virtio_blk_shm *)map_phys(VIRTIO_BLK_BASE, VIRTIO_BLK_SIZE);
        if (!blk_shm) {
            fprintf(stderr, "[Frontend] Failed to map Virtio-Blk region\n");
            enable_blk = 0;
        }
    }
    if (enable_console) {
        con_shm = (struct virtio_console_shm *)map_phys(VIRTIO_CON_BASE, VIRTIO_CON_SIZE);
        if (!con_shm) {
            fprintf(stderr, "[Frontend] Failed to map Virtio-Console region\n");
            enable_console = 0;
        }
    }
    if (enable_net) {
        for (i = 0; i < VIRTIO_NUM_NET; i++) {
            net_shm[i] = (struct virtio_net_shm *)map_phys(net_bases[i], VIRTIO_NET_SIZE);
            if (!net_shm[i])
                fprintf(stderr, "[Frontend] Failed to map Virtio-Net%d region\n", i);
        }
    }

    if (!enable_blk && !enable_console && !enable_net) {
        fprintf(stderr, "[Frontend] No devices could be mapped, exiting\n");
        return 1;
    }

    /* Wait for backend to initialize shared memory (polls for magic values) */
    {
        int waited = 0;
        int blk_ready = !enable_blk;
        int con_ready = !enable_console;
        int net_ready = !enable_net;

        printf("[Frontend] Waiting for backend to initialize shared memory...\n");
        while (running && (!blk_ready || !con_ready || !net_ready)) {
            __sync_synchronize();
            if (enable_blk && !blk_ready && blk_shm->magic == VIRTIO_BLK_MAGIC)
                blk_ready = 1;
            if (enable_console && !con_ready && con_shm->magic == VIRTIO_CONSOLE_MAGIC)
                con_ready = 1;
            if (enable_net && !net_ready) {
                /* Ready when at least one net region has magic */
                for (i = 0; i < VIRTIO_NUM_NET; i++) {
                    if (net_shm[i] && net_shm[i]->magic == VIRTIO_NET_MAGIC) {
                        net_ready = 1;
                        break;
                    }
                }
            }
            if (!blk_ready || !con_ready || !net_ready) {
                if (waited % 30 == 0)
                    printf("[Frontend] Still waiting... (blk=%s con=%s net=%s) %ds\n",
                           blk_ready ? "ready" : "waiting",
                           con_ready ? "ready" : "waiting",
                           net_ready ? "ready" : "waiting", waited);
                sleep(1);
                waited++;
                if (waited > 300) {
                    fprintf(stderr, "[Frontend] Timeout waiting for backend (300s)\n");
                    break;
                }
            }
        }
        if (!running)
            return 0;
        printf("[Frontend] Backend shared memory ready (waited %ds)\n", waited);
    }

    /* Setup devices */
    if (enable_blk) {
        if (setup_nbd_device() < 0) {
            fprintf(stderr, "[Frontend] Blk setup failed (is CONFIG_BLK_DEV_NBD enabled?)\n");
            enable_blk = 0;
        }
    }
    if (enable_console) {
        if (setup_console_device() < 0) {
            fprintf(stderr, "[Frontend] Console setup failed\n");
            enable_console = 0;
        }
    }
    if (enable_net) {
        int net_count = setup_net_devices();
        if (net_count == 0) {
            fprintf(stderr, "[Frontend] Net setup failed (is CONFIG_TUN enabled?)\n");
            enable_net = 0;
        } else {
            printf("[Frontend] %d net devices configured\n", net_count);
        }
    }

    if (!enable_blk && !enable_console && !enable_net) {
        fprintf(stderr, "[Frontend] No devices enabled, exiting\n");
        return 1;
    }

    printf("[Frontend] Running (Ctrl+C to stop)...\n");

    /* Main poll loop */
    while (running) {
        if (enable_console)
            console_process();
        if (enable_net) {
            for (i = 0; i < VIRTIO_NUM_NET; i++)
                net_process(i);
        }
        usleep(1000);   /* 1ms poll interval */
    }

    /* Cleanup */
    printf("[Frontend] Shutting down...\n");
    if (nbd_fd >= 0) {
        ioctl(nbd_fd, NBD_DISCONNECT);
        ioctl(nbd_fd, NBD_CLEAR_SOCK);
        close(nbd_fd);
    }
    if (nbd_sock[0] >= 0) close(nbd_sock[0]);
    if (nbd_sock[1] >= 0) close(nbd_sock[1]);
    if (pty_master >= 0) close(pty_master);
    for (i = 0; i < VIRTIO_NUM_NET; i++) {
        if (net_tap_fd[i] >= 0) close(net_tap_fd[i]);
    }
    unlink("/dev/hvc0");

    return 0;
}
