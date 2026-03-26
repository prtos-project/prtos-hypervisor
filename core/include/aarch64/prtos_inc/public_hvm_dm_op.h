/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2016, Citrix Systems Inc
 *
 */

#ifndef __PRTOS_PUBLIC_HVM_DM_OP_H__
#define __PRTOS_PUBLIC_HVM_DM_OP_H__

#include "public_prtos.h"
#include "public_event_channel.h"

#ifndef uint64_aligned_t
#define uint64_aligned_t uint64_t
#endif

/*
 * IOREQ Servers
 *
 * The interface between an I/O emulator and PRTOS is called an IOREQ Server.
 * A domain supports a single 'legacy' IOREQ Server which is instantiated if
 * parameter...
 *
 * HVM_PARAM_IOREQ_PFN is read (to get the gfn containing the synchronous
 * ioreq structures), or...
 * HVM_PARAM_BUFIOREQ_PFN is read (to get the gfn containing the buffered
 * ioreq ring), or...
 * HVM_PARAM_BUFIOREQ_EVTCHN is read (to get the event channel that PRTOS uses
 * to request buffered I/O emulation).
 *
 * The following hypercalls facilitate the creation of IOREQ Servers for
 * 'secondary' emulators which are invoked to implement port I/O, memory, or
 * PCI config space ranges which they explicitly register.
 */

typedef uint16_t ioservid_t;

/*
 * PRTOS_DMOP_create_ioreq_server: Instantiate a new IOREQ Server for a
 *                               secondary emulator.
 *
 * The <id> handed back is unique for target domain. The valur of
 * <handle_bufioreq> should be one of HVM_IOREQSRV_BUFIOREQ_* defined in
 * hvm_op.h. If the value is HVM_IOREQSRV_BUFIOREQ_OFF then  the buffered
 * ioreq ring will not be allocated and hence all emulation requests to
 * this server will be synchronous.
 */
#define PRTOS_DMOP_create_ioreq_server 1

struct prtos_dm_op_create_ioreq_server {
    /* IN - should server handle buffered ioreqs */
    uint8_t handle_bufioreq;
    uint8_t pad[3];
    /* OUT - server id */
    ioservid_t id;
};
typedef struct prtos_dm_op_create_ioreq_server prtos_dm_op_create_ioreq_server_t;

/*
 * PRTOS_DMOP_get_ioreq_server_info: Get all the information necessary to
 *                                 access IOREQ Server <id>.
 *
 * If the IOREQ Server is handling buffered emulation requests, the
 * emulator needs to bind to event channel <bufioreq_port> to listen for
 * them. (The event channels used for synchronous emulation requests are
 * specified in the per-CPU ioreq structures).
 * In addition, if the PRTOSMEM_acquire_resource memory op cannot be used,
 * the emulator will need to map the synchronous ioreq structures and
 * buffered ioreq ring (if it exists) from guest memory. If <flags> does
 * not contain PRTOS_DMOP_no_gfns then these pages will be made available and
 * the frame numbers passed back in gfns <ioreq_gfn> and <bufioreq_gfn>
 * respectively. (If the IOREQ Server is not handling buffered emulation
 * only <ioreq_gfn> will be valid).
 *
 * NOTE: To access the synchronous ioreq structures and buffered ioreq
 *       ring, it is preferable to use the PRTOSMEM_acquire_resource memory
 *       op specifying resource type PRTOSMEM_resource_ioreq_server.
 */
#define PRTOS_DMOP_get_ioreq_server_info 2

struct prtos_dm_op_get_ioreq_server_info {
    /* IN - server id */
    ioservid_t id;
    /* IN - flags */
    uint16_t flags;

#define _PRTOS_DMOP_no_gfns 0
#define PRTOS_DMOP_no_gfns (1u << _PRTOS_DMOP_no_gfns)

    /* OUT - buffered ioreq port */
    evtchn_port_t bufioreq_port;
    /* OUT - sync ioreq gfn (see block comment above) */
    uint64_aligned_t ioreq_gfn;
    /* OUT - buffered ioreq gfn (see block comment above)*/
    uint64_aligned_t bufioreq_gfn;
};
typedef struct prtos_dm_op_get_ioreq_server_info prtos_dm_op_get_ioreq_server_info_t;

