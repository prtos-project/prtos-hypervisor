/*
 * virtio_blk.c - Virtio Block device backend logic for PRTOS System Partition.
 *
 * Implements a virtual block device via shared memory:
 *   - Guest submits block I/O requests (read/write/flush) to req ring
 *   - Backend processes requests using pread()/pwrite() on a backing store
 *   - Backend updates response status and signals completion
 *
 * Backing store options:
 *   - RAM disk (default): 1MB in-memory buffer for demo purposes
 *   - File-backed: --backing-file /path/to/disk.img for persistent storage
 *
 * Data flow:
 *   Guest mount /dev/vda1 -> block request (sector, len, type)
 *   -> req ring in shared memory -> Backend reads request
 *   -> Backend performs pread/pwrite on backing store
 *   -> Backend writes status to response -> Guest completes I/O
 *
 * Verification: mount /dev/vda1 /mnt  (from Guest Linux)
 *   -> Backend logs pread/pwrite operations with sector numbers
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "virtio_be.h"

/* In-memory RAM disk for demo (1MB) */
#define RAMDISK_SIZE    (1024 * 1024)
static uint8_t ramdisk[RAMDISK_SIZE];

/* File-backed disk (optional) */
static int backing_fd = -1;

void virtio_blk_init(void *shmem_base, const char *backing_file)
{
    struct virtio_blk_ring *blk = VIRTIO_BLK(shmem_base);
    uint64_t capacity;

    memset(blk, 0, sizeof(*blk));
    blk->magic = VIRTIO_BLK_MAGIC;
    blk->num_slots = VIRTIO_BLK_REQ_SLOTS;
    blk->req_head = 0;
    blk->req_tail = 0;
    blk->resp_head = 0;
    blk->resp_tail = 0;

    if (backing_file) {
        backing_fd = open(backing_file, O_RDWR);
        if (backing_fd >= 0) {
            off_t size = lseek(backing_fd, 0, SEEK_END);
            lseek(backing_fd, 0, SEEK_SET);
            capacity = (uint64_t)size / VIRTIO_BLK_SECTOR_SIZE;
            blk->capacity_sectors = capacity;
            printf("[Backend] Virtio-Blk initialized with file: %s (%lu sectors)\n",
                   backing_file, (unsigned long)capacity);
        } else {
            fprintf(stderr, "[Backend] Warning: cannot open %s: %s, using RAM disk\n",
                    backing_file, strerror(errno));
            backing_fd = -1;
        }
    }

    if (backing_fd < 0) {
        /* Use RAM disk */
        capacity = RAMDISK_SIZE / VIRTIO_BLK_SECTOR_SIZE;
        blk->capacity_sectors = capacity;
        memset(ramdisk, 0, RAMDISK_SIZE);
        printf("[Backend] Virtio-Blk initialized with RAM disk (%lu sectors, %d KB)\n",
               (unsigned long)capacity, RAMDISK_SIZE / 1024);
    }

    printf("[Backend] Virtio-Blk: %u request slots\n", VIRTIO_BLK_REQ_SLOTS);
    __sync_synchronize();
}

void virtio_blk_process(void *shmem_base)
{
    struct virtio_blk_ring *blk = VIRTIO_BLK(shmem_base);
    uint32_t head, tail;

    __sync_synchronize();
    head = blk->req_head;
    tail = blk->req_tail;

    while (tail != head) {
        uint32_t idx = tail % blk->num_slots;
        struct virtio_blk_req *req = &blk->slots[idx];

        uint64_t offset = req->sector * VIRTIO_BLK_SECTOR_SIZE;
        uint32_t len = req->len;
        uint64_t max_size = (backing_fd >= 0) ?
            (blk->capacity_sectors * VIRTIO_BLK_SECTOR_SIZE) : RAMDISK_SIZE;

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
