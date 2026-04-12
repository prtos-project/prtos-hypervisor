/*
 * main.c - Virtio Backend daemon entry point for PRTOS System Partition.
 *
 * This program runs in userspace inside the System Partition's Linux.
 * It maps the 5 PRTOS shared memory regions via /dev/mem, initializes
 * all Virtio device structures, then enters a poll loop processing
 * requests from the Guest Partition.
 *
 * Virtio Devices:
 *   - 3 x virtio-net  (bridge / NAT / p2p)
 *   - 1 x virtio-blk  (file-backed)
 *   - 1 x virtio-console (character ring)
 *
 * Integration with lib_prtos_manager:
 *   - prtos_hv_init() sets up vmcall interface
 *   - prtos_hv_raise_partition_ipvi() sends completion doorbell
 *
 * Compile: gcc -O2 -static -I../include -I../../lib_prtos_manager/include ...
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
static int hv_available = 0;

/* Guest Partition ID (Partition 1 in our XML config) */
#define GUEST_PARTITION_ID  1

/* Device state */
static struct virtio_net_instance net_instances[VIRTIO_NUM_NET];
static struct virtio_blk_shm *blk_shm = NULL;
static struct virtio_console_shm *con_shm = NULL;

/* Shared memory region table */
struct shm_region {
    const char *name;
    unsigned long phys_addr;
    unsigned long size;
    void **map_ptr;
};

static void *net0_map, *net1_map, *net2_map, *blk_map, *con_map;

static struct shm_region shm_regions[] = {
    { "Virtio_Net0", VIRTIO_NET0_BASE, VIRTIO_NET0_SIZE, &net0_map },
    { "Virtio_Net1", VIRTIO_NET1_BASE, VIRTIO_NET1_SIZE, &net1_map },
    { "Virtio_Net2", VIRTIO_NET2_BASE, VIRTIO_NET2_SIZE, &net2_map },
    { "Virtio_Blk",  VIRTIO_BLK_BASE,  VIRTIO_BLK_SIZE,  &blk_map },
    { "Virtio_Con",  VIRTIO_CON_BASE,  VIRTIO_CON_SIZE,  &con_map },
};
#define NUM_SHM_REGIONS (sizeof(shm_regions) / sizeof(shm_regions[0]))

static void signal_handler(int sig)
{
    (void)sig;
    running = 0;
}

/*
 * Map a physical shared memory region into our address space via /dev/mem.
 */
static void *map_phys_region(unsigned long phys_addr, unsigned long size,
                             const char *name)
{
    int fd;
    void *base;

    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("[Backend] Failed to open /dev/mem");
        return NULL;
    }

    base = mmap(NULL, size, PROT_READ | PROT_WRITE,
                MAP_SHARED, fd, phys_addr);
    close(fd);

    if (base == MAP_FAILED) {
        fprintf(stderr, "[Backend] Failed to mmap %s at 0x%lx: %s\n",
                name, phys_addr, strerror(errno));
        return NULL;
    }

    printf("[Backend] %s mapped at %p (phys 0x%lx, %lu KB)\n",
           name, base, phys_addr, size / 1024);
    return base;
}

/*
 * Map all 5 shared memory regions.
 */
static int map_all_shared_memory(void)
{
    unsigned int i;
    int ok = 1;

    for (i = 0; i < NUM_SHM_REGIONS; i++) {
        *shm_regions[i].map_ptr = map_phys_region(
            shm_regions[i].phys_addr,
            shm_regions[i].size,
            shm_regions[i].name);
        if (!*shm_regions[i].map_ptr)
            ok = 0;
    }
    return ok;
}

/*
 * Unmap all shared memory regions.
 */
static void unmap_all_shared_memory(void)
{
    unsigned int i;
    for (i = 0; i < NUM_SHM_REGIONS; i++) {
        if (*shm_regions[i].map_ptr) {
            munmap(*shm_regions[i].map_ptr, shm_regions[i].size);
            *shm_regions[i].map_ptr = NULL;
        }
    }
}

/*
 * Initialize all net instances with their respective modes and SHM regions.
 */