/*
 * PRTOS_DMOP_map_io_range_to_ioreq_server: Register an I/O range for
 *                                        emulation by the client of
 *                                        IOREQ Server <id>.
 * PRTOS_DMOP_unmap_io_range_from_ioreq_server: Deregister an I/O range
 *                                            previously registered for
 *                                            emulation by the client of
 *                                            IOREQ Server <id>.
 *
 * There are three types of I/O that can be emulated: port I/O, memory
 * accesses and PCI config space accesses. The <type> field denotes which
 * type of range* the <start> and <end> (inclusive) fields are specifying.
 * PCI config space ranges are specified by segment/bus/device/function
 * values which should be encoded using the DMOP_PCI_SBDF helper macro
 * below.
 *
 * NOTE: unless an emulation request falls entirely within a range mapped
 * by a secondary emulator, it will not be passed to that emulator.
 */
#define PRTOS_DMOP_map_io_range_to_ioreq_server 3
#define PRTOS_DMOP_unmap_io_range_from_ioreq_server 4

struct prtos_dm_op_ioreq_server_range {
    /* IN - server id */
    ioservid_t id;
    uint16_t pad;
    /* IN - type of range */
    uint32_t type;
# define PRTOS_DMOP_IO_RANGE_PORT   0 /* I/O port range */
# define PRTOS_DMOP_IO_RANGE_MEMORY 1 /* MMIO range */
# define PRTOS_DMOP_IO_RANGE_PCI    2 /* PCI segment/bus/dev/func range */
    /* IN - inclusive start and end of range */
    uint64_aligned_t start, end;
};
typedef struct prtos_dm_op_ioreq_server_range prtos_dm_op_ioreq_server_range_t;

#define PRTOS_DMOP_PCI_SBDF(s,b,d,f) \
	((((s) & 0xffff) << 16) |  \
	 (((b) & 0xff) << 8) |     \
	 (((d) & 0x1f) << 3) |     \
	 ((f) & 0x07))

/*
 * PRTOS_DMOP_set_ioreq_server_state: Enable or disable the IOREQ Server <id>
 *
 * The IOREQ Server will not be passed any emulation requests until it is
 * in the enabled state.
 * Note that the contents of the ioreq_gfn and bufioreq_gfn (see
 * PRTOS_DMOP_get_ioreq_server_info) are not meaningful until the IOREQ Server
 * is in the enabled state.
 */
#define PRTOS_DMOP_set_ioreq_server_state 5

struct prtos_dm_op_set_ioreq_server_state {
    /* IN - server id */
    ioservid_t id;
    /* IN - enabled? */
    uint8_t enabled;
    uint8_t pad;
};
typedef struct prtos_dm_op_set_ioreq_server_state prtos_dm_op_set_ioreq_server_state_t;

/*
 * PRTOS_DMOP_destroy_ioreq_server: Destroy the IOREQ Server <id>.
 *
 * Any registered I/O ranges will be automatically deregistered.
 */
#define PRTOS_DMOP_destroy_ioreq_server 6

struct prtos_dm_op_destroy_ioreq_server {
    /* IN - server id */
    ioservid_t id;
    uint16_t pad;
};
typedef struct prtos_dm_op_destroy_ioreq_server prtos_dm_op_destroy_ioreq_server_t;

/*
 * PRTOS_DMOP_track_dirty_vram: Track modifications to the specified pfn
 *                            range.
 *
 * NOTE: The bitmap passed back to the caller is passed in a
 *       secondary buffer.
 */
#define PRTOS_DMOP_track_dirty_vram 7

struct prtos_dm_op_track_dirty_vram {
    /* IN - number of pages to be tracked */
    uint32_t nr;
    uint32_t pad;
    /* IN - first pfn to track */
    uint64_aligned_t first_pfn;
};
typedef struct prtos_dm_op_track_dirty_vram prtos_dm_op_track_dirty_vram_t;

/*
 * PRTOS_DMOP_set_pci_intx_level: Set the logical level of one of a domain's
 *                              PCI INTx pins.
 */
#define PRTOS_DMOP_set_pci_intx_level 8

struct prtos_dm_op_set_pci_intx_level {
    /* IN - PCI INTx identification (domain:bus:device:intx) */
    uint16_t domain;
    uint8_t bus, device, intx;
    /* IN - Level: 0 -> deasserted, 1 -> asserted */
    uint8_t  level;
};
typedef struct prtos_dm_op_set_pci_intx_level prtos_dm_op_set_pci_intx_level_t;

