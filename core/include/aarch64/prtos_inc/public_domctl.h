/* SPDX-License-Identifier: MIT */
/******************************************************************************
 * domctl.h
 *
 * Domain management operations. For use by node control stack.
 *
 * Copyright (c) 2002-2003, B Dragovic
 * Copyright (c) 2002-2006, K Fraser
 */

#ifndef __PRTOS_PUBLIC_DOMCTL_H__
#define __PRTOS_PUBLIC_DOMCTL_H__

#if !defined(__PRTOS_AARCH64__) && !defined(__PRTOS_TOOLS__)
#error "domctl operations are intended for use by node control tools only"
#endif

#include "public_prtos.h"
#include "public_event_channel.h"
#include "public_grant_table.h"
#include "public_hvm_save.h"
#include "public_memory.h"

#define PRTOS_DOMCTL_INTERFACE_VERSION 0x00000017

/*
 * NB. prtos_domctl.domain is an IN/OUT parameter for this operation.
 * If it is specified as an invalid value (0 or >= DOMID_FIRST_RESERVED),
 * an id is auto-allocated and returned.
 */
/* PRTOS_DOMCTL_createdomain */
struct prtos_domctl_createdomain {
    /* IN parameters */
    uint32_t ssidref;
    prtos_domain_handle_t handle;
 /* Is this an HVM guest (as opposed to a PV guest)? */
#define _PRTOS_DOMCTL_CDF_hvm           0
#define PRTOS_DOMCTL_CDF_hvm            (1U<<_PRTOS_DOMCTL_CDF_hvm)
 /* Use hardware-assisted paging if available? */
#define _PRTOS_DOMCTL_CDF_hap           1
#define PRTOS_DOMCTL_CDF_hap            (1U<<_PRTOS_DOMCTL_CDF_hap)
 /* Should domain memory integrity be verifed by tboot during Sx? */
#define _PRTOS_DOMCTL_CDF_s3_integrity  2
#define PRTOS_DOMCTL_CDF_s3_integrity   (1U<<_PRTOS_DOMCTL_CDF_s3_integrity)
 /* Disable out-of-sync shadow page tables? */
#define _PRTOS_DOMCTL_CDF_oos_off       3
#define PRTOS_DOMCTL_CDF_oos_off        (1U<<_PRTOS_DOMCTL_CDF_oos_off)
 /* Is this a prtosstore domain? */
#define _PRTOS_DOMCTL_CDF_xs_domain     4
#define PRTOS_DOMCTL_CDF_xs_domain      (1U<<_PRTOS_DOMCTL_CDF_xs_domain)
 /* Should this domain be permitted to use the IOMMU? */
#define _PRTOS_DOMCTL_CDF_iommu         5
#define PRTOS_DOMCTL_CDF_iommu          (1U<<_PRTOS_DOMCTL_CDF_iommu)
#define _PRTOS_DOMCTL_CDF_nested_virt   6
#define PRTOS_DOMCTL_CDF_nested_virt    (1U << _PRTOS_DOMCTL_CDF_nested_virt)
/* Should we expose the vPMU to the guest? */
#define PRTOS_DOMCTL_CDF_vpmu           (1U << 7)

/* Max PRTOS_DOMCTL_CDF_* constant.  Used for ABI checking. */
#define PRTOS_DOMCTL_CDF_MAX PRTOS_DOMCTL_CDF_vpmu

    uint32_t flags;

#define _PRTOS_DOMCTL_IOMMU_no_sharept  0
#define PRTOS_DOMCTL_IOMMU_no_sharept   (1U << _PRTOS_DOMCTL_IOMMU_no_sharept)

/* Max PRTOS_DOMCTL_IOMMU_* constant.  Used for ABI checking. */
#define PRTOS_DOMCTL_IOMMU_MAX PRTOS_DOMCTL_IOMMU_no_sharept

    uint32_t iommu_opts;

    /*
     * Various domain limits, which impact the quantity of resources
     * (global mapping space, prtosheap, etc) a guest may consume.  For
     * max_grant_frames and max_maptrack_frames, < 0 means "use the
     * default maximum value in the hypervisor".
     */
    uint32_t max_vcpus;
    uint32_t max_evtchn_port;
    int32_t max_grant_frames;
    int32_t max_maptrack_frames;

/* Grant version, use low 4 bits. */
#define PRTOS_DOMCTL_GRANT_version_mask    0xf
#define PRTOS_DOMCTL_GRANT_version(v)      ((v) & PRTOS_DOMCTL_GRANT_version_mask)

    uint32_t grant_opts;

/*
 * Enable altp2m mixed mode.
 *
 * Note that 'mixed' mode has not been evaluated for safety from a security
 * perspective.  Before using this mode in a security-critical environment,
 * each subop should be evaluated for safety, with unsafe subops blacklisted in
 * XSM.
 */
#define PRTOS_DOMCTL_ALTP2M_mixed      (1U)
/* Enable altp2m external mode. */
#define PRTOS_DOMCTL_ALTP2M_external   (2U)
/* Enable altp2m limited mode. */
#define PRTOS_DOMCTL_ALTP2M_limited    (3U)
/* Altp2m mode signaling uses bits [0, 1]. */
#define PRTOS_DOMCTL_ALTP2M_mode_mask  (0x3U)
#define PRTOS_DOMCTL_ALTP2M_mode(m)    ((m) & PRTOS_DOMCTL_ALTP2M_mode_mask)
    uint32_t altp2m_opts;

    /* Per-vCPU buffer size in bytes.  0 to disable. */
    uint32_t vmtrace_size;

    /* CPU pool to use; specify 0 or a specific existing pool */
    uint32_t cpupool_id;

    struct prtos_arch_domainconfig arch;
};

/* PRTOS_DOMCTL_getdomaininfo */
struct prtos_domctl_getdomaininfo {
    /* OUT variables. */
    domid_t  domain;              /* Also echoed in domctl.domain */
    uint16_t pad1;
 /* Domain is scheduled to die. */
#define _PRTOS_DOMINF_dying     0
#define PRTOS_DOMINF_dying      (1U<<_PRTOS_DOMINF_dying)
 /* Domain is an HVM guest (as opposed to a PV guest). */
#define _PRTOS_DOMINF_hvm_guest 1
#define PRTOS_DOMINF_hvm_guest  (1U<<_PRTOS_DOMINF_hvm_guest)
 /* The guest OS has shut down. */
#define _PRTOS_DOMINF_shutdown  2
#define PRTOS_DOMINF_shutdown   (1U<<_PRTOS_DOMINF_shutdown)
 /* Currently paused by control software. */
#define _PRTOS_DOMINF_paused    3
#define PRTOS_DOMINF_paused     (1U<<_PRTOS_DOMINF_paused)
 /* Currently blocked pending an event.     */
#define _PRTOS_DOMINF_blocked   4
#define PRTOS_DOMINF_blocked    (1U<<_PRTOS_DOMINF_blocked)
 /* Domain is currently running.            */
#define _PRTOS_DOMINF_running   5
#define PRTOS_DOMINF_running    (1U<<_PRTOS_DOMINF_running)
 /* Being debugged.  */
#define _PRTOS_DOMINF_debugged  6
#define PRTOS_DOMINF_debugged   (1U<<_PRTOS_DOMINF_debugged)
/* domain is a prtosstore domain */
#define _PRTOS_DOMINF_xs_domain 7
#define PRTOS_DOMINF_xs_domain  (1U<<_PRTOS_DOMINF_xs_domain)
/* domain has hardware assisted paging */
#define _PRTOS_DOMINF_hap       8
#define PRTOS_DOMINF_hap        (1U<<_PRTOS_DOMINF_hap)
 /* PRTOS_DOMINF_shutdown guest-supplied code.  */
#define PRTOS_DOMINF_shutdownmask 255
#define PRTOS_DOMINF_shutdownshift 16
    uint32_t flags;              /* PRTOS_DOMINF_* */
    uint64_aligned_t tot_pages;
    uint64_aligned_t max_pages;
    uint64_aligned_t outstanding_pages;
    uint64_aligned_t shr_pages;
    uint64_aligned_t paged_pages;
    uint64_aligned_t shared_info_frame; /* GMFN of shared_info struct */
    uint64_aligned_t cpu_time;
    uint32_t nr_online_vcpus;    /* Number of VCPUs currently online. */
#define PRTOS_INVALID_MAX_VCPU_ID (~0U) /* Domain has no vcpus? */
    uint32_t max_vcpu_id;        /* Maximum VCPUID in use by this domain. */
    uint32_t ssidref;
    prtos_domain_handle_t handle;
    uint32_t cpupool;
    uint8_t gpaddr_bits; /* Guest physical address space size. */
    uint8_t pad2[7];
    struct prtos_arch_domainconfig arch_config;
};
typedef struct prtos_domctl_getdomaininfo prtos_domctl_getdomaininfo_t;
DEFINE_PRTOS_GUEST_HANDLE(prtos_domctl_getdomaininfo_t);


