/* SPDX-License-Identifier: MIT */
/******************************************************************************
 * memory.h
 *
 * Memory reservation and information.
 *
 * Copyright (c) 2005, Keir Fraser <keir@xensource.com>
 */

#ifndef __PRTOS_PUBLIC_MEMORY_H__
#define __PRTOS_PUBLIC_MEMORY_H__

#include "public_prtos.h"
#include "public_physdev.h"

/*
 * Increase or decrease the specified domain's memory reservation. Returns the
 * number of extents successfully allocated or freed.
 * arg == addr of struct prtos_memory_reservation.
 */
#define PRTOSMEM_increase_reservation 0
#define PRTOSMEM_decrease_reservation 1
#define PRTOSMEM_populate_physmap     6

#if __XEN_INTERFACE_VERSION__ >= 0x00030209
/*
 * Maximum # bits addressable by the user of the allocated region (e.g., I/O
 * devices often have a 32-bit limitation even in 64-bit systems). If zero
 * then the user has no addressing restriction. This field is not used by
 * PRTOSMEM_decrease_reservation.
 */
#define PRTOSMEMF_address_bits(x)     (x)
#define PRTOSMEMF_get_address_bits(x) ((x) & 0xffu)
/* NUMA node to allocate from. */
#define PRTOSMEMF_node(x)     (((x) + 1) << 8)
#define PRTOSMEMF_get_node(x) ((((x) >> 8) - 1) & 0xffu)
/* Flag to populate physmap with populate-on-demand entries */
#define PRTOSMEMF_populate_on_demand (1<<16)
/* Flag to request allocation only from the node specified */
#define PRTOSMEMF_exact_node_request  (1<<17)
#define PRTOSMEMF_exact_node(n) (PRTOSMEMF_node(n) | PRTOSMEMF_exact_node_request)
/* Flag to indicate the node specified is virtual node */
#define PRTOSMEMF_vnode  (1<<18)
#endif

struct prtos_memory_reservation {

    /*
     * PRTOSMEM_increase_reservation:
     *   OUT: MFN (*not* GMFN) bases of extents that were allocated
     * PRTOSMEM_decrease_reservation:
     *   IN:  GMFN bases of extents to free
     * PRTOSMEM_populate_physmap:
     *   IN:  GPFN bases of extents to populate with memory
     *   OUT: GMFN bases of extents that were allocated
     *   (NB. This command also updates the mach_to_phys translation table)
     * PRTOSMEM_claim_pages:
     *   IN: must be zero
     */
    PRTOS_GUEST_HANDLE(prtos_pfn_t) extent_start;

    /* Number of extents, and size/alignment of each (2^extent_order pages). */
    prtos_ulong_t    nr_extents;
    unsigned int   extent_order;

#if __XEN_INTERFACE_VERSION__ >= 0x00030209
    /* XENMEMF flags. */
    unsigned int   mem_flags;
#else
    unsigned int   address_bits;
#endif

    /*
     * Domain whose reservation is being changed.
     * Unprivileged domains can specify only DOMID_SELF.
     */
    domid_t        domid;
};
typedef struct prtos_memory_reservation prtos_memory_reservation_t;
DEFINE_PRTOS_GUEST_HANDLE(prtos_memory_reservation_t);

/*
 * An atomic exchange of memory pages. If return code is zero then
 * @out.extent_list provides GMFNs of the newly-allocated memory.
 * Returns zero on complete success, otherwise a negative error code.
 * On complete success then always @nr_exchanged == @in.nr_extents.
 * On partial success @nr_exchanged indicates how much work was done.
 *
 * Note that only PV guests can use this operation.
 */
#define PRTOSMEM_exchange             11
struct prtos_memory_exchange {
    /*
     * [IN] Details of memory extents to be exchanged (GMFN bases).
     * Note that @in.address_bits is ignored and unused.
     */
    struct prtos_memory_reservation in;

    /*
     * [IN/OUT] Details of new memory extents.
     * We require that:
     *  1. @in.domid == @out.domid
     *  2. @in.nr_extents  << @in.extent_order ==
     *     @out.nr_extents << @out.extent_order
     *  3. @in.extent_start and @out.extent_start lists must not overlap
     *  4. @out.extent_start lists GPFN bases to be populated
     *  5. @out.extent_start is overwritten with allocated GMFN bases
     */
    struct prtos_memory_reservation out;

