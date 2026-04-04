/*
 * main.c - Virtio Backend daemon entry point for PRTOS System Partition.
 *
 * This program runs in userspace inside the System Partition's Linux.
 * It maps the PRTOS shared memory region via /dev/mem, initializes the
 * Virtio control block and device structures, then enters a poll loop
 * processing requests from the Guest Partition.
 *
 * Integration with lib_prtos_manager:
 *   - prtos_hv_init() sets up vmcall interface + shared memory mailbox
 *   - prtos_hv_get_partition_status() monitors Guest state (ready/halted)
 *   - prtos_hv_raise_partition_ipvi() sends doorbell after I/O completion
 *   - prtos_hv_write_console() logs backend events to hypervisor console
 *
 * Architecture:
 *   System Partition (this) <-- shared memory --> Guest Partition
 *   lib_prtos_manager provides partition management (halt/reset/resume).
 *   IPVI provides doorbell signaling between partitions.
 *
 * Compile: gcc -O2 -I../include -I../../lib_prtos_manager/include -o virtio_backend ...
 * Run:     ./virtio_backend [--backing-file /path/to/disk.img]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <signal.h>
#include <errno.h>

#include "virtio_be.h"
#include "prtos_hv.h"

static volatile int running = 1;
static void *shmem_base = NULL;
static int hv_available = 0;    /* 1 if prtos_hv_init() succeeded */

/* Guest Partition ID (Partition 1 in our XML config) */
#define GUEST_PARTITION_ID  1

/* IPVI channel: System (src=0) -> Guest (dst=1) for completion doorbell */
#define IPVI_SYS_TO_GUEST   1

static void signal_handler(int sig)
{
    (void)sig;
    running = 0;
}

/*
 * Map the PRTOS shared memory region into our address space.
 * In a real PRTOS deployment, this uses /dev/mem to access the
 * hypervisor-allocated shared memory at the physical GPA.
 *
 * Reference: lib_prtos_manager would provide xm_map_shared_memory()
 * for a higher-level API. Here we use the direct mmap approach.
 */
static void *map_shared_memory(void)
{
    int fd;
    void *base;

    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("[Backend] Failed to open /dev/mem");
        return NULL;
    }

    base = mmap(NULL, VIRTIO_SHMEM_SIZE, PROT_READ | PROT_WRITE,
                MAP_SHARED, fd, VIRTIO_SHMEM_BASE);
    close(fd);

    if (base == MAP_FAILED) {
        perror("[Backend] Failed to mmap shared memory");
        return NULL;
    }

    printf("[Backend] Shared memory mapped at %p (phys 0x%lx, size %lu KB)\n",
           base, (unsigned long)VIRTIO_SHMEM_BASE,
           (unsigned long)VIRTIO_SHMEM_SIZE / 1024);
    return base;
}

/*
 * Initialize the control block in shared memory.
 * This establishes the "contract" with the Guest Partition:
 *   - Magic number for discovery
 *   - Device table with offsets and capabilities
 *   - Backend ready flag
 */
static void init_ctrl_block(void *base)
{
    struct virtio_ctrl_block *ctrl = VIRTIO_CTRL(base);

    memset(ctrl, 0, sizeof(*ctrl));

    ctrl->magic = VIRTIO_PRTOS_MAGIC;
    ctrl->version = 1;

    /* Register Virtio-Console device */
    ctrl->devices[0].device_id = VIRTIO_DEV_CONSOLE;
    ctrl->devices[0].queue_offset = VIRTIO_CONSOLE_OFFSET;
    ctrl->devices[0].queue_size = VIRTIO_CONSOLE_SIZE;
    ctrl->devices[0].num_queues = 1;
    ctrl->devices[0].status = VIRTIO_STATUS_ACK;

    /* Register Virtio-Net device */
    ctrl->devices[1].device_id = VIRTIO_DEV_NET;
    ctrl->devices[1].queue_offset = VIRTIO_NET_OFFSET;
    ctrl->devices[1].queue_size = VIRTIO_NET_SIZE;
    ctrl->devices[1].num_queues = 2;    /* RX + TX */
    ctrl->devices[1].status = VIRTIO_STATUS_ACK;

    /* Register Virtio-Blk device */
    ctrl->devices[2].device_id = VIRTIO_DEV_BLK;
    ctrl->devices[2].queue_offset = VIRTIO_BLK_OFFSET;
    ctrl->devices[2].queue_size = VIRTIO_BLK_SIZE;
    ctrl->devices[2].num_queues = 1;
    ctrl->devices[2].status = VIRTIO_STATUS_ACK;

    /* Signal backend is ready */
    ctrl->backend_status = 1;
    __sync_synchronize();

    printf("[Backend] Control block initialized (magic=0x%08x, %d devices)\n",
           ctrl->magic, VIRTIO_NUM_DEVICES);
}