/* PRTOS_DOMCTL_getpageframeinfo */

#define PRTOS_DOMCTL_PFINFO_LTAB_SHIFT 28
#define PRTOS_DOMCTL_PFINFO_NOTAB   (0x0U<<28)
#define PRTOS_DOMCTL_PFINFO_L1TAB   (0x1U<<28)
#define PRTOS_DOMCTL_PFINFO_L2TAB   (0x2U<<28)
#define PRTOS_DOMCTL_PFINFO_L3TAB   (0x3U<<28)
#define PRTOS_DOMCTL_PFINFO_L4TAB   (0x4U<<28)
#define PRTOS_DOMCTL_PFINFO_LTABTYPE_MASK (0x7U<<28)
#define PRTOS_DOMCTL_PFINFO_LPINTAB (0x1U<<31)
#define PRTOS_DOMCTL_PFINFO_XTAB    (0xfU<<28) /* invalid page */
#define PRTOS_DOMCTL_PFINFO_XALLOC  (0xeU<<28) /* allocate-only page */
#define PRTOS_DOMCTL_PFINFO_BROKEN  (0xdU<<28) /* broken page */
#define PRTOS_DOMCTL_PFINFO_LTAB_MASK (0xfU<<28)

/* PRTOS_DOMCTL_getpageframeinfo3 */
/*
 * Both value `num' and `array' may get modified by the hypercall to allow
 * preemption.
 */
struct prtos_domctl_getpageframeinfo3 {
    /* IN variables. */
    uint64_aligned_t num;
    /* IN/OUT variables. */
    PRTOS_GUEST_HANDLE_64(prtos_pfn_t) array;
};


/*
 * Control shadow pagetables operation
 */
/* PRTOS_DOMCTL_shadow_op */

/* Disable shadow mode. */
#define PRTOS_DOMCTL_SHADOW_OP_OFF         0

/* Enable shadow mode (mode contains ORed PRTOS_DOMCTL_SHADOW_ENABLE_* flags). */
#define PRTOS_DOMCTL_SHADOW_OP_ENABLE      32

/* Log-dirty bitmap operations. */
 /* Return the bitmap and clean internal copy for next round. */
#define PRTOS_DOMCTL_SHADOW_OP_CLEAN       11
 /* Return the bitmap but do not modify internal copy. */
#define PRTOS_DOMCTL_SHADOW_OP_PEEK        12

/*
 * Memory allocation accessors.  These APIs are broken and will be removed.
 * Use PRTOS_DOMCTL_{get,set}_paging_mempool_size instead.
 */
#define PRTOS_DOMCTL_SHADOW_OP_GET_ALLOCATION   30
#define PRTOS_DOMCTL_SHADOW_OP_SET_ALLOCATION   31

/* Legacy enable operations. */
 /* Equiv. to ENABLE with no mode flags. */
#define PRTOS_DOMCTL_SHADOW_OP_ENABLE_TEST       1
 /* Equiv. to ENABLE with mode flag ENABLE_LOG_DIRTY. */
#define PRTOS_DOMCTL_SHADOW_OP_ENABLE_LOGDIRTY   2
 /*
  * No longer supported, was equiv. to ENABLE with mode flags
  * ENABLE_REFCOUNT and ENABLE_TRANSLATE:
#define PRTOS_DOMCTL_SHADOW_OP_ENABLE_TRANSLATE  3
  */

/* Mode flags for PRTOS_DOMCTL_SHADOW_OP_ENABLE. */
 /*
  * Shadow pagetables are refcounted: guest does not use explicit mmu
  * operations nor write-protect its pagetables.
  */
#define PRTOS_DOMCTL_SHADOW_ENABLE_REFCOUNT  (1 << 1)
 /*
  * Log pages in a bitmap as they are dirtied.
  * Used for live relocation to determine which pages must be re-sent.
  */
#define PRTOS_DOMCTL_SHADOW_ENABLE_LOG_DIRTY (1 << 2)
 /*
  * Automatically translate GPFNs into MFNs.
  */
#define PRTOS_DOMCTL_SHADOW_ENABLE_TRANSLATE (1 << 3)
 /*
  * PRTOS does not steal virtual address space from the guest.
  * Requires HVM support.
  */
#define PRTOS_DOMCTL_SHADOW_ENABLE_EXTERNAL  (1 << 4)

/* Mode flags for PRTOS_DOMCTL_SHADOW_OP_{CLEAN,PEEK}. */
 /*
  * This is the final iteration: Requesting to include pages mapped
  * writably by the hypervisor in the dirty bitmap.
  */
#define PRTOS_DOMCTL_SHADOW_LOGDIRTY_FINAL   (1 << 0)

struct prtos_domctl_shadow_op_stats {
    uint32_t fault_count;
    uint32_t dirty_count;
};

struct prtos_domctl_shadow_op {
    /* IN variables. */
    uint32_t       op;       /* PRTOS_DOMCTL_SHADOW_OP_* */

    /* OP_ENABLE: PRTOS_DOMCTL_SHADOW_ENABLE_* */
    /* OP_PEAK / OP_CLEAN: PRTOS_DOMCTL_SHADOW_LOGDIRTY_* */
    uint32_t       mode;

    /* OP_GET_ALLOCATION / OP_SET_ALLOCATION */
    uint32_t       mb;       /* Shadow memory allocation in MB */

    /* OP_PEEK / OP_CLEAN */
    PRTOS_GUEST_HANDLE_64(uint8) dirty_bitmap;
    uint64_aligned_t pages; /* Size of buffer. Updated with actual size. */
    struct prtos_domctl_shadow_op_stats stats;
};


/* PRTOS_DOMCTL_max_mem */
struct prtos_domctl_max_mem {
    /* IN variables. */
    uint64_aligned_t max_memkb;
};


/* PRTOS_DOMCTL_setvcpucontext */
/* PRTOS_DOMCTL_getvcpucontext */
struct prtos_domctl_vcpucontext {
    uint32_t              vcpu;                  /* IN */
    PRTOS_GUEST_HANDLE_64(vcpu_guest_context_t) ctxt; /* IN/OUT */
};


/* PRTOS_DOMCTL_getvcpuinfo */
struct prtos_domctl_getvcpuinfo {
    /* IN variables. */
    uint32_t vcpu;
    /* OUT variables. */
    uint8_t  online;                  /* currently online (not hotplugged)? */
    uint8_t  blocked;                 /* blocked waiting for an event? */
    uint8_t  running;                 /* currently scheduled on its CPU? */
    uint64_aligned_t cpu_time;        /* total cpu time consumed (ns) */
    uint32_t cpu;                     /* current mapping   */
};


/* Get/set the NUMA node(s) with which the guest has affinity with. */
/* PRTOS_DOMCTL_setnodeaffinity */
/* PRTOS_DOMCTL_getnodeaffinity */
struct prtos_domctl_nodeaffinity {
    struct prtosctl_bitmap nodemap;/* IN */
};


