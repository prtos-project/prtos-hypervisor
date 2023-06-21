/*
 * FILE: kdevice.h
 *
 * kernel devices
 *
 * www.prtos.org
 */

#ifndef _PRTOS_KDEVICE_H_
#define _PRTOS_KDEVICE_H_

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

#include <prtosconf.h>
#include <devid.h>
#include <linkage.h>

typedef struct kdev {
    prtos_u16_t sub_id;
    prtos_s32_t (*reset)(const struct kdev *);
    prtos_s32_t (*write)(const struct kdev *, prtos_u8_t *buffer, prtos_s32_t len);
    prtos_s32_t (*read)(const struct kdev *, prtos_u8_t *buffer, prtos_s32_t len);
    prtos_s32_t (*seek)(const struct kdev *, prtos_u32_t offset, prtos_u32_t whence);
} kdevice_t;

typedef prtos_s32_t (*kdev_setup_t)(void);
typedef const kdevice_t *(*kdev_table_t)(const prtos_u32_t sub_id);

#define DEV_SEEK_CURRENT 0x0
#define DEV_SEEK_START 0x1
#define DEV_SEEK_END 0x2

#define KDEV_OK 0
#define KDEV_OP_NOT_ALLOWED 1

static inline prtos_s32_t kdev_reset(const kdevice_t *kdev) {
    if (kdev && (kdev->reset)) return kdev->reset(kdev);
    return -KDEV_OP_NOT_ALLOWED;
}

static inline prtos_s32_t kdev_write(const kdevice_t *kdev, void *buffer, prtos_s32_t len) {
    if (kdev && (kdev->write)) return kdev->write(kdev, buffer, len);
    return -KDEV_OP_NOT_ALLOWED;
}

static inline prtos_s32_t kdev_read(const kdevice_t *kdev, void *buffer, prtos_s32_t len) {
    if (kdev && (kdev->read)) return kdev->read(kdev, buffer, len);
    return -KDEV_OP_NOT_ALLOWED;
}

static inline prtos_s32_t kdev_seek(const kdevice_t *kdev, prtos_u32_t offset, prtos_u32_t whence) {
    if (kdev && (kdev->seek)) return kdev->seek(kdev, offset, whence);
    return -KDEV_OP_NOT_ALLOWED;
}

extern kdev_table_t get_kdev_table[NO_KDEV];
extern void setup_kdev(void);
extern const kdevice_t *find_kdev(const prtos_dev_t *dev);

#define REGISTER_KDEV_SETUP(_init)           \
    __asm__(".section .kdevsetup, \"a\"\n\t" \
            ".align 4\n\t"                   \
            ".long " #_init "\n\t"           \
            ".previous\n\t")

#define RESERVE_HWIRQ(_irq)                   \
    __asm__(".section .rsv_hwirqs, \"a\"\n\t" \
            ".align 4\n\t"                    \
            ".long " TO_STR(_irq) "\n\t"      \
                                  ".previous\n\t")

#define RESERVE_IOPORTS(_base, _offset)                            \
    __asm__(".section .rsv_ioports, \"a\"\n\t"                     \
            ".align 4\n\t"                                         \
            ".long " TO_STR(_base) "\n\t"                          \
                                   ".long " TO_STR(_offset) "\n\t" \
                                                            ".previous\n\t")

#define RESERVE_PHYSPAGES(_addr, _num_of_pages)                          \
    __asm__(".section .rsv_physpages, \"a\"\n\t"                         \
            ".align 4\n\t"                                               \
            ".long " TO_STR(_addr) "\n\t"                                \
                                   ".long " TO_STR(_num_of_pages) "\n\t" \
                                                                  ".previous\n\t")

#endif