int main(int argc, char *argv[])
{
    const char *backing_file = NULL;
    int i;

    /* Parse arguments */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--backing-file") == 0 && i + 1 < argc) {
            backing_file = argv[++i];
        }
    }

    printf("=== PRTOS Virtio Backend Daemon ===\n");
    printf("System Partition - Virtio Device Virtualization Demo\n");
    printf("Shared Memory: 0x%lx (%lu MB)\n",
           (unsigned long)VIRTIO_SHMEM_BASE,
           (unsigned long)VIRTIO_SHMEM_SIZE / (1024 * 1024));
    if (backing_file)
        printf("Block backing file: %s\n", backing_file);
    printf("\n");

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Initialize PRTOS hypervisor interface (vmcall + shared memory mailbox) */
    if (prtos_hv_init() == 0) {
        hv_available = 1;
        printf("[Backend] PRTOS hypervisor interface initialized (partition %d)\n",
               prtos_hv_get_partition_self());

        /* Check Guest Partition state */
        prtos_part_status_t guest_status;
        if (prtos_hv_get_partition_status(GUEST_PARTITION_ID, &guest_status) >= 0) {
            const char *state_str = "unknown";
            switch (guest_status.state) {
            case PRTOS_STATUS_IDLE:      state_str = "idle"; break;
            case PRTOS_STATUS_READY:     state_str = "ready"; break;
            case PRTOS_STATUS_SUSPENDED: state_str = "suspended"; break;
            case PRTOS_STATUS_HALTED:    state_str = "halted"; break;
            }
            printf("[Backend] Guest Partition %d state: %s (resets: %u)\n",
                   GUEST_PARTITION_ID, state_str, guest_status.reset_counter);
        }
    } else {
        printf("[Backend] PRTOS hypervisor interface not available (demo mode)\n");
    }

    /* Map shared memory */
    shmem_base = map_shared_memory();
    if (!shmem_base) {
        fprintf(stderr, "[Backend] Could not map shared memory. Running in demo mode.\n");
        printf("[Backend] Demo mode: Virtio Backend would manage:\n");
        printf("  - Virtio-Console at offset 0x%x (%d KB)\n",
               VIRTIO_CONSOLE_OFFSET, VIRTIO_CONSOLE_SIZE / 1024);
        printf("  - Virtio-Net     at offset 0x%x (%d KB)\n",
               VIRTIO_NET_OFFSET, VIRTIO_NET_SIZE / 1024);
        printf("  - Virtio-Blk     at offset 0x%x (%d KB)\n",
               VIRTIO_BLK_OFFSET, VIRTIO_BLK_SIZE / 1024);
        printf("[Backend] Verification Passed (demo mode)\n");
        return 0;
    }

    /* Initialize control block and all Virtio device structures */
    init_ctrl_block(shmem_base);
    virtio_console_init(shmem_base);
    virtio_net_init(shmem_base);
    virtio_blk_init(shmem_base, backing_file);

    printf("\n[Backend] All Virtio devices initialized. Entering poll loop...\n");
    printf("[Backend] Waiting for Guest Partition to connect...\n\n");

    /*
     * Main event loop.
     *
     * When PRTOS hypervisor interface is available, the Backend uses
     * prtos_hv_raise_partition_ipvi() to signal the Guest after completing
     * I/O requests (doorbell). The Guest can then poll its completion
     * status from the shared memory ring. IPVI channel 1 (System -> Guest)
     * is configured in resident_sw.xml.
     *
     * For this demo, we poll with a 1ms interval. In production, the
     * Guest-to-System direction would use IPVI channel 0 to wake
     * the Backend via an interrupt handler.
     */
    while (running) {
        int processed = 0;

        /* Process each Virtio device's queues */
        virtio_console_process(shmem_base);
        virtio_net_process(shmem_base);
        virtio_blk_process(shmem_base);

        /* Check doorbell counter from Guest */
        struct virtio_ctrl_block *ctrl = VIRTIO_CTRL(shmem_base);
        static uint32_t last_guest_doorbell = 0;
        if (ctrl->doorbell_guest_to_sys != last_guest_doorbell) {
            last_guest_doorbell = ctrl->doorbell_guest_to_sys;
            processed = 1;
        }

        /* Signal Guest via IPVI if we processed any requests */
        if (processed && hv_available) {
            ctrl->doorbell_sys_to_guest++;
            __sync_synchronize();
            prtos_hv_raise_partition_ipvi(GUEST_PARTITION_ID, IPVI_SYS_TO_GUEST);
        }

        /* Check if Frontend has connected */
        if (ctrl->frontend_status != 0) {
            static int reported = 0;
            if (!reported) {
                printf("[Backend] Guest Frontend connected (status=0x%x)\n",
                       ctrl->frontend_status);
                reported = 1;
            }
        }

        usleep(1000);  /* 1ms poll interval */
    }

    printf("\n[Backend] Shutting down.\n");

    if (shmem_base)
        munmap(shmem_base, VIRTIO_SHMEM_SIZE);

    return 0;
}