/* Get/set which physical cpus a vcpu can execute on. */
/* PRTOS_DOMCTL_setvcpuaffinity */
/* PRTOS_DOMCTL_getvcpuaffinity */
struct prtos_domctl_vcpuaffinity {
    /* IN variables. */
    uint32_t  vcpu;
 /* Set/get the hard affinity for vcpu */
#define _PRTOS_VCPUAFFINITY_HARD  0
#define PRTOS_VCPUAFFINITY_HARD   (1U<<_PRTOS_VCPUAFFINITY_HARD)
 /* Set/get the soft affinity for vcpu */
#define _PRTOS_VCPUAFFINITY_SOFT  1
#define PRTOS_VCPUAFFINITY_SOFT   (1U<<_PRTOS_VCPUAFFINITY_SOFT)
 /* Undo SCHEDOP_pin_override */
#define _PRTOS_VCPUAFFINITY_FORCE 2
#define PRTOS_VCPUAFFINITY_FORCE  (1U<<_PRTOS_VCPUAFFINITY_FORCE)
    uint32_t flags;
    /*
     * IN/OUT variables.
     *
     * Both are IN/OUT for PRTOS_DOMCTL_setvcpuaffinity, in which case they
     * contain effective hard or/and soft affinity. That is, upon successful
     * return, cpumap_soft, contains the intersection of the soft affinity,
     * hard affinity and the cpupool's online CPUs for the domain (if
     * PRTOS_VCPUAFFINITY_SOFT was set in flags). cpumap_hard contains the
     * intersection between hard affinity and the cpupool's online CPUs (if
     * PRTOS_VCPUAFFINITY_HARD was set in flags).
     *
     * Both are OUT-only for PRTOS_DOMCTL_getvcpuaffinity, in which case they
     * contain the plain hard and/or soft affinity masks that were set during
     * previous successful calls to PRTOS_DOMCTL_setvcpuaffinity (or the
     * default values), without intersecting or altering them in any way.
     */
    struct prtosctl_bitmap cpumap_hard;
    struct prtosctl_bitmap cpumap_soft;
};


/*
 * PRTOS_DOMCTL_max_vcpus:
 *
 * The parameter passed to PRTOS_DOMCTL_max_vcpus must match the value passed to
 * PRTOS_DOMCTL_createdomain.  This hypercall is in the process of being removed
 * (once the failure paths in domain_create() have been improved), but is
 * still required in the short term to allocate the vcpus themselves.
 */
struct prtos_domctl_max_vcpus {
    uint32_t max;           /* maximum number of vcpus */
};


/* PRTOS_DOMCTL_scheduler_op */
/* Scheduler types. */
/* #define PRTOS_SCHEDULER_SEDF  4 (Removed) */
#define PRTOS_SCHEDULER_CREDIT   5
#define PRTOS_SCHEDULER_CREDIT2  6
#define PRTOS_SCHEDULER_ARINC653 7
#define PRTOS_SCHEDULER_RTDS     8
#define PRTOS_SCHEDULER_NULL     9

struct prtos_domctl_sched_credit {
    uint16_t weight;
    uint16_t cap;
};

struct prtos_domctl_sched_credit2 {
    uint16_t weight;
    uint16_t cap;
};

struct prtos_domctl_sched_rtds {
    uint32_t period;
    uint32_t budget;
/* Can this vCPU execute beyond its reserved amount of time? */
#define _PRTOS_DOMCTL_SCHEDRT_extra   0
#define PRTOS_DOMCTL_SCHEDRT_extra    (1U<<_PRTOS_DOMCTL_SCHEDRT_extra)
    uint32_t flags;
};

typedef struct prtos_domctl_schedparam_vcpu {
    union {
        struct prtos_domctl_sched_credit credit;
        struct prtos_domctl_sched_credit2 credit2;
        struct prtos_domctl_sched_rtds rtds;
    } u;
    uint32_t vcpuid;
} prtos_domctl_schedparam_vcpu_t;
DEFINE_PRTOS_GUEST_HANDLE(prtos_domctl_schedparam_vcpu_t);

/*
 * Set or get info?
 * For schedulers supporting per-vcpu settings (e.g., RTDS):
 *  PRTOS_DOMCTL_SCHEDOP_putinfo sets params for all vcpus;
 *  PRTOS_DOMCTL_SCHEDOP_getinfo gets default params;
 *  PRTOS_DOMCTL_SCHEDOP_put(get)vcpuinfo sets (gets) params of vcpus;
 *
 * For schedulers not supporting per-vcpu settings:
 *  PRTOS_DOMCTL_SCHEDOP_putinfo sets params for all vcpus;
 *  PRTOS_DOMCTL_SCHEDOP_getinfo gets domain-wise params;
 *  PRTOS_DOMCTL_SCHEDOP_put(get)vcpuinfo returns error;
 */
#define PRTOS_DOMCTL_SCHEDOP_putinfo 0
#define PRTOS_DOMCTL_SCHEDOP_getinfo 1
#define PRTOS_DOMCTL_SCHEDOP_putvcpuinfo 2
#define PRTOS_DOMCTL_SCHEDOP_getvcpuinfo 3
struct prtos_domctl_scheduler_op {
    uint32_t sched_id;  /* PRTOS_SCHEDULER_* */
    uint32_t cmd;       /* PRTOS_DOMCTL_SCHEDOP_* */
    /* IN/OUT */
    union {
        struct prtos_domctl_sched_credit credit;
        struct prtos_domctl_sched_credit2 credit2;
        struct prtos_domctl_sched_rtds rtds;
        struct {
            PRTOS_GUEST_HANDLE_64(prtos_domctl_schedparam_vcpu_t) vcpus;
            /*
             * IN: Number of elements in vcpus array.
             * OUT: Number of processed elements of vcpus array.
             */
            uint32_t nr_vcpus;
            uint32_t padding;
        } v;
    } u;
};


/* PRTOS_DOMCTL_setdomainhandle */
struct prtos_domctl_setdomainhandle {
    prtos_domain_handle_t handle;
};


/* PRTOS_DOMCTL_setdebugging */
struct prtos_domctl_setdebugging {
    uint8_t enable;
};


/* PRTOS_DOMCTL_irq_permission */
struct prtos_domctl_irq_permission {
    uint32_t pirq;
    uint8_t allow_access;    /* flag to specify enable/disable of IRQ access */
    uint8_t pad[3];
};


/* PRTOS_DOMCTL_iomem_permission */
struct prtos_domctl_iomem_permission {
    uint64_aligned_t first_mfn;/* first page (physical page number) in range */
    uint64_aligned_t nr_mfns;  /* number of pages in range (>0) */
    uint8_t  allow_access;     /* allow (!0) or deny (0) access to range? */
};


/* PRTOS_DOMCTL_ioport_permission */
struct prtos_domctl_ioport_permission {
    uint32_t first_port;              /* first port int range */
    uint32_t nr_ports;                /* size of port range */
    uint8_t  allow_access;            /* allow or deny access to range? */
};


/* PRTOS_DOMCTL_hypercall_init */
struct prtos_domctl_hypercall_init {
    uint64_aligned_t  gmfn;           /* GMFN to be initialised */
};


/* PRTOS_DOMCTL_settimeoffset */
struct prtos_domctl_settimeoffset {
    int64_aligned_t time_offset_seconds; /* applied to domain wallclock time */
};

/* PRTOS_DOMCTL_gethvmcontext */
/* PRTOS_DOMCTL_sethvmcontext */
struct prtos_domctl_hvmcontext {
    uint32_t size; /* IN/OUT: size of buffer / bytes filled */
    PRTOS_GUEST_HANDLE_64(uint8) buffer; /* IN/OUT: data, or call
                                        * gethvmcontext with NULL
                                        * buffer to get size req'd */
};


