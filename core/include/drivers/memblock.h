/*
 * FILE: memblock.h
 *
 * Memory block driver
 *
 * www.prtos.org
 */

#ifndef _PRTOS_MEMBLOCK_DRV_H_
#define _PRTOS_MEMBLOCK_DRV_H_

#include <prtosconf.h>
struct mem_block_data {
    prtos_s32_t pos;
    prtos_address_t addr;
    struct prtos_conf_mem_block *cfg;
};

#endif
