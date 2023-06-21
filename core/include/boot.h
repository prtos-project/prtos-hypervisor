/*
 * FILE: boot.h
 *
 * Processor functions
 *
 * www.prtos.org
 */

#ifndef _PRTOS_BOOT_H_
#define _PRTOS_BOOT_H_

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

#include <kthread.h>

#define __VBOOT __attribute__((__section__(".vboot.text")))
#define __VBOOTDATA __attribute__((__section__(".vboot.data")))

#define __BOOT __attribute__((__section__(".boot.text")))
#define __BOOTDATA __attribute__((__section__(".boot.data")))

#endif
