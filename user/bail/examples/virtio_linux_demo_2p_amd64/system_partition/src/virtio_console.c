/*
 * virtio_console.c - Virtio Console backend logic for PRTOS System Partition.
 *
 * Implements a simple character ring buffer console:
 *   - Reads characters from Guest's tx_buf and prints to System stdout
 *   - Writes characters to rx_buf for Guest to read
 *
 * Data flow:
 *   Guest writes to /dev/hvc0 -> characters enter tx_buf in shared memory
 *   -> Backend reads tx_buf -> Backend prints to System stdout (QEMU terminal)
 *
 * Verification: echo "PRTOS Virtio Test" > /dev/hvc0  (from Guest Linux)
 *   -> message appears on System Partition's console output
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "virtio_be.h"

void virtio_console_init(void *shmem_base)
{
    struct virtio_console_ring *console = VIRTIO_CONSOLE(shmem_base);

    memset(console, 0, sizeof(*console));
    console->magic = VIRTIO_CONSOLE_MAGIC;
    console->buf_size = VIRTIO_CONSOLE_BUF_SIZE;
    console->tx_head = 0;
    console->tx_tail = 0;
    console->rx_head = 0;
    console->rx_tail = 0;

    __sync_synchronize();
    printf("[Backend] Virtio-Console initialized (buf_size=%u)\n",
           console->buf_size);
}

void virtio_console_process(void *shmem_base)
{
    struct virtio_console_ring *console = VIRTIO_CONSOLE(shmem_base);
    uint32_t head, tail;

    __sync_synchronize();
    head = console->tx_head;
    tail = console->tx_tail;

    /* Read all available characters from Guest's TX buffer */
    while (tail != head) {
        char c = console->tx_buf[tail % console->buf_size];
        putchar(c);
        tail = (tail + 1) % console->buf_size;
    }

    if (console->tx_tail != tail) {
        console->tx_tail = tail;
        __sync_synchronize();
        fflush(stdout);
    }
}
