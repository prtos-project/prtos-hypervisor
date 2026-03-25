/* Minimal XSM - Flask removed, PRTOS uses dummy XSM only */
/* === BEGIN INLINED: xsm_core.c === */
#include <xen_xen_config.h>
/*
 *  This work is based on the LSM implementation in Linux 2.6.13.4.
 *
 *  Author:  George Coker, <gscoker@alpha.ncsc.mil>
 *
 *  Contributors: Michael LeMay, <mdlemay@epoch.ncsc.mil>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2,
 *  as published by the Free Software Foundation.
 */

#include <xen_init.h>
#include <xen_errno.h>
#include <xen_lib.h>
#include <xen_param.h>

#include <xen_hypercall.h>
#include <xsm_xsm.h>

#ifdef CONFIG_XSM

#ifdef CONFIG_MULTIBOOT
#include <asm_setup.h>
#endif

#ifdef CONFIG_HAS_DEVICE_TREE
#include <asm_setup.h>
#endif

#define XSM_FRAMEWORK_VERSION    "1.0.1"

struct xsm_ops __alt_call_maybe_initdata xsm_ops;

enum xsm_ops_state {
    XSM_OPS_UNREGISTERED,
    XSM_OPS_REG_FAILED,
    XSM_OPS_REGISTERED,
};

static enum xsm_ops_state __initdata xsm_ops_registered = XSM_OPS_UNREGISTERED;

enum xsm_bootparam {
    XSM_BOOTPARAM_DUMMY,
    XSM_BOOTPARAM_FLASK,
    XSM_BOOTPARAM_SILO,
};

static enum xsm_bootparam __initdata xsm_bootparam =
#ifdef CONFIG_XSM_FLASK_DEFAULT
    XSM_BOOTPARAM_FLASK;
#elif CONFIG_XSM_SILO_DEFAULT
    XSM_BOOTPARAM_SILO;
#else
    XSM_BOOTPARAM_DUMMY;
#endif

static int __init cf_check parse_xsm_param(const char *s)
{
    int rc = 0;

    if ( !strcmp(s, "dummy") )
        xsm_bootparam = XSM_BOOTPARAM_DUMMY;
#ifdef CONFIG_XSM_FLASK
    else if ( !strcmp(s, "flask") )
        xsm_bootparam = XSM_BOOTPARAM_FLASK;
#endif
#ifdef CONFIG_XSM_SILO
    else if ( !strcmp(s, "silo") )
        xsm_bootparam = XSM_BOOTPARAM_SILO;
#endif
    else
        rc = -EINVAL;

    return rc;
}
custom_param("xsm", parse_xsm_param);

static int __init xsm_core_init(const void *policy_buffer, size_t policy_size)
{
    const struct xsm_ops *ops = NULL;

#ifdef CONFIG_XSM_FLASK_POLICY
    if ( policy_size == 0 )
    {
        policy_buffer = xsm_flask_init_policy;
        policy_size = xsm_flask_init_policy_size;
    }
#endif

    if ( xsm_ops_registered != XSM_OPS_UNREGISTERED )
    {
        printk(XENLOG_ERR
               "Could not init XSM, xsm_ops register already attempted\n");
        return -EIO;
    }

    switch ( xsm_bootparam )
    {
    case XSM_BOOTPARAM_DUMMY:
        xsm_ops_registered = XSM_OPS_REGISTERED;
        break;

    case XSM_BOOTPARAM_FLASK:
        ops = flask_init(policy_buffer, policy_size);
        break;

    case XSM_BOOTPARAM_SILO:
        ops = silo_init();
        break;

    default:
        ASSERT_UNREACHABLE();
        break;
    }

    if ( ops )
    {
        xsm_ops_registered = XSM_OPS_REGISTERED;
        xsm_ops = *ops;
    }
    /*
     * This handles three cases,
     *   - dummy policy module was selected
     *   - a policy module does not provide all handlers
     *   - a policy module failed to init
     */
    xsm_fixup_ops(&xsm_ops);

    if ( xsm_ops_registered != XSM_OPS_REGISTERED )
    {
        xsm_ops_registered = XSM_OPS_REG_FAILED;
        printk(XENLOG_ERR
               "Could not init XSM, xsm_ops register failed\n");
        return -EFAULT;
    }

    return 0;
}

#ifdef CONFIG_MULTIBOOT
int __init xsm_multiboot_init(
    unsigned long *module_map, const multiboot_info_t *mbi)
{
    int ret = 0;
    void *policy_buffer = NULL;
    size_t policy_size = 0;

    printk("XSM Framework v" XSM_FRAMEWORK_VERSION " initialized\n");

    if ( XSM_MAGIC )
    {
        ret = xsm_multiboot_policy_init(module_map, mbi, &policy_buffer,
                                        &policy_size);
        if ( ret )
        {
            bootstrap_map(NULL);
            printk(XENLOG_ERR "Error %d initializing XSM policy\n", ret);
            return -EINVAL;
        }
    }

    ret = xsm_core_init(policy_buffer, policy_size);
    bootstrap_map(NULL);

    return 0;
}
#endif