    /*
     * [OUT] Number of input extents that were successfully exchanged:
     *  1. The first @nr_exchanged input extents were successfully
     *     deallocated.
     *  2. The corresponding first entries in the output extent list correctly
     *     indicate the GMFNs that were successfully exchanged.
     *  3. All other input and output extents are untouched.
     *  4. If not all input exents are exchanged then the return code of this
     *     command will be non-zero.
     *  5. THIS FIELD MUST BE INITIALISED TO ZERO BY THE CALLER!
     */
    prtos_ulong_t nr_exchanged;
};
typedef struct prtos_memory_exchange prtos_memory_exchange_t;
DEFINE_PRTOS_GUEST_HANDLE(prtos_memory_exchange_t);

/*
 * Returns the maximum machine frame number of mapped RAM in this system.
 * This command always succeeds (it never returns an error code).
 * arg == NULL.
 */
#define PRTOSMEM_maximum_ram_page     2

struct prtos_memory_domain {
    /* [IN] Domain information is being queried for. */
    domid_t domid;
};

/*
 * Returns the current or maximum memory reservation, in pages, of the
 * specified domain (may be DOMID_SELF). Returns -ve errcode on failure.
 * arg == addr of struct prtos_memory_domain.
 */
#define PRTOSMEM_current_reservation  3
#define PRTOSMEM_maximum_reservation  4

/*
 * Returns the maximum GFN in use by the specified domain (may be DOMID_SELF).
 * Returns -ve errcode on failure.
 * arg == addr of struct prtos_memory_domain.
 */
#define PRTOSMEM_maximum_gpfn         14

/*
 * Returns a list of MFN bases of 2MB extents comprising the machine_to_phys
 * mapping table. Architectures which do not have a m2p table do not implement
 * this command.
 * arg == addr of prtos_machphys_mfn_list_t.
 */
#define PRTOSMEM_machphys_mfn_list    5
struct prtos_machphys_mfn_list {
    /*
     * Size of the 'extent_start' array. Fewer entries will be filled if the
     * machphys table is smaller than max_extents * 2MB.
     */
    unsigned int max_extents;

    /*
     * Pointer to buffer to fill with list of extent starts. If there are
     * any large discontiguities in the machine address space, 2MB gaps in
     * the machphys table will be represented by an MFN base of zero.
     */
    PRTOS_GUEST_HANDLE(prtos_pfn_t) extent_start;

    /*
     * Number of extents written to the above array. This will be smaller
     * than 'max_extents' if the machphys table is smaller than max_e * 2MB.
     */
    unsigned int nr_extents;
};
typedef struct prtos_machphys_mfn_list prtos_machphys_mfn_list_t;
DEFINE_PRTOS_GUEST_HANDLE(prtos_machphys_mfn_list_t);

/*
 * For a compat caller, this is identical to PRTOSMEM_machphys_mfn_list.
 *
 * For a non compat caller, this functions similarly to
 * PRTOSMEM_machphys_mfn_list, but returns the mfns making up the compatibility
 * m2p table.
 */
#define PRTOSMEM_machphys_compat_mfn_list     25

/*
 * Returns the location in virtual address space of the machine_to_phys
 * mapping table. Architectures which do not have a m2p table, or which do not
 * map it by default into guest address space, do not implement this command.
 * arg == addr of prtos_machphys_mapping_t.
 */
#define PRTOSMEM_machphys_mapping     12
struct prtos_machphys_mapping {
    prtos_ulong_t v_start, v_end; /* Start and end virtual addresses.   */
    prtos_ulong_t max_mfn;        /* Maximum MFN that can be looked up. */
};
typedef struct prtos_machphys_mapping prtos_machphys_mapping_t;
DEFINE_PRTOS_GUEST_HANDLE(prtos_machphys_mapping_t);

/* Source mapping space. */
/* ` enum phys_map_space { */
#define PRTOSMAPSPACE_shared_info  0 /* shared info page */
#define PRTOSMAPSPACE_grant_table  1 /* grant table page */
#define PRTOSMAPSPACE_gmfn         2 /* GMFN */
#define PRTOSMAPSPACE_gmfn_range   3 /* GMFN range, PRTOSMEM_add_to_physmap only. */
#define PRTOSMAPSPACE_gmfn_foreign 4 /* GMFN from another dom,
                                    * PRTOSMEM_add_to_physmap_batch only. */
