/******************************************************************************
 * prtosoprof.h
 * 
 * PRTOSoprof: PRTOSoprof enables performance profiling in PRTOS
 * 
 * Copyright (C) 2005 Hewlett-Packard Co.
 * written by Aravind Menon & Jose Renato Santos
 */

#ifndef __PRTOS_PROF_H__
#define __PRTOS_PROF_H__

#define PMU_OWNER_NONE          0
#define PMU_OWNER_PRTOSOPROF      1
#define PMU_OWNER_HVM           2

#ifdef CONFIG_PRTOSOPROF

#include <prtos_stdint.h>
#include <asm/prtosoprof.h>

struct domain;
struct vcpu;
struct cpu_user_regs;

int acquire_pmu_ownership(int pmu_ownership);
void release_pmu_ownership(int pmu_ownership);

int is_active(struct domain *d);
int is_passive(struct domain *d);
void free_prtosoprof_pages(struct domain *d);

int prtosoprof_add_trace(struct vcpu *, uint64_t pc, int mode);

void prtosoprof_log_event(struct vcpu *, const struct cpu_user_regs *,
                        uint64_t pc, int mode, int event);

#else
static inline int acquire_pmu_ownership(int pmu_ownership)
{
    return 1;
}

static inline void release_pmu_ownership(int pmu_ownership)
{
}
#endif /* CONFIG_PRTOSOPROF */

#endif  /* __PRTOS_AARCH64__XENOPROF_H__ */
