/*
 * FILE: conv.h
 *
 * convertion definitions
 *
 * www.prtos.org
 */

#ifndef _CONV_H_
#define _CONV_H_

extern prtos_u32_t to_region_flags(char *s);
extern prtos_u32_t to_version(char *s);
extern prtos_u32_t to_u32(char *s, int base);
extern prtos_u32_t to_freq(char *s);
extern prtos_u32_t to_time(char *s);
extern prtos_u_size_t to_size(char *s);
extern prtos_u32_t to_partition_flags(char *s, int line);
extern prtos_u32_t to_phys_mem_area_flags(char *s, int line);
extern prtos_u32_t to_hm_action(char *s, int line);
extern prtos_u32_t to_hm_event(char *s, int line);
extern prtos_u32_t to_bitmask_trace_hyp(char *s, int line);
extern void to_hw_irq_lines(char *s, int line_number);
extern int to_yes_no_true_false(char *s, int line);
extern prtos_u32_t to_comm_port_direction(char *s, int line);
extern prtos_u32_t to_comm_port_type(char *s, int line);
extern void process_id_list(char *s, void (*call_back)(int, char *), int line);

#endif
