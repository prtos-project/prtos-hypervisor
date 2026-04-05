/*
 * virtio_net.c - Virtio Network backend for PRTOS System Partition.
 *
 * Supports 3 instances with different backend modes:
 *   Instance 0 (bridge): TAP device (/dev/net/tun)
 *   Instance 1 (NAT):    Loopback (QEMU user networking accessed via System's eth0)
 *   Instance 2 (p2p):    Loopback (socket-based, extensible)
 *
 * Each instance operates on its own 1MB shared memory region.
 *
 * Data flow:
 *   Guest virtio-net driver -> tx_slots in shared memory
 *   -> Backend reads packet -> Backend forwards via TAP/socket/loopback
 *   -> Response written to rx_slots -> Guest receives
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/if_tun.h>

#include "virtio_be.h"

static const char *mode_name(int mode)
{
    switch (mode) {
    case VIRTIO_NET_MODE_BRIDGE:   return "bridge";
    case VIRTIO_NET_MODE_NAT:      return "NAT";
    case VIRTIO_NET_MODE_P2P:      return "p2p";
    case VIRTIO_NET_MODE_LOOPBACK: return "loopback";
    default:                       return "unknown";
    }
}

/*
 * Open a TAP device for bridge mode networking.
 * Returns fd on success, -1 on failure.
 */
static int open_tap_device(const char *dev_name)
{
    struct ifreq ifr;
    int fd;

    fd = open("/dev/net/tun", O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "[Backend] Cannot open /dev/net/tun: %s\n",
                strerror(errno));
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    if (dev_name)
        strncpy(ifr.ifr_name, dev_name, IFNAMSIZ - 1);

    if (ioctl(fd, TUNSETIFF, &ifr) < 0) {
        fprintf(stderr, "[Backend] TUNSETIFF failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    printf("[Backend] TAP device %s opened (fd=%d)\n", ifr.ifr_name, fd);
    return fd;
}

int virtio_net_init(struct virtio_net_instance *inst)
{
    struct virtio_net_shm *net = inst->shm;

    if (!net)
        return -1;

    memset(net, 0, sizeof(*net));
    net->magic = VIRTIO_NET_MAGIC;
    net->version = 1;
    net->num_slots = VIRTIO_NET_PKT_SLOTS;
    net->mode = inst->mode;
    net->device_status = VIRTIO_STATUS_ACK;
    net->backend_ready = 1;
    __sync_synchronize();

    /* Open backend device based on mode */
    switch (inst->mode) {
    case VIRTIO_NET_MODE_BRIDGE:
        inst->backend_fd = open_tap_device("tap0");
        if (inst->backend_fd < 0) {
            printf("[Backend] Net%d: TAP unavailable, falling back to loopback\n",
                   inst->id);
            inst->mode = VIRTIO_NET_MODE_LOOPBACK;
            net->mode = VIRTIO_NET_MODE_LOOPBACK;
        }
        break;
    case VIRTIO_NET_MODE_NAT:
    case VIRTIO_NET_MODE_P2P:
        /* These modes use loopback for now; extensible to real sockets */
        inst->backend_fd = -1;
        break;
    default:
        inst->backend_fd = -1;
        break;
    }

    printf("[Backend] Virtio-Net%d initialized (mode=%s, %u slots, max %u B/pkt)\n",
           inst->id, mode_name(inst->mode),
           VIRTIO_NET_PKT_SLOTS, VIRTIO_NET_PKT_MAX_SIZE);
    return 0;
}

void virtio_net_process(struct virtio_net_instance *inst)
{
    struct virtio_net_shm *net = inst->shm;
    uint32_t head, tail;

    if (!net)
        return;

    __sync_synchronize();
    head = net->tx_head;
    tail = net->tx_tail;

    while (tail != head) {
        uint32_t idx = tail % net->num_slots;
        struct virtio_net_pkt_slot *slot = &net->tx_slots[idx];

        if (slot->len > 0 && slot->len <= VIRTIO_NET_PKT_MAX_SIZE) {
            printf("[Backend] Net%d TX: %u bytes from Guest\n",
                   inst->id, slot->len);

            if (inst->backend_fd >= 0) {
                /* Bridge mode: write to TAP device */
                ssize_t n = write(inst->backend_fd, slot->data, slot->len);
                if (n < 0)
                    fprintf(stderr, "[Backend] Net%d: TAP write error: %s\n",
                            inst->id, strerror(errno));
            }

            /* Loopback: echo packet back to Guest's RX ring */
            uint32_t rx_idx = net->rx_head % net->num_slots;
            struct virtio_net_pkt_slot *rx_slot = &net->rx_slots[rx_idx];
            memcpy(rx_slot->data, slot->data, slot->len);
            rx_slot->len = slot->len;
            __sync_synchronize();
            net->rx_head = (net->rx_head + 1) % net->num_slots;

            slot->len = 0;  /* Mark as consumed */
        }
        tail = (tail + 1) % net->num_slots;
    }

    if (net->tx_tail != tail) {
        net->tx_tail = tail;
        __sync_synchronize();
    }

    /* Bridge mode: read from TAP and deliver to Guest RX */
    if (inst->backend_fd >= 0) {
        uint8_t buf[VIRTIO_NET_PKT_MAX_SIZE];
        ssize_t n = read(inst->backend_fd, buf, sizeof(buf));
        if (n > 0) {
            uint32_t rx_idx = net->rx_head % net->num_slots;
            struct virtio_net_pkt_slot *rx_slot = &net->rx_slots[rx_idx];
            memcpy(rx_slot->data, buf, n);
            rx_slot->len = (uint32_t)n;
            __sync_synchronize();
            net->rx_head = (net->rx_head + 1) % net->num_slots;
            printf("[Backend] Net%d RX: %zd bytes from TAP\n", inst->id, n);
        }
    }
}

void virtio_net_cleanup(struct virtio_net_instance *inst)
{
    if (inst->backend_fd >= 0) {
        close(inst->backend_fd);
        inst->backend_fd = -1;
        printf("[Backend] Net%d: TAP device closed\n", inst->id);
    }
}
