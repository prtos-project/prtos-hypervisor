/*
 * virtio_net.c - Virtio Network backend logic for PRTOS System Partition.
 *
 * Implements packet exchange between Guest and Backend via shared memory:
 *   - Guest TX: Backend reads packets from tx_slots, forwards to physical NIC
 *   - Guest RX: Backend writes received packets into rx_slots
 *
 * In this demo, the Backend operates in loopback mode: packets sent by the
 * Guest are echoed back to the Guest's RX ring. In a production system,
 * the Backend would bridge packets to QEMU's physical virtio-net-pci device
 * that is passthrough to the System Partition.
 *
 * Data flow (production):
 *   Guest virtio-net driver -> tx_slots in shared memory
 *   -> Backend reads packet -> Backend sends via System's physical eth0
 *   -> Physical NIC -> QEMU tap interface -> Host network
 *
 * Data flow (demo loopback):
 *   Guest sends packet -> tx_slots -> Backend copies to rx_slots -> Guest receives
 *
 * Verification: ifconfig eth0 192.168.1.2 && ping 192.168.1.1  (from Guest)
 *   -> Backend logs packet receipt and forwards/echoes
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "virtio_be.h"

void virtio_net_init(void *shmem_base)
{
    struct virtio_net_ring *net = VIRTIO_NET(shmem_base);

    memset(net, 0, sizeof(*net));
    net->magic = VIRTIO_NET_MAGIC;
    net->num_slots = VIRTIO_NET_PKT_SLOTS;
    net->tx_head = 0;
    net->tx_tail = 0;
    net->rx_head = 0;
    net->rx_tail = 0;

    __sync_synchronize();
    printf("[Backend] Virtio-Net initialized (%u packet slots, max %u bytes/pkt)\n",
           VIRTIO_NET_PKT_SLOTS, VIRTIO_NET_PKT_MAX_SIZE);
}

void virtio_net_process(void *shmem_base)
{
    struct virtio_net_ring *net = VIRTIO_NET(shmem_base);
    uint32_t head, tail;

    __sync_synchronize();
    head = net->tx_head;
    tail = net->tx_tail;

    while (tail != head) {
        uint32_t idx = tail % net->num_slots;
        struct virtio_net_pkt_slot *slot = &net->tx_slots[idx];

        if (slot->len > 0 && slot->len <= VIRTIO_NET_PKT_MAX_SIZE) {
            printf("[Backend] Net TX: received packet (%u bytes) from Guest\n",
                   slot->len);

            /*
             * Loopback: echo the packet back to Guest's RX ring.
             * In production, this would call:
             *   send(system_eth0_fd, slot->data, slot->len, 0);
             * to forward to the physical NIC assigned to System Partition.
             */
            uint32_t rx_idx = net->rx_head % net->num_slots;
            struct virtio_net_pkt_slot *rx_slot = &net->rx_slots[rx_idx];

            memcpy(rx_slot->data, slot->data, slot->len);
            rx_slot->len = slot->len;
            __sync_synchronize();
            net->rx_head = (net->rx_head + 1) % net->num_slots;

            /*
             * In production, after forwarding, signal the Guest via IPVI:
             *   prtos_raise_partition_ipvi(GUEST_PARTITION_ID, PRTOS_VT_EXT_IPVI1);
             */

            slot->len = 0;  /* Mark as consumed */
        }
        tail = (tail + 1) % net->num_slots;
    }

    if (net->tx_tail != tail) {
        net->tx_tail = tail;
        __sync_synchronize();
    }
}
