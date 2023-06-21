/*
 * FILE: vmmap.h
 *
 * Virtual memory map manager
 *
 * Version: prtos-1.0.0
 *
 * www.prtos.org
 */

#ifndef _PRTOS_VMMAP_H_
#define _PRTOS_VMMAP_H_

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

#define _PG_ATTR_PRESENT (1 << 16)
#define _PG_ATTR_USER (1 << 17)
#define _PG_ATTR_RW (1 << 18)
#define _PG_ATTR_CACHED (1 << 19)
//#define _PG_ATTR_PARTITION (1<<20)

#ifdef CONFIG_MMU
extern void setup_vm_map(prtos_address_t *start_frame_area, prtos_s32_t *num_of_frames);
#endif
extern prtos_address_t setup_page_table(partition_t *p, prtos_address_t page_table, prtos_u_size_t size) __WARN_UNUSED_RESULT;
extern void vm_map_page(prtos_address_t p_addr, prtos_address_t v_addr, prtos_word_t flags);
extern void setup_ptd_level_1_table(prtos_word_t *ptd_level_1, kthread_t *k);
extern prtos_s32_t vm_map_user_page(partition_t *k, prtos_word_t *ptd_level_1, prtos_address_t p_addr, prtos_address_t v_addr, prtos_u32_t flags,
                                    prtos_address_t (*alloc)(struct prtos_conf_part *, prtos_u_size_t, prtos_u32_t, prtos_address_t *, prtos_s_size_t *),
                                    prtos_address_t *pool, prtos_s_size_t *pool_size) __WARN_UNUSED_RESULT;
extern prtos_u32_t vm_arch_attr_to_attr(prtos_u32_t entry);
extern prtos_u32_t vm_attr_to_arch_attr(prtos_u32_t entry);

#define ROUNDUP(addr, _ps) ((((~(addr)) + 1) & ((_ps)-1)) + (addr))
#define ROUNDDOWN(addr, _ps) ((addr) & ~((_ps)-1))
#define SIZE2PAGES(size) (((((~(size)) + 1) & (PAGE_SIZE - 1)) + (size)) >> PAGE_SHIFT)

#endif