#define PRTOSMAPSPACE_dev_mmio     5 /* device mmio region
                                      ARM only; the region is mapped in
                                      Stage-2 using the Normal Memory
                                      Inner/Outer Write-Back Cacheable
                                      memory attribute. */
/* ` } */

/*
 * Sets the GPFN at which a particular page appears in the specified guest's
 * physical address space (translated guests only).
 * arg == addr of prtos_add_to_physmap_t.
 */
#define PRTOSMEM_add_to_physmap      7
struct prtos_add_to_physmap {
    /* Which domain to change the mapping for. */
    domid_t domid;

    /* Number of pages to go through for gmfn_range */
    uint16_t    size;

    unsigned int space; /* => enum phys_map_space */

#define PRTOSMAPIDX_grant_table_status 0x80000000U

    /* Index into space being mapped. */
    prtos_ulong_t idx;

    /* GPFN in domid where the source mapping page should appear. */
    prtos_pfn_t     gpfn;
};
typedef struct prtos_add_to_physmap prtos_add_to_physmap_t;
DEFINE_PRTOS_GUEST_HANDLE(prtos_add_to_physmap_t);

/* A batched version of add_to_physmap. */
#define PRTOSMEM_add_to_physmap_batch 23
struct prtos_add_to_physmap_batch {
    /* IN */
    /* Which domain to change the mapping for. */
    domid_t domid;
    uint16_t space; /* => enum phys_map_space */

    /* Number of pages to go through */
    uint16_t size;

#if __XEN_INTERFACE_VERSION__ < 0x00040700
    domid_t foreign_domid; /* IFF gmfn_foreign. Should be 0 for other spaces. */
#else
    union prtos_add_to_physmap_batch_extra {
        domid_t foreign_domid; /* gmfn_foreign */
        uint16_t res0;  /* All the other spaces. Should be 0 */
    } u;
#endif

    /* Indexes into space being mapped. */
    PRTOS_GUEST_HANDLE(prtos_ulong_t) idxs;

    /* GPFN in domid where the source mapping page should appear. */
    PRTOS_GUEST_HANDLE(prtos_pfn_t) gpfns;

    /* OUT */

    /* Per index error code. */
    PRTOS_GUEST_HANDLE(int) errs;
};
typedef struct prtos_add_to_physmap_batch prtos_add_to_physmap_batch_t;
DEFINE_PRTOS_GUEST_HANDLE(prtos_add_to_physmap_batch_t);

#if __XEN_INTERFACE_VERSION__ < 0x00040400
#define PRTOSMEM_add_to_physmap_range PRTOSMEM_add_to_physmap_batch
#define prtos_add_to_physmap_range prtos_add_to_physmap_batch
typedef struct prtos_add_to_physmap_batch prtos_add_to_physmap_range_t;
DEFINE_PRTOS_GUEST_HANDLE(prtos_add_to_physmap_range_t);
#endif

/*
 * Unmaps the page appearing at a particular GPFN from the specified guest's
 * physical address space (translated guests only).
 * arg == addr of prtos_remove_from_physmap_t.
 */
#define PRTOSMEM_remove_from_physmap      15
struct prtos_remove_from_physmap {
    /* Which domain to change the mapping for. */
    domid_t domid;

    /* GPFN of the current mapping of the page. */
    prtos_pfn_t     gpfn;
};
typedef struct prtos_remove_from_physmap prtos_remove_from_physmap_t;
DEFINE_PRTOS_GUEST_HANDLE(prtos_remove_from_physmap_t);

/*** REMOVED ***/
/*#define PRTOSMEM_translate_gpfn_list  8*/

/*
 * Returns the pseudo-physical memory map as it was when the domain
 * was started (specified by PRTOSMEM_set_memory_map).
 * arg == addr of prtos_memory_map_t.
 */
#define PRTOSMEM_memory_map           9
struct prtos_memory_map {
    /*
     * On call the number of entries which can be stored in buffer. On
     * return the number of entries which have been stored in
     * buffer.
     */
    unsigned int nr_entries;