/* PRTOS_DOMCTL_set_address_size */
/* PRTOS_DOMCTL_get_address_size */
struct prtos_domctl_address_size {
    uint32_t size;
};


/* PRTOS_DOMCTL_sendtrigger */
#define PRTOS_DOMCTL_SENDTRIGGER_NMI    0
#define PRTOS_DOMCTL_SENDTRIGGER_RESET  1
#define PRTOS_DOMCTL_SENDTRIGGER_INIT   2
#define PRTOS_DOMCTL_SENDTRIGGER_POWER  3
#define PRTOS_DOMCTL_SENDTRIGGER_SLEEP  4
struct prtos_domctl_sendtrigger {
    uint32_t  trigger;  /* IN */
    uint32_t  vcpu;     /* IN */
};


/* Assign a device to a guest. Sets up IOMMU structures. */
/* PRTOS_DOMCTL_assign_device */
/*
 * PRTOS_DOMCTL_test_assign_device: Pass DOMID_INVALID to find out whether the
 * given device is assigned to any DomU at all. Pass a specific domain ID to
 * find out whether the given device can be assigned to that domain.
 */
/*
 * PRTOS_DOMCTL_deassign_device: The behavior of this DOMCTL differs
 * between the different type of device:
 *  - PCI device (PRTOS_DOMCTL_DEV_PCI) will be reassigned to DOM0
 *  - DT device (PRTOS_DOMCTL_DEV_DT) will left unassigned. DOM0
 *  will have to call PRTOS_DOMCTL_assign_device in order to use the
 *  device.
 */
#define PRTOS_DOMCTL_DEV_PCI      0
#define PRTOS_DOMCTL_DEV_DT       1
struct prtos_domctl_assign_device {
    /* IN */
    uint32_t dev;   /* PRTOS_DOMCTL_DEV_* */
    uint32_t flags;
#define PRTOS_DOMCTL_DEV_RDM_RELAXED      1 /* assign only */
    union {
        struct {
            uint32_t machine_sbdf;   /* machine PCI ID of assigned device */
        } pci;
        struct {
            uint32_t size; /* Length of the path */
            PRTOS_GUEST_HANDLE_64(char) path; /* path to the device tree node */
        } dt;
    } u;
};

/* Retrieve sibling devices information of machine_sbdf */
/* PRTOS_DOMCTL_get_device_group */
struct prtos_domctl_get_device_group {
    uint32_t  machine_sbdf;     /* IN */
    uint32_t  max_sdevs;        /* IN */
    uint32_t  num_sdevs;        /* OUT */
    PRTOS_GUEST_HANDLE_64(uint32)  sdev_array;   /* OUT */
};

/* Pass-through interrupts: bind real irq -> hvm devfn. */
/* PRTOS_DOMCTL_bind_pt_irq */
/* PRTOS_DOMCTL_unbind_pt_irq */
enum pt_irq_type {
    PT_IRQ_TYPE_PCI,
    PT_IRQ_TYPE_ISA,
    PT_IRQ_TYPE_MSI,
    PT_IRQ_TYPE_MSI_TRANSLATE,
    PT_IRQ_TYPE_SPI,    /* ARM: valid range 32-1019 */
};
struct prtos_domctl_bind_pt_irq {
    uint32_t machine_irq;
    uint32_t irq_type; /* enum pt_irq_type */

    union {
        struct {
            uint8_t isa_irq;
        } isa;
        struct {
            uint8_t bus;
            uint8_t device;
            uint8_t intx;
        } pci;
        struct {
            uint8_t gvec;
            uint32_t gflags;
#define PRTOS_DOMCTL_VMSI_X86_DEST_ID_MASK 0x0000ff
#define PRTOS_DOMCTL_VMSI_X86_RH_MASK      0x000100
#define PRTOS_DOMCTL_VMSI_X86_DM_MASK      0x000200
#define PRTOS_DOMCTL_VMSI_X86_DELIV_MASK   0x007000
#define PRTOS_DOMCTL_VMSI_X86_TRIG_MASK    0x008000
#define PRTOS_DOMCTL_VMSI_X86_UNMASKED     0x010000

            uint64_aligned_t gtable;
        } msi;
        struct {
            uint16_t spi;
        } spi;
    } u;
};


/* Bind machine I/O address range -> HVM address range. */
/* PRTOS_DOMCTL_memory_mapping */
/* Returns
   - zero     success, everything done
   - -E2BIG   passed in nr_mfns value too large for the implementation
   - positive partial success for the first <result> page frames (with
              <result> less than nr_mfns), requiring re-invocation by the
              caller after updating inputs
   - negative error; other than -E2BIG
*/
#define DPCI_ADD_MAPPING         1
#define DPCI_REMOVE_MAPPING      0
struct prtos_domctl_memory_mapping {
    uint64_aligned_t first_gfn; /* first page (hvm guest phys page) in range */
    uint64_aligned_t first_mfn; /* first page (machine page) in range */
    uint64_aligned_t nr_mfns;   /* number of pages in range (>0) */
    uint32_t add_mapping;       /* add or remove mapping */
    uint32_t padding;           /* padding for 64-bit aligned structure */
};


/* Bind machine I/O port range -> HVM I/O port range. */
/* PRTOS_DOMCTL_ioport_mapping */
struct prtos_domctl_ioport_mapping {
    uint32_t first_gport;     /* first guest IO port*/
    uint32_t first_mport;     /* first machine IO port */
    uint32_t nr_ports;        /* size of port range */
    uint32_t add_mapping;     /* add or remove mapping */
};


/*
 * Pin caching type of RAM space for x86 HVM domU.
 */
/* PRTOS_DOMCTL_pin_mem_cacheattr */
/* Caching types: these happen to be the same as x86 MTRR/PAT type codes. */
#define PRTOS_DOMCTL_MEM_CACHEATTR_UC  0
#define PRTOS_DOMCTL_MEM_CACHEATTR_WC  1
#define PRTOS_DOMCTL_MEM_CACHEATTR_WT  4
#define PRTOS_DOMCTL_MEM_CACHEATTR_WP  5
#define PRTOS_DOMCTL_MEM_CACHEATTR_WB  6
#define PRTOS_DOMCTL_MEM_CACHEATTR_UCM 7
#define PRTOS_DOMCTL_DELETE_MEM_CACHEATTR (~(uint32_t)0)


/* PRTOS_DOMCTL_set_ext_vcpucontext */
/* PRTOS_DOMCTL_get_ext_vcpucontext */
struct prtos_domctl_ext_vcpucontext {
    /* IN: VCPU that this call applies to. */
    uint32_t         vcpu;
    /*
     * SET: Size of struct (IN)
     * GET: Size of struct (OUT, up to 128 bytes)
     */
    uint32_t         size;
#if defined(__i386__) || defined(__x86_64__)
    /* SYSCALL from 32-bit mode and SYSENTER callback information. */
    /* NB. SYSCALL from 64-bit mode is contained in vcpu_guest_context_t */
    uint64_aligned_t syscall32_callback_eip;
    uint64_aligned_t sysenter_callback_eip;
    uint16_t         syscall32_callback_cs;
    uint16_t         sysenter_callback_cs;
    uint8_t          syscall32_disables_events;
    uint8_t          sysenter_disables_events;
#if defined(__GNUC__)
    union {
        uint64_aligned_t mcg_cap;
        struct hvm_vmce_vcpu vmce;
    };
#else
    struct hvm_vmce_vcpu vmce;
#endif
#endif
};

/*
 * Set the target domain for a domain
 */
/* PRTOS_DOMCTL_set_target */
struct prtos_domctl_set_target {
    domid_t target;
};

#if defined(__i386__) || defined(__x86_64__)
# define PRTOS_CPUID_INPUT_UNUSED  0xFFFFFFFF

/*
 * PRTOS_DOMCTL_{get,set}_cpu_policy (x86 specific)
 *
 * Query or set the CPUID and MSR policies for a specific domain.
 */
