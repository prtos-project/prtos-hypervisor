/*
 * FILE: hm.c
 *
 * Health Monitor
 *
 * www.prtos.org
 */

#include <assert.h>
#include <rsvmem.h>
#include <boot.h>
#include <hypercalls.h>
#include <kthread.h>
#include <physmm.h>
#include <stdc.h>
#include <sched.h>
#include <objects/status.h>

prtos_sys_status_t system_status;
prtos_part_status_t *partition_status;
prtos_virtual_cpu_status_t vcpu_status[CONFIG_NO_CPUS];

static prtos_s32_t ctrl_status(prtos_obj_desc_t desc, prtos_u32_t cmd, union statusCmd *__g_param args) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    extern prtos_u32_t sys_reset_counter[];
    prtos_id_t part_id;
    prtos_id_t vcpu_id;
    part_id = OBJDESC_GET_PARTITIONID(desc);
    if (part_id != get_partition(info->sched.current_kthread)->cfg->id) {
        if (!(get_partition(info->sched.current_kthread)->cfg->flags & PRTOS_PART_SYSTEM)) return PRTOS_PERM_ERROR;
    }

    switch (cmd) {
        case PRTOS_SET_PARTITION_OPMODE:
            if (part_id == PRTOS_HYPERVISOR_ID) return PRTOS_INVALID_PARAM;
            if ((part_id < 0) || (part_id >= prtos_conf_table.num_of_partitions)) return PRTOS_INVALID_PARAM;
            if ((args->op_mode < 0) || (args->op_mode > 3)) return PRTOS_INVALID_PARAM;
            part_table[part_id].op_mode = args->op_mode;
            return PRTOS_OK;
        case PRTOS_GET_SYSTEM_STATUS:
            if (part_id == PRTOS_HYPERVISOR_ID) {
                system_status.reset_counter = sys_reset_counter[0];
                memcpy(&args->status.system, &system_status, sizeof(prtos_sys_status_t));
            } else {
                prtos_s32_t num_of_vcpus;
                prtos_s32_t e;
                prtos_s32_t halt = 0, ready = 0, suspend = 0;
                if ((part_id < 0) || (part_id >= prtos_conf_table.num_of_partitions)) return PRTOS_INVALID_PARAM;
                num_of_vcpus = part_table[part_id].cfg->num_of_vcpus;
                for (e = 0; e < num_of_vcpus; e++) {
                    if (are_kthread_flags_set(part_table[part_id].kthread[e], KTHREAD_HALTED_F))
                        halt++;
                    else if (are_kthread_flags_set(part_table[part_id].kthread[e], KTHREAD_SUSPENDED_F))
                        suspend++;
                    else if (are_kthread_flags_set(part_table[part_id].kthread[e], KTHREAD_READY_F))
                        ready++;
                }

                if (ready) {
                    partition_status[part_id].state = PRTOS_STATUS_READY;
                } else if (suspend) {
                    partition_status[part_id].state = PRTOS_STATUS_SUSPENDED;
                } else if (halt) {
                    partition_status[part_id].state = PRTOS_STATUS_HALTED;
                } else {
                    partition_status[part_id].state = PRTOS_STATUS_IDLE;
                }

                partition_status[part_id].reset_counter = part_table[part_id].kthread[0]->ctrl.g->part_ctrl_table->reset_counter;
                partition_status[part_id].reset_status = part_table[part_id].kthread[0]->ctrl.g->part_ctrl_table->reset_status;
                partition_status[part_id].op_mode = part_table[part_id].op_mode;
                partition_status[part_id].exec_clock = get_time_usec_vclock(&part_table[part_id].kthread[0]->ctrl.g->vclock);

                memcpy(&args->status.partition, &partition_status[part_id].state, sizeof(prtos_part_status_t));
            }
            return PRTOS_OK;
        case PRTOS_GET_VCPU_STATUS:
            if ((part_id < 0) || (part_id >= prtos_conf_table.num_of_partitions)) return PRTOS_INVALID_PARAM;
            vcpu_id = OBJDESC_GET_VCPUID(desc);
            if (vcpu_id > part_table[part_id].cfg->num_of_vcpus) return PRTOS_INVALID_PARAM;
            if (are_kthread_flags_set(part_table[part_id].kthread[vcpu_id], KTHREAD_HALTED_F)) {
                vcpu_status[vcpu_id].state = PRTOS_STATUS_HALTED;
            } else if (are_kthread_flags_set(part_table[part_id].kthread[vcpu_id], KTHREAD_SUSPENDED_F)) {
                vcpu_status[vcpu_id].state = PRTOS_STATUS_SUSPENDED;
            } else if (are_kthread_flags_set(part_table[part_id].kthread[vcpu_id], KTHREAD_READY_F)) {
                vcpu_status[vcpu_id].state = PRTOS_STATUS_READY;
            } else {
                vcpu_status[vcpu_id].state = PRTOS_STATUS_IDLE;
            }
            vcpu_status[vcpu_id].op_mode = part_table[part_id].kthread[vcpu_id]->ctrl.g->op_mode;
            memcpy(&args->status.vcpu, &vcpu_status[vcpu_id], sizeof(prtos_virtual_cpu_status_t));
            return PRTOS_OK;
        case PRTOS_GET_SCHED_PLAN_STATUS: {
            if (prtos_conf_table.hpv.cpu_table[GET_CPU_ID()].sched_policy != CYCLIC_SCHED) return PRTOS_NO_ACTION;
            args->status.plan.switch_time = info->sched.data->plan_switch_time;
            args->status.plan.next = info->sched.data->plan.new->id;
            args->status.plan.current = info->sched.data->plan.current->id;
            if (info->sched.data->plan.prev)
                args->status.plan.prev = info->sched.data->plan.prev->id;
            else
                args->status.plan.prev = -1;
            return PRTOS_OK;
        }
        case PRTOS_GET_PHYSPAGE_STATUS: {
            struct phys_page *page;
            if ((part_id < 0) || (part_id >= prtos_conf_table.num_of_partitions)) return PRTOS_INVALID_PARAM;
            if (!(page = pmm_find_page(args->status.phys_page.p_addr, &part_table[part_id], 0))) return PRTOS_INVALID_PARAM;
            args->status.phys_page.counter = page->counter;
            args->status.phys_page.type = page->type;
            return PRTOS_OK;
        }
        default:
            return PRTOS_INVALID_PARAM;
    }
    return PRTOS_INVALID_PARAM;
}

static const struct object status_obj = {
    .ctrl = (ctrl_obj_op_t)ctrl_status,
};

prtos_s32_t __VBOOT setup_status(void) {
    GET_MEMZ(partition_status, sizeof(prtos_part_status_t) * prtos_conf_table.num_of_partitions);
    object_table[OBJ_CLASS_STATUS] = &status_obj;

    return 0;
}

REGISTER_OBJ(setup_status);
