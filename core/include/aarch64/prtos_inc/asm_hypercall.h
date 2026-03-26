#ifndef __PRTOS_HYPERCALL_H__
#error "asm/hypercall.h should not be included directly - include prtos/hypercall.h instead"
#endif

#ifndef __ASM_ARM_HYPERCALL_H__
#define __ASM_ARM_HYPERCALL_H__

#include <public_domctl.h> /* for arch_do_domctl */

long subarch_do_domctl(struct prtos_domctl *domctl, struct domain *d,
                       PRTOS_GUEST_HANDLE_PARAM(prtos_domctl_t) u_domctl);

#endif /* __ASM_ARM_HYPERCALL_H__ */
/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