struct prtos_domctl_cpu_policy {
    uint32_t nr_leaves; /* IN/OUT: Number of leaves in/written to 'leaves' */
    uint32_t nr_msrs;   /* IN/OUT: Number of MSRs in/written to 'msrs' */
    PRTOS_GUEST_HANDLE_64(prtos_cpuid_leaf_t) leaves; /* IN/OUT */
    PRTOS_GUEST_HANDLE_64(prtos_msr_entry_t)  msrs;   /* IN/OUT */

    /*
     * OUT, set_policy only.  Written in some (but not all) error cases to
     * identify the CPUID leaf/subleaf and/or MSR which auditing objects to.
     */
    uint32_t err_leaf, err_subleaf, err_msr;
};
typedef struct prtos_domctl_cpu_policy prtos_domctl_cpu_policy_t;
DEFINE_PRTOS_GUEST_HANDLE(prtos_domctl_cpu_policy_t);
#endif

/*
 * Arranges that if the domain suspends (specifically, if it shuts
 * down with code SHUTDOWN_suspend), this event channel will be
 * notified.
 *
 * This is _instead of_ the usual notification to the global
 * VIRQ_DOM_EXC.  (In most systems that pirq is owned by prtosstored.)
 *
 * Only one subscription per domain is possible.  Last subscriber
 * wins; others are silently displaced.
 *
 * NB that contrary to the rather general name, it only applies to
 * domain shutdown with code suspend.  Shutdown for other reasons
 * (including crash), and domain death, are notified to VIRQ_DOM_EXC
 * regardless.
 */
/* PRTOS_DOMCTL_subscribe */
struct prtos_domctl_subscribe {
    uint32_t port; /* IN */
};

/* PRTOS_DOMCTL_debug_op */
#define PRTOS_DOMCTL_DEBUG_OP_SINGLE_STEP_OFF         0
#define PRTOS_DOMCTL_DEBUG_OP_SINGLE_STEP_ON          1
struct prtos_domctl_debug_op {
    uint32_t op;   /* IN */
    uint32_t vcpu; /* IN */
};

/*
 * Request a particular record from the HVM context
 */
/* PRTOS_DOMCTL_gethvmcontext_partial */
struct prtos_domctl_hvmcontext_partial {
    uint32_t type;                      /* IN: Type of record required */
    uint32_t instance;                  /* IN: Instance of that type */
    uint64_aligned_t bufsz;             /* IN: size of buffer */
    PRTOS_GUEST_HANDLE_64(uint8) buffer;  /* OUT: buffer to write record into */
};


/* PRTOS_DOMCTL_gettscinfo */
/* PRTOS_DOMCTL_settscinfo */
struct prtos_domctl_tsc_info {
    /* IN/OUT */
    uint32_t tsc_mode;
    uint32_t gtsc_khz;
    uint32_t incarnation;
    uint32_t pad;
    uint64_aligned_t elapsed_nsec;
};

/* PRTOS_DOMCTL_gdbsx_guestmemio      guest mem io */
struct prtos_domctl_gdbsx_memio {
    /* IN */
    uint64_aligned_t pgd3val;/* optional: init_mm.pgd[3] value */
    uint64_aligned_t gva;    /* guest virtual address */
    uint64_aligned_t uva;    /* user buffer virtual address */
    uint32_t         len;    /* number of bytes to read/write */
    uint8_t          gwr;    /* 0 = read from guest. 1 = write to guest */
    /* OUT */
    uint32_t         remain; /* bytes remaining to be copied */
};

/* PRTOS_DOMCTL_gdbsx_pausevcpu */
/* PRTOS_DOMCTL_gdbsx_unpausevcpu */
struct prtos_domctl_gdbsx_pauseunp_vcpu { /* pause/unpause a vcpu */
    uint32_t         vcpu;         /* which vcpu */
};

/* PRTOS_DOMCTL_gdbsx_domstatus */
struct prtos_domctl_gdbsx_domstatus {
    /* OUT */
    uint8_t          paused;     /* is the domain paused */
    uint32_t         vcpu_id;    /* any vcpu in an event? */
    uint32_t         vcpu_ev;    /* if yes, what event? */
};

/*
 * VM event operations
 */

/* PRTOS_DOMCTL_vm_event_op */

/*
 * There are currently three rings available for VM events:
 * sharing, monitor and paging. This hypercall allows one to
 * control these rings (enable/disable), as well as to signal
 * to the hypervisor to pull responses (resume) from the given
 * ring.
 */
#define PRTOS_VM_EVENT_ENABLE               0
#define PRTOS_VM_EVENT_DISABLE              1
#define PRTOS_VM_EVENT_RESUME               2
#define PRTOS_VM_EVENT_GET_VERSION          3

/*
 * Domain memory paging
 * Page memory in and out.
 * Domctl interface to set up and tear down the
 * pager<->hypervisor interface. Use PRTOSMEM_paging_op*
 * to perform per-page operations.
 *
 * The PRTOS_VM_EVENT_PAGING_ENABLE domctl returns several
 * non-standard error codes to indicate why paging could not be enabled:
 * ENODEV - host lacks HAP support (EPT/NPT) or HAP is disabled in guest
 * EMLINK - guest has iommu passthrough enabled
 * EXDEV  - guest has PoD enabled
 * EBUSY  - guest has or had paging enabled, ring buffer still active
 */
#define PRTOS_DOMCTL_VM_EVENT_OP_PAGING            1

/*
 * Monitor helper.
 *
 * As with paging, use the domctl for teardown/setup of the
 * helper<->hypervisor interface.
 *
 * The monitor interface can be used to register for various VM events. For
 * example, there are HVM hypercalls to set the per-page access permissions
 * of every page in a domain.  When one of these permissions--independent,
 * read, write, and execute--is violated, the VCPU is paused and a memory event
 * is sent with what happened. The memory event handler can then resume the
 * VCPU and redo the access with a PRTOS_VM_EVENT_RESUME option.
 *
 * See public/vm_event.h for the list of available events that can be
 * subscribed to via the monitor interface.
 *
 * The PRTOS_VM_EVENT_MONITOR_* domctls returns
 * non-standard error codes to indicate why access could not be enabled:
 * ENODEV - host lacks HAP support (EPT/NPT) or HAP is disabled in guest
 * EBUSY  - guest has or had access enabled, ring buffer still active
 *
 */
#define PRTOS_DOMCTL_VM_EVENT_OP_MONITOR           2

/*
 * Sharing ENOMEM helper.
 *
 * As with paging, use the domctl for teardown/setup of the
 * helper<->hypervisor interface.
 *
 * If setup, this ring is used to communicate failed allocations
 * in the unshare path. PRTOSMEM_sharing_op_resume is used to wake up
 * vcpus that could not unshare.
 *
 * Note that sharing can be turned on (as per the domctl below)
 * *without* this ring being setup.
 */
#define PRTOS_DOMCTL_VM_EVENT_OP_SHARING           3

/* Use for teardown/setup of helper<->hypervisor interface for paging,
 * access and sharing.*/
struct prtos_domctl_vm_event_op {
    uint32_t       op;           /* PRTOS_VM_EVENT_* */
    uint32_t       mode;         /* PRTOS_DOMCTL_VM_EVENT_OP_* */

    union {
        struct {
            uint32_t port;       /* OUT: event channel for ring */
        } enable;

        uint32_t version;
    } u;
};

/*
 * Memory sharing operations
 */
/* PRTOS_DOMCTL_mem_sharing_op.
 * The CONTROL sub-domctl is used for bringup/teardown. */
#define PRTOS_DOMCTL_MEM_SHARING_CONTROL          0

struct prtos_domctl_mem_sharing_op {
    uint8_t op; /* PRTOS_DOMCTL_MEM_SHARING_* */

    union {
        uint8_t enable;                   /* CONTROL */
    } u;
};

struct prtos_domctl_audit_p2m {
    /* OUT error counts */
    uint64_t orphans;
    uint64_t m2p_bad;
    uint64_t p2m_bad;
};

