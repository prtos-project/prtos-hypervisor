/*
 * FILE: status.c
 *
 * Status functionality
 *
 * www.prtos.org
 */

#include <prtos.h>
#include <status.h>
#include <prtos_inc/objects/status.h>

prtos_s32_t prtos_get_system_status(prtos_sys_status_t *status) {
    return prtos_ctrl_object(OBJDESC_BUILD(OBJ_CLASS_STATUS, PRTOS_HYPERVISOR_ID, 0), PRTOS_GET_SYSTEM_STATUS, status);
}

prtos_s32_t prtos_get_partition_status(prtos_id_t id, prtos_part_status_t *status) {
    return prtos_ctrl_object(OBJDESC_BUILD(OBJ_CLASS_STATUS, id, 0), PRTOS_GET_SYSTEM_STATUS, status);
}

prtos_s32_t prtos_get_vcpu_status(prtos_id_t vcpu, prtos_virtual_cpu_status_t *status) {
    if (vcpu > prtos_get_number_vcpus()) return PRTOS_INVALID_PARAM;
    return prtos_ctrl_object(OBJDESC_BUILD_VCPUID(OBJ_CLASS_STATUS, vcpu, PRTOS_PARTITION_SELF, 0), PRTOS_GET_VCPU_STATUS, status);
}

prtos_s32_t prtos_set_partition_opmode(prtos_s32_t op_mode) {
    return prtos_ctrl_object(OBJDESC_BUILD(OBJ_CLASS_STATUS, PRTOS_PARTITION_SELF, 0), PRTOS_SET_PARTITION_OPMODE, &op_mode);
}

prtos_s32_t prtos_get_partition_vcpu_status(prtos_id_t id, prtos_id_t vcpu, prtos_virtual_cpu_status_t *status) {
    return prtos_ctrl_object(OBJDESC_BUILD_VCPUID(OBJ_CLASS_STATUS, vcpu, id, 0), PRTOS_GET_VCPU_STATUS, status);
}

prtos_s32_t prtos_get_plan_status(prtos_plan_status_t *status) {
    return prtos_ctrl_object(OBJDESC_BUILD(OBJ_CLASS_STATUS, PRTOS_PARTITION_SELF, 0), PRTOS_GET_SCHED_PLAN_STATUS, status);
}

prtos_s32_t prtos_get_physpage_status(prtos_address_t p_addr, prtos_phys_page_status_t *status) {
    status->p_addr = p_addr;
    return prtos_ctrl_object(OBJDESC_BUILD(OBJ_CLASS_STATUS, PRTOS_PARTITION_SELF, 0), PRTOS_GET_PHYSPAGE_STATUS, status);
}
