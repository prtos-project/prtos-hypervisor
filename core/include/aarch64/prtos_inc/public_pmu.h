/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2015 Oracle and/or its affiliates. All rights reserved.
 */

#ifndef __PRTOS_PUBLIC_PMU_H__
#define __PRTOS_PUBLIC_PMU_H__

#include "public_prtos.h"
#if defined(__i386__) || defined(__x86_64__)
#include "arch-x86/pmu.h"
#elif defined (__arm__) || defined (__aarch64__)
#include "public_arch-arm.h"
#elif defined (__powerpc64__)
#include "arch-ppc.h"
#elif defined(__riscv)
#include "arch-riscv.h"
#else
#error "Unsupported architecture"
#endif

#define PRTOSPMU_VER_MAJ    0
#define PRTOSPMU_VER_MIN    1

/*
 * ` enum neg_errnoval
 * ` HYPERVISOR_prtospmu_op(enum prtospmu_op cmd, struct prtospmu_params *args);
 *
 * @cmd  == PRTOSPMU_* (PMU operation)
 * @args == struct prtospmu_params
 */
/* ` enum prtospmu_op { */
#define PRTOSPMU_mode_get        0 /* Also used for getting PMU version */
#define PRTOSPMU_mode_set        1
#define PRTOSPMU_feature_get     2
#define PRTOSPMU_feature_set     3
#define PRTOSPMU_init            4
#define PRTOSPMU_finish          5
#define PRTOSPMU_lvtpc_set       6
#define PRTOSPMU_flush           7 /* Write cached MSR values to HW     */
/* ` } */

/* Parameters structure for HYPERVISOR_prtospmu_op call */
struct prtos_pmu_params {
    /* IN/OUT parameters */
    struct {
        uint32_t maj;
        uint32_t min;
    } version;
    uint64_t val;

    /* IN parameters */
    uint32_t vcpu;
    uint32_t pad;
};
typedef struct prtos_pmu_params prtos_pmu_params_t;
DEFINE_PRTOS_GUEST_HANDLE(prtos_pmu_params_t);

/* PMU modes:
 * - PRTOSPMU_MODE_OFF:   No PMU virtualization
 * - PRTOSPMU_MODE_SELF:  Guests can profile themselves
 * - PRTOSPMU_MODE_HV:    Guests can profile themselves, dom0 profiles
 *                      itself and PRTOS
 * - PRTOSPMU_MODE_ALL:   Only dom0 has access to VPMU and it profiles
 *                      everyone: itself, the hypervisor and the guests.
 */
#define PRTOSPMU_MODE_OFF           0
#define PRTOSPMU_MODE_SELF          (1<<0)
#define PRTOSPMU_MODE_HV            (1<<1)
#define PRTOSPMU_MODE_ALL           (1<<2)

/*
 * PMU features:
 * - PRTOSPMU_FEATURE_INTEL_BTS:  Intel BTS support (ignored on AMD)
 * - PRTOSPMU_FEATURE_IPC_ONLY:   Restrict PMCs to the most minimum set possible.
 *                              Instructions, cycles, and ref cycles. Can be
 *                              used to calculate instructions-per-cycle (IPC)
 *                              (ignored on AMD).
 * - PRTOSPMU_FEATURE_ARCH_ONLY:  Restrict PMCs to the Intel Pre-Defined
 *                              Architectural Performance Events exposed by
 *                              cpuid and listed in the Intel developer's manual
 *                              (ignored on AMD).
 */
#define PRTOSPMU_FEATURE_INTEL_BTS  (1<<0)
#define PRTOSPMU_FEATURE_IPC_ONLY   (1<<1)
#define PRTOSPMU_FEATURE_ARCH_ONLY  (1<<2)

/*
 * Shared PMU data between hypervisor and PV(H) domains.
 *
 * The hypervisor fills out this structure during PMU interrupt and sends an
 * interrupt to appropriate VCPU.
 * Architecture-independent fields of prtos_pmu_data are WO for the hypervisor
 * and RO for the guest but some fields in prtos_pmu_arch can be writable
 * by both the hypervisor and the guest (see arch-$arch/pmu.h).
 */
struct prtos_pmu_data {
    /* Interrupted VCPU */
    uint32_t vcpu_id;

    /*
     * Physical processor on which the interrupt occurred. On non-privileged
     * guests set to vcpu_id;
     */
    uint32_t pcpu_id;

    /*
     * Domain that was interrupted. On non-privileged guests set to DOMID_SELF.
     * On privileged guests can be DOMID_SELF, DOMID_PRTOS, or, when in
     * PRTOSPMU_MODE_ALL mode, domain ID of another domain.
     */
    domid_t  domain_id;

    uint8_t pad[6];

    /* Architecture-specific information */
    prtos_pmu_arch_t pmu;
};

#endif /* __PRTOS_PUBLIC_PMU_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
