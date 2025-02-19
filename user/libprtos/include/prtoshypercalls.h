/*
 * FILE: prtoshypercalls.h
 *
 * Generic hypercall definition
 *
 * www.prtos.org
 */

#ifndef _LIB_PRTOS_HYPERCALLS_H_
#define _LIB_PRTOS_HYPERCALLS_H_

#ifdef _PRTOS_KERNEL_
#error Guest file, do not include.
#endif

#include <prtos_inc/config.h>
#include <prtos_inc/linkage.h>
#include <prtos_inc/hypercalls.h>
#include <prtos_inc/arch/irqs.h>
#include <prtos_inc/arch/linkage.h>
#include <arch/prtoshypercalls.h>

#ifndef __ASSEMBLY__
#include <prtos_inc/arch/arch_types.h>
#include <prtos_inc/objdir.h>

extern __stdcall prtos_s32_t prtos_get_gid_by_name(prtos_u8_t *name, prtos_u32_t entity);
extern __stdcall prtos_id_t prtos_get_vcpuid(void);

// Time management hypercalls
extern __stdcall prtos_s32_t prtos_get_time(prtos_u32_t clock_id, prtos_time_t *time);
extern __stdcall prtos_s32_t prtos_set_timer(prtos_u32_t clock_id, prtos_time_t abs_time, prtos_time_t interval);

// Partition status hypercalls
extern __stdcall prtos_s32_t prtos_suspend_partition(prtos_u32_t partition_id);
extern __stdcall prtos_s32_t prtos_resume_partition(prtos_u32_t partition_id);
extern __stdcall prtos_s32_t prtos_shutdown_partition(prtos_u32_t partition_id);
extern __stdcall prtos_s32_t prtos_reset_partition(prtos_u32_t partition_id, prtos_u32_t reset_mode, prtos_u32_t status);
extern __stdcall prtos_s32_t prtos_halt_partition(prtos_u32_t partition_id);
extern __stdcall prtos_s32_t prtos_idle_self(void);
extern __stdcall prtos_s32_t prtos_suspend_vcpu(prtos_u32_t vcpu_id);
extern __stdcall prtos_s32_t prtos_resume_vcpu(prtos_u32_t vcpu_id);
extern __stdcall prtos_s32_t prtos_reset_vcpu(prtos_u32_t vcpu_id, prtos_address_t ptd_level_1, prtos_address_t entry, prtos_u32_t status);
extern __stdcall prtos_s32_t prtos_halt_vcpu(prtos_u32_t vcpu_id);

// system status hypercalls
extern __stdcall prtos_s32_t prtos_halt_system(void);
extern __stdcall prtos_s32_t prtos_reset_system(prtos_u32_t reset_mode);

// Object related hypercalls
extern __stdcall prtos_s32_t prtos_read_object(prtos_obj_desc_t obj_desc, void *buffer, prtos_u32_t size, prtos_u32_t *flags);
extern __stdcall prtos_s32_t prtos_write_object(prtos_obj_desc_t obj_desc, void *buffer, prtos_u32_t size, prtos_u32_t *flags);
extern __stdcall prtos_s32_t prtos_seek_object(prtos_obj_desc_t obj_desc, prtos_u32_t offset, prtos_u32_t whence);
extern __stdcall prtos_s32_t prtos_ctrl_object(prtos_obj_desc_t obj_desc, prtos_u32_t cmd, void *arg);

// Paging hypercalls
extern __stdcall prtos_s32_t prtos_set_page_type(prtos_address_t p_addr, prtos_u32_t type);
extern __stdcall prtos_s32_t prtos_update_page32(prtos_address_t p_addr, prtos_u32_t val);
extern __stdcall prtos_s32_t prtos_invld_tlb(prtos_address_t v_addr);

extern __stdcall void prtos_lazy_set_page_type(prtos_address_t p_addr, prtos_u32_t type);
extern __stdcall void prtos_lazy_update_page32(prtos_address_t p_addr, prtos_u32_t val);
extern __stdcall void prtos_lazy_invld_tlb(prtos_address_t v_addr);

// Hw interrupt management
extern __stdcall prtos_s32_t prtos_override_trap_hndl(prtos_s32_t entry, struct trap_handler *);
extern __stdcall prtos_s32_t prtos_clear_irqmask(prtos_u32_t hw_irqs_mask, prtos_u32_t ext_irqs_to_mask);
extern __stdcall prtos_s32_t prtos_set_irqmask(prtos_u32_t hw_irqs_mask, prtos_u32_t ext_irqs_to_mask);
extern __stdcall prtos_s32_t prtos_set_irqpend(prtos_u32_t hw_irq_pend, prtos_u32_t ext_irq_pend);
extern __stdcall prtos_s32_t prtos_clear_irqpend(prtos_u32_t hw_irq_pend, prtos_u32_t ext_irq_pend);
extern __stdcall prtos_s32_t prtos_route_irq(prtos_u32_t type, prtos_u32_t irq, prtos_u16_t vector);

extern __stdcall prtos_s32_t prtos_raise_ipvi(prtos_u8_t no_ipvi);
extern __stdcall prtos_s32_t prtos_raise_partition_ipvi(prtos_u32_t partition_id, prtos_u8_t no_ipvi);

extern __stdcall prtos_s32_t prtos_flush_cache(prtos_u32_t cache);
extern __stdcall prtos_s32_t prtos_set_cache_state(prtos_u32_t cache);

extern __stdcall prtos_s32_t prtos_switch_sched_plan(prtos_u32_t new_plan_id, prtos_u32_t *current_plan_id);

// Deferred hypercalls
extern __stdcall prtos_s32_t prtos_multicall(void *start_addr, void *end_addr);

#endif

#endif
