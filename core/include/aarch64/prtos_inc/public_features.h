/* SPDX-License-Identifier: MIT */
/******************************************************************************
 * features.h
 *
 * Feature flags, reported by PRTOSVER_get_features.
 *
 * Copyright (c) 2006, Keir Fraser <keir@xensource.com>
 */

#ifndef __PRTOS_PUBLIC_FEATURES_H__
#define __PRTOS_PUBLIC_FEATURES_H__

/*
 * `incontents 200 elfnotes_features PRTOS_ELFNOTE_FEATURES
 *
 * The list of all the features the guest supports. They are set by
 * parsing the PRTOS_ELFNOTE_FEATURES and PRTOS_ELFNOTE_SUPPORTED_FEATURES
 * string. The format is the  feature names (as given here without the
 * "PRTOSFEAT_" prefix) separated by '|' characters.
 * If a feature is required for the kernel to function then the feature name
 * must be preceded by a '!' character.
 *
 * Note that if PRTOS_ELFNOTE_SUPPORTED_FEATURES is used, then in the
 * PRTOSFEAT_dom0 MUST be set if the guest is to be booted as dom0,
 */

/*
 * If set, the guest does not need to write-protect its pagetables, and can
 * update them via direct writes.
 */
#define PRTOSFEAT_writable_page_tables       0

/*
 * If set, the guest does not need to write-protect its segment descriptor
 * tables, and can update them via direct writes.
 */
#define PRTOSFEAT_writable_descriptor_tables 1

/*
 * If set, translation between the guest's 'pseudo-physical' address space
 * and the host's machine address space are handled by the hypervisor. In this
 * mode the guest does not need to perform phys-to/from-machine translations
 * when performing page table operations.
 */
#define PRTOSFEAT_auto_translated_physmap    2

/* If set, the guest is running in supervisor mode (e.g., x86 ring 0). */
#define PRTOSFEAT_supervisor_mode_kernel     3

/*
 * If set, the guest does not need to allocate x86 PAE page directories
 * below 4GB. This flag is usually implied by auto_translated_physmap.
 */
#define PRTOSFEAT_pae_pgdir_above_4gb        4

/* x86: Does this PRTOS host support the MMU_PT_UPDATE_PRESERVE_AD hypercall? */
#define PRTOSFEAT_mmu_pt_update_preserve_ad  5

/* x86: Does this PRTOS host support the MMU_{CLEAR,COPY}_PAGE hypercall? */
#define PRTOSFEAT_highmem_assist             6

/*
 * If set, GNTTABOP_map_grant_ref honors flags to be placed into guest kernel
 * available pte bits.
 */
#define PRTOSFEAT_gnttab_map_avail_bits      7

/* x86: Does this PRTOS host support the HVM callback vector type? */
#define PRTOSFEAT_hvm_callback_vector        8

/* x86: pvclock algorithm is safe to use on HVM */
#define PRTOSFEAT_hvm_safe_pvclock           9

/* x86: pirq can be used by HVM guests */
#define PRTOSFEAT_hvm_pirqs                 10

/* operation as Dom0 is supported */
#define PRTOSFEAT_dom0                      11

/* PRTOS also maps grant references at pfn = mfn.
 * This feature flag is deprecated and should not be used.
#define PRTOSFEAT_grant_map_identity        12
 */

/* Guest can use PRTOSMEMF_vnode to specify virtual node for memory op. */
#define PRTOSFEAT_memory_op_vnode_supported 13

/* arm: Hypervisor supports ARM SMC calling convention. */
#define PRTOSFEAT_ARM_SMCCC_supported       14

/*
 * x86/PVH: If set, ACPI RSDP can be placed at any address. Otherwise RSDP
 * must be located in lower 1MB, as required by ACPI Specification for IA-PC
 * systems.
 * This feature flag is only consulted if PRTOS_ELFNOTE_GUEST_OS contains
 * the "linux" string.
 */
#define PRTOSFEAT_linux_rsdp_unrestricted   15

/*
 * A direct-mapped (or 1:1 mapped) domain is a domain for which its
 * local pages have gfn == mfn. If a domain is direct-mapped,
 * PRTOSFEAT_direct_mapped is set; otherwise PRTOSFEAT_not_direct_mapped
 * is set.
 *
 * If neither flag is set (e.g. older PRTOS releases) the assumptions are:
 * - not auto_translated domains (x86 only) are always direct-mapped
 * - on x86, auto_translated domains are not direct-mapped
 * - on ARM, Dom0 is direct-mapped, DomUs are not
 */
#define PRTOSFEAT_not_direct_mapped         16
#define PRTOSFEAT_direct_mapped             17

/*
 * Signal whether the domain is able to use the following hypercalls:
 *
 * VCPUOP_register_runstate_phys_area
 * VCPUOP_register_vcpu_time_phys_area
 */
#define PRTOSFEAT_runstate_phys_area        18
#define PRTOSFEAT_vcpu_time_phys_area       19

/*
 * If set, PRTOS will passthrough all MSI-X vector ctrl writes to device model,
 * not only those unmasking an entry. This allows device model to properly keep
 * track of the MSI-X table without having to read it from the device behind
 * PRTOS's backs. This information is relevant only for device models.
 */
#define PRTOSFEAT_dm_msix_all_writes        20

#define PRTOSFEAT_NR_SUBMAPS 1

#endif /* __PRTOS_PUBLIC_FEATURES_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
