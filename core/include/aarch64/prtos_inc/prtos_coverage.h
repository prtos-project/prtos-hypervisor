#ifndef _PRTOS_COV_H
#define _PRTOS_COV_H

#ifdef CONFIG_COVERAGE
#include <public_sysctl.h>
int sysctl_cov_op(struct prtos_sysctl_coverage_op *op);
#else
static inline int sysctl_cov_op(void *unused)
{
    return -EOPNOTSUPP;
}
#endif

#endif	/* _PRTOS_GCOV_H */
