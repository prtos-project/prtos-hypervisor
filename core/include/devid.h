/*
 * FILE: devid.h
 *
 * Devices Ids
 *
 * www.prtos.org
 */

#ifndef _PRTOS_DEVID_H_
#define _PRTOS_DEVID_H_

#define PRTOS_DEV_INVALID_ID 0xFFFF

#define PRTOS_DEV_LOGSTORAGE_ID 0
#define PRTOS_DEV_UART_ID 1
#define PRTOS_DEV_VGA_ID 2
#if defined(CONFIG_EXT_SYNC_MPT_IO) || defined(CONFIG_PLAN_EXTSYNC)
#define PRTOS_DEV_SPARTAN6_EXTSYNC_ID 3
#endif

#define NO_KDEV 7

#endif
