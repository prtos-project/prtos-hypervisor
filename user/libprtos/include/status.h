/*
 * FILE: trace.h
 *
 * tracing subsystem
 *
 * www.prtos.org
 */

#ifndef _LIB_PRTOS_STATUS_H_
#define _LIB_PRTOS_STATUS_H_

#include <prtos_inc/config.h>
#include <prtos_inc/objdir.h>
#include <prtos_inc/objects/status.h>

extern prtos_s32_t prtos_get_partition_status(prtos_id_t id, prtos_part_status_t *status);
extern prtos_s32_t prtos_get_system_status(prtos_sys_status_t *status);
extern prtos_s32_t prtos_get_vcpu_status(prtos_id_t vcpu, prtos_virtual_cpu_status_t *status);
extern prtos_s32_t prtos_set_partition_opmode(prtos_s32_t op_mode);
extern prtos_s32_t prtos_get_plan_status(prtos_plan_status_t *status);
extern prtos_s32_t prtos_get_physpage_status(prtos_address_t p_addr, prtos_phys_page_status_t *status);

/*only for testing*/
extern prtos_s32_t prtos_get_partition_vcpu_status(prtos_id_t id, prtos_id_t vcpu, prtos_virtual_cpu_status_t *status);
#endif