    /*
     * Entries in the buffer are in the same format as returned by the
     * BIOS INT 0x15 EAX=0xE820 call.
     */
    PRTOS_GUEST_HANDLE(void) buffer;
};
typedef struct prtos_memory_map prtos_memory_map_t;
DEFINE_PRTOS_GUEST_HANDLE(prtos_memory_map_t);

/*
 * Returns the real physical memory map. Passes the same structure as
 * PRTOSMEM_memory_map.
 * Specifying buffer as NULL will return the number of entries required
 * to store the complete memory map.
 * arg == addr of prtos_memory_map_t.
 */
#define PRTOSMEM_machine_memory_map   10

/*
 * Set the pseudo-physical memory map of a domain, as returned by
 * PRTOSMEM_memory_map.
 * arg == addr of prtos_foreign_memory_map_t.
 */
#define PRTOSMEM_set_memory_map       13
struct prtos_foreign_memory_map {
    domid_t domid;
    struct prtos_memory_map map;
};
typedef struct prtos_foreign_memory_map prtos_foreign_memory_map_t;
DEFINE_PRTOS_GUEST_HANDLE(prtos_foreign_memory_map_t);

#define PRTOSMEM_set_pod_target       16
#define PRTOSMEM_get_pod_target       17
struct prtos_pod_target {
    /* IN */
    uint64_t target_pages;
    /* OUT */
    uint64_t tot_pages;
    uint64_t pod_cache_pages;
    uint64_t pod_entries;
    /* IN */
    domid_t domid;
};
typedef struct prtos_pod_target prtos_pod_target_t;

#if defined(__PRTOS_AARCH64__) || defined(__PRTOS_TOOLS__)

#ifndef uint64_aligned_t
#define uint64_aligned_t uint64_t
#endif

/*
 * Get the number of MFNs saved through memory sharing.
 * The call never fails.
 */
#define PRTOSMEM_get_sharing_freed_pages    18
#define PRTOSMEM_get_sharing_shared_pages   19

#define PRTOSMEM_paging_op                    20
#define PRTOSMEM_paging_op_nominate           0
#define PRTOSMEM_paging_op_evict              1
#define PRTOSMEM_paging_op_prep               2

struct prtos_mem_paging_op {
    uint8_t     op;         /* PRTOSMEM_paging_op_* */
    domid_t     domain;

    /* IN: (PRTOSMEM_paging_op_prep) buffer to immediately fill page from */
    PRTOS_GUEST_HANDLE_64(const_uint8) buffer;
    /* IN:  gfn of page being operated on */
    uint64_aligned_t    gfn;
};
typedef struct prtos_mem_paging_op prtos_mem_paging_op_t;
DEFINE_PRTOS_GUEST_HANDLE(prtos_mem_paging_op_t);

#define PRTOSMEM_access_op                    21
#define PRTOSMEM_access_op_set_access         0
#define PRTOSMEM_access_op_get_access         1
/*
 * PRTOSMEM_access_op_enable_emulate and PRTOSMEM_access_op_disable_emulate are
 * currently unused, but since they have been in use please do not reuse them.
 *
 * #define PRTOSMEM_access_op_enable_emulate     2
 * #define PRTOSMEM_access_op_disable_emulate    3
 */
#define PRTOSMEM_access_op_set_access_multi   4

typedef enum {
    PRTOSMEM_access_n,
    PRTOSMEM_access_r,
    PRTOSMEM_access_w,
    PRTOSMEM_access_rw,
    PRTOSMEM_access_x,
    PRTOSMEM_access_rx,
    PRTOSMEM_access_wx,
    PRTOSMEM_access_rwx,
    /*
     * Page starts off as r-x, but automatically
     * change to r-w on a write
     */
    PRTOSMEM_access_rx2rw,
    /*
     * Log access: starts off as n, automatically
     * goes to rwx, generating an event without
     * pausing the vcpu
     */
    PRTOSMEM_access_n2rwx,
    /* Take the domain default */
    PRTOSMEM_access_default
} prtosmem_access_t;