/*
 * PRTOS_DMOP_set_isa_irq_level: Set the logical level of a one of a domain's
 *                             ISA IRQ lines.
 */
#define PRTOS_DMOP_set_isa_irq_level 9

struct prtos_dm_op_set_isa_irq_level {
    /* IN - ISA IRQ (0-15) */
    uint8_t  isa_irq;
    /* IN - Level: 0 -> deasserted, 1 -> asserted */
    uint8_t  level;
};
typedef struct prtos_dm_op_set_isa_irq_level prtos_dm_op_set_isa_irq_level_t;

/*
 * PRTOS_DMOP_set_pci_link_route: Map a PCI INTx line to an IRQ line.
 */
#define PRTOS_DMOP_set_pci_link_route 10

struct prtos_dm_op_set_pci_link_route {
    /* PCI INTx line (0-3) */
    uint8_t  link;
    /* ISA IRQ (1-15) or 0 -> disable link */
    uint8_t  isa_irq;
};
typedef struct prtos_dm_op_set_pci_link_route prtos_dm_op_set_pci_link_route_t;

/*
 * PRTOS_DMOP_modified_memory: Notify that a set of pages were modified by
 *                           an emulator.
 *
 * DMOP buf 1 contains an array of prtos_dm_op_modified_memory_extent with
 * @nr_extents entries.
 *
 * On error, @nr_extents will contain the index+1 of the extent that
 * had the error.  It is not defined if or which pages may have been
 * marked as dirty, in this event.
 */
#define PRTOS_DMOP_modified_memory 11

struct prtos_dm_op_modified_memory {
    /*
     * IN - Number of extents to be processed
     * OUT -returns n+1 for failing extent
     */
    uint32_t nr_extents;
    /* IN/OUT - Must be set to 0 */
    uint32_t opaque;
};
typedef struct prtos_dm_op_modified_memory prtos_dm_op_modified_memory_t;

struct prtos_dm_op_modified_memory_extent {
    /* IN - number of contiguous pages modified */
    uint32_t nr;
    uint32_t pad;
    /* IN - first pfn modified */
    uint64_aligned_t first_pfn;
};

/*
 * PRTOS_DMOP_set_mem_type: Notify that a region of memory is to be treated
 *                        in a specific way. (See definition of
 *                        hvmmem_type_t).
 *
 * NOTE: In the event of a continuation (return code -ERESTART), the
 *       @first_pfn is set to the value of the pfn of the remaining
 *       region and @nr reduced to the size of the remaining region.
 */
#define PRTOS_DMOP_set_mem_type 12

struct prtos_dm_op_set_mem_type {
    /* IN - number of contiguous pages */
    uint32_t nr;
    /* IN - new hvmmem_type_t of region */
    uint16_t mem_type;
    uint16_t pad;
    /* IN - first pfn in region */
    uint64_aligned_t first_pfn;
};
typedef struct prtos_dm_op_set_mem_type prtos_dm_op_set_mem_type_t;

/*
 * PRTOS_DMOP_inject_event: Inject an event into a VCPU, which will
 *                        get taken up when it is next scheduled.
 *
 * Note that the caller should know enough of the state of the CPU before
 * injecting, to know what the effect of injecting the event will be.
 */
#define PRTOS_DMOP_inject_event 13

struct prtos_dm_op_inject_event {
    /* IN - index of vCPU */
    uint32_t vcpuid;
    /* IN - interrupt vector */
    uint8_t vector;
    /* IN - event type (DMOP_EVENT_* ) */
    uint8_t type;
/* NB. This enumeration precisely matches hvm.h:X86_EVENTTYPE_* */
# define PRTOS_DMOP_EVENT_ext_int    0 /* external interrupt */
# define PRTOS_DMOP_EVENT_nmi        2 /* nmi */
# define PRTOS_DMOP_EVENT_hw_exc     3 /* hardware exception */
# define PRTOS_DMOP_EVENT_sw_int     4 /* software interrupt (CD nn) */
# define PRTOS_DMOP_EVENT_pri_sw_exc 5 /* ICEBP (F1) */
# define PRTOS_DMOP_EVENT_sw_exc     6 /* INT3 (CC), INTO (CE) */
    /* IN - instruction length */
    uint8_t insn_len;
    uint8_t pad0;
    /* IN - error code (or ~0 to skip) */
    uint32_t error_code;
    uint32_t pad1;
    /* IN - type-specific extra data (%cr2 for #PF, pending_dbg for #DB) */
    uint64_aligned_t cr2;
};
typedef struct prtos_dm_op_inject_event prtos_dm_op_inject_event_t;

