/*
 * FILE: console.c
 *
 * Object console
 *
 * www.prtos.org
 */

#include <assert.h>
#include <rsvmem.h>
#include <boot.h>
#include <hypercalls.h>
#include <sched.h>
#include <spinlock.h>
#include <objdir.h>
#include <stdc.h>

#include <objects/console.h>

static struct console prtos_console, *partition_console_table;
static spin_lock_t console_lock = SPINLOCK_INIT;

void console_put_char(prtos_u8_t c) {
    if (prtos_console.dev) {
        if (kdev_write(prtos_console.dev, &c, 1) != 1) {
            kdev_seek(prtos_console.dev, 0, DEV_SEEK_START);
            kdev_write(prtos_console.dev, &c, 1);
        }
    }
}

static inline prtos_s32_t write_mod(struct console *con, prtos_u8_t *b) {
    if (kdev_write(con->dev, b, 1) != 1) {
        kdev_seek(con->dev, 0, DEV_SEEK_START);
        if (kdev_write(con->dev, b, 1) != 1) return 0;
    }
    return 1;
}

static prtos_s32_t write_console_obj(prtos_obj_desc_t desc, prtos_u8_t *__g_param buffer, prtos_u_size_t length) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    prtos_id_t part_id = OBJDESC_GET_PARTITIONID(desc);
    struct console *con;
    prtos_s32_t e;

    if (part_id != KID2PARTID(info->sched.current_kthread->ctrl.g->id)) return PRTOS_PERM_ERROR;

    if (check_gp_param(buffer, length, 1, PFLAG_NOT_NULL) < 0) return PRTOS_INVALID_PARAM;

    // Only strings of a maximum of 128 bytes are allowed
    if (length > 128) return PRTOS_INVALID_PARAM;

    con = (part_id == PRTOS_HYPERVISOR_ID) ? &prtos_console : &partition_console_table[part_id];

    spin_lock(&console_lock);
    for (e = 0; e < length; e++) {
        preemption_on();
        preemption_off();
        if (!write_mod(con, &buffer[e])) {
            spin_unlock(&console_lock);
            return e;
        }
    }
    spin_unlock(&console_lock);
    return length;
}

static const struct object console_obj = {
    .write = (write_obj_op_t)write_console_obj,
};

prtos_s32_t __VBOOT setup_console(void) {
    prtos_s32_t e;

    GET_MEMZ(partition_console_table, sizeof(struct console) * prtos_conf_table.num_of_partitions);
    prtos_console.dev = find_kdev(&prtos_conf_table.hpv.console_device);
    object_table[OBJ_CLASS_CONSOLE] = &console_obj;
    for (e = 0; e < prtos_conf_table.num_of_partitions; e++) {
        partition_console_table[e].dev = find_kdev(&prtos_conf_partition_table[e].console_device);
    }
    return 0;
}

REGISTER_OBJ(setup_console);
