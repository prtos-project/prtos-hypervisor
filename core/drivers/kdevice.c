/*
 * FILE: kdevice.c
 *
 * Kernel devices's management
 *
 * www.prtos.org
 */

#include <kdevice.h>
#include <stdc.h>
#include <processor.h>

kdev_table_t get_kdev_table[NO_KDEV];

void setup_kdev(void) {
    extern kdev_setup_t kdev_setup[];
    prtos_s32_t e;
    memset(get_kdev_table, 0, sizeof(kdev_table_t) * NO_KDEV);
    for (e = 0; kdev_setup[e]; e++) kdev_setup[e]();
}

const kdevice_t *find_kdev(const prtos_dev_t *dev) {
    if ((dev->id < 0) || (dev->id >= NO_KDEV) || (dev->id == PRTOS_DEV_INVALID_ID)) return 0;

    if (get_kdev_table[dev->id]) return get_kdev_table[dev->id](dev->sub_id);

    return 0;
}
