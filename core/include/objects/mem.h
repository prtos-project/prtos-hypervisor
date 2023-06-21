/*
 * FILE: mem.h
 *
 * memory object definitions
 *
 * www.prtos.org
 */

#ifndef _PRTOS_OBJ_MEM_H_
#define _PRTOS_OBJ_MEM_H_

#define PRTOS_OBJ_MEM_CPY_AREA 0x1

union mem_cmd {
    struct cpy_area {
        prtos_id_t dst_id;
        prtos_address_t dst_addr;
        prtos_id_t src_id;
        prtos_address_t src_addr;
        prtos_s_size_t size;
    } cpy_area;
};

#endif
