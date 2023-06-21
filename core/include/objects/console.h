/*
 * FILE: console.h
 *
 * Console definition
 *
 * www.prtos.org
 */

#ifndef _PRTOS_OBJ_CONSOLE_H_
#define _PRTOS_OBJ_CONSOLE_H_

#ifdef _PRTOS_KERNEL_
#include <kdevice.h>

extern void console_init(const kdevice_t *kdev);
extern void console_put_char(prtos_u8_t c);

struct console {
    const kdevice_t *dev;
};

#endif
#endif
