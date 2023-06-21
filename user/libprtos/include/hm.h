/*
 * FILE: hm.h
 *
 * health monitor
 *
 * www.prtos.org
 */

#ifndef _LIB_PRTOS_HM_H_
#define _LIB_PRTOS_HM_H_

#include <prtos_inc/config.h>
#include <prtos_inc/objdir.h>
#include <prtos_inc/objects/hm.h>

extern prtos_s32_t prtos_hm_read(prtos_hm_log_t *hm_log_ptr);
extern prtos_s32_t prtos_hm_seek(prtos_s32_t offset, prtos_u32_t whence);
extern prtos_s32_t prtos_hm_status(prtos_hm_status_t *hm_status_ptr);

#endif