struct prtos_mem_access_op {
    /* PRTOSMEM_access_op_* */
    uint8_t op;
    /* prtosmem_access_t */
    uint8_t access;
    domid_t domid;
    /*
     * Number of pages for set op (or size of pfn_list for
     * PRTOSMEM_access_op_set_access_multi)
     * Ignored on setting default access and other ops
     */
    uint32_t nr;
    /*
     * First pfn for set op
     * pfn for get op
     * ~0ull is used to set and get the default access for pages
     */
    uint64_aligned_t pfn;
    /*
     * List of pfns to set access for
     * Used only with PRTOSMEM_access_op_set_access_multi
     */
    PRTOS_GUEST_HANDLE(const_uint64) pfn_list;
    /*
     * Corresponding list of access settings for pfn_list
     * Used only with PRTOSMEM_access_op_set_access_multi
     */
    PRTOS_GUEST_HANDLE(const_uint8) access_list;
};
typedef struct prtos_mem_access_op prtos_mem_access_op_t;
DEFINE_PRTOS_GUEST_HANDLE(prtos_mem_access_op_t);

#define PRTOSMEM_sharing_op                   22
#define PRTOSMEM_sharing_op_nominate_gfn      0
#define PRTOSMEM_sharing_op_nominate_gref     1
#define PRTOSMEM_sharing_op_share             2
#define PRTOSMEM_sharing_op_debug_gfn         3
#define PRTOSMEM_sharing_op_debug_mfn         4
#define PRTOSMEM_sharing_op_debug_gref        5
#define PRTOSMEM_sharing_op_add_physmap       6
#define PRTOSMEM_sharing_op_audit             7
#define PRTOSMEM_sharing_op_range_share       8
#define PRTOSMEM_sharing_op_fork              9
#define PRTOSMEM_sharing_op_fork_reset        10

#define PRTOSMEM_SHARING_OP_S_HANDLE_INVALID  (-10)
#define PRTOSMEM_SHARING_OP_C_HANDLE_INVALID  (-9)

/* The following allows sharing of grant refs. This is useful
 * for sharing utilities sitting as "filters" in IO backends
 * (e.g. memshr + blktap(2)). The IO backend is only exposed
 * to grant references, and this allows sharing of the grefs */
#define PRTOSMEM_SHARING_OP_FIELD_IS_GREF_FLAG   (prtos_mk_ullong(1) << 62)

#define PRTOSMEM_SHARING_OP_FIELD_MAKE_GREF(field, val)  \
    (field) = (PRTOSMEM_SHARING_OP_FIELD_IS_GREF_FLAG | (val))
#define PRTOSMEM_SHARING_OP_FIELD_IS_GREF(field)         \
    ((field) & PRTOSMEM_SHARING_OP_FIELD_IS_GREF_FLAG)
#define PRTOSMEM_SHARING_OP_FIELD_GET_GREF(field)        \
    ((field) & (~PRTOSMEM_SHARING_OP_FIELD_IS_GREF_FLAG))

struct prtos_mem_sharing_op {
    uint8_t     op;     /* PRTOSMEM_sharing_op_* */
    domid_t     domain;

    union {
        struct mem_sharing_op_nominate {  /* OP_NOMINATE_xxx           */
            union {
                uint64_aligned_t gfn;     /* IN: gfn to nominate       */
                uint32_t      grant_ref;  /* IN: grant ref to nominate */
            } u;
            uint64_aligned_t  handle;     /* OUT: the handle           */
        } nominate;
        struct mem_sharing_op_share {     /* OP_SHARE/ADD_PHYSMAP */
            uint64_aligned_t source_gfn;    /* IN: the gfn of the source page */
            uint64_aligned_t source_handle; /* IN: handle to the source page */
            uint64_aligned_t client_gfn;    /* IN: the client gfn */
            uint64_aligned_t client_handle; /* IN: handle to the client page */
            domid_t  client_domain; /* IN: the client domain id */
        } share;
        struct mem_sharing_op_range {         /* OP_RANGE_SHARE */
            uint64_aligned_t first_gfn;      /* IN: the first gfn */
            uint64_aligned_t last_gfn;       /* IN: the last gfn */
            uint64_aligned_t opaque;         /* Must be set to 0 */
            domid_t client_domain;           /* IN: the client domain id */
            uint16_t _pad[3];                /* Must be set to 0 */
        } range;
        struct mem_sharing_op_debug {     /* OP_DEBUG_xxx */
            union {
                uint64_aligned_t gfn;      /* IN: gfn to debug          */
                uint64_aligned_t mfn;      /* IN: mfn to debug          */
                uint32_t gref;     /* IN: gref to debug         */
            } u;
        } debug;
        struct mem_sharing_op_fork {      /* OP_FORK{,_RESET} */
            domid_t parent_domain;        /* IN: parent's domain id */
/* Only makes sense for short-lived forks */
#define PRTOSMEM_FORK_WITH_IOMMU_ALLOWED (1u << 0)
/* Only makes sense for short-lived forks */
#define PRTOSMEM_FORK_BLOCK_INTERRUPTS   (1u << 1)
#define PRTOSMEM_FORK_RESET_STATE        (1u << 2)
#define PRTOSMEM_FORK_RESET_MEMORY       (1u << 3)
            uint16_t flags;               /* IN: optional settings */
            uint32_t pad;                 /* Must be set to 0 */
        } fork;
    } u;
};
typedef struct prtos_mem_sharing_op prtos_mem_sharing_op_t;
DEFINE_PRTOS_GUEST_HANDLE(prtos_mem_sharing_op_t);

