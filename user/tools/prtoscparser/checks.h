/*
 * FILE: checks.h
 *
 * checks definitions
 *
 * www.prtos.org
 */

#ifndef _CHECKS_H_
#define _CHECKS_H_
#include "prtos_conf.h"

extern void check_all_mem_areas(struct prtos_conf_memory_area *mem_areas, struct prtos_conf_memory_area_line_number *mem_area_line_number, int len);
extern void check_all_memreg(void);
extern void check_hw_irq(int line, int line_number);
extern void check_port_name(int port, int partition);
extern void check_memory_region(int region);
extern int check_phys_mem_area(int mem_area);
extern void check_mem_area_per_partition(void);
extern void check_hpv_mem_area_flags(void);
extern void check_uart_id(int uart_id, int line);
extern void check_sched_cyclic_plan(struct prtos_conf_sched_cyclic_plan *plan, struct prtos_conf_sched_cyclic_plan_line_number *plan_line_number);
extern void check_cyclic_plan_partition_id(void);
extern void check_cyclic_plan_vcpuid(void);
extern void check_partition_name(char *name, int line);
extern void hm_hpv_is_action_permitted_on_event(int event, int action, int line);
extern void hm_part_is_action_permitted_on_event(int event, int action, int line);
extern void check_io_ports(void);
extern void hm_check_exist_maintenance_plan(void);
extern void check_ipvi_table(void);
extern void check_max_num_of_kthreads(void);
extern void check_part_not_alloc_to_more_than_a_cpu(void);

#endif
