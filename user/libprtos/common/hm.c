/*
 * FILE: hm.c
 *
 * Health Monitor
 *
 * www.prtos.org
 */

#include <prtos.h>
#include <hm.h>
#include <prtos_inc/objects/hm.h>

prtos_s32_t prtos_hm_read(prtos_hm_log_t *hm_log_ptr) {
    prtos_s32_t ret;

    if (!(lib_prtos_params.part_ctrl_table[prtos_get_vcpuid()]->flags & PRTOS_PART_SYSTEM)) return PRTOS_PERM_ERROR;

    if (!hm_log_ptr) {
        return PRTOS_INVALID_PARAM;
    }
    ret = prtos_read_object(OBJDESC_BUILD(OBJ_CLASS_HM, PRTOS_HYPERVISOR_ID, 0), hm_log_ptr, sizeof(prtos_hm_log_t), 0);
    return (ret > 0) ? (ret / sizeof(prtos_hm_log_t)) : ret;
}

prtos_s32_t prtos_hm_seek(prtos_s32_t offset, prtos_u32_t whence) {
    if (!(lib_prtos_params.part_ctrl_table[prtos_get_vcpuid()]->flags & PRTOS_PART_SYSTEM)) return PRTOS_PERM_ERROR;

    return prtos_seek_object(OBJDESC_BUILD(OBJ_CLASS_HM, PRTOS_HYPERVISOR_ID, 0), offset, whence);
}

prtos_s32_t prtos_hm_status(prtos_hm_status_t *hm_status_ptr) {
    if (!(lib_prtos_params.part_ctrl_table[prtos_get_vcpuid()]->flags & PRTOS_PART_SYSTEM)) return PRTOS_PERM_ERROR;

    if (!hm_status_ptr) {
        return PRTOS_INVALID_PARAM;
    }
    return prtos_ctrl_object(OBJDESC_BUILD(OBJ_CLASS_HM, PRTOS_HYPERVISOR_ID, 0), PRTOS_HM_GET_STATUS, hm_status_ptr);
}