/*
 * Attempt to stake a claim for a domain on a quantity of pages
 * of system RAM, but _not_ assign specific pageframes.  Only
 * arithmetic is performed so the hypercall is very fast and need
 * not be preemptible, thus sidestepping time-of-check-time-of-use
 * races for memory allocation.  Returns 0 if the hypervisor page
 * allocator has atomically and successfully claimed the requested
 * number of pages, else non-zero.
 *
 * Any domain may have only one active claim.  When sufficient memory
 * has been allocated to resolve the claim, the claim silently expires.
 * Claiming zero pages effectively resets any outstanding claim and
 * is always successful.
 *
 * Note that a valid claim may be staked even after memory has been
 * allocated for a domain.  In this case, the claim is not incremental,
 * i.e. if the domain's total page count is 3, and a claim is staked
 * for 10, only 7 additional pages are claimed.
 *
 * Caller must be privileged or the hypercall fails.
 */
#define PRTOSMEM_claim_pages                  24

/*
 * PRTOSMEM_claim_pages flags - the are no flags at this time.
 * The zero value is appropriate.
 */

/*
 * With some legacy devices, certain guest-physical addresses cannot safely
 * be used for other purposes, e.g. to map guest RAM.  This hypercall
 * enumerates those regions so the toolstack can avoid using them.
 */
#define PRTOSMEM_reserved_device_memory_map   27
struct prtos_reserved_device_memory {
    prtos_pfn_t start_pfn;
    prtos_ulong_t nr_pages;
};
typedef struct prtos_reserved_device_memory prtos_reserved_device_memory_t;
DEFINE_PRTOS_GUEST_HANDLE(prtos_reserved_device_memory_t);

struct prtos_reserved_device_memory_map {
#define PRTOSMEM_RDM_ALL 1 /* Request all regions (ignore dev union). */
    /* IN */
    uint32_t flags;
    /*
     * IN/OUT
     *
     * Gets set to the required number of entries when too low,
     * signaled by error code -ERANGE.
     */
    unsigned int nr_entries;
    /* OUT */
    PRTOS_GUEST_HANDLE(prtos_reserved_device_memory_t) buffer;
    /* IN */
    union {
        physdev_pci_device_t pci;
    } dev;
};
typedef struct prtos_reserved_device_memory_map prtos_reserved_device_memory_map_t;
DEFINE_PRTOS_GUEST_HANDLE(prtos_reserved_device_memory_map_t);

#endif /* defined(__PRTOS_AARCH64__) || defined(__PRTOS_TOOLS__) */

/*
 * Get the pages for a particular guest resource, so that they can be
 * mapped directly by a tools domain.
 */
#define PRTOSMEM_acquire_resource 28
struct prtos_mem_acquire_resource {
    /* IN - The domain whose resource is to be mapped */
    domid_t domid;
    /* IN - the type of resource */
    uint16_t type;

#define PRTOSMEM_resource_ioreq_server 0
#define PRTOSMEM_resource_grant_table 1
#define PRTOSMEM_resource_vmtrace_buf 2

