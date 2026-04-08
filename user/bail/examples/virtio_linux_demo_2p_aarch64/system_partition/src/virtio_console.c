/*
 * virtio_console.c - Virtio Console backend for PRTOS System Partition.
 *
 * Uses the Virtio_Con shared memory region at GPA 0x16500000 (256KB).
 *
 * Data flow:
 *   Guest writes to /dev/hvc0 -> characters enter tx_buf in shared memory
 *   -> Backend reads tx_buf -> Backend prints to System stdout (UART terminal)
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "virtio_be.h"

void virtio_console_init(struct virtio_console_shm *con)
{
    if (!con)
        return;

    memset(con, 0, sizeof(*con));
    con->magic = VIRTIO_CONSOLE_MAGIC;
    con->version = 1;
    con->buf_size = VIRTIO_CONSOLE_BUF_SIZE;
    con->device_status = VIRTIO_STATUS_ACK;
    con->backend_ready = 1;
    __sync_synchronize();

    printf("[Backend] Virtio-Console initialized (buf_size=%u)\n",
           con->buf_size);
}

void virtio_console_process(struct virtio_console_shm *con)
{
    uint32_t head, tail;

    if (!con)
        return;

    __sync_synchronize();
    head = con->tx_head;
    tail = con->tx_tail;

    /* Read all available characters from Guest's TX buffer */
    while (tail != head) {
        char c = con->tx_buf[tail % con->buf_size];
        putchar(c);
        tail = (tail + 1) % con->buf_size;
    }

    if (con->tx_tail != tail) {
        con->tx_tail = tail;
        __sync_synchronize();
        fflush(stdout);
    }
}
