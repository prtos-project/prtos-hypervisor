/*
 * FILE: prtos_hv.c
 *
 * PRTOS Hypervisor Interface Implementation.
 * Uses vmcall from userspace + shared memory mailbox for buffer-based hypercalls.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "prtos_hv.h"

/* Shared memory mapping for hypercall mailbox */
static void *shmem_base = NULL;
static void *mailbox_ptr = NULL;
static int partition_self = -1;

int prtos_hv_init(void)
{
    int fd;

    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        /* /dev/mem not available - running outside PRTOS or demo mode */
        return -1;
    }

    shmem_base = mmap(NULL, VIRTIO_SHMEM_SIZE, PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd, VIRTIO_SHMEM_BASE);
    close(fd);

    if (shmem_base == MAP_FAILED) {
        shmem_base = NULL;
        return -1;
    }

    mailbox_ptr = (char *)shmem_base + HC_MAILBOX_OFFSET;

    /* Get our partition ID via vmcall */
    partition_self = (int)prtos_vmcall(HC_GET_VCPUID, 0, 0, 0, 0, 0);
    /* get_vcpuid returns a composite id; extract partition id (lower 16 bits) */
    partition_self = partition_self & 0xFFFF;

    return 0;
}

int prtos_hv_get_partition_self(void)
{
    return partition_self;
}

/* Simple hypercalls (no buffer pointers needed) */

int prtos_hv_halt_partition(int partition_id)
{
    return (int)prtos_vmcall(HC_HALT_PARTITION, partition_id, 0, 0, 0, 0);
}

int prtos_hv_suspend_partition(int partition_id)
{
    return (int)prtos_vmcall(HC_SUSPEND_PARTITION, partition_id, 0, 0, 0, 0);
}

int prtos_hv_resume_partition(int partition_id)
{
    return (int)prtos_vmcall(HC_RESUME_PARTITION, partition_id, 0, 0, 0, 0);
}

int prtos_hv_reset_partition(int partition_id, int reset_mode, int status)
{
    return (int)prtos_vmcall(HC_RESET_PARTITION, partition_id, reset_mode, status, 0, 0);
}

int prtos_hv_shutdown_partition(int partition_id)
{
    return (int)prtos_vmcall(HC_SHUTDOWN_PARTITION, partition_id, 0, 0, 0, 0);
}

int prtos_hv_halt_system(void)
{
    return (int)prtos_vmcall(HC_HALT_SYSTEM, 0, 0, 0, 0, 0);
}

int prtos_hv_reset_system(int reset_mode)
{
    return (int)prtos_vmcall(HC_RESET_SYSTEM, reset_mode, 0, 0, 0, 0);
}

int prtos_hv_raise_ipvi(int ipvi_no)
{
    return (int)prtos_vmcall(HC_RAISE_IPVI, ipvi_no, 0, 0, 0, 0);
}

int prtos_hv_raise_partition_ipvi(int partition_id, int ipvi_no)
{
    return (int)prtos_vmcall(HC_RAISE_PARTITION_IPVI, partition_id, ipvi_no, 0, 0, 0);
}

int prtos_hv_set_plan(int plan_id)
{
    /* switch_sched_plan takes (new_plan_id, *current_plan_id) */
    uint32_t *current_plan = (uint32_t *)mailbox_ptr;
    *current_plan = 0;
    return (int)prtos_vmcall(HC_SWITCH_SCHED_PLAN, plan_id,
                             (long)HC_MAILBOX_GPA, 0, 0, 0);
}

/*
 * Buffer-based hypercalls.
 * These use the shared memory mailbox because the hypervisor needs to
 * write results to a buffer. The mailbox GPA is identity-mapped
 * (GPA == HPA in EPT), so the hypervisor can access it directly.
 *
 * The ctrl_object hypercall is used for status queries.
 * ctrl_object(obj_desc, cmd, arg_buffer)
 */

int prtos_hv_get_partition_status(int partition_id, prtos_part_status_t *status)
{
    status_fmd_t *fmd;
    int ret;

    if (!mailbox_ptr)
        return PRTOS_INVALID_PARAM;

    /* Clear the mailbox area */
    fmd = (status_fmd_t *)mailbox_ptr;
    memset(fmd, 0, sizeof(*fmd));

    /* ctrl_object(obj_desc, cmd, arg_buffer_gpa) */
    ret = (int)prtos_vmcall(HC_CTRL_OBJECT,
                            OBJDESC_BUILD(OBJ_CLASS_STATUS, partition_id, 0),
                            PRTOS_GET_SYSTEM_STATUS,
                            (long)HC_MAILBOX_GPA, 0, 0);

    if (ret >= 0 && status) {
        memcpy(status, &fmd->status.partition, sizeof(*status));
    }

    return ret;
}

int prtos_hv_get_system_status(prtos_sys_status_t *status)
{
    status_fmd_t *fmd;
    int ret;

    if (!mailbox_ptr)
        return PRTOS_INVALID_PARAM;

    fmd = (status_fmd_t *)mailbox_ptr;
    memset(fmd, 0, sizeof(*fmd));

    ret = (int)prtos_vmcall(HC_CTRL_OBJECT,
                            OBJDESC_BUILD(OBJ_CLASS_STATUS, PRTOS_HYPERVISOR_ID, 0),
                            PRTOS_GET_SYSTEM_STATUS,
                            (long)HC_MAILBOX_GPA, 0, 0);

    if (ret >= 0 && status) {
        memcpy(status, &fmd->status.system, sizeof(*status));
    }

    return ret;
}

int prtos_hv_get_plan_status(prtos_plan_status_t *status)
{
    status_fmd_t *fmd;
    int ret;

    if (!mailbox_ptr)
        return PRTOS_INVALID_PARAM;

    fmd = (status_fmd_t *)mailbox_ptr;
    memset(fmd, 0, sizeof(*fmd));

    ret = (int)prtos_vmcall(HC_CTRL_OBJECT,
                            OBJDESC_BUILD(OBJ_CLASS_STATUS, partition_self, 0),
                            PRTOS_GET_SCHED_PLAN_STATUS,
                            (long)HC_MAILBOX_GPA, 0, 0);

    if (ret >= 0 && status) {
        memcpy(status, &fmd->status.plan, sizeof(*status));
    }

    return ret;
}

int prtos_hv_write_console(const char *buffer, int length)
{
    /* write_object(obj_desc, buffer_gpa, size, flags) */
    /* Copy to mailbox first, then pass GPA */
    if (!mailbox_ptr || length <= 0)
        return PRTOS_INVALID_PARAM;

    if (length > 2048)
        length = 2048;

    memcpy(mailbox_ptr, buffer, length);

    return (int)prtos_vmcall(HC_WRITE_OBJECT,
                            OBJDESC_BUILD(OBJ_CLASS_CONSOLE, partition_self, 0),
                            (long)HC_MAILBOX_GPA, length, 0, 0);
}

int prtos_hv_get_num_partitions(void)
{
    prtos_part_status_t status;
    int i;

    /* Probe partition IDs until we get an error */
    for (i = 0; i < 32; i++) {
        int ret = prtos_hv_get_partition_status(i, &status);
        if (ret < 0)
            break;
    }

    return i;
}