    /*
     * IN - a type-specific resource identifier, which must be zero
     *      unless stated otherwise.
     *
     * type == PRTOSMEM_resource_ioreq_server -> id == ioreq server id
     * type == PRTOSMEM_resource_grant_table -> id defined below
     */
    uint32_t id;

#define PRTOSMEM_resource_grant_table_id_shared 0
#define PRTOSMEM_resource_grant_table_id_status 1

    /*
     * IN/OUT
     *
     * As an IN parameter number of frames of the resource to be mapped.
     * This value may be updated over the course of the operation.
     *
     * When frame_list is NULL and nr_frames is 0, this is interpreted as a
     * request for the size of the resource, which shall be returned in the
     * nr_frames field.
     *
     * The size of a resource will never be zero, but a nonzero result doesn't
     * guarantee that a subsequent mapping request will be successful.  There
     * are further type/id specific constraints which may change between the
     * two calls.
     */
    uint32_t nr_frames;
    /*
     * Padding field, must be zero on input.
     * In a previous version this was an output field with the lowest bit
     * named PRTOSMEM_rsrc_acq_caller_owned. Future versions of this interface
     * will not reuse this bit as an output with the field being zero on
     * input.
     */
    uint32_t pad;
    /*
     * IN - the index of the initial frame to be mapped. This parameter
     *      is ignored if nr_frames is 0.  This value may be updated
     *      over the course of the operation.
     */
    uint64_t frame;

#define PRTOSMEM_resource_ioreq_server_frame_bufioreq 0
#define PRTOSMEM_resource_ioreq_server_frame_ioreq(n) (1 + (n))

    /*
     * IN/OUT - If the tools domain is PV then, upon return, frame_list
     *          will be populated with the MFNs of the resource.
     *          If the tools domain is HVM then it is expected that, on
     *          entry, frame_list will be populated with a list of GFNs
     *          that will be mapped to the MFNs of the resource.
     *          If -EIO is returned then the frame_list has only been
     *          partially mapped and it is up to the caller to unmap all
     *          the GFNs.
     *          This parameter may be NULL if nr_frames is 0.  This
     *          value may be updated over the course of the operation.
     */
    PRTOS_GUEST_HANDLE(prtos_pfn_t) frame_list;
};
typedef struct prtos_mem_acquire_resource prtos_mem_acquire_resource_t;
DEFINE_PRTOS_GUEST_HANDLE(prtos_mem_acquire_resource_t);

/*
 * PRTOSMEM_get_vnumainfo used by guest to get
 * vNUMA topology from hypervisor.
 */
#define PRTOSMEM_get_vnumainfo                26

/* vNUMA node memory ranges */
struct prtos_vmemrange {
    uint64_t start, end;
    unsigned int flags;
    unsigned int nid;
};
typedef struct prtos_vmemrange prtos_vmemrange_t;
DEFINE_PRTOS_GUEST_HANDLE(prtos_vmemrange_t);

/*
 * vNUMA topology specifies vNUMA node number, distance table,
 * memory ranges and vcpu mapping provided for guests.
 * PRTOSMEM_get_vnumainfo hypercall expects to see from guest
 * nr_vnodes, nr_vmemranges and nr_vcpus to indicate available memory.
 * After filling guests structures, nr_vnodes, nr_vmemranges and nr_vcpus
 * copied back to guest. Domain returns expected values of nr_vnodes,
 * nr_vmemranges and nr_vcpus to guest if the values where incorrect.
 */
struct prtos_vnuma_topology_info {
    /* IN */
    domid_t domid;
    uint16_t pad;
    /* IN/OUT */
    unsigned int nr_vnodes;
    unsigned int nr_vcpus;
    unsigned int nr_vmemranges;
    /* OUT */
    union {
        PRTOS_GUEST_HANDLE(uint) h;
        uint64_t pad;
    } vdistance;
    union {
        PRTOS_GUEST_HANDLE(uint) h;
        uint64_t pad;
    } vcpu_to_vnode;
    union {
        PRTOS_GUEST_HANDLE(prtos_vmemrange_t) h;
        uint64_t pad;
    } vmemrange;
};
typedef struct prtos_vnuma_topology_info prtos_vnuma_topology_info_t;
DEFINE_PRTOS_GUEST_HANDLE(prtos_vnuma_topology_info_t);

/* Next available subop number is 29 */

#endif /* __PRTOS_PUBLIC_MEMORY_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
