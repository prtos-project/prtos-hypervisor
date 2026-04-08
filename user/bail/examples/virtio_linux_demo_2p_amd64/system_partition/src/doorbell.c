/*
 * doorbell.c - IPVI doorbell dispatcher for PRTOS System Partition.
 *
 * Handles inter-partition virtual interrupt (IPVI) signaling between
 * the Guest and System partitions. Maps IPVI vectors to device handlers.
 *
 * IPVI Vectors (Guest -> System):
 *   0: virtio-net0 (bridge)
 *   1: virtio-net1 (NAT)
 *   2: virtio-net2 (p2p)
 *   3: virtio-blk
 *   4: virtio-console
 *
 * System -> Guest completion:
 *   5: completion doorbell (signals I/O completion to Guest)
 */

#include <stdio.h>
#include <stdint.h>

#include "virtio_be.h"
#include "prtos_hv.h"

#define GUEST_PARTITION_ID  1

static int hv_ok = 0;

void doorbell_init(void)
{
    if (prtos_hv_init() == 0) {
        hv_ok = 1;
        printf("[Backend] Doorbell: PRTOS IPVI interface ready\n");
    } else {
        printf("[Backend] Doorbell: IPVI not available (polling mode)\n");
    }
}

void doorbell_signal_guest(void)
{
    if (hv_ok) {
        prtos_hv_raise_partition_ipvi(GUEST_PARTITION_ID, IPVI_SYS_TO_GUEST);
    }
}