struct prtos_domctl_set_virq_handler {
    uint32_t virq; /* IN */
};

#if defined(__i386__) || defined(__x86_64__)
/* PRTOS_DOMCTL_setvcpuextstate */
/* PRTOS_DOMCTL_getvcpuextstate */
struct prtos_domctl_vcpuextstate {
    /* IN: VCPU that this call applies to. */
    uint32_t         vcpu;
    /*
     * SET: Ignored.
     * GET: xfeature support mask of struct (IN/OUT)
     * xfeature mask is served as identifications of the saving format
     * so that compatible CPUs can have a check on format to decide
     * whether it can restore.
     */
    uint64_aligned_t         xfeature_mask;
    /*
     * SET: Size of struct (IN)
     * GET: Size of struct (IN/OUT)
     */
    uint64_aligned_t         size;
    PRTOS_GUEST_HANDLE_64(uint64) buffer;
};
#endif

/* PRTOS_DOMCTL_set_access_required: sets whether a memory event listener
 * must be present to handle page access events: if false, the page
 * access will revert to full permissions if no one is listening;
 *  */
struct prtos_domctl_set_access_required {
    uint8_t access_required;
};

struct prtos_domctl_set_broken_page_p2m {
    uint64_aligned_t pfn;
};

/*
 * ARM: Clean and invalidate caches associated with given region of
 * guest memory.
 */
struct prtos_domctl_cacheflush {
    /* IN: page range to flush. */
    prtos_pfn_t start_pfn, nr_pfns;
};

/*
 * PRTOS_DOMCTL_get_paging_mempool_size / PRTOS_DOMCTL_set_paging_mempool_size.
 *
 * Get or set the paging memory pool size.  The size is in bytes.
 *
 * This is a dedicated pool of memory for PRTOS to use while managing the guest,
 * typically containing pagetables.  As such, there is an implementation
 * specific minimum granularity.
 *
 * The set operation can fail mid-way through the request (e.g. PRTOS running
 * out of memory, no free memory to reclaim from the pool, etc.).
 */
struct prtos_domctl_paging_mempool {
    uint64_aligned_t size; /* Size in bytes. */
};

#if defined(__i386__) || defined(__x86_64__)
struct prtos_domctl_vcpu_msr {
    uint32_t         index;
    uint32_t         reserved;
    uint64_aligned_t value;
};
typedef struct prtos_domctl_vcpu_msr prtos_domctl_vcpu_msr_t;
DEFINE_PRTOS_GUEST_HANDLE(prtos_domctl_vcpu_msr_t);

/*
 * PRTOS_DOMCTL_set_vcpu_msrs / PRTOS_DOMCTL_get_vcpu_msrs.
 *
 * Input:
 * - A NULL 'msrs' guest handle is a request for the maximum 'msr_count'.
 * - Otherwise, 'msr_count' is the number of entries in 'msrs'.
 *
 * Output for get:
 * - If 'msr_count' is less than the number PRTOS needs to write, -ENOBUFS shall
 *   be returned and 'msr_count' updated to reflect the intended number.
 * - On success, 'msr_count' shall indicate the number of MSRs written, which
 *   may be less than the maximum if some are not currently used by the vcpu.
 *
 * Output for set:
 * - If PRTOS encounters an error with a specific MSR, -EINVAL shall be returned
 *   and 'msr_count' shall be set to the offending index, to aid debugging.
 */
struct prtos_domctl_vcpu_msrs {
    uint32_t vcpu;                                   /* IN     */
    uint32_t msr_count;                              /* IN/OUT */
    PRTOS_GUEST_HANDLE_64(prtos_domctl_vcpu_msr_t) msrs; /* IN/OUT */
};
#endif

/* PRTOS_DOMCTL_setvnumainfo: specifies a virtual NUMA topology for the guest */
struct prtos_domctl_vnuma {
    /* IN: number of vNUMA nodes to setup. Shall be greater than 0 */
    uint32_t nr_vnodes;
    /* IN: number of memory ranges to setup */
    uint32_t nr_vmemranges;
    /*
     * IN: number of vCPUs of the domain (used as size of the vcpu_to_vnode
     * array declared below). Shall be equal to the domain's max_vcpus.
     */
    uint32_t nr_vcpus;
    uint32_t pad;                                  /* must be zero */

    /*
     * IN: array for specifying the distances of the vNUMA nodes
     * between each others. Shall have nr_vnodes*nr_vnodes elements.
     */
    PRTOS_GUEST_HANDLE_64(uint) vdistance;
    /*
     * IN: array for specifying to what vNUMA node each vCPU belongs.
     * Shall have nr_vcpus elements.
     */
    PRTOS_GUEST_HANDLE_64(uint) vcpu_to_vnode;
    /*
     * IN: array for specifying on what physical NUMA node each vNUMA
     * node is placed. Shall have nr_vnodes elements.
     */
    PRTOS_GUEST_HANDLE_64(uint) vnode_to_pnode;
    /*
     * IN: array for specifying the memory ranges. Shall have
     * nr_vmemranges elements.
     */
    PRTOS_GUEST_HANDLE_64(prtos_vmemrange_t) vmemrange;
};

struct prtos_domctl_psr_cmt_op {
#define PRTOS_DOMCTL_PSR_CMT_OP_DETACH         0
#define PRTOS_DOMCTL_PSR_CMT_OP_ATTACH         1
#define PRTOS_DOMCTL_PSR_CMT_OP_QUERY_RMID     2
    uint32_t cmd;
    uint32_t data;
};

/*  PRTOS_DOMCTL_MONITOR_*
 *
 * Enable/disable monitoring various VM events.
 * This domctl configures what events will be reported to helper apps
 * via the ring buffer "MONITOR". The ring has to be first enabled
 * with the domctl PRTOS_DOMCTL_VM_EVENT_OP_MONITOR.
 *
 * GET_CAPABILITIES can be used to determine which of these features is
 * available on a given platform.
 *
 * NOTICE: mem_access events are also delivered via the "MONITOR" ring buffer;
 * however, enabling/disabling those events is performed with the use of
 * memory_op hypercalls!
 */
#define PRTOS_DOMCTL_MONITOR_OP_ENABLE            0
#define PRTOS_DOMCTL_MONITOR_OP_DISABLE           1
#define PRTOS_DOMCTL_MONITOR_OP_GET_CAPABILITIES  2
#define PRTOS_DOMCTL_MONITOR_OP_EMULATE_EACH_REP  3
/*
 * Control register feature can result in guest-crashes when the monitor
 * subsystem is being turned off. User has to take special precautions
 * to ensure all vCPUs have resumed before it is safe to turn it off.
 */
#define PRTOS_DOMCTL_MONITOR_OP_CONTROL_REGISTERS 4

#define PRTOS_DOMCTL_MONITOR_EVENT_WRITE_CTRLREG         0
#define PRTOS_DOMCTL_MONITOR_EVENT_MOV_TO_MSR            1
#define PRTOS_DOMCTL_MONITOR_EVENT_SINGLESTEP            2
#define PRTOS_DOMCTL_MONITOR_EVENT_SOFTWARE_BREAKPOINT   3
#define PRTOS_DOMCTL_MONITOR_EVENT_GUEST_REQUEST         4
#define PRTOS_DOMCTL_MONITOR_EVENT_DEBUG_EXCEPTION       5
#define PRTOS_DOMCTL_MONITOR_EVENT_CPUID                 6
#define PRTOS_DOMCTL_MONITOR_EVENT_PRIVILEGED_CALL       7
#define PRTOS_DOMCTL_MONITOR_EVENT_INTERRUPT             8
#define PRTOS_DOMCTL_MONITOR_EVENT_DESC_ACCESS           9
#define PRTOS_DOMCTL_MONITOR_EVENT_EMUL_UNIMPLEMENTED    10
/* Enabled by default */
#define PRTOS_DOMCTL_MONITOR_EVENT_INGUEST_PAGEFAULT     11
#define PRTOS_DOMCTL_MONITOR_EVENT_VMEXIT                12
#define PRTOS_DOMCTL_MONITOR_EVENT_IO                    13

