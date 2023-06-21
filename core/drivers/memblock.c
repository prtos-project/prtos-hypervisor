/*
 * FILE: memblock.c
 *
 * Memory block management
 *
 * www.prtos.org
 */

#include <assert.h>
#include <boot.h>
#include <rsvmem.h>
#include <kdevice.h>
#include <spinlock.h>
#include <sched.h>
#include <stdc.h>
#include <virtmm.h>
#include <vmmap.h>
#include <arch/physmm.h>
#include <drivers/memblock.h>

static kdevice_t *mem_block_table = 0;
static struct mem_block_data *mem_block_data;

static prtos_s32_t reset_mem_block(const kdevice_t *kdev) {
    mem_block_data[kdev->sub_id].pos = 0;
    return 0;
}

static prtos_s32_t read_mem_block(const kdevice_t *kdev, prtos_u8_t *buffer, prtos_s_size_t len) {
    prtos_u8_t *ptr;
    prtos_s32_t e;
    ASSERT(buffer);
    if (((prtos_s_size_t)prtos_conf_phys_mem_area_table[mem_block_data[kdev->sub_id].cfg->physical_memory_areas_offset].size -
         (prtos_s_size_t)mem_block_data[kdev->sub_id].pos - len) < 0)
        len = prtos_conf_phys_mem_area_table[mem_block_data[kdev->sub_id].cfg->physical_memory_areas_offset].size - mem_block_data[kdev->sub_id].pos;

    if (len <= 0) {
        return 0;
    }
    ptr = (prtos_u8_t *)(mem_block_data[kdev->sub_id].addr + mem_block_data[kdev->sub_id].pos);

    for (e = 0; e < len; e++, ptr++) {
#ifdef CONFIG_ARCH_MMU_BYPASS
        buffer[e] = read_by_pass_mmu_byte(ptr);
#else
#ifdef CONFIG_x86
        buffer[e] = *ptr;
#endif
#endif
        if (!(e & 0x7f)) {
            preemption_on();
            preemption_off();
        }
    }
    mem_block_data[kdev->sub_id].pos += len;
    return len;
}

static prtos_s32_t write_mem_block(const kdevice_t *kdev, prtos_u8_t *buffer, prtos_s_size_t len) {
#ifdef CONFIG_ARCH_MMU_BYPASS
    static prtos_u32_t reserve_memory(prtos_u32_t * v_addr) {
        return *v_addr;
    }
#endif
    prtos_u8_t *ptr;
    ASSERT(buffer);
    if (((prtos_s_size_t)prtos_conf_phys_mem_area_table[mem_block_data[kdev->sub_id].cfg->physical_memory_areas_offset].size -
         (prtos_s_size_t)mem_block_data[kdev->sub_id].pos - len) < 0)
        len = prtos_conf_phys_mem_area_table[mem_block_data[kdev->sub_id].cfg->physical_memory_areas_offset].size - mem_block_data[kdev->sub_id].pos;

    if (len <= 0) return 0;

    ptr = (prtos_u8_t *)(mem_block_data[kdev->sub_id].addr + mem_block_data[kdev->sub_id].pos);

#ifdef CONFIG_ARCH_MMU_BYPASS
    unalign_memcpy(ptr, buffer, len, reserve_memory, (rd_mem_t)read_by_pass_mmu_word, (wr_mem_t)write_by_pass_mmu_word);
#else
#ifdef CONFIG_x86
    memcpy(ptr, buffer, len);
#endif
#endif
    //  memcpy((prtos_u8_t *)(mem_block_data[kdev->sub_id].v_addr+mem_block_data[kdev->sub_id].pos), buffer, len);
    mem_block_data[kdev->sub_id].pos += len;

    return len;
}

static prtos_s32_t seek_mem_block(const kdevice_t *kdev, prtos_u32_t offset, prtos_u32_t whence) {
    prtos_s32_t off = offset;

    switch ((whence)) {
        case DEV_SEEK_START:
            break;
        case DEV_SEEK_CURRENT:
            off += mem_block_data[kdev->sub_id].pos;
            break;
        case DEV_SEEK_END:
            off += prtos_conf_phys_mem_area_table[mem_block_data[kdev->sub_id].cfg->physical_memory_areas_offset].size;
            break;
    }
    if (off < 0) off = 0;
    if (off > prtos_conf_phys_mem_area_table[mem_block_data[kdev->sub_id].cfg->physical_memory_areas_offset].size)
        off = prtos_conf_phys_mem_area_table[mem_block_data[kdev->sub_id].cfg->physical_memory_areas_offset].size;
    mem_block_data[kdev->sub_id].pos = off;

    return off;
}

static const kdevice_t *get_mem_block(prtos_u32_t sub_id) {
    return &mem_block_table[sub_id];
}

prtos_s32_t __VBOOT init_mem_block(void) {
#if defined(CONFIG_MMU) && !defined(CONFIG_ARCH_MMU_BYPASS)
    prtos_s32_t i, num_of_pages;
#endif
    prtos_s32_t e;

    GET_MEMZ(mem_block_table, sizeof(kdevice_t) * prtos_conf_table.device_table.num_of_mem_blocks);
    GET_MEMZ(mem_block_data, sizeof(struct mem_block_data) * prtos_conf_table.device_table.num_of_mem_blocks);
    for (e = 0; e < prtos_conf_table.device_table.num_of_mem_blocks; e++) {
        mem_block_table[e].sub_id = e;
        mem_block_table[e].reset = reset_mem_block;
        mem_block_table[e].write = write_mem_block;
        mem_block_table[e].read = read_mem_block;
        mem_block_table[e].seek = seek_mem_block;
        mem_block_data[e].cfg = &prtos_conf_mem_block_table[e];
#if defined(CONFIG_MMU) && !defined(CONFIG_ARCH_MMU_BYPASS)
        // Mapping on virtual memory
        num_of_pages = SIZE2PAGES(prtos_conf_phys_mem_area_table[prtos_conf_mem_block_table[e].physical_memory_areas_offset].size);
        if (!(mem_block_data[e].addr = vmm_alloc(num_of_pages))) {
            cpu_ctxt_t ctxt;
            get_cpu_ctxt(&ctxt);
            system_panic(&ctxt, "[init_mem_block] system is out of free frames\n");
        }
        for (i = 0; i < (num_of_pages * PAGE_SIZE); i += PAGE_SIZE)
            vm_map_page(prtos_conf_phys_mem_area_table[prtos_conf_mem_block_table[e].physical_memory_areas_offset].start_addr + i, mem_block_data[e].addr + i,
                        _PG_ATTR_PRESENT | _PG_ATTR_RW);
#else
        mem_block_data[e].addr = prtos_conf_phys_mem_area_table[prtos_conf_mem_block_table[e].physical_memory_areas_offset].start_addr;
#endif
    }

    get_kdev_table[PRTOS_DEV_LOGSTORAGE_ID] = get_mem_block;

    return 0;
}

#ifdef CONFIG_DEV_MEMBLOCK_MODULE
PRTOS_MODULE("memory_block", init_mem_block, DRV_MODULE);
#else
REGISTER_KDEV_SETUP(init_mem_block);
#endif