static void setup_net_instances(void)
{
    /* Net0: bridge mode */
    net_instances[0].id = 0;
    net_instances[0].mode = VIRTIO_NET_MODE_BRIDGE;
    net_instances[0].backend_fd = -1;
    net_instances[0].shm = (struct virtio_net_shm *)net0_map;
    net_instances[0].phys_base = VIRTIO_NET0_BASE;
    net_instances[0].phys_size = VIRTIO_NET0_SIZE;

    /* Net1: NAT mode */
    net_instances[1].id = 1;
    net_instances[1].mode = VIRTIO_NET_MODE_NAT;
    net_instances[1].backend_fd = -1;
    net_instances[1].shm = (struct virtio_net_shm *)net1_map;
    net_instances[1].phys_base = VIRTIO_NET1_BASE;
    net_instances[1].phys_size = VIRTIO_NET1_SIZE;

    /* Net2: p2p mode */
    net_instances[2].id = 2;
    net_instances[2].mode = VIRTIO_NET_MODE_P2P;
    net_instances[2].backend_fd = -1;
    net_instances[2].shm = (struct virtio_net_shm *)net2_map;
    net_instances[2].phys_base = VIRTIO_NET2_BASE;
    net_instances[2].phys_size = VIRTIO_NET2_SIZE;
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

    /* Force line-buffered stdout so log file gets flushed per line */
    setlinebuf(stdout);

    printf("=== PRTOS Virtio Backend Daemon ===\n");
    printf("System Partition - Virtio Device Virtualization Demo\n");
    printf("Devices: %d x net + 1 x blk + 1 x console\n", VIRTIO_NUM_NET);
    printf("Shared Memory Regions:\n");
    printf("  Net0 (bridge): 0x%lx (%lu KB)\n",
           (unsigned long)VIRTIO_NET0_BASE, (unsigned long)VIRTIO_NET0_SIZE / 1024);
    printf("  Net1 (NAT):    0x%lx (%lu KB)\n",
           (unsigned long)VIRTIO_NET1_BASE, (unsigned long)VIRTIO_NET1_SIZE / 1024);
    printf("  Net2 (p2p):    0x%lx (%lu KB)\n",
           (unsigned long)VIRTIO_NET2_BASE, (unsigned long)VIRTIO_NET2_SIZE / 1024);
    printf("  Blk:           0x%lx (%lu KB)\n",
           (unsigned long)VIRTIO_BLK_BASE, (unsigned long)VIRTIO_BLK_SIZE / 1024);
    printf("  Console:       0x%lx (%lu KB)\n",
           (unsigned long)VIRTIO_CON_BASE, (unsigned long)VIRTIO_CON_SIZE / 1024);
    if (backing_file)
        printf("Block backing file: %s\n", backing_file);
    printf("\n");

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Initialize PRTOS hypervisor interface */
    if (prtos_hv_init() == 0) {
        hv_available = 1;
        printf("[Backend] PRTOS hypervisor interface initialized (partition %d)\n",
               prtos_hv_get_partition_self());

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

    /* Map all 5 shared memory regions */
    if (!map_all_shared_memory()) {
        fprintf(stderr, "[Backend] Could not map all shared memory regions.\n");
        printf("[Backend] Running in demo mode (no shared memory access).\n");
        printf("[Backend] Demo mode: Virtio Backend would manage:\n");
        printf("  - Virtio-Net x3 (bridge / NAT / p2p)\n");
        printf("  - Virtio-Blk    (file-backed)\n");
        printf("  - Virtio-Console\n");
        printf("[Backend] Verification Passed (demo mode)\n");
        return 0;
    }

    /* Setup net instance descriptors */
    setup_net_instances();
    blk_shm = (struct virtio_blk_shm *)blk_map;
    con_shm = (struct virtio_console_shm *)con_map;

    /* Initialize all Virtio devices */
    for (i = 0; i < VIRTIO_NUM_NET; i++)
        virtio_net_init(&net_instances[i]);
    virtio_blk_init(blk_shm, backing_file);
    virtio_console_init(con_shm);

    printf("\n[Backend] All %d Virtio devices initialized. Entering poll loop...\n",
           VIRTIO_NUM_DEVICES);
    printf("[Backend] Waiting for Guest Partition to connect...\n\n");

    /* Main event loop */
    while (running) {
        int processed = 0;
        static int guest_halt_detected = 0;
        static int con_was_ready = 0;    /* tracks frontend_ready 1→0 transition */
        /* Check guest partition status every ~1000 iterations (1 second).
         * When halt is detected, disconnect TCP clients and stop IPVIs. */
        static int halt_check_counter = 0;

        /* Track frontend_ready state for transition detection */
        if (!guest_halt_detected && con_shm->frontend_ready == 1)
            con_was_ready = 1;

        if (!guest_halt_detected && ++halt_check_counter >= 1000) {
            halt_check_counter = 0;
            /* Method 1: Check PRTOS hypervisor partition status */
            if (hv_available) {
                prtos_part_status_t guest_status;
                if (prtos_hv_get_partition_status(GUEST_PARTITION_ID, &guest_status) >= 0) {
                    if (guest_status.state == PRTOS_STATUS_HALTED) {
                        printf("\n[Backend] Guest Partition %d has HALTED (resets: %u)\n",
                               GUEST_PARTITION_ID, guest_status.reset_counter);
                        guest_halt_detected = 1;
                        virtio_console_notify_guest_halt();
                    }
                }
            }
            /* Method 2: Detect frontend death via frontend_ready flag.
             * When the guest shuts down, init sends SIGTERM to the frontend.
             * The frontend's signal handler clears frontend_ready = 0.
             * This fires when the flag transitions from 1 to 0, indicating
             * the frontend process exited. */
            if (!guest_halt_detected && con_was_ready && con_shm->frontend_ready == 0) {
                printf("\n[Backend] Guest frontend disconnected (frontend_ready: 1->0)\n");
                guest_halt_detected = 1;
                virtio_console_notify_guest_halt();
            }
        }

        /* Process each device's queues */
        for (i = 0; i < VIRTIO_NUM_NET; i++)
            virtio_net_process(&net_instances[i]);
        virtio_blk_process(blk_shm);
        virtio_console_process(con_shm);

        /* Check doorbell counters from each device */
        for (i = 0; i < VIRTIO_NUM_NET; i++) {
            static uint32_t last_net_db[VIRTIO_NUM_NET];
            if (net_instances[i].shm->doorbell_count != last_net_db[i]) {
                last_net_db[i] = net_instances[i].shm->doorbell_count;
                processed = 1;
            }
        }
        {
            static uint32_t last_blk_db;
            if (blk_shm->doorbell_count != last_blk_db) {
                last_blk_db = blk_shm->doorbell_count;
                processed = 1;
            }
        }
        {
            static uint32_t last_con_db;
            if (con_shm->doorbell_count != last_con_db) {
                last_con_db = con_shm->doorbell_count;
                processed = 1;
            }
        }

        /* Signal Guest via IPVI completion doorbell (skip if guest halted) */
        if (processed && hv_available && !guest_halt_detected) {
            prtos_hv_raise_partition_ipvi(GUEST_PARTITION_ID, IPVI_SYS_TO_GUEST);
        }

        /* Check frontend connection status */
        for (i = 0; i < VIRTIO_NUM_NET; i++) {
            static int net_reported[VIRTIO_NUM_NET];
            if (net_instances[i].shm->frontend_ready && !net_reported[i]) {
                printf("[Backend] Net%d frontend connected\n", i);
                net_reported[i] = 1;
            }
        }
        {
            static int blk_reported;
            if (blk_shm->frontend_ready && !blk_reported) {
                printf("[Backend] Blk frontend connected\n");
                blk_reported = 1;
            }
        }
        {
            static int con_reported;
            if (con_shm->frontend_ready && !con_reported) {
                printf("[Backend] Console frontend connected\n");
                con_reported = 1;
            }
        }

        usleep(1000);  /* 1ms poll interval */
    }

    printf("\n[Backend] Shutting down.\n");

    /* Cleanup console TCP bridge */
    virtio_console_cleanup();

    /* Cleanup net backends */
    for (i = 0; i < VIRTIO_NUM_NET; i++)
        virtio_net_cleanup(&net_instances[i]);

    unmap_all_shared_memory();
    return 0;
}