struct prtos_domctl_monitor_op {
    uint32_t op; /* PRTOS_DOMCTL_MONITOR_OP_* */

    /*
     * When used with ENABLE/DISABLE this has to be set to
     * the requested PRTOS_DOMCTL_MONITOR_EVENT_* value.
     * With GET_CAPABILITIES this field returns a bitmap of
     * events supported by the platform, in the format
     * (1 << PRTOS_DOMCTL_MONITOR_EVENT_*).
     */
    uint32_t event;

    /*
     * Further options when issuing PRTOS_DOMCTL_MONITOR_OP_ENABLE.
     */
    union {
        struct {
            /* Which control register */
            uint8_t index;
            /* Pause vCPU until response */
            uint8_t sync;
            /* Send event only on a change of value */
            uint8_t onchangeonly;
            /* Allignment padding */
            uint8_t pad1;
            uint32_t pad2;
            /*
             * Send event only if the changed bit in the control register
             * is not masked.
             */
            uint64_aligned_t bitmask;
        } mov_to_cr;

        struct {
            uint32_t msr;
            /* Send event only on a change of value */
            uint8_t onchangeonly;
        } mov_to_msr;

        struct {
            /* Pause vCPU until response */
            uint8_t sync;
            uint8_t allow_userspace;
        } guest_request;

        struct {
            /* Pause vCPU until response */
            uint8_t sync;
        } debug_exception;

        struct {
            /* Send event and don't process vmexit */
            uint8_t sync;
        } vmexit;
    } u;
};

struct prtos_domctl_psr_alloc {
#define PRTOS_DOMCTL_PSR_SET_L3_CBM     0
#define PRTOS_DOMCTL_PSR_GET_L3_CBM     1
#define PRTOS_DOMCTL_PSR_SET_L3_CODE    2
#define PRTOS_DOMCTL_PSR_SET_L3_DATA    3
#define PRTOS_DOMCTL_PSR_GET_L3_CODE    4
#define PRTOS_DOMCTL_PSR_GET_L3_DATA    5
#define PRTOS_DOMCTL_PSR_SET_L2_CBM     6
#define PRTOS_DOMCTL_PSR_GET_L2_CBM     7
#define PRTOS_DOMCTL_PSR_SET_MBA_THRTL  8
#define PRTOS_DOMCTL_PSR_GET_MBA_THRTL  9
    uint32_t cmd;       /* IN: PRTOS_DOMCTL_PSR_* */
    uint32_t target;    /* IN */
    uint64_t data;      /* IN/OUT */
};

/* PRTOS_DOMCTL_vuart_op */
struct prtos_domctl_vuart_op {
#define PRTOS_DOMCTL_VUART_OP_INIT  0
        uint32_t cmd;           /* PRTOS_DOMCTL_VUART_OP_* */
#define PRTOS_DOMCTL_VUART_TYPE_VPL011 0
        uint32_t type;          /* IN - type of vuart.
                                 *      Currently only vpl011 supported.
                                 */
        uint64_aligned_t  gfn;  /* IN - guest gfn to be used as a
                                 *      ring buffer.
                                 */
        domid_t console_domid;  /* IN - domid of domain running the
                                 *      backend console.
                                 */
        uint8_t pad[2];
        evtchn_port_t evtchn;   /* OUT - remote port of the event
                                 *       channel used for sending
                                 *       ring buffer events.
                                 */
};

/* PRTOS_DOMCTL_vmtrace_op: Perform VM tracing operations. */
struct prtos_domctl_vmtrace_op {
    uint32_t cmd;           /* IN */
    uint32_t vcpu;          /* IN */
    uint64_aligned_t key;   /* IN     - @cmd specific data. */
    uint64_aligned_t value; /* IN/OUT - @cmd specific data. */

    /*
     * General enable/disable of tracing.
     *
     * PRTOS_DOMCTL_vmtrace_reset_and_enable is provided as optimisation for
     * common usecases, which want to reset status and position information
     * when turning tracing back on.
     */
#define PRTOS_DOMCTL_vmtrace_enable             1
#define PRTOS_DOMCTL_vmtrace_disable            2
#define PRTOS_DOMCTL_vmtrace_reset_and_enable   3

    /* Obtain the current output position within the buffer.  Fills @value. */
#define PRTOS_DOMCTL_vmtrace_output_position    4

    /*
     * Get/Set platform specific configuration.
     *
     * For Intel Processor Trace, @key/@value are interpreted as MSR
     * reads/writes to MSR_RTIT_*, filtered to a safe subset.
     */
#define PRTOS_DOMCTL_vmtrace_get_option         5
#define PRTOS_DOMCTL_vmtrace_set_option         6
};
typedef struct prtos_domctl_vmtrace_op prtos_domctl_vmtrace_op_t;
DEFINE_PRTOS_GUEST_HANDLE(prtos_domctl_vmtrace_op_t);

#if defined(__arm__) || defined(__aarch64__)
struct prtos_domctl_dt_overlay {
    PRTOS_GUEST_HANDLE_64(const_void) overlay_fdt;  /* IN: overlay fdt. */
    uint32_t overlay_fdt_size;              /* IN: Overlay dtb size. */
#define PRTOS_DOMCTL_DT_OVERLAY_ATTACH                1
    uint8_t overlay_op;                     /* IN: Attach. */
    uint8_t pad[3];                         /* IN: Must be zero. */
};
#endif

