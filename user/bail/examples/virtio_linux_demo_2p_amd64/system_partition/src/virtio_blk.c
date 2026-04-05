/*
 * virtio_blk.c - Virtio Block device backend for PRTOS System Partition.
 *
 * Implements a virtual block device via the Virtio_Blk shared memory region
 * at GPA 0x16300000 (2MB).
 *
 * Backing store:
 *   - File-backed (default): --backing-file /path/to/disk.img
 *   - RAM disk (fallback): 1MB in-memory buffer
 *
 * Data flow:
 *   Guest /dev/vda -> block request (sector, len, type)
 *   -> shared memory req ring -> Backend pread/pwrite on backing store
 *   -> status written back -> Guest completes I/O
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "virtio_be.h"

/* In-memory RAM disk for demo fallback (1MB) */
#define RAMDISK_SIZE    (1024 * 1024)
static uint8_t ramdisk[RAMDISK_SIZE];

/* File-backed disk */
static int backing_fd = -1;
static uint64_t backing_capacity_sectors = 0;

void virtio_blk_init(struct virtio_blk_shm *blk, const char *backing_file)
{
    if (!blk)
        return;

    memset(blk, 0, sizeof(*blk));
    blk->magic = VIRTIO_BLK_MAGIC;
    blk->version = 1;
    blk->num_slots = VIRTIO_BLK_REQ_SLOTS;
    blk->device_status = VIRTIO_STATUS_ACK;

    if (backing_file) {
        backing_fd = open(backing_file, O_RDWR);
        if (backing_fd >= 0) {
            off_t size = lseek(backing_fd, 0, SEEK_END);
            lseek(backing_fd, 0, SEEK_SET);
            backing_capacity_sectors = (uint64_t)size / VIRTIO_BLK_SECTOR_SIZE;
            blk->capacity_sectors = backing_capacity_sectors;
            printf("[Backend] Virtio-Blk initialized with file: %s (%lu sectors)\n",
                   backing_file, (unsigned long)backing_capacity_sectors);
        } else {
            fprintf(stderr, "[Backend] Warning: cannot open %s: %s, using RAM disk\n",
                    backing_file, strerror(errno));
            backing_fd = -1;
        }
    }

    if (backing_fd < 0) {
        backing_capacity_sectors = RAMDISK_SIZE / VIRTIO_BLK_SECTOR_SIZE;
        blk->capacity_sectors = backing_capacity_sectors;
        memset(ramdisk, 0, RAMDISK_SIZE);
        printf("[Backend] Virtio-Blk initialized with RAM disk (%lu sectors, %d KB)\n",
               (unsigned long)backing_capacity_sectors, RAMDISK_SIZE / 1024);
    }

    blk->backend_ready = 1;
    __sync_synchronize();
    printf("[Backend] Virtio-Blk: %u request slots, capacity %lu sectors\n",
           VIRTIO_BLK_REQ_SLOTS, (unsigned long)blk->capacity_sectors);
}

void virtio_blk_process(struct virtio_blk_shm *blk)
{
    uint32_t head, tail;
    uint64_t max_size;

    if (!blk)
        return;

    __sync_synchronize();
    head = blk->req_head;
    tail = blk->req_tail;

    max_size = (backing_fd >= 0) ?
        (backing_capacity_sectors * VIRTIO_BLK_SECTOR_SIZE) : RAMDISK_SIZE;

    while (tail != head) {
        uint32_t idx = tail % blk->num_slots;
        struct virtio_blk_req *req = &blk->slots[idx];

        uint64_t offset = req->sector * VIRTIO_BLK_SECTOR_SIZE;
        uint32_t len = req->len;

        if (offset + len > max_size) {
            printf("[Backend] Blk: request out of range (sector %lu, len %u)\n",
                   (unsigned long)req->sector, len);
            req->status = VIRTIO_BLK_S_IOERR;
        } else {
            switch (req->type) {
            case VIRTIO_BLK_T_IN:  /* Read */
                if (backing_fd >= 0) {
                    ssize_t n = pread(backing_fd, req->data, len, (off_t)offset);
                    req->status = (n == (ssize_t)len) ?
                        VIRTIO_BLK_S_OK : VIRTIO_BLK_S_IOERR;
                } else {
                    memcpy(req->data, ramdisk + offset, len);
                    req->status = VIRTIO_BLK_S_OK;
                }
                printf("[Backend] Blk: READ sector %lu, %u bytes -> %s\n",
                       (unsigned long)req->sector, len,
                       req->status == VIRTIO_BLK_S_OK ? "OK" : "ERR");
                break;

            case VIRTIO_BLK_T_OUT: /* Write */
                if (backing_fd >= 0) {
                    ssize_t n = pwrite(backing_fd, req->data, len, (off_t)offset);
                    req->status = (n == (ssize_t)len) ?
                        VIRTIO_BLK_S_OK : VIRTIO_BLK_S_IOERR;
                } else {
                    memcpy(ramdisk + offset, req->data, len);
                    req->status = VIRTIO_BLK_S_OK;
                }
                printf("[Backend] Blk: WRITE sector %lu, %u bytes -> %s\n",
                       (unsigned long)req->sector, len,
                       req->status == VIRTIO_BLK_S_OK ? "OK" : "ERR");
                break;

            case VIRTIO_BLK_T_FLUSH:
                if (backing_fd >= 0)
                    fsync(backing_fd);
                req->status = VIRTIO_BLK_S_OK;
                printf("[Backend] Blk: FLUSH -> OK\n");
                break;

            case VIRTIO_BLK_T_GET_ID:
                memset(req->data, 0, len);
                snprintf((char *)req->data,
                         len < 20 ? len : 20,
                         "prtos-virtblk0");
                req->status = VIRTIO_BLK_S_OK;
                printf("[Backend] Blk: GET_ID -> OK\n");
                break;

            default:
                req->status = VIRTIO_BLK_S_UNSUPP;
                printf("[Backend] Blk: unsupported type %u\n", req->type);
                break;
            }
        }

        __sync_synchronize();
        blk->resp_head = (blk->resp_head + 1) % blk->num_slots;
        tail = (tail + 1) % blk->num_slots;
    }

    if (blk->req_tail != tail) {
        blk->req_tail = tail;
        __sync_synchronize();
    }
}
