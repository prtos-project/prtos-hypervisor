/*
 * FILE: virtmm.c
 *
 * Virtual memory manager
 *
 * www.prtos.org
 */

#include <assert.h>
#include <rsvmem.h>
#include <boot.h>
#include <processor.h>
#include <virtmm.h>
#include <vmmap.h>
#include <spinlock.h>
#include <stdc.h>
#include <arch/paging.h>
#include <arch/prtos_def.h>

static prtos_address_t vmm_start_addr;
static prtos_s32_t num_of_frames;

prtos_address_t vmm_alloc(prtos_s32_t num_of_pages) {
    prtos_address_t v_addr;
    if ((num_of_frames - num_of_pages) < 0) return 0;
    v_addr = vmm_start_addr;
    vmm_start_addr += (num_of_pages << PAGE_SHIFT);
    num_of_frames -= num_of_pages;
    ASSERT(num_of_frames >= 0);
    return v_addr;
}

prtos_s32_t vmm_get_num_of_free_frames(void) {
    ASSERT(num_of_frames >= 0);
    return num_of_frames;
}

void __VBOOT setup_virt_mm(void) {
    prtos_address_t st, end;
    prtos_u32_t flags;

    st = prtos_conf_phys_mem_area_table[prtos_conf_table.hpv.physical_memory_areas_offset].start_addr;
    end = st + prtos_conf_phys_mem_area_table[prtos_conf_table.hpv.physical_memory_areas_offset].size - 1;
    flags = prtos_conf_phys_mem_area_table[prtos_conf_table.hpv.physical_memory_areas_offset].flags;
    eprintf("PRTOS map: [0x%" PRNT_ADDR_FMT "x - 0x%" PRNT_ADDR_FMT "x] flags: 0x%x\n", st, end, flags);
    ASSERT(st == CONFIG_PRTOS_LOAD_ADDR);
    setup_vm_map(&vmm_start_addr, &num_of_frames);
    eprintf("[VMM] Free [0x%" PRNT_ADDR_FMT "x-0x%" PRNT_ADDR_FMT "x] %d frames\n", vmm_start_addr, PRTOS_VMAPEND, num_of_frames);
}
