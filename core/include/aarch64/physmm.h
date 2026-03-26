/*
 * FILE: physmm.h
 *
 * Physical memory manager
 *
 * www.prtos.org
 */

#ifndef _PRTOS_ARCH_PHYSMM_H_
#define _PRTOS_ARCH_PHYSMM_H_

#define LOW_MEMORY_START_ADDR 0x0
#define LOW_MEMORY_END_ADDR 0x100000

extern prtos_u8_t read_by_pass_mmu_byte(void *p_addr);
extern prtos_u32_t read_by_pass_mmu_word(void *p_addr);
extern void write_by_pass_mmu_word(void *p_addr, prtos_u32_t val);

#endif
