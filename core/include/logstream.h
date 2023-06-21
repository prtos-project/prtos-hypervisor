/*
 * FILE: logstream.h
 *
 * Log Stream
 *
 * www.prtos.org
 */

#ifndef _PRTOS_LOGSTREAM_H_
#define _PRTOS_LOGSTREAM_H_

#include <assert.h>
#include <kdevice.h>
#include <stdc.h>
#include <spinlock.h>

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

struct log_stream_hdr {
#define LOGSTREAM_MAGIC1 0xF9E8D7C6
    prtos_u32_t magic1;
    struct log_stream_info {
        prtos_s32_t elem_size, max_num_of_elem, lock, c_hdr;
    } info;
    struct log_stream_ctrl {
        prtos_s32_t tail, elem, head, d;
    } ctrl[2];
#define LOGSTREAM_MAGIC2 0x1A2B3C4D
    prtos_u32_t magic2;
};

struct log_stream {
    struct log_stream_info info;
    struct log_stream_ctrl ctrl;
    spin_lock_t lock;
    const kdevice_t *kdev;
};

static inline void log_stream_commit(struct log_stream *ls) {
    spin_lock(&ls->lock);
    if (ls->info.max_num_of_elem) {
        kdev_seek(ls->kdev, OFFSETOF(struct log_stream_hdr, ctrl[ls->info.c_hdr]), DEV_SEEK_START);
        kdev_write(ls->kdev, &ls->ctrl, sizeof(struct log_stream_ctrl));
        kdev_seek(ls->kdev, OFFSETOF(struct log_stream_hdr, info.c_hdr), DEV_SEEK_START);
        kdev_write(ls->kdev, &ls->info.c_hdr, sizeof(prtos_s32_t));
        ls->info.c_hdr = !(ls->info.c_hdr) ? 1 : 0;
    }
    spin_unlock(&ls->lock);
}

static inline void log_stream_init(struct log_stream *ls, const kdevice_t *kdev, prtos_s32_t elem_size) {
    struct log_stream_hdr hdr;
    ASSERT(elem_size > 0);
    memset(ls, 0, sizeof(struct log_stream));
    ls->kdev = kdev;
    kdev_reset(kdev);
    ls->lock = SPINLOCK_INIT;
    ls->info.elem_size = elem_size;
    ls->info.max_num_of_elem = (prtos_s32_t)kdev_seek(ls->kdev, 0, DEV_SEEK_END) - sizeof(struct log_stream_hdr);
    ls->info.max_num_of_elem = (ls->info.max_num_of_elem > 0) ? ls->info.max_num_of_elem / elem_size : 0;
    if (ls->info.max_num_of_elem) {
        kdev_seek(ls->kdev, 0, DEV_SEEK_START);
        kdev_read(ls->kdev, &hdr, sizeof(struct log_stream_hdr));
        if ((hdr.magic1 != LOGSTREAM_MAGIC1) || (hdr.magic2 != LOGSTREAM_MAGIC2)) goto INIT_HDR;
        if ((hdr.info.max_num_of_elem != ls->info.max_num_of_elem) || (hdr.info.elem_size != ls->info.elem_size)) goto INIT_HDR;
        ls->info.lock = hdr.info.lock;
        ls->info.c_hdr = !(hdr.info.c_hdr) ? 1 : 0;
        memcpy(&ls->ctrl, &hdr.ctrl[hdr.info.c_hdr], sizeof(struct log_stream_ctrl));
    }

    return;
INIT_HDR:
    memset(&hdr, 0, sizeof(struct log_stream_hdr));
    hdr.magic1 = LOGSTREAM_MAGIC1;
    hdr.magic2 = LOGSTREAM_MAGIC2;
    hdr.info.max_num_of_elem = ls->info.max_num_of_elem;
    hdr.info.elem_size = ls->info.elem_size;
    kdev_seek(ls->kdev, 0, DEV_SEEK_START);
    kdev_write(ls->kdev, &hdr, sizeof(struct log_stream_hdr));
}

static inline prtos_s32_t log_stream_insert(struct log_stream *ls, void *log) {
    prtos_s32_t smashed = 0;

    ASSERT(ls->info.max_num_of_elem >= 0);
    spin_lock(&ls->lock);
    if (ls->info.max_num_of_elem) {
        if (ls->ctrl.elem >= ls->info.max_num_of_elem) {
            if (ls->info.lock) {
                spin_unlock(&ls->lock);
                return -1;
            }

            ls->ctrl.head = ((ls->ctrl.head + 1) < ls->info.max_num_of_elem) ? ls->ctrl.head + 1 : 0;
            if (ls->ctrl.d > 0) ls->ctrl.d--;
            ls->ctrl.elem--;
            smashed++;
        }
        ASSERT_LOCK(ls->ctrl.elem < ls->info.max_num_of_elem, &ls->lock);
        kdev_seek(ls->kdev, sizeof(struct log_stream_hdr) + ls->ctrl.tail * ls->info.elem_size, DEV_SEEK_START);
        kdev_write(ls->kdev, log, ls->info.elem_size);
        ls->ctrl.tail = ((ls->ctrl.tail + 1) < ls->info.max_num_of_elem) ? ls->ctrl.tail + 1 : 0;
        ls->ctrl.elem++;
    } else {
        kdev_write(ls->kdev, log, ls->info.elem_size);
    }
    spin_unlock(&ls->lock);
    log_stream_commit(ls);
    return smashed;
}

static inline prtos_s32_t log_stream_get(struct log_stream *ls, void *log) {
    prtos_s32_t ptr;
    spin_lock(&ls->lock);
    if ((ls->ctrl.elem) > 0 && (ls->ctrl.d < ls->ctrl.elem)) {
        ptr = (ls->ctrl.d + ls->ctrl.head) % ls->info.max_num_of_elem;
        ls->ctrl.d++;
        kdev_seek(ls->kdev, sizeof(struct log_stream_hdr) + ptr * ls->info.elem_size, DEV_SEEK_START);
        kdev_read(ls->kdev, log, ls->info.elem_size);
        spin_unlock(&ls->lock);
        log_stream_commit(ls);
        return 0;
    }
    spin_unlock(&ls->lock);
    return -1;
}

// These values must match with the ones defined in hypercall.h
#define PRTOS_LOGSTREAM_CURRENT 0x0
#define PRTOS_LOGSTREAM_START 0x1
#define PRTOS_LOGSTREAM_END 0x2

static inline prtos_s32_t log_stream_seek(struct log_stream *ls, prtos_s32_t offset, prtos_u32_t whence) {
    prtos_s32_t off = offset;
    spin_lock(&ls->lock);
    switch ((whence)) {
        case PRTOS_LOGSTREAM_START:
            break;
        case PRTOS_LOGSTREAM_CURRENT:
            off += ls->ctrl.d;
            break;
        case PRTOS_LOGSTREAM_END:
            off += ls->ctrl.elem;
            break;
        default:
            spin_unlock(&ls->lock);
            return -1;
    }

    if (off > ls->ctrl.elem) off = ls->ctrl.elem;
    if (off < 0) off = 0;
    ls->ctrl.d = off;
    spin_unlock(&ls->lock);
    log_stream_commit(ls);
    return off;
}

static inline void log_stream_lock(struct log_stream *ls) {
    spin_lock(&ls->lock);
    ls->info.lock = 1;
    spin_unlock(&ls->lock);
}

static inline void log_stream_unlock(struct log_stream *ls) {
    spin_lock(&ls->lock);
    ls->info.lock = 0;
    spin_unlock(&ls->lock);
}

#endif