struct prtos_domctl {
    uint32_t cmd;
#define PRTOS_DOMCTL_createdomain                   1
#define PRTOS_DOMCTL_destroydomain                  2
#define PRTOS_DOMCTL_pausedomain                    3
#define PRTOS_DOMCTL_unpausedomain                  4
#define PRTOS_DOMCTL_getdomaininfo                  5
/* #define PRTOS_DOMCTL_getmemlist                  6 Removed */
/* #define PRTOS_DOMCTL_getpageframeinfo            7 Obsolete - use getpageframeinfo3 */
/* #define PRTOS_DOMCTL_getpageframeinfo2           8 Obsolete - use getpageframeinfo3 */
#define PRTOS_DOMCTL_setvcpuaffinity                9
#define PRTOS_DOMCTL_shadow_op                     10
#define PRTOS_DOMCTL_max_mem                       11
#define PRTOS_DOMCTL_setvcpucontext                12
#define PRTOS_DOMCTL_getvcpucontext                13
#define PRTOS_DOMCTL_getvcpuinfo                   14
#define PRTOS_DOMCTL_max_vcpus                     15
#define PRTOS_DOMCTL_scheduler_op                  16
#define PRTOS_DOMCTL_setdomainhandle               17
#define PRTOS_DOMCTL_setdebugging                  18
#define PRTOS_DOMCTL_irq_permission                19
#define PRTOS_DOMCTL_iomem_permission              20
#define PRTOS_DOMCTL_ioport_permission             21
#define PRTOS_DOMCTL_hypercall_init                22
#ifdef __PRTOS_AARCH64__
/* #define PRTOS_DOMCTL_arch_setup                 23 Obsolete IA64 only */
#define PRTOS_DOMCTL_soft_reset_cont               23
#endif
#define PRTOS_DOMCTL_settimeoffset                 24
#define PRTOS_DOMCTL_getvcpuaffinity               25
#define PRTOS_DOMCTL_real_mode_area                26 /* Obsolete PPC only */
#define PRTOS_DOMCTL_resumedomain                  27
#define PRTOS_DOMCTL_sendtrigger                   28
#define PRTOS_DOMCTL_subscribe                     29
#define PRTOS_DOMCTL_gethvmcontext                 33
#define PRTOS_DOMCTL_sethvmcontext                 34
#define PRTOS_DOMCTL_set_address_size              35
#define PRTOS_DOMCTL_get_address_size              36
#define PRTOS_DOMCTL_assign_device                 37
#define PRTOS_DOMCTL_bind_pt_irq                   38
#define PRTOS_DOMCTL_memory_mapping                39
#define PRTOS_DOMCTL_ioport_mapping                40
/* #define PRTOS_DOMCTL_pin_mem_cacheattr          41 Removed - use dmop */
#define PRTOS_DOMCTL_set_ext_vcpucontext           42
#define PRTOS_DOMCTL_get_ext_vcpucontext           43
#define PRTOS_DOMCTL_set_opt_feature               44 /* Obsolete IA64 only */
#define PRTOS_DOMCTL_test_assign_device            45
#define PRTOS_DOMCTL_set_target                    46
#define PRTOS_DOMCTL_deassign_device               47
#define PRTOS_DOMCTL_unbind_pt_irq                 48
/* #define PRTOS_DOMCTL_set_cpuid                  49 - Obsolete - use set_cpu_policy */
#define PRTOS_DOMCTL_get_device_group              50
/* #define PRTOS_DOMCTL_set_machine_address_size   51 - Obsolete */
/* #define PRTOS_DOMCTL_get_machine_address_size   52 - Obsolete */
/* #define PRTOS_DOMCTL_suppress_spurious_page_faults 53 - Obsolete */
#define PRTOS_DOMCTL_debug_op                      54
#define PRTOS_DOMCTL_gethvmcontext_partial         55
#define PRTOS_DOMCTL_vm_event_op                   56
#define PRTOS_DOMCTL_mem_sharing_op                57
/* #define PRTOS_DOMCTL_disable_migrate            58 - Obsolete */
#define PRTOS_DOMCTL_gettscinfo                    59
#define PRTOS_DOMCTL_settscinfo                    60
#define PRTOS_DOMCTL_getpageframeinfo3             61
#define PRTOS_DOMCTL_setvcpuextstate               62
#define PRTOS_DOMCTL_getvcpuextstate               63
#define PRTOS_DOMCTL_set_access_required           64
#define PRTOS_DOMCTL_audit_p2m                     65
#define PRTOS_DOMCTL_set_virq_handler              66
#define PRTOS_DOMCTL_set_broken_page_p2m           67
#define PRTOS_DOMCTL_setnodeaffinity               68
#define PRTOS_DOMCTL_getnodeaffinity               69
/* #define PRTOS_DOMCTL_set_max_evtchn             70 - Moved into PRTOS_DOMCTL_createdomain */
#define PRTOS_DOMCTL_cacheflush                    71
#define PRTOS_DOMCTL_get_vcpu_msrs                 72
#define PRTOS_DOMCTL_set_vcpu_msrs                 73
#define PRTOS_DOMCTL_setvnumainfo                  74
#define PRTOS_DOMCTL_psr_cmt_op                    75
#define PRTOS_DOMCTL_monitor_op                    77
#define PRTOS_DOMCTL_psr_alloc                     78
#define PRTOS_DOMCTL_soft_reset                    79
/* #define PRTOS_DOMCTL_set_gnttab_limits          80 - Moved into PRTOS_DOMCTL_createdomain */
#define PRTOS_DOMCTL_vuart_op                      81
#define PRTOS_DOMCTL_get_cpu_policy                82
#define PRTOS_DOMCTL_set_cpu_policy                83
#define PRTOS_DOMCTL_vmtrace_op                    84
#define PRTOS_DOMCTL_get_paging_mempool_size       85
#define PRTOS_DOMCTL_set_paging_mempool_size       86
#define PRTOS_DOMCTL_dt_overlay                    87
#define PRTOS_DOMCTL_gdbsx_guestmemio            1000
#define PRTOS_DOMCTL_gdbsx_pausevcpu             1001
#define PRTOS_DOMCTL_gdbsx_unpausevcpu           1002
#define PRTOS_DOMCTL_gdbsx_domstatus             1003
    uint32_t interface_version; /* PRTOS_DOMCTL_INTERFACE_VERSION */
    domid_t  domain;
    uint16_t _pad[3];
    union {
        struct prtos_domctl_createdomain      createdomain;
        struct prtos_domctl_getdomaininfo     getdomaininfo;
        struct prtos_domctl_getpageframeinfo3 getpageframeinfo3;
        struct prtos_domctl_nodeaffinity      nodeaffinity;
        struct prtos_domctl_vcpuaffinity      vcpuaffinity;
        struct prtos_domctl_shadow_op         shadow_op;
        struct prtos_domctl_max_mem           max_mem;
        struct prtos_domctl_vcpucontext       vcpucontext;
        struct prtos_domctl_getvcpuinfo       getvcpuinfo;
        struct prtos_domctl_max_vcpus         max_vcpus;
        struct prtos_domctl_scheduler_op      scheduler_op;
        struct prtos_domctl_setdomainhandle   setdomainhandle;
        struct prtos_domctl_setdebugging      setdebugging;
        struct prtos_domctl_irq_permission    irq_permission;
        struct prtos_domctl_iomem_permission  iomem_permission;
        struct prtos_domctl_ioport_permission ioport_permission;
        struct prtos_domctl_hypercall_init    hypercall_init;
        struct prtos_domctl_settimeoffset     settimeoffset;
        struct prtos_domctl_tsc_info          tsc_info;
        struct prtos_domctl_hvmcontext        hvmcontext;
        struct prtos_domctl_hvmcontext_partial hvmcontext_partial;
        struct prtos_domctl_address_size      address_size;
        struct prtos_domctl_sendtrigger       sendtrigger;
        struct prtos_domctl_get_device_group  get_device_group;
        struct prtos_domctl_assign_device     assign_device;
        struct prtos_domctl_bind_pt_irq       bind_pt_irq;
        struct prtos_domctl_memory_mapping    memory_mapping;
        struct prtos_domctl_ioport_mapping    ioport_mapping;
        struct prtos_domctl_ext_vcpucontext   ext_vcpucontext;
        struct prtos_domctl_set_target        set_target;
        struct prtos_domctl_subscribe         subscribe;
        struct prtos_domctl_debug_op          debug_op;
        struct prtos_domctl_vm_event_op       vm_event_op;
        struct prtos_domctl_mem_sharing_op    mem_sharing_op;
#if defined(__i386__) || defined(__x86_64__)
        struct prtos_domctl_cpu_policy        cpu_policy;
        struct prtos_domctl_vcpuextstate      vcpuextstate;
        struct prtos_domctl_vcpu_msrs         vcpu_msrs;
#endif
        struct prtos_domctl_set_access_required access_required;
        struct prtos_domctl_audit_p2m         audit_p2m;
        struct prtos_domctl_set_virq_handler  set_virq_handler;
        struct prtos_domctl_gdbsx_memio       gdbsx_guest_memio;
        struct prtos_domctl_set_broken_page_p2m set_broken_page_p2m;
        struct prtos_domctl_cacheflush        cacheflush;
        struct prtos_domctl_gdbsx_pauseunp_vcpu gdbsx_pauseunp_vcpu;
        struct prtos_domctl_gdbsx_domstatus   gdbsx_domstatus;
        struct prtos_domctl_vnuma             vnuma;
        struct prtos_domctl_psr_cmt_op        psr_cmt_op;
        struct prtos_domctl_monitor_op        monitor_op;
        struct prtos_domctl_psr_alloc         psr_alloc;
        struct prtos_domctl_vuart_op          vuart_op;
        struct prtos_domctl_vmtrace_op        vmtrace_op;
        struct prtos_domctl_paging_mempool    paging_mempool;
#if defined(__arm__) || defined(__aarch64__)
        struct prtos_domctl_dt_overlay        dt_overlay;
#endif
        uint8_t                             pad[128];
    } u;
};
typedef struct prtos_domctl prtos_domctl_t;
DEFINE_PRTOS_GUEST_HANDLE(prtos_domctl_t);

#endif /* __PRTOS_PUBLIC_DOMCTL_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
