/*
 * manager_if.c - lib_prtos_manager wrapper for partition lifecycle management.
 *
 * Provides high-level functions for the Virtio Backend daemon to query
 * and control the Guest partition via PRTOS hypervisor hypercalls.
 */

#include <stdio.h>
#include <stdint.h>

#include "virtio_be.h"
#include "prtos_hv.h"

#define GUEST_PARTITION_ID  1

static int mgr_initialized = 0;

int manager_init(void)
{
    if (prtos_hv_init() == 0) {
        mgr_initialized = 1;
        printf("[Backend] Manager: PRTOS interface initialized (self=%d)\n",
               prtos_hv_get_partition_self());
        return 0;
    }
    printf("[Backend] Manager: PRTOS interface not available\n");
    return -1;
}

int manager_get_guest_status(void)
{
    prtos_part_status_t status;

    if (!mgr_initialized)
        return -1;

    if (prtos_hv_get_partition_status(GUEST_PARTITION_ID, &status) >= 0)
        return (int)status.state;

    return -1;
}