/*
 * PRTOS_DMOP_inject_msi: Inject an MSI for an emulated device.
 */
#define PRTOS_DMOP_inject_msi 14

struct prtos_dm_op_inject_msi {
    /* IN - MSI data (lower 32 bits) */
    uint32_t data;
    uint32_t pad;
    /* IN - MSI address (0xfeexxxxx) */
    uint64_aligned_t addr;
};
typedef struct prtos_dm_op_inject_msi prtos_dm_op_inject_msi_t;

/*
 * PRTOS_DMOP_map_mem_type_to_ioreq_server : map or unmap the IOREQ Server <id>
 *                                      to specific memory type <type>
 *                                      for specific accesses <flags>
 *
 * For now, flags only accept the value of PRTOS_DMOP_IOREQ_MEM_ACCESS_WRITE,
 * which means only write operations are to be forwarded to an ioreq server.
 * Support for the emulation of read operations can be added when an ioreq
 * server has such requirement in future.
 */
#define PRTOS_DMOP_map_mem_type_to_ioreq_server 15

struct prtos_dm_op_map_mem_type_to_ioreq_server {
    ioservid_t id;      /* IN - ioreq server id */
    uint16_t type;      /* IN - memory type */
    uint32_t flags;     /* IN - types of accesses to be forwarded to the
                           ioreq server. flags with 0 means to unmap the
                           ioreq server */

#define PRTOS_DMOP_IOREQ_MEM_ACCESS_READ (1u << 0)
#define PRTOS_DMOP_IOREQ_MEM_ACCESS_WRITE (1u << 1)

    uint64_t opaque;    /* IN/OUT - only used for hypercall continuation,
                           has to be set to zero by the caller */
};
typedef struct prtos_dm_op_map_mem_type_to_ioreq_server prtos_dm_op_map_mem_type_to_ioreq_server_t;

/*
 * PRTOS_DMOP_remote_shutdown : Declare a shutdown for another domain
 *                            Identical to SCHEDOP_remote_shutdown
 */
#define PRTOS_DMOP_remote_shutdown 16

struct prtos_dm_op_remote_shutdown {
    uint32_t reason;       /* SHUTDOWN_* => enum sched_shutdown_reason */
                           /* (Other reason values are not blocked) */
};
typedef struct prtos_dm_op_remote_shutdown prtos_dm_op_remote_shutdown_t;

/*
 * PRTOS_DMOP_relocate_memory : Relocate GFNs for the specified guest.
 *                            Identical to PRTOSMEM_add_to_physmap with
 *                            space == PRTOSMAPSPACE_gmfn_range.
 */
#define PRTOS_DMOP_relocate_memory 17

struct prtos_dm_op_relocate_memory {
    /* All fields are IN/OUT, with their OUT state undefined. */
    /* Number of GFNs to process. */
    uint32_t size;
    uint32_t pad;
    /* Starting GFN to relocate. */
    uint64_aligned_t src_gfn;
    /* Starting GFN where GFNs should be relocated. */
    uint64_aligned_t dst_gfn;
};
typedef struct prtos_dm_op_relocate_memory prtos_dm_op_relocate_memory_t;

/*
 * PRTOS_DMOP_pin_memory_cacheattr : Pin caching type of RAM space.
 *                                 Identical to PRTOS_DOMCTL_pin_mem_cacheattr.
 */
#define PRTOS_DMOP_pin_memory_cacheattr 18

struct prtos_dm_op_pin_memory_cacheattr {
    uint64_aligned_t start; /* Start gfn. */
    uint64_aligned_t end;   /* End gfn. */
/* Caching types: these happen to be the same as x86 MTRR/PAT type codes. */
#define PRTOS_DMOP_MEM_CACHEATTR_UC  0
#define PRTOS_DMOP_MEM_CACHEATTR_WC  1
#define PRTOS_DMOP_MEM_CACHEATTR_WT  4
#define PRTOS_DMOP_MEM_CACHEATTR_WP  5
#define PRTOS_DMOP_MEM_CACHEATTR_WB  6
#define PRTOS_DMOP_MEM_CACHEATTR_UCM 7
#define PRTOS_DMOP_DELETE_MEM_CACHEATTR (~(uint32_t)0)
    uint32_t type;          /* PRTOS_DMOP_MEM_CACHEATTR_* */
    uint32_t pad;
};
typedef struct prtos_dm_op_pin_memory_cacheattr prtos_dm_op_pin_memory_cacheattr_t;

