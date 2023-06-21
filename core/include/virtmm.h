/*
 * FILE: virtmm.h
 *
 * Virtual memory manager
 *
 * www.prtos.org
 *
 */

#ifndef _PRTOS_VIRTMM_H_
#define _PRTOS_VIRTMM_H_

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

extern void setup_virt_mm(void);
extern prtos_address_t vmm_alloc(prtos_s32_t npag);
extern void vmm_print_map(void);
extern prtos_s32_t vmm_is_free(prtos_address_t v_addr);
extern prtos_s32_t vmm_get_num_of_free_frames(void);

#endif