#ifdef CONFIG_HAS_DEVICE_TREE
int __init xsm_dt_init(void)
{
    int ret = 0;
    void *policy_buffer = NULL;
    size_t policy_size = 0;

    printk("XSM Framework v" XSM_FRAMEWORK_VERSION " initialized\n");

    if ( XSM_MAGIC )
    {
        ret = xsm_dt_policy_init(&policy_buffer, &policy_size);
        if ( ret )
        {
            printk(XENLOG_ERR "Error %d initializing XSM policy\n", ret);
            return -EINVAL;
        }
    }

    ret = xsm_core_init(policy_buffer, policy_size);

    xfree(policy_buffer);

    return ret ?: (xsm_bootparam == XSM_BOOTPARAM_SILO);
}

/**
 * has_xsm_magic - Check XSM Magic of the module header by phy address
 * A XSM module has a special header
 * ------------------------------------------------
 * uint magic | uint target_len | uchar target[8] |
 * 0xf97cff8c |        8        |    "XenFlask"   |
 * ------------------------------------------------
 * 0xf97cff8c is policy magic number (XSM_MAGIC).
 * Here we only check the "magic" of the module.
 */
bool __init has_xsm_magic(paddr_t start)
{
    xsm_magic_t magic;

    if ( XSM_MAGIC )
    {
        copy_from_paddr(&magic, start, sizeof(magic) );
        return ( magic == XSM_MAGIC );
    }

    return false;
}
#endif

#endif

long do_xsm_op(XEN_GUEST_HANDLE_PARAM(void) op)
{
    return xsm_do_xsm_op(op);
}

#ifdef CONFIG_COMPAT
int compat_xsm_op(XEN_GUEST_HANDLE_PARAM(void) op)
{
    return xsm_do_compat_op(op);
}
#endif

/* === END INLINED: xsm_core.c === */
/* === BEGIN INLINED: xsm_policy.c === */
#include <xen_xen_config.h>
/*
 *  Copyright (C) 2005 IBM Corporation
 *
 *  Authors:
 *  Reiner Sailer, <sailer@watson.ibm.com>
 *  Stefan Berger, <stefanb@watson.ibm.com>
 *
 *  Contributors:
 *  Michael LeMay, <mdlemay@epoch.ncsc.mil>
 *  George Coker, <gscoker@alpha.ncsc.mil>
 *  
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2,
 *  as published by the Free Software Foundation.
 *
 *
 *  This file contains the XSM policy init functions for Xen.
 *
 */

#include <xsm_xsm.h>
#ifdef CONFIG_MULTIBOOT
#include <xen_multiboot.h>
#include <asm_setup.h>
#endif
#include <xen_bitops.h>
#ifdef CONFIG_HAS_DEVICE_TREE
# include <asm_setup.h>
# include <xen_device_tree.h>
#endif

#ifdef CONFIG_MULTIBOOT
int __init xsm_multiboot_policy_init(
    unsigned long *module_map, const multiboot_info_t *mbi,
    void **policy_buffer, size_t *policy_size)
{
    int i;
    module_t *mod = (module_t *)__va(mbi->mods_addr);
    int rc = 0;
    u32 *_policy_start;
    unsigned long _policy_len;

    /*
     * Try all modules and see whichever could be the binary policy.
     * Adjust module_map for the module that is the binary policy.
     */
    for ( i = mbi->mods_count-1; i >= 1; i-- )
    {
        if ( !test_bit(i, module_map) )
            continue;

        _policy_start = bootstrap_map(mod + i);
        _policy_len   = mod[i].mod_end;

        if ( (xsm_magic_t)(*_policy_start) == XSM_MAGIC )
        {
            *policy_buffer = _policy_start;
            *policy_size = _policy_len;

            printk("Policy len %#lx, start at %p.\n",
                   _policy_len,_policy_start);

            __clear_bit(i, module_map);
            break;

        }

        bootstrap_map(NULL);
    }

    return rc;
}
#endif

#ifdef CONFIG_HAS_DEVICE_TREE
int __init xsm_dt_policy_init(void **policy_buffer, size_t *policy_size)
{
    struct bootmodule *mod = boot_module_find_by_kind(BOOTMOD_XSM);
    paddr_t paddr, len;

    if ( !mod || !mod->size )
        return 0;

    paddr = mod->start;
    len = mod->size;

    if ( !has_xsm_magic(paddr) )
    {
        printk(XENLOG_ERR "xsm: Invalid magic for XSM blob\n");
        return -EINVAL;
    }

    printk("xsm: Policy len = 0x%"PRIpaddr" start at 0x%"PRIpaddr"\n",
           len, paddr);

    *policy_buffer = xmalloc_bytes(len);
    if ( !*policy_buffer )
        return -ENOMEM;

    copy_from_paddr(*policy_buffer, paddr, len);
    *policy_size = len;

    return 0;
}
#endif

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: xsm_policy.c === */