/*
 * PRTOS_DMOP_set_irq_level: Set the logical level of a one of a domain's
 *                         IRQ lines (currently Arm only).
 * Only SPIs are supported.
 */
#define PRTOS_DMOP_set_irq_level 19

struct prtos_dm_op_set_irq_level {
    uint32_t irq;
    /* IN - Level: 0 -> deasserted, 1 -> asserted */
    uint8_t level;
    uint8_t pad[3];
};
typedef struct prtos_dm_op_set_irq_level prtos_dm_op_set_irq_level_t;

/*
 * PRTOS_DMOP_nr_vcpus: Query the number of vCPUs a domain has.
 *
 * This is the number of vcpu objects allocated in PRTOS for the domain, and is
 * fixed from creation time.  This bound is applicable to e.g. the vcpuid
 * parameter of PRTOS_DMOP_inject_event, or number of struct ioreq objects
 * mapped via PRTOSMEM_acquire_resource.
 */
#define PRTOS_DMOP_nr_vcpus 20

struct prtos_dm_op_nr_vcpus {
    uint32_t vcpus; /* OUT */
};
typedef struct prtos_dm_op_nr_vcpus prtos_dm_op_nr_vcpus_t;

struct prtos_dm_op {
    uint32_t op;
    uint32_t pad;
    union {
        prtos_dm_op_create_ioreq_server_t create_ioreq_server;
        prtos_dm_op_get_ioreq_server_info_t get_ioreq_server_info;
        prtos_dm_op_ioreq_server_range_t map_io_range_to_ioreq_server;
        prtos_dm_op_ioreq_server_range_t unmap_io_range_from_ioreq_server;
        prtos_dm_op_set_ioreq_server_state_t set_ioreq_server_state;
        prtos_dm_op_destroy_ioreq_server_t destroy_ioreq_server;
        prtos_dm_op_track_dirty_vram_t track_dirty_vram;
        prtos_dm_op_set_pci_intx_level_t set_pci_intx_level;
        prtos_dm_op_set_isa_irq_level_t set_isa_irq_level;
        prtos_dm_op_set_irq_level_t set_irq_level;
        prtos_dm_op_set_pci_link_route_t set_pci_link_route;
        prtos_dm_op_modified_memory_t modified_memory;
        prtos_dm_op_set_mem_type_t set_mem_type;
        prtos_dm_op_inject_event_t inject_event;
        prtos_dm_op_inject_msi_t inject_msi;
        prtos_dm_op_map_mem_type_to_ioreq_server_t map_mem_type_to_ioreq_server;
        prtos_dm_op_remote_shutdown_t remote_shutdown;
        prtos_dm_op_relocate_memory_t relocate_memory;
        prtos_dm_op_pin_memory_cacheattr_t pin_memory_cacheattr;
        prtos_dm_op_nr_vcpus_t nr_vcpus;
    } u;
};

struct prtos_dm_op_buf {
    PRTOS_GUEST_HANDLE(void) h;
    prtos_ulong_t size;
};
typedef struct prtos_dm_op_buf prtos_dm_op_buf_t;
DEFINE_PRTOS_GUEST_HANDLE(prtos_dm_op_buf_t);

/* ` enum neg_errnoval
 * ` HYPERVISOR_dm_op(domid_t domid,
 * `                  unsigned int nr_bufs,
 * `                  prtos_dm_op_buf_t bufs[])
 * `
 *
 * @domid is the domain the hypercall operates on.
 * @nr_bufs is the number of buffers in the @bufs array.
 * @bufs points to an array of buffers where @bufs[0] contains a struct
 * prtos_dm_op, describing the specific device model operation and its
 * parameters.
 * @bufs[1..] may be referenced in the parameters for the purposes of
 * passing extra information to or from the domain.
 */

#endif /* __PRTOS_PUBLIC_HVM_DM_OP_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
