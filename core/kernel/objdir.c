/*
 * FILE: objdir.c
 *
 * Object directory
 *
 * www.prtos.org
 */

#include <rsvmem.h>
#include <objdir.h>
#include <stdc.h>
#include <processor.h>

typedef prtos_s32_t (*object_setup_table_t)(void);

const struct object *object_table[OBJ_NO_CLASSES] = {[0 ... OBJ_NO_CLASSES - 1] = 0};

void setup_obj_dir(void) {
    extern object_setup_table_t object_setup_table[];
    prtos_u32_t e;
    for (e = 0; object_setup_table[e]; e++) {
        if (object_setup_table[e] && object_setup_table[e]() < 0) {
            cpu_ctxt_t ctxt;
            get_cpu_ctxt(&ctxt);
            system_panic(&ctxt, "[obj_dir] Error setting up object at 0x%x\n", object_setup_table);
        }
    }
}
