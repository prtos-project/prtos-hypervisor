/*
 * FILE: hypercalls.c
 *
 * prtos system calls definitions
 *
 * www.prtos.org
 */

#include <prtoshypercalls.h>
#include <prtos_inc/hypercalls.h>
#include <hypervisor.h>

prtos_lazy_hcall2(update_page32, prtos_address_t, p_addr, prtos_u32_t, val);
prtos_lazy_hcall2(set_page_type, prtos_address_t, p_addr, prtos_u32_t, type);
