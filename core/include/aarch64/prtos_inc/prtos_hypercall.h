/******************************************************************************
 * hypercall.h
 */

#ifndef __PRTOS_HYPERCALL_H__
#define __PRTOS_HYPERCALL_H__

#include <prtos_types.h>
#include <prtos_time.h>
#include <public_prtos.h>
#include <public_domctl.h>
#include <public_sysctl.h>
#include <public_platform.h>
#include <public_event_channel.h>
#include <public_version.h>
#include <public_pmu.h>
#include <public_hvm_dm_op.h>
#ifdef CONFIG_COMPAT
#include <compat/platform.h>
#endif
#include <asm_hypercall.h>
#include <prtos_xsm_xsm.h>

/* Needs to be after asm/hypercall.h. */
#include <prtos_hypercall-defs.h>

extern long
arch_do_domctl(
    struct prtos_domctl *domctl, struct domain *d,
    PRTOS_GUEST_HANDLE_PARAM(prtos_domctl_t) u_domctl);

extern long
arch_do_sysctl(
    struct prtos_sysctl *sysctl,
    PRTOS_GUEST_HANDLE_PARAM(prtos_sysctl_t) u_sysctl);

extern long
pci_physdev_op(
    int cmd, PRTOS_GUEST_HANDLE_PARAM(void) arg);

/*
 * To allow safe resume of do_memory_op() after preemption, we need to know
 * at what point in the page list to resume. For this purpose I steal the
 * high-order bits of the @cmd parameter, which are otherwise unused and zero.
 *
 * Note that both of these values are effectively part of the ABI, even if
 * we don't need to make them a formal part of it: A guest suspended for
 * migration in the middle of a continuation would fail to work if resumed on
 * a hypervisor using different values.
 */
#define MEMOP_EXTENT_SHIFT 6 /* cmd[:6] == start_extent */
#define MEMOP_CMD_MASK     ((1 << MEMOP_EXTENT_SHIFT) - 1)

extern long
common_vcpu_op(int cmd,
    struct vcpu *v,
    PRTOS_GUEST_HANDLE_PARAM(void) arg);

void arch_get_prtos_caps(prtos_capabilities_info_t *info);

#endif /* __PRTOS_HYPERCALL_H__ */
