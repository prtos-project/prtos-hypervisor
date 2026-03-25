/* Xen miscellaneous - consolidated */
/* === BEGIN INLINED: common_kernel.c === */
#include <xen_xen_config.h>
/******************************************************************************
 * kernel.c
 * 
 * Copyright (c) 2002-2005 K A Fraser
 */

#include <xen_init.h>
#include <xen_lib.h>
#include <xen_errno.h>
#include <xen_param.h>
#include <xen_version.h>
#include <xen_sched.h>
#include <xen_paging.h>
#include <xen_guest_access.h>
#include <xen_hypercall.h>
#include <xen_hypfs.h>
#include <xsm_xsm.h>
#include <asm_current.h>
#include <public_version.h>

#ifdef CONFIG_COMPAT
#include <compat/version.h>

CHECK_build_id;
CHECK_compile_info;
CHECK_feature_info;
#endif

enum system_state system_state = SYS_STATE_early_boot;

#ifdef CONFIG_HAS_DIT
bool __ro_after_init opt_dit = IS_ENABLED(CONFIG_DIT_DEFAULT);
boolean_param("dit", opt_dit);
#endif

static xen_commandline_t saved_cmdline;
static const char __initconst opt_builtin_cmdline[] = CONFIG_CMDLINE;

static int assign_integer_param(const struct kernel_param *param, uint64_t val)
{
    switch ( param->len )
    {
    case sizeof(uint8_t):
        if ( val > UINT8_MAX && val < (uint64_t)INT8_MIN )
            return -EOVERFLOW;
        *(uint8_t *)param->par.var = val;
        break;
    case sizeof(uint16_t):
        if ( val > UINT16_MAX && val < (uint64_t)INT16_MIN )
            return -EOVERFLOW;
        *(uint16_t *)param->par.var = val;
        break;
    case sizeof(uint32_t):
        if ( val > UINT32_MAX && val < (uint64_t)INT32_MIN )
            return -EOVERFLOW;
        *(uint32_t *)param->par.var = val;
        break;
    case sizeof(uint64_t):
        *(uint64_t *)param->par.var = val;
        break;
    default:
        BUG();
    }

    return 0;
}

static int parse_params(const char *cmdline, const struct kernel_param *start,
                        const struct kernel_param *end)
{
    char opt[MAX_PARAM_SIZE], *optval, *optkey, *q;
    const char *p = cmdline, *key;
    const struct kernel_param *param;
    int rc, final_rc = 0;
    bool bool_assert, found;

    for ( ; ; )
    {
        /* Skip whitespace. */
        while ( *p == ' ' )
            p++;
        if ( *p == '\0' )
            break;

        /* Grab the next whitespace-delimited option. */
        q = optkey = opt;
        while ( (*p != ' ') && (*p != '\0') )
        {
            if ( (q-opt) < (sizeof(opt)-1) ) /* avoid overflow */
                *q++ = *p;
            p++;
        }
        *q = '\0';

        /* Search for value part of a key=value option. */
        optval = strchr(opt, '=');
        if ( optval != NULL )
        {
            *optval++ = '\0'; /* nul-terminate the option value */
            q = strpbrk(opt, "([{<");
        }
        else
        {
            optval = q;       /* default option value is empty string */
            q = NULL;
        }

        /* Boolean parameters can be inverted with 'no-' prefix. */
        key = optkey;
        bool_assert = !!strncmp("no-", optkey, 3);
        if ( !bool_assert )
            optkey += 3;

        rc = 0;
        found = false;
        for ( param = start; param < end; param++ )
        {
            int rctmp;
            const char *s;

            if ( strcmp(param->name, optkey) )
            {
                if ( param->type == OPT_CUSTOM && q &&
                     strlen(param->name) == q + 1 - opt &&
                     !strncmp(param->name, opt, q + 1 - opt) )
                {
                    found = true;
                    optval[-1] = '=';
                    rctmp = param->par.func(q);
                    optval[-1] = '\0';
                    if ( !rc )
                        rc = rctmp;
                }
                continue;
            }

            rctmp = 0;
            found = true;
            switch ( param->type )
            {
            case OPT_STR:
                strlcpy(param->par.var, optval, param->len);
                break;
            case OPT_UINT:
                rctmp = assign_integer_param(
                    param,
                    simple_strtoll(optval, &s, 0));
                if ( *s )
                    rctmp = -EINVAL;
                break;
            case OPT_BOOL:
                rctmp = *optval ? parse_bool(optval, NULL) : 1;
                if ( rctmp < 0 )
                    break;
                if ( !rctmp )
                    bool_assert = !bool_assert;
                rctmp = 0;
                assign_integer_param(param, bool_assert);
                break;
            case OPT_SIZE:
                rctmp = assign_integer_param(
                    param,
                    parse_size_and_unit(optval, &s));
                if ( *s )
                    rctmp = -EINVAL;
                break;
            case OPT_CUSTOM:
                rctmp = -EINVAL;
                if ( !bool_assert )
                {
                    if ( *optval )
                        break;
                    safe_strcpy(opt, "no");
                    optval = opt;
                }
                rctmp = param->par.func(optval);
                break;
            case OPT_IGNORE:
                break;
            default:
                BUG();
                break;
            }

            if ( !rc )
                rc = rctmp;
        }

        if ( rc )
        {
            printk("parameter \"%s\" has invalid value \"%s\", rc=%d!\n",
                    key, optval, rc);
            final_rc = rc;
        }
        if ( !found )
        {
            printk("parameter \"%s\" unknown!\n", key);
            final_rc = -EINVAL;
        }
    }

    return final_rc;
}

static void __init _cmdline_parse(const char *cmdline)
{
    parse_params(cmdline, __setup_start, __setup_end);
}

/**
 *    cmdline_parse -- parses the xen command line.
 * If CONFIG_CMDLINE is set, it would be parsed prior to @cmdline.
 * But if CONFIG_CMDLINE_OVERRIDE is set to y, @cmdline will be ignored.
 */
void __init cmdline_parse(const char *cmdline)
{
    if ( opt_builtin_cmdline[0] )
    {
        printk("Built-in command line: %s\n", opt_builtin_cmdline);
        _cmdline_parse(opt_builtin_cmdline);
    }

#ifndef CONFIG_CMDLINE_OVERRIDE
    if ( cmdline == NULL )
        return;

    safe_strcpy(saved_cmdline, cmdline);
    _cmdline_parse(cmdline);
#endif
}

int parse_bool(const char *s, const char *e)
{
    size_t len = e ? ({ ASSERT(e >= s); e - s; }) : strlen(s);

    switch ( len )
    {
    case 1:
        if ( *s == '1' )
            return 1;
        if ( *s == '0' )
            return 0;
        break;

    case 2:
        if ( !strncmp("on", s, 2) )
            return 1;
        if ( !strncmp("no", s, 2) )
            return 0;
        break;

    case 3:
        if ( !strncmp("yes", s, 3) )
            return 1;
        if ( !strncmp("off", s, 3) )
            return 0;
        break;

    case 4:
        if ( !strncmp("true", s, 4) )
            return 1;
        break;

    case 5:
        if ( !strncmp("false", s, 5) )
            return 0;
        break;

    case 6:
        if ( !strncmp("enable", s, 6) )
            return 1;
        break;

    case 7:
        if ( !strncmp("disable", s, 7) )
            return 0;
        break;
    }

    return -1;
}

int parse_boolean(const char *name, const char *s, const char *e)
{
    size_t slen, nlen;
    bool has_neg_prefix = !strncmp(s, "no-", 3);

    if ( has_neg_prefix )
        s += 3;

    slen = e ? ({ ASSERT(e >= s); e - s; }) : strlen(s);
    nlen = strlen(name);

    /* Does s now start with name? */
    if ( slen < nlen || strncmp(s, name, nlen) )
        return -1;

    /* Exact, unadorned name?  Result depends on the 'no-' prefix. */
    if ( slen == nlen )
        return !has_neg_prefix;

    /* Inexact match with a 'no-' prefix?  Not valid. */
    if ( has_neg_prefix )
        return -1;

    /* =$SOMETHING?  Defer to the regular boolean parsing. */
    if ( s[nlen] == '=' )
    {
        int b = parse_bool(&s[nlen + 1], e);

        if ( b >= 0 )
            return b;

        /* Not a boolean, but the name matched.  Signal specially. */
        return -2;
    }

    /* Unrecognised.  Give up. */
    return -1;
}

int __init parse_signed_integer(const char *name, const char *s, const char *e,
                                long long *val)
{
    size_t slen, nlen;
    const char *str;
    long long pval;

    slen = e ? ({ ASSERT(e >= s); e - s; }) : strlen(s);
    nlen = strlen(name);

    if ( !e )
        e = s + slen;

    /* Check that this is the name we're looking for and a value was provided */
    if ( slen <= nlen || strncmp(s, name, nlen) || s[nlen] != '=' )
        return -1;

    pval = simple_strtoll(&s[nlen + 1], &str, 10);

    /* Number not recognised */
    if ( str != e )
        return -2;

    *val = pval;

    return 0;
}

int cmdline_strcmp(const char *frag, const char *name)
{
    for ( ; ; frag++, name++ )
    {
        unsigned char f = *frag, n = *name;
        int res = f - n;

        if ( res || n == '\0' )
        {
            /*
             * NUL in 'name' matching a comma, colon, semicolon or equals in
             * 'frag' implies success.
             */
            if ( n == '\0' && (f == ',' || f == ':' || f == ';' || f == '=') )
                res = 0;

            return res;
        }
    }
}

unsigned int tainted;

/**
 *      print_tainted - return a string to represent the kernel taint state.
 *
 *  'C' - Console output is synchronous.
 *  'E' - An error (e.g. a machine check exceptions) has been injected.
 *  'H' - HVM forced emulation prefix is permitted.
 *  'I' - Platform is insecure (usually due to an errata on the platform).
 *  'M' - Machine had a machine check experience.
 *  'S' - Out of spec CPU (Incompatible features on one or more cores).
 *
 *      The string is overwritten by the next call to print_taint().
 */
char *print_tainted(char *str)
{
    if ( tainted )
    {
        snprintf(str, TAINT_STRING_MAX_LEN, "Tainted: %c%c%c%c%c%c",
                 tainted & TAINT_MACHINE_INSECURE ? 'I' : ' ',
                 tainted & TAINT_MACHINE_CHECK ? 'M' : ' ',
                 tainted & TAINT_SYNC_CONSOLE ? 'C' : ' ',
                 tainted & TAINT_ERROR_INJECT ? 'E' : ' ',
                 tainted & TAINT_HVM_FEP ? 'H' : ' ',
                 tainted & TAINT_CPU_OUT_OF_SPEC ? 'S' : ' ');
    }
    else
    {
        snprintf(str, TAINT_STRING_MAX_LEN, "Not tainted");
    }

    return str;
}

void add_taint(unsigned int taint)
{
    tainted |= taint;
}

extern const initcall_t __initcall_start[], __presmp_initcall_end[],
    __initcall_end[];

void __init do_presmp_initcalls(void)
{
    const initcall_t *call;
    for ( call = __initcall_start; call < __presmp_initcall_end; call++ )
        (*call)();
}

void __init do_initcalls(void)
{
    const initcall_t *call;
    for ( call = __presmp_initcall_end; call < __initcall_end; call++ )
        (*call)();
 }

#ifdef CONFIG_HYPFS
static unsigned int __read_mostly major_version;
static unsigned int __read_mostly minor_version;

static HYPFS_DIR_INIT(buildinfo, "buildinfo");
static HYPFS_DIR_INIT(compileinfo, "compileinfo");
static HYPFS_DIR_INIT(version, "version");
static HYPFS_UINT_INIT(major, "major", major_version);
static HYPFS_UINT_INIT(minor, "minor", minor_version);
static HYPFS_STRING_INIT(changeset, "changeset");
static HYPFS_STRING_INIT(compiler, "compiler");
static HYPFS_STRING_INIT(compile_by, "compile_by");
static HYPFS_STRING_INIT(compile_date, "compile_date");
static HYPFS_STRING_INIT(compile_domain, "compile_domain");
static HYPFS_STRING_INIT(extra, "extra");

#ifdef CONFIG_HYPFS_CONFIG
static HYPFS_STRING_INIT(config, "config");
#endif

static int __init cf_check buildinfo_init(void)
{
    hypfs_add_dir(&hypfs_root, &buildinfo, true);

    hypfs_string_set_reference(&changeset, xen_changeset());
    hypfs_add_leaf(&buildinfo, &changeset, true);

    hypfs_add_dir(&buildinfo, &compileinfo, true);
    hypfs_string_set_reference(&compiler, xen_compiler());
    hypfs_string_set_reference(&compile_by, xen_compile_by());
    hypfs_string_set_reference(&compile_date, xen_compile_date());
    hypfs_string_set_reference(&compile_domain, xen_compile_domain());
    hypfs_add_leaf(&compileinfo, &compiler, true);
    hypfs_add_leaf(&compileinfo, &compile_by, true);
    hypfs_add_leaf(&compileinfo, &compile_date, true);
    hypfs_add_leaf(&compileinfo, &compile_domain, true);

    major_version = xen_major_version();
    minor_version = xen_minor_version();
    hypfs_add_dir(&buildinfo, &version, true);
    hypfs_string_set_reference(&extra, xen_extra_version());
    hypfs_add_leaf(&version, &extra, true);
    hypfs_add_leaf(&version, &major, true);
    hypfs_add_leaf(&version, &minor, true);

#ifdef CONFIG_HYPFS_CONFIG
    config.e.encoding = XEN_HYPFS_ENC_GZIP;
    config.e.size = xen_config_data_size;
    config.u.content = xen_config_data;
    hypfs_add_leaf(&buildinfo, &config, true);
#endif

    return 0;
}
__initcall(buildinfo_init);

static HYPFS_DIR_INIT(params, "params");

static int __init cf_check param_init(void)
{
    struct param_hypfs *param;

    hypfs_add_dir(&hypfs_root, &params, true);

    for ( param = __paramhypfs_start; param < __paramhypfs_end; param++ )
    {
        if ( param->init_leaf )
            param->init_leaf(param);
        else if ( param->hypfs.e.type == XEN_HYPFS_TYPE_STRING )
            param->hypfs.e.size = strlen(param->hypfs.u.content) + 1;
        hypfs_add_leaf(&params, &param->hypfs, true);
    }

    return 0;
}
__initcall(param_init);
#endif

long do_xen_version(int cmd, XEN_GUEST_HANDLE_PARAM(void) arg)
{
    bool deny = xsm_xen_version(XSM_OTHER, cmd);

    switch ( cmd )
    {
    case XENVER_version:
        return (xen_major_version() << 16) | xen_minor_version();

    case XENVER_extraversion:
    {
        xen_extraversion_t extraversion;

        memset(extraversion, 0, sizeof(extraversion));
        safe_strcpy(extraversion, deny ? xen_deny() : xen_extra_version());
        if ( copy_to_guest(arg, extraversion, ARRAY_SIZE(extraversion)) )
            return -EFAULT;
        return 0;
    }

    case XENVER_compile_info:
    {
        xen_compile_info_t info;

        memset(&info, 0, sizeof(info));
        safe_strcpy(info.compiler,       deny ? xen_deny() : xen_compiler());
        safe_strcpy(info.compile_by,     deny ? xen_deny() : xen_compile_by());
        safe_strcpy(info.compile_domain, deny ? xen_deny() : xen_compile_domain());
        safe_strcpy(info.compile_date,   deny ? xen_deny() : xen_compile_date());
        if ( copy_to_guest(arg, &info, 1) )
            return -EFAULT;
        return 0;
    }

    case XENVER_capabilities:
    {
        xen_capabilities_info_t info;

        memset(info, 0, sizeof(info));
        if ( !deny )
            arch_get_xen_caps(&info);

        if ( copy_to_guest(arg, info, ARRAY_SIZE(info)) )
            return -EFAULT;
        return 0;
    }
    
    case XENVER_platform_parameters:
    {
        const struct vcpu *curr = current;

#ifdef CONFIG_COMPAT
        if ( curr->hcall_compat )
        {
            compat_platform_parameters_t params = {
                .virt_start = is_pv_vcpu(curr)
                            ? HYPERVISOR_COMPAT_VIRT_START(curr->domain)
                            : 0,
            };

            if ( copy_to_guest(arg, &params, 1) )
                return -EFAULT;
        }
        else
#endif
        {
            xen_platform_parameters_t params = {
                /*
                 * Out of an abundance of caution, retain the useless return
                 * value for 64bit PV guests, but in release builds only.
                 *
                 * This is not expected to cause any problems, but if it does,
                 * the developer impacted will be the one best suited to fix
                 * the caller not to issue this hypercall.
                 */
                .virt_start = !IS_ENABLED(CONFIG_DEBUG) && is_pv_vcpu(curr)
                              ? HYPERVISOR_VIRT_START
                              : 0,
            };

            if ( copy_to_guest(arg, &params, 1) )
                return -EFAULT;
        }

        return 0;
        
    }
    
    case XENVER_changeset:
    {
        xen_changeset_info_t chgset;

        memset(chgset, 0, sizeof(chgset));
        safe_strcpy(chgset, deny ? xen_deny() : xen_changeset());
        if ( copy_to_guest(arg, chgset, ARRAY_SIZE(chgset)) )
            return -EFAULT;
        return 0;
    }

    case XENVER_get_features:
    {
        xen_feature_info_t fi;
        struct domain *d = current->domain;

        if ( copy_from_guest(&fi, arg, 1) )
            return -EFAULT;

        switch ( fi.submap_idx )
        {
        case 0:
            fi.submap = (1U << XENFEAT_memory_op_vnode_supported) |
#ifdef CONFIG_X86
                        (1U << XENFEAT_vcpu_time_phys_area) |
#endif
                        (1U << XENFEAT_runstate_phys_area);
            if ( VM_ASSIST(d, pae_extended_cr3) )
                fi.submap |= (1U << XENFEAT_pae_pgdir_above_4gb);
            if ( paging_mode_translate(d) )
                fi.submap |= 
                    (1U << XENFEAT_writable_page_tables) |
                    (1U << XENFEAT_auto_translated_physmap);
            if ( is_hardware_domain(d) )
                fi.submap |= 1U << XENFEAT_dom0;
#ifdef CONFIG_ARM
            fi.submap |= (1U << XENFEAT_ARM_SMCCC_supported);
#endif
#ifdef CONFIG_X86
            if ( is_pv_domain(d) )
                fi.submap |= (1U << XENFEAT_mmu_pt_update_preserve_ad) |
                             (1U << XENFEAT_highmem_assist) |
                             (1U << XENFEAT_gnttab_map_avail_bits);
            else
                fi.submap |= (1U << XENFEAT_hvm_safe_pvclock) |
                             (1U << XENFEAT_hvm_callback_vector) |
                             (has_pirq(d) ? (1U << XENFEAT_hvm_pirqs) : 0);
            fi.submap |= (1U << XENFEAT_dm_msix_all_writes);
#endif
            if ( !paging_mode_translate(d) || is_domain_direct_mapped(d) )
                fi.submap |= (1U << XENFEAT_direct_mapped);
            else
                fi.submap |= (1U << XENFEAT_not_direct_mapped);
            break;
        default:
            return -EINVAL;
        }

        if ( __copy_to_guest(arg, &fi, 1) )
            return -EFAULT;
        return 0;
    }

    case XENVER_pagesize:
        if ( deny )
            return 0;
        return (!guest_handle_is_null(arg) ? -EINVAL : PAGE_SIZE);

    case XENVER_guest_handle:
    {
        xen_domain_handle_t hdl;

        if ( deny )
            memset(&hdl, 0, ARRAY_SIZE(hdl));

        BUILD_BUG_ON(ARRAY_SIZE(current->domain->handle) != ARRAY_SIZE(hdl));

        if ( copy_to_guest(arg, deny ? hdl : current->domain->handle,
                           ARRAY_SIZE(hdl) ) )
            return -EFAULT;
        return 0;
    }

    case XENVER_commandline:
    {
        size_t len = ARRAY_SIZE(saved_cmdline);

        if ( deny )
            len = strlen(xen_deny()) + 1;

        if ( copy_to_guest(arg, deny ? xen_deny() : saved_cmdline, len) )
            return -EFAULT;
        return 0;
    }

    case XENVER_build_id:
    {
        xen_build_id_t build_id;
        unsigned int sz;
        int rc;
        const void *p;

        if ( deny )
            return -EPERM;

        /* Only return size. */
        if ( !guest_handle_is_null(arg) )
        {
            if ( copy_from_guest(&build_id, arg, 1) )
                return -EFAULT;

            if ( build_id.len == 0 )
                return -EINVAL;
        }

        rc = xen_build_id(&p, &sz);
        if ( rc )
            return rc;

        if ( guest_handle_is_null(arg) )
            return sz;

        if ( sz > build_id.len )
            return -ENOBUFS;

        if ( copy_to_guest_offset(arg, offsetof(xen_build_id_t, buf), p, sz) )
            return -EFAULT;

        return sz;
    }
    }

    return -ENOSYS;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: common_kernel.c === */
/* === BEGIN INLINED: kernel.c === */
#include <xen_xen_config.h>
/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Kernel image loading.
 *
 * Copyright (C) 2011 Citrix Systems, Inc.
 */
#include <xen_domain_page.h>
#include <xen_errno.h>
#include <xen_guest_access.h>
#include <xen_gunzip.h>
#include <xen_init.h>
#include <xen_lib.h>
#include <xen_libfdt_libfdt.h>
#include <xen_mm.h>
#include <xen_sched.h>
#include <xen_vmap.h>

#include <asm_byteorder.h>
#include <asm_kernel.h>
#include <asm_setup.h>

#define UIMAGE_MAGIC          0x27051956
#define UIMAGE_NMLEN          32

#define ZIMAGE32_MAGIC_OFFSET 0x24
#define ZIMAGE32_START_OFFSET 0x28
#define ZIMAGE32_END_OFFSET   0x2c
#define ZIMAGE32_HEADER_LEN   0x30

#define ZIMAGE32_MAGIC 0x016f2818

#define ZIMAGE64_MAGIC_V0 0x14000008
#define ZIMAGE64_MAGIC_V1 0x644d5241 /* "ARM\x64" */

struct minimal_dtb_header {
    uint32_t magic;
    uint32_t total_size;
    /* There are other fields but we don't use them yet. */
};

#define DTB_MAGIC 0xd00dfeedU

static void __init place_modules(struct kernel_info *info,
                                 paddr_t kernbase, paddr_t kernend)
{
    /* Align DTB and initrd size to 2Mb. Linux only requires 4 byte alignment */
    const struct bootmodule *mod = info->initrd_bootmodule;
    const struct membanks *mem = kernel_info_get_mem(info);
    const paddr_t initrd_len = ROUNDUP(mod ? mod->size : 0, MB(2));
    const paddr_t dtb_len = ROUNDUP(fdt_totalsize(info->fdt), MB(2));
    const paddr_t modsize = initrd_len + dtb_len;

    /* Convenient */
    const paddr_t rambase = mem->bank[0].start;
    const paddr_t ramsize = mem->bank[0].size;
    const paddr_t ramend = rambase + ramsize;
    const paddr_t kernsize = ROUNDUP(kernend, MB(2)) - kernbase;
    const paddr_t ram128mb = rambase + MB(128);

    paddr_t modbase;

    if ( modsize + kernsize > ramsize )
        panic("Not enough memory in the first bank for the kernel+dtb+initrd\n");

    /*
     * DTB must be loaded such that it does not conflict with the
     * kernel decompressor. For 32-bit Linux Documentation/arm/Booting
     * recommends just after the 128MB boundary while for 64-bit Linux
     * the recommendation in Documentation/arm64/booting.txt is below
     * 512MB.
     *
     * If the bootloader provides an initrd, it will be loaded just
     * after the DTB.
     *
     * We try to place dtb+initrd at 128MB or if we have less RAM
     * as high as possible. If there is no space then fallback to
     * just before the kernel.
     *
     * If changing this then consider
     * tools/libxc/xc_dom_arm.c:arch_setup_meminit as well.
     */
    if ( ramend >= ram128mb + modsize && kernend < ram128mb )
        modbase = ram128mb;
    else if ( ramend - modsize > ROUNDUP(kernend, MB(2)) )
        modbase = ramend - modsize;
    else if ( kernbase - rambase > modsize )
        modbase = kernbase - modsize;
    else
    {
        panic("Unable to find suitable location for dtb+initrd\n");
        return;
    }

    info->dtb_paddr = modbase;
    info->initrd_paddr = info->dtb_paddr + dtb_len;
}

static paddr_t __init kernel_zimage_place(struct kernel_info *info)
{
    const struct membanks *mem = kernel_info_get_mem(info);
    paddr_t load_addr;

#ifdef CONFIG_ARM_64
    if ( (info->type == DOMAIN_64BIT) && (info->zimage.start == 0) )
        return mem->bank[0].start + info->zimage.text_offset;
#endif

    /*
     * If start is zero, the zImage is position independent, in this
     * case Documentation/arm/Booting recommends loading below 128MiB
     * and above 32MiB. Load it as high as possible within these
     * constraints, while also avoiding the DTB.
     */
    if ( info->zimage.start == 0 )
    {
        paddr_t load_end;

        load_end = mem->bank[0].start + mem->bank[0].size;
        load_end = MIN(mem->bank[0].start + MB(128), load_end);

        load_addr = load_end - info->zimage.len;
        /* Align to 2MB */
        load_addr &= ~((2 << 20) - 1);
    }
    else
        load_addr = info->zimage.start;

    return load_addr;
}

static void __init kernel_zimage_load(struct kernel_info *info)
{
    paddr_t load_addr = kernel_zimage_place(info);
    paddr_t paddr = info->zimage.kernel_addr;
    paddr_t len = info->zimage.len;
    void *kernel;
    int rc;

    /*
     * If the image does not have a fixed entry point, then use the load
     * address as the entry point.
     */
    if ( info->entry == 0 )
        info->entry = load_addr;

    place_modules(info, load_addr, load_addr + len);

    printk("Loading zImage from %"PRIpaddr" to %"PRIpaddr"-%"PRIpaddr"\n",
           paddr, load_addr, load_addr + len);

    kernel = ioremap_wc(paddr, len);
    if ( !kernel )
        panic("Unable to map the hwdom kernel\n");

    rc = copy_to_guest_phys_flush_dcache(info->d, load_addr,
                                         kernel, len);
    if ( rc != 0 )
        panic("Unable to copy the kernel in the hwdom memory\n");

    iounmap(kernel);
}

static __init uint32_t output_length(char *image, unsigned long image_len)
{
    return *(uint32_t *)&image[image_len - 4];
}

static __init int kernel_decompress(struct bootmodule *mod, uint32_t offset)
{
    char *output, *input;
    char magic[2];
    int rc;
    unsigned int kernel_order_out;
    paddr_t output_size;
    struct page_info *pages;
    mfn_t mfn;
    int i;
    paddr_t addr = mod->start;
    paddr_t size = mod->size;

    if ( size < offset )
        return -EINVAL;

    /*
     * It might be that gzip header does not appear at the start address
     * (e.g. in case of compressed uImage) so take into account offset to
     * gzip header.
     */
    addr += offset;
    size -= offset;

    if ( size < 2 )
        return -EINVAL;

    copy_from_paddr(magic, addr, sizeof(magic));

    /* only gzip is supported */
    if ( !gzip_check(magic, size) )
        return -EINVAL;

    input = ioremap_cache(addr, size);
    if ( input == NULL )
        return -EFAULT;

    output_size = output_length(input, size);
    kernel_order_out = get_order_from_bytes(output_size);
    pages = alloc_domheap_pages(NULL, kernel_order_out, 0);
    if ( pages == NULL )
    {
        iounmap(input);
        return -ENOMEM;
    }
    mfn = page_to_mfn(pages);
    output = __vmap(&mfn, 1 << kernel_order_out, 1, 1, PAGE_HYPERVISOR, VMAP_DEFAULT);

    rc = perform_gunzip(output, input, size);
    clean_dcache_va_range(output, output_size);
    iounmap(input);
    vunmap(output);

    if ( rc )
    {
        free_domheap_pages(pages, kernel_order_out);
        return rc;
    }

    mod->start = page_to_maddr(pages);
    mod->size = output_size;

    /*
     * Need to free pages after output_size here because they won't be
     * freed by discard_initial_modules
     */
    i = PFN_UP(output_size);
    for ( ; i < (1 << kernel_order_out); i++ )
        free_domheap_page(pages + i);

    /*
     * When freeing the kernel, we need to pass the module start address and
     * size as they were before taking an offset to gzip header into account,
     * so that the entire region will be freed.
     */
    addr -= offset;
    size += offset;

    /*
     * Free the original kernel, update the pointers to the
     * decompressed kernel
     */
    fw_unreserved_regions(addr, addr + size, init_domheap_pages, 0);

    return 0;
}

/*
 * Uimage CPU Architecture Codes
 */
#define IH_ARCH_ARM             2       /* ARM          */
#define IH_ARCH_ARM64           22      /* ARM64        */

/* uImage Compression Types */
#define IH_COMP_GZIP            1

/*
 * Check if the image is a uImage and setup kernel_info
 */
static int __init kernel_uimage_probe(struct kernel_info *info,
                                      struct bootmodule *mod)
{
    struct {
        __be32 magic;   /* Image Header Magic Number */
        __be32 hcrc;    /* Image Header CRC Checksum */
        __be32 time;    /* Image Creation Timestamp  */
        __be32 size;    /* Image Data Size           */
        __be32 load;    /* Data Load Address         */
        __be32 ep;      /* Entry Point Address       */
        __be32 dcrc;    /* Image Data CRC Checksum   */
        uint8_t os;     /* Operating System          */
        uint8_t arch;   /* CPU architecture          */
        uint8_t type;   /* Image Type                */
        uint8_t comp;   /* Compression Type          */
        uint8_t name[UIMAGE_NMLEN]; /* Image Name  */
    } uimage;

    uint32_t len;
    paddr_t addr = mod->start;
    paddr_t size = mod->size;

    if ( size < sizeof(uimage) )
        return -ENOENT;

    copy_from_paddr(&uimage, addr, sizeof(uimage));

    if ( be32_to_cpu(uimage.magic) != UIMAGE_MAGIC )
        return -ENOENT;

    len = be32_to_cpu(uimage.size);

    if ( len > size - sizeof(uimage) )
        return -EINVAL;

    /* Only gzip compression is supported. */
    if ( uimage.comp && uimage.comp != IH_COMP_GZIP )
    {
        printk(XENLOG_ERR
               "Unsupported uImage compression type %"PRIu8"\n", uimage.comp);
        return -EOPNOTSUPP;
    }

    info->zimage.start = be32_to_cpu(uimage.load);
    info->entry = be32_to_cpu(uimage.ep);

    /*
     * While uboot considers 0x0 to be a valid load/start address, for Xen
     * to maintain parity with zImage, we consider 0x0 to denote position
     * independent image. That means Xen is free to load such an image at
     * any valid address.
     */
    if ( info->zimage.start == 0 )
        printk(XENLOG_INFO
               "No load address provided. Xen will decide where to load it.\n");
    else
        printk(XENLOG_INFO
               "Provided load address: %"PRIpaddr" and entry address: %"PRIpaddr"\n",
               info->zimage.start, info->entry);

    /*
     * If the image supports position independent execution, then user cannot
     * provide an entry point as Xen will load such an image at any appropriate
     * memory address. Thus, we need to return error.
     */
    if ( (info->zimage.start == 0) && (info->entry != 0) )
    {
        printk(XENLOG_ERR
               "Entry point cannot be non zero for PIE image.\n");
        return -EINVAL;
    }

    if ( uimage.comp )
    {
        int rc;

        /*
         * In case of a compressed uImage, the gzip header is right after
         * the u-boot header, so pass sizeof(uimage) as an offset to gzip
         * header.
         */
        rc = kernel_decompress(mod, sizeof(uimage));
        if ( rc )
            return rc;

        info->zimage.kernel_addr = mod->start;
        info->zimage.len = mod->size;
    }
    else
    {
        info->zimage.kernel_addr = addr + sizeof(uimage);
        info->zimage.len = len;
    }

    info->load = kernel_zimage_load;

#ifdef CONFIG_ARM_64
    switch ( uimage.arch )
    {
    case IH_ARCH_ARM:
        info->type = DOMAIN_32BIT;
        break;
    case IH_ARCH_ARM64:
        info->type = DOMAIN_64BIT;
        break;
    default:
        printk(XENLOG_ERR "Unsupported uImage arch type %d\n", uimage.arch);
        return -EINVAL;
    }

    /*
     * If there is a uImage header, then we do not parse zImage or zImage64
     * header. In other words if the user provides a uImage header on top of
     * zImage or zImage64 header, Xen uses the attributes of uImage header only.
     * Thus, Xen uses uimage.load attribute to determine the load address and
     * zimage.text_offset is ignored.
     */
    info->zimage.text_offset = 0;
#endif

    return 0;
}

#ifdef CONFIG_ARM_64
/*
 * Check if the image is a 64-bit Image.
 */
static int __init kernel_zimage64_probe(struct kernel_info *info,
                                        paddr_t addr, paddr_t size)
{
    /* linux/Documentation/arm64/booting.txt */
    struct {
        uint32_t magic0;
        uint32_t res0;
        uint64_t text_offset;  /* Image load offset */
        uint64_t res1;
        uint64_t res2;
        /* zImage V1 only from here */
        uint64_t res3;
        uint64_t res4;
        uint64_t res5;
        uint32_t magic1;
        uint32_t res6;
    } zimage;
    uint64_t start, end;

    if ( size < sizeof(zimage) )
        return -EINVAL;

    copy_from_paddr(&zimage, addr, sizeof(zimage));

    if ( zimage.magic0 != ZIMAGE64_MAGIC_V0 &&
         zimage.magic1 != ZIMAGE64_MAGIC_V1 )
        return -EINVAL;

    /* Currently there is no length in the header, so just use the size */
    start = 0;
    end = size;

    /*
     * Given the above this check is a bit pointless, but leave it
     * here in case someone adds a length field in the future.
     */
    if ( (end - start) > size )
        return -EINVAL;

    info->zimage.kernel_addr = addr;
    info->zimage.len = end - start;
    info->zimage.text_offset = zimage.text_offset;
    info->zimage.start = 0;

    info->load = kernel_zimage_load;

    info->type = DOMAIN_64BIT;

    return 0;
}
#endif

/*
 * Check if the image is a 32-bit zImage and setup kernel_info
 */
static int __init kernel_zimage32_probe(struct kernel_info *info,
                                        paddr_t addr, paddr_t size)
{
    uint32_t zimage[ZIMAGE32_HEADER_LEN/4];
    uint32_t start, end;
    struct minimal_dtb_header dtb_hdr;

    if ( size < ZIMAGE32_HEADER_LEN )
        return -EINVAL;

    copy_from_paddr(zimage, addr, sizeof(zimage));

    if (zimage[ZIMAGE32_MAGIC_OFFSET/4] != ZIMAGE32_MAGIC)
        return -EINVAL;

    start = zimage[ZIMAGE32_START_OFFSET/4];
    end = zimage[ZIMAGE32_END_OFFSET/4];

    if ( (end - start) > size )
        return -EINVAL;

    /*
     * Check for an appended DTB.
     */
    if ( addr + end - start + sizeof(dtb_hdr) <= size )
    {
        copy_from_paddr(&dtb_hdr, addr + end - start, sizeof(dtb_hdr));
        if (be32_to_cpu(dtb_hdr.magic) == DTB_MAGIC) {
            end += be32_to_cpu(dtb_hdr.total_size);

            if ( end > addr + size )
                return -EINVAL;
        }
    }

    info->zimage.kernel_addr = addr;

    info->zimage.start = start;
    info->zimage.len = end - start;

    info->load = kernel_zimage_load;

#ifdef CONFIG_ARM_64
    info->type = DOMAIN_32BIT;
#endif

    return 0;
}

int __init kernel_probe(struct kernel_info *info,
                        const struct dt_device_node *domain)
{
    struct bootmodule *mod = NULL;
    struct bootcmdline *cmd = NULL;
    struct dt_device_node *node;
    u64 kernel_addr, initrd_addr, dtb_addr, size;
    int rc;

    /*
     * We need to initialize start to 0. This field may be populated during
     * kernel_xxx_probe() if the image has a fixed entry point (for e.g.
     * uimage.ep).
     * We will use this to determine if the image has a fixed entry point or
     * the load address should be used as the start address.
     */
    info->entry = 0;

    /* domain is NULL only for the hardware domain */
    if ( domain == NULL )
    {
        ASSERT(is_hardware_domain(info->d));

        mod = boot_module_find_by_kind(BOOTMOD_KERNEL);

        info->kernel_bootmodule = mod;
        info->initrd_bootmodule = boot_module_find_by_kind(BOOTMOD_RAMDISK);

        cmd = boot_cmdline_find_by_kind(BOOTMOD_KERNEL);
        if ( cmd )
            info->cmdline = &cmd->cmdline[0];
    }
    else
    {
        const char *name = NULL;

        dt_for_each_child_node(domain, node)
        {
            if ( dt_device_is_compatible(node, "multiboot,kernel") )
            {
                u32 len;
                const __be32 *val;

                val = dt_get_property(node, "reg", &len);
                dt_get_range(&val, node, &kernel_addr, &size);
                mod = boot_module_find_by_addr_and_kind(
                        BOOTMOD_KERNEL, kernel_addr);
                info->kernel_bootmodule = mod;
            }
            else if ( dt_device_is_compatible(node, "multiboot,ramdisk") )
            {
                u32 len;
                const __be32 *val;

                val = dt_get_property(node, "reg", &len);
                dt_get_range(&val, node, &initrd_addr, &size);
                info->initrd_bootmodule = boot_module_find_by_addr_and_kind(
                        BOOTMOD_RAMDISK, initrd_addr);
            }
            else if ( dt_device_is_compatible(node, "multiboot,device-tree") )
            {
                uint32_t len;
                const __be32 *val;

                val = dt_get_property(node, "reg", &len);
                if ( val == NULL )
                    continue;
                dt_get_range(&val, node, &dtb_addr, &size);
                info->dtb_bootmodule = boot_module_find_by_addr_and_kind(
                        BOOTMOD_GUEST_DTB, dtb_addr);
            }
            else
                continue;
        }
        name = dt_node_name(domain);
        cmd = boot_cmdline_find_by_name(name);
        if ( cmd )
            info->cmdline = &cmd->cmdline[0];
    }
    if ( !mod || !mod->size )
    {
        printk(XENLOG_ERR "Missing kernel boot module?\n");
        return -ENOENT;
    }

    printk("Loading %pd kernel from boot module @ %"PRIpaddr"\n",
           info->d, info->kernel_bootmodule->start);
    if ( info->initrd_bootmodule )
        printk("Loading ramdisk from boot module @ %"PRIpaddr"\n",
               info->initrd_bootmodule->start);

    /*
     * uImage header always appears at the top of the image (even compressed),
     * so it needs to be probed first. Note that in case of compressed uImage,
     * kernel_decompress is called from kernel_uimage_probe making the function
     * self-containing (i.e. fall through only in case of a header not found).
     */
    rc = kernel_uimage_probe(info, mod);
    if ( rc != -ENOENT )
        return rc;

    /*
     * If it is a gzip'ed image, 32bit or 64bit, uncompress it.
     * At this point, gzip header appears (if at all) at the top of the image,
     * so pass 0 as an offset.
     */
    rc = kernel_decompress(mod, 0);
    if ( rc && rc != -EINVAL )
        return rc;

#ifdef CONFIG_ARM_64
    rc = kernel_zimage64_probe(info, mod->start, mod->size);
    if (rc < 0)
#endif
        rc = kernel_zimage32_probe(info, mod->start, mod->size);

    return rc;
}

void __init kernel_load(struct kernel_info *info)
{
    info->load(info);
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: kernel.c === */
/* === BEGIN INLINED: version.c === */
#include <xen_xen_config.h>
#include <xen_bug.h>
#include <xen_compile.h>
#include <xen_init.h>
#include <xen_errno.h>
#include <xen_lib.h>
#include <xen_string.h>
#include <xen_types.h>
#include <xen_efi.h>
#include <xen_elf.h>
#include <xen_version.h>

#include <asm_cache.h>

const char *xen_compile_date(void)
{
    return XEN_COMPILE_DATE;
}


const char *xen_compile_by(void)
{
    return XEN_COMPILE_BY;
}

const char *xen_compile_domain(void)
{
    return XEN_COMPILE_DOMAIN;
}


const char *xen_compiler(void)
{
    return XEN_COMPILER;
}

unsigned int xen_major_version(void)
{
    return XEN_VERSION;
}

unsigned int xen_minor_version(void)
{
    return XEN_SUBVERSION;
}

const char *xen_extra_version(void)
{
    return XEN_EXTRAVERSION;
}

const char *xen_changeset(void)
{
    return XEN_CHANGESET;
}

const char *xen_banner(void)
{
    return XEN_BANNER;
}

const char *xen_deny(void)
{
    return "<denied>";
}

static const char build_info[] =
    "debug="
#ifdef CONFIG_DEBUG
    "y"
#else
    "n"
#endif
#ifdef CONFIG_COVERAGE
# ifdef __clang__
    " llvmcov=y"
# else
    " gcov=y"
# endif
#endif
#ifdef CONFIG_UBSAN
    " ubsan=y"
#endif
    "";

const char *xen_build_info(void)
{
    return build_info;
}

static const void *build_id_p __read_mostly;
static unsigned int build_id_len __read_mostly;

int xen_build_id(const void **p, unsigned int *len)
{
    if ( !build_id_len )
        return -ENODATA;

    *len = build_id_len;
    *p = build_id_p;

    return 0;
}

#ifdef BUILD_ID
/* Defined in linker script. */
extern const Elf_Note __note_gnu_build_id_start[], __note_gnu_build_id_end[];

int xen_build_id_check(const Elf_Note *n, unsigned int n_sz,
                       const void **p, unsigned int *len)
{
    /* Check if we really have a build-id. */
    ASSERT(n_sz > sizeof(*n));

    if ( NT_GNU_BUILD_ID != n->type )
        return -ENODATA;

    if ( n->namesz + n->descsz < n->namesz )
        return -EINVAL;

    if ( n->namesz < 4 /* GNU\0 */)
        return -EINVAL;

    if ( n->namesz + n->descsz > n_sz - sizeof(*n) )
        return -EINVAL;

    /* Sanity check, name should be "GNU" for ld-generated build-id. */
    if ( strncmp(ELFNOTE_NAME(n), "GNU", n->namesz) != 0 )
        return -ENODATA;

    if ( len )
        *len = n->descsz;
    if ( p )
        *p = ELFNOTE_DESC(n);

    return 0;
}

struct pe_external_debug_directory
{
    uint32_t characteristics;
    uint32_t time_stamp;
    uint16_t major_version;
    uint16_t minor_version;
#define PE_IMAGE_DEBUG_TYPE_CODEVIEW 2
    uint32_t type;
    uint32_t size;
    uint32_t rva_of_data;
    uint32_t filepos_of_data;
};

struct cv_info_pdb70
{
#define CVINFO_PDB70_CVSIGNATURE 0x53445352 /* "RSDS" */
    uint32_t cv_signature;
    unsigned char signature[16];
    uint32_t age;
    char pdb_filename[];
};

void __init xen_build_init(void)
{
    const Elf_Note *n = __note_gnu_build_id_start;
    unsigned int sz;
    int rc;

    /* --build-id invoked with wrong parameters. */
    if ( __note_gnu_build_id_end <= &n[0] )
        return;

    /* Check for full Note header. */
    if ( &n[1] >= __note_gnu_build_id_end )
        return;

    sz = (uintptr_t)__note_gnu_build_id_end - (uintptr_t)n;

    rc = xen_build_id_check(n, sz, &build_id_p, &build_id_len);

#ifdef CONFIG_X86
    /* Alternatively we may have a CodeView record from an EFI build. */
    if ( rc && efi_enabled(EFI_LOADER) )
    {
        const struct pe_external_debug_directory *dir = (const void *)n;

        /*
         * Validate that the full-note-header check above won't prevent
         * fall-through to the CodeView case here.
         */
        BUILD_BUG_ON(sizeof(*n) > sizeof(*dir));

        if ( sz > sizeof(*dir) + sizeof(struct cv_info_pdb70) &&
             dir->type == PE_IMAGE_DEBUG_TYPE_CODEVIEW &&
             dir->size > sizeof(struct cv_info_pdb70) &&
             XEN_VIRT_START + dir->rva_of_data == (unsigned long)(dir + 1) )
        {
            const struct cv_info_pdb70 *info = (const void *)(dir + 1);

            if ( info->cv_signature == CVINFO_PDB70_CVSIGNATURE )
            {
                build_id_p = info->signature;
                build_id_len = sizeof(info->signature);
                rc = 0;
            }
        }
    }
#endif
    if ( !rc )
        printk(XENLOG_INFO "build-id: %*phN\n", build_id_len, build_id_p);
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

/* === END INLINED: version.c === */
/* === BEGIN INLINED: symbols.c === */
#include <xen_xen_config.h>
/*
 * symbols.c: in-kernel printing of symbolic oopses and stack traces.
 *
 * Copyright 2002 Rusty Russell <rusty@rustcorp.com.au> IBM Corporation
 *
 * ChangeLog:
 *
 * (25/Aug/2004) Paulo Marques <pmarques@grupopie.com>
 *      Changed the compression method from stem compression to "table lookup"
 *      compression (see tools/symbols.c for a more complete description)
 */

#include <xen_symbols.h>
#include <xen_kernel.h>
#include <xen_init.h>
#include <xen_lib.h>
#include <xen_string.h>
#include <xen_spinlock.h>
#include <xen_virtual_region.h>
#include <public_platform.h>
#include <xen_guest_access.h>
#include <xen_errno.h>

#ifdef SYMBOLS_ORIGIN
extern const unsigned int symbols_offsets[];
#define symbols_address(n) (SYMBOLS_ORIGIN + symbols_offsets[n])
#else
extern const unsigned long symbols_addresses[];
#define symbols_address(n) symbols_addresses[n]
#endif
extern const unsigned int symbols_num_syms;
extern const u8 symbols_names[];

extern const struct symbol_offset symbols_sorted_offsets[];

extern const u8 symbols_token_table[];
extern const u16 symbols_token_index[];

extern const unsigned int symbols_markers[];

/* expand a compressed symbol data into the resulting uncompressed string,
   given the offset to where the symbol is in the compressed stream */
static unsigned int symbols_expand_symbol(unsigned int off, char *result)
{
    int len, skipped_first = 0;
    const u8 *tptr, *data;

    /* get the compressed symbol length from the first symbol byte */
    data = &symbols_names[off];
    len = *data;
    data++;

    /* update the offset to return the offset for the next symbol on
     * the compressed stream */
    off += len + 1;

    /* for every byte on the compressed symbol data, copy the table
       entry for that byte */
    while(len) {
        tptr = &symbols_token_table[ symbols_token_index[*data] ];
        data++;
        len--;

        while (*tptr) {
            if(skipped_first) {
                *result = *tptr;
                result++;
            } else
                skipped_first = 1;
            tptr++;
        }
    }

    *result = '\0';

    /* return to offset to the next symbol */
    return off;
}

/* find the offset on the compressed stream given and index in the
 * symbols array */
static unsigned int get_symbol_offset(unsigned long pos)
{
    const u8 *name;
    int i;

    /* use the closest marker we have. We have markers every 256 positions,
     * so that should be close enough */
    name = &symbols_names[ symbols_markers[pos>>8] ];

    /* sequentially scan all the symbols up to the point we're searching for.
     * Every symbol is stored in a [<len>][<len> bytes of data] format, so we
     * just need to add the len to the current pointer for every symbol we
     * wish to skip */
    for(i = 0; i < (pos&0xFF); i++)
        name = name + (*name) + 1;

    return name - symbols_names;
}


const char *symbols_lookup(unsigned long addr,
                           unsigned long *symbolsize,
                           unsigned long *offset,
                           char *namebuf)
{
    unsigned long i, low, high, mid;
    unsigned long symbol_end = 0;
    const struct virtual_region *region;

    namebuf[KSYM_NAME_LEN] = 0;
    namebuf[0] = 0;

    region = find_text_region(addr);
    if (!region)
        return NULL;

    if (region->symbols_lookup)
        return region->symbols_lookup(addr, symbolsize, offset, namebuf);

        /* do a binary search on the sorted symbols_addresses array */
    low = 0;
    high = symbols_num_syms;

    while (high-low > 1) {
        mid = (low + high) / 2;
        if (symbols_address(mid) <= addr) low = mid;
        else high = mid;
    }

    /* search for the first aliased symbol. Aliased symbols are
           symbols with the same address */
    while (low && symbols_address(low - 1) == symbols_address(low))
        --low;

        /* Grab name */
    symbols_expand_symbol(get_symbol_offset(low), namebuf);

    /* Search for next non-aliased symbol */
    for (i = low + 1; i < symbols_num_syms; i++) {
        if (symbols_address(i) > symbols_address(low)) {
            symbol_end = symbols_address(i);
            break;
        }
    }

    // /* if we found no next symbol, we use the end of the section */
    // if (!symbol_end)
    //     symbol_end = is_kernel_inittext(addr) ?
    //         (unsigned long)_einittext : (unsigned long)_etext;

    // *symbolsize = symbol_end - symbols_address(low);
    // *offset = addr - symbols_address(low);
    return namebuf;
}

/*
 * Get symbol type information. This is encoded as a single char at the
 * beginning of the symbol name.
 */
static char symbols_get_symbol_type(unsigned int off)
{
    /*
     * Get just the first code, look it up in the token table,
     * and return the first char from this token.
     */
    return symbols_token_table[symbols_token_index[symbols_names[off + 1]]];
}



/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: symbols.c === */
/* === BEGIN INLINED: symbols-dummy.c === */
#include <xen_xen_config.h>
/*
 * symbols-dummy.c: dummy symbol-table definitions for the inital partial
 *                  link of the hypervisor image.
 */

#include <xen_types.h>
#include <xen_symbols.h>

#ifdef SYMBOLS_ORIGIN
const unsigned int symbols_offsets[1];
#else
const unsigned long symbols_addresses[1];
#endif
const unsigned int symbols_num_syms;
const u8 symbols_names[1];

#ifdef CONFIG_FAST_SYMBOL_LOOKUP
const struct symbol_offset symbols_sorted_offsets[1];
#endif

const u8 symbols_token_table[1];
const u16 symbols_token_index[1];

const unsigned int symbols_markers[1];

/* === END INLINED: symbols-dummy.c === */
/* === BEGIN INLINED: ctors.c === */
#include <xen_xen_config.h>
#include <xen_init.h>
#include <xen_lib.h>

typedef void (*ctor_func_t)(void);
extern const ctor_func_t __ctors_start[], __ctors_end[];

void __init init_constructors(void)
{
    // const ctor_func_t *f;
    // for ( f = __ctors_start; f < __ctors_end; ++f )
    //     (*f)();

    // /* Putting this here seems as good (or bad) as any other place. */
    // BUILD_BUG_ON(sizeof(size_t) != sizeof(ssize_t));
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: ctors.c === */
/* === BEGIN INLINED: io.c === */
#include <xen_xen_config.h>
/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * xen/arch/arm/io.c
 *
 * ARM I/O handlers
 *
 * Copyright (c) 2011 Citrix Systems.
 */

#include <xen_ioreq.h>
#include <xen_lib.h>
#include <xen_spinlock.h>
#include <xen_sched.h>
#include <xen_sort.h>
#include <asm_cpuerrata.h>
#include <asm_current.h>
#include <asm_ioreq.h>
#include <asm_mmio.h>
#include <asm_traps.h>

#include "decode.h"

static enum io_state handle_read(const struct mmio_handler *handler,
                                 struct vcpu *v,
                                 mmio_info_t *info)
{
    const struct hsr_dabt dabt = info->dabt;
    struct cpu_user_regs *regs = guest_cpu_user_regs();
    /*
     * Initialize to zero to avoid leaking data if there is an
     * implementation error in the emulation (such as not correctly
     * setting r).
     */
    register_t r = 0;

    if ( !handler->ops->read(v, info, &r, handler->priv) )
        return IO_ABORT;

    r = sign_extend(dabt, r);

    set_user_reg(regs, dabt.reg, r);

    return IO_HANDLED;
}

static enum io_state handle_write(const struct mmio_handler *handler,
                                  struct vcpu *v,
                                  mmio_info_t *info)
{
    const struct hsr_dabt dabt = info->dabt;
    struct cpu_user_regs *regs = guest_cpu_user_regs();
    int ret;

    ret = handler->ops->write(v, info, get_user_reg(regs, dabt.reg),
                              handler->priv);
    return ret ? IO_HANDLED : IO_ABORT;
}

/* This function assumes that mmio regions are not overlapped */
static int cmp_mmio_handler(const void *key, const void *elem)
{
    const struct mmio_handler *handler0 = key;
    const struct mmio_handler *handler1 = elem;

    if ( handler0->addr < handler1->addr )
        return -1;

    if ( handler0->addr >= (handler1->addr + handler1->size) )
        return 1;

    return 0;
}

static void swap_mmio_handler(void *_a, void *_b, size_t size)
{
    struct mmio_handler *a = _a, *b = _b;

    SWAP(*a, *b);
}

static const struct mmio_handler *find_mmio_handler(struct domain *d,
                                                    paddr_t gpa)
{
    struct vmmio *vmmio = &d->arch.vmmio;
    struct mmio_handler key = {.addr = gpa};
    const struct mmio_handler *handler;

    read_lock(&vmmio->lock);
    handler = bsearch(&key, vmmio->handlers, vmmio->num_entries,
                      sizeof(*handler), cmp_mmio_handler);
    read_unlock(&vmmio->lock);

    return handler;
}

void try_decode_instruction(const struct cpu_user_regs *regs,
                            mmio_info_t *info)
{
    int rc;

    if ( info->dabt.valid )
    {
        info->dabt_instr.state = INSTR_VALID;

        /*
         * Erratum 766422: Thumb store translation fault to Hypervisor may
         * not have correct HSR Rt value.
         */
        if ( check_workaround_766422() && (regs->cpsr & PSR_THUMB) &&
             info->dabt.write )
        {
            rc = decode_instruction(regs, info);
            if ( rc )
            {
                gprintk(XENLOG_DEBUG, "Unable to decode instruction\n");
                info->dabt_instr.state = INSTR_ERROR;
            }
        }
        return;
    }

    /*
     * At this point, we know that the stage1 translation table is either in an
     * emulated MMIO region or its address is invalid . This is not expected by
     * Xen and thus it forwards the abort to the guest.
     */
    if ( info->dabt.s1ptw )
    {
        info->dabt_instr.state = INSTR_ERROR;
        return;
    }

    /*
     * When the data abort is caused due to cache maintenance, Xen should check
     * if the address belongs to an emulated MMIO region or not. The behavior
     * will differ accordingly.
     */
    if ( info->dabt.cache )
    {
        info->dabt_instr.state = INSTR_CACHE;
        return;
    }

    /*
     * Armv8 processor does not provide a valid syndrome for decoding some
     * instructions. So in order to process these instructions, Xen must
     * decode them.
     */
    rc = decode_instruction(regs, info);
    if ( rc )
    {
        gprintk(XENLOG_ERR, "Unable to decode instruction\n");
        info->dabt_instr.state = INSTR_ERROR;
    }
}

enum io_state try_handle_mmio(struct cpu_user_regs *regs,
                              mmio_info_t *info)
{
    struct vcpu *v = current;
    const struct mmio_handler *handler = NULL;
    int rc;

    ASSERT(info->dabt.ec == HSR_EC_DATA_ABORT_LOWER_EL);

    if ( !(info->dabt.valid || (info->dabt_instr.state == INSTR_CACHE)) )
    {
        ASSERT_UNREACHABLE();
        return IO_ABORT;
    }

    handler = find_mmio_handler(v->domain, info->gpa);
    if ( !handler )
    {
        rc = try_fwd_ioserv(regs, v, info);
        if ( rc == IO_HANDLED )
            return handle_ioserv(regs, v);

        return rc;
    }

    /*
     * When the data abort is caused due to cache maintenance and the address
     * belongs to an emulated region, Xen should ignore this instruction.
     */
    if ( info->dabt_instr.state == INSTR_CACHE )
        return IO_HANDLED;

    /*
     * At this point, we know that the instruction is either valid or has been
     * decoded successfully. Thus, Xen should be allowed to execute the
     * instruction on the emulated MMIO region.
     */
    if ( info->dabt.write )
        return handle_write(handler, v, info);
    else
        return handle_read(handler, v, info);
}

void register_mmio_handler(struct domain *d,
                           const struct mmio_handler_ops *ops,
                           paddr_t addr, paddr_t size, void *priv)
{
    struct vmmio *vmmio = &d->arch.vmmio;
    struct mmio_handler *handler;

    BUG_ON(vmmio->num_entries >= vmmio->max_num_entries);

    write_lock(&vmmio->lock);

    handler = &vmmio->handlers[vmmio->num_entries];

    handler->ops = ops;
    handler->addr = addr;
    handler->size = size;
    handler->priv = priv;

    vmmio->num_entries++;

    /* Sort mmio handlers in ascending order based on base address */
    sort(vmmio->handlers, vmmio->num_entries, sizeof(struct mmio_handler),
         cmp_mmio_handler, swap_mmio_handler);

    write_unlock(&vmmio->lock);
}

int domain_io_init(struct domain *d, unsigned int max_count)
{
    rwlock_init(&d->arch.vmmio.lock);
    d->arch.vmmio.num_entries = 0;
    d->arch.vmmio.max_num_entries = max_count;
    d->arch.vmmio.handlers = xzalloc_array(struct mmio_handler, max_count);
    if ( !d->arch.vmmio.handlers )
        return -ENOMEM;

    return 0;
}

void domain_io_free(struct domain *d)
{
    xfree(d->arch.vmmio.handlers);
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: io.c === */
/* === BEGIN INLINED: keyhandler.c === */
#include <xen_xen_config.h>
/******************************************************************************
 * keyhandler.c
 */

#include <asm_regs.h>
#include <xen_delay.h>
#include <xen_keyhandler.h>
#include <xen_param.h>
#include <xen_shutdown.h>
#include <xen_event.h>
#include <xen_console.h>
#include <xen_serial.h>
#include <xen_sched.h>
#include <xen_tasklet.h>
#include <xen_domain.h>
#include <xen_rangeset.h>
#include <xen_compat.h>
#include <xen_ctype.h>
#include <xen_perfc.h>
#include <xen_mm.h>
#include <xen_watchdog.h>
#include <xen_init.h>
#include <asm_div64.h>

static unsigned char keypress_key;
static bool alt_key_handling;

static keyhandler_fn_t cf_check show_handlers, cf_check dump_hwdom_registers,
    cf_check dump_domains, cf_check read_clocks;
static irq_keyhandler_fn_t cf_check do_toggle_alt_key, cf_check dump_registers,
    cf_check reboot_machine, cf_check run_all_keyhandlers;

static struct keyhandler {
    union {
        keyhandler_fn_t *fn;
        irq_keyhandler_fn_t *irq_fn;
    };

    const char *desc;    /* Description for help message.                 */
    bool irq_callback,   /* Call in irq context? if not, tasklet context. */
        diagnostic;      /* Include in 'dump all' handler.                */
} key_table[128] __read_mostly =
{
#define KEYHANDLER(k, f, desc, diag)            \
    [k] = { { .fn = (f) }, desc, 0, diag }

#define IRQ_KEYHANDLER(k, f, desc, diag)        \
    [k] = { { .irq_fn = (f) }, desc, 1, diag }

    IRQ_KEYHANDLER('A', do_toggle_alt_key, "toggle alternative key handling", 0),
    IRQ_KEYHANDLER('d', dump_registers, "dump registers", 1),
        KEYHANDLER('h', show_handlers, "show this message", 0),
        KEYHANDLER('q', dump_domains, "dump domain (and guest debug) info", 1),
        KEYHANDLER('r', dump_runq, "dump run queues", 1),
    IRQ_KEYHANDLER('R', reboot_machine, "reboot machine", 0),
        KEYHANDLER('t', read_clocks, "display multi-cpu clock info", 1),
        KEYHANDLER('0', dump_hwdom_registers, "dump Dom0 registers", 1),
    IRQ_KEYHANDLER('*', run_all_keyhandlers, "print all diagnostics", 0),

#ifdef CONFIG_PERF_COUNTERS
    KEYHANDLER('p', perfc_printall, "print performance counters", 1),
    KEYHANDLER('P', perfc_reset, "reset performance counters", 0),
#endif

#ifdef CONFIG_DEBUG_LOCK_PROFILE
    KEYHANDLER('l', spinlock_profile_printall, "print lock profile info", 1),
    KEYHANDLER('L', spinlock_profile_reset, "reset lock profile info", 0),
#endif

#undef IRQ_KEYHANDLER
#undef KEYHANDLER
};

static void cf_check keypress_action(void *unused)
{
    handle_keypress(keypress_key, true);
}

static DECLARE_TASKLET(keypress_tasklet, keypress_action, NULL);

void handle_keypress(unsigned char key, bool need_context)
{
    struct keyhandler *h;

    if ( key >= ARRAY_SIZE(key_table) || !(h = &key_table[key])->fn )
        return;

    if ( !in_irq() || h->irq_callback )
    {
        console_start_log_everything();
        h->irq_callback ? h->irq_fn(key, need_context) : h->fn(key);
        console_end_log_everything();
    }
    else
    {
        keypress_key = key;
        tasklet_schedule(&keypress_tasklet);
    }
}

void register_keyhandler(unsigned char key, keyhandler_fn_t *fn,
                         const char *desc, bool diagnostic)
{
    BUG_ON(key >= ARRAY_SIZE(key_table)); /* Key in range? */
    ASSERT(!key_table[key].fn);           /* Clobbering something else? */

    key_table[key].fn = fn;
    key_table[key].desc = desc;
    key_table[key].irq_callback = 0;
    key_table[key].diagnostic = diagnostic;
}

void register_irq_keyhandler(unsigned char key, irq_keyhandler_fn_t *fn,
                             const char *desc, bool diagnostic)
{
    BUG_ON(key >= ARRAY_SIZE(key_table)); /* Key in range? */
    ASSERT(!key_table[key].irq_fn);       /* Clobbering something else? */

    key_table[key].irq_fn = fn;
    key_table[key].desc = desc;
    key_table[key].irq_callback = 1;
    key_table[key].diagnostic = diagnostic;
}

static void cf_check show_handlers(unsigned char key)
{
    unsigned int i;

    printk("'%c' pressed -> showing installed handlers\n", key);
    for ( i = 0; i < ARRAY_SIZE(key_table); i++ )
        if ( key_table[i].fn )
            printk(" key '%c' (ascii '%02x') => %s\n",
                   isprint(i) ? i : ' ', i, key_table[i].desc);
}

static cpumask_t dump_execstate_mask;

void cf_check dump_execstate(const struct cpu_user_regs *regs)
{
    unsigned int cpu = smp_processor_id();

    if ( !guest_mode(regs) )
    {
        printk("*** Dumping CPU%u host state: ***\n", cpu);
        show_execution_state(regs);
    }

    if ( !is_idle_vcpu(current) )
    {
        printk("*** Dumping CPU%u guest state (%pv): ***\n",
               smp_processor_id(), current);
        show_execution_state(guest_cpu_user_regs());
        printk("\n");
    }

    cpumask_clear_cpu(cpu, &dump_execstate_mask);
    if ( !alt_key_handling )
        return;

    cpu = cpumask_cycle(cpu, &dump_execstate_mask);
    if ( cpu < nr_cpu_ids )
    {
        smp_send_state_dump(cpu);
        return;
    }

    console_end_sync();
    watchdog_enable();
}

static void cf_check dump_registers(
    unsigned char key, bool need_context)
{
    unsigned int cpu;

    /* We want to get everything out that we possibly can. */
    watchdog_disable();
    console_start_sync();

    printk("'%c' pressed -> dumping registers\n\n", key);

    cpumask_copy(&dump_execstate_mask, &cpu_online_map);

    /* Get local execution state out immediately, in case we get stuck. */
    if ( !need_context )
        dump_execstate(get_irq_regs() ?: guest_cpu_user_regs());
    else
        run_in_exception_handler(dump_execstate);

    /* Alt. handling: remaining CPUs are dumped asynchronously one-by-one. */
    if ( alt_key_handling )
        return;

    /* Normal handling: synchronously dump the remaining CPUs' states. */
    for_each_cpu ( cpu, &dump_execstate_mask )
    {
        smp_send_state_dump(cpu);
        while ( cpumask_test_cpu(cpu, &dump_execstate_mask) )
            cpu_relax();
    }

    console_end_sync();
    watchdog_enable();
}

static DECLARE_TASKLET(dump_hwdom_tasklet, NULL, NULL);

static void cf_check dump_hwdom_action(void *data)
{
    struct vcpu *v = data;

    for ( ; ; )
    {
        vcpu_show_execution_state(v);
        if ( (v = v->next_in_list) == NULL )
            break;
        if ( softirq_pending(smp_processor_id()) )
        {
            dump_hwdom_tasklet.data = v;
            tasklet_schedule_on_cpu(&dump_hwdom_tasklet, v->processor);
            break;
        }
    }
}

static void cf_check dump_hwdom_registers(unsigned char key)
{
    struct vcpu *v;

    if ( hardware_domain == NULL )
        return;

    printk("'%c' pressed -> dumping Dom0's registers\n", key);

    for_each_vcpu ( hardware_domain, v )
    {
        if ( alt_key_handling && softirq_pending(smp_processor_id()) )
        {
            tasklet_kill(&dump_hwdom_tasklet);
            tasklet_init(&dump_hwdom_tasklet, dump_hwdom_action, v);
            tasklet_schedule_on_cpu(&dump_hwdom_tasklet, v->processor);
            return;
        }
        vcpu_show_execution_state(v);
    }
}

static void cf_check reboot_machine(unsigned char key, bool unused)
{
    printk("'%c' pressed -> rebooting machine\n", key);
    machine_restart(0);
}

static void cf_check dump_domains(unsigned char key)
{
    struct domain *d;
    const struct sched_unit *unit;
    struct vcpu   *v;
    s_time_t       now = NOW();

    printk("'%c' pressed -> dumping domain info (now = %"PRI_stime")\n",
           key, now);

    rcu_read_lock(&domlist_read_lock);

    for_each_domain ( d )
    {
        unsigned int i;

        process_pending_softirqs();

        printk("General information for domain %u:\n", d->domain_id);
        printk("    refcnt=%d dying=%d pause_count=%d\n",
               atomic_read(&d->refcnt), d->is_dying,
               atomic_read(&d->pause_count));
        printk("    nr_pages=%u xenheap_pages=%u"
#ifdef CONFIG_MEM_SHARING
               " shared_pages=%u"
#endif
#ifdef CONFIG_MEM_PAGING
               " paged_pages=%u"
#endif
               " dirty_cpus={%*pbl} max_pages=%u\n",
               domain_tot_pages(d), d->xenheap_pages,
#ifdef CONFIG_MEM_SHARING
               atomic_read(&d->shr_pages),
#endif
#ifdef CONFIG_MEM_PAGING
               atomic_read(&d->paged_pages),
#endif
               CPUMASK_PR(d->dirty_cpumask), d->max_pages);
        printk("    handle=%02x%02x%02x%02x-%02x%02x-%02x%02x-"
               "%02x%02x-%02x%02x%02x%02x%02x%02x vm_assist=%08lx\n",
               d->handle[ 0], d->handle[ 1], d->handle[ 2], d->handle[ 3],
               d->handle[ 4], d->handle[ 5], d->handle[ 6], d->handle[ 7],
               d->handle[ 8], d->handle[ 9], d->handle[10], d->handle[11],
               d->handle[12], d->handle[13], d->handle[14], d->handle[15],
               d->vm_assist);
        for ( i = 0 ; i < NR_DOMAIN_WATCHDOG_TIMERS; i++ )
            if ( test_bit(i, &d->watchdog_inuse_map) )
                printk("    watchdog %d expires in %d seconds\n",
                       i, (u32)((d->watchdog_timer[i].expires - NOW()) >> 30));

        arch_dump_domain_info(d);

        rangeset_domain_printk(d);

        dump_pageframe_info(d);

        printk("NODE affinity for domain %d: [%*pbl]\n",
               d->domain_id, NODEMASK_PR(&d->node_affinity));

        printk("VCPU information and callbacks for domain %u:\n",
               d->domain_id);

        for_each_sched_unit ( d, unit )
        {
            printk("  UNIT%d affinities: hard={%*pbl} soft={%*pbl}\n",
                   unit->unit_id, CPUMASK_PR(unit->cpu_hard_affinity),
                   CPUMASK_PR(unit->cpu_soft_affinity));

            for_each_sched_unit_vcpu ( unit, v )
            {
                if ( !(v->vcpu_id & 0x3f) )
                    process_pending_softirqs();

                printk("    VCPU%d: CPU%d [has=%c] poll=%d "
                       "upcall_pend=%02x upcall_mask=%02x ",
                       v->vcpu_id, v->processor,
                       v->is_running ? 'T':'F', v->poll_evtchn,
                       vcpu_info(v, evtchn_upcall_pending),
                       !vcpu_event_delivery_is_enabled(v));
                if ( vcpu_cpu_dirty(v) )
                    printk("dirty_cpu=%u", read_atomic(&v->dirty_cpu));
                printk("\n");
                printk("    pause_count=%d pause_flags=%lx\n",
                       atomic_read(&v->pause_count), v->pause_flags);
                arch_dump_vcpu_info(v);

                if ( v->periodic_period == 0 )
                    printk("No periodic timer\n");
                else
                    printk("%"PRI_stime" Hz periodic timer (period %"PRI_stime" ms)\n",
                           1000000000 / v->periodic_period,
                           v->periodic_period / 1000000);
            }
        }
    }

    for_each_domain ( d )
    {
        for_each_vcpu ( d, v )
        {
            if ( !(v->vcpu_id & 0x3f) )
                process_pending_softirqs();

            printk("Notifying guest %d:%d (virq %d, port %d)\n",
                   d->domain_id, v->vcpu_id,
                   VIRQ_DEBUG, v->virq_to_evtchn[VIRQ_DEBUG]);
            send_guest_vcpu_virq(v, VIRQ_DEBUG);
        }
    }

#ifdef CONFIG_MEM_SHARING
    arch_dump_shared_mem_info();
#endif

    rcu_read_unlock(&domlist_read_lock);
}

static cpumask_t read_clocks_cpumask;
static DEFINE_PER_CPU(s_time_t, read_clocks_time);
static DEFINE_PER_CPU(u64, read_cycles_time);

static void cf_check read_clocks_slave(void *unused)
{
    unsigned int cpu = smp_processor_id();
    local_irq_disable();
    while ( !cpumask_test_cpu(cpu, &read_clocks_cpumask) )
        cpu_relax();
    per_cpu(read_clocks_time, cpu) = NOW();
    per_cpu(read_cycles_time, cpu) = get_cycles();
    cpumask_clear_cpu(cpu, &read_clocks_cpumask);
    local_irq_enable();
}

static void cf_check read_clocks(unsigned char key)
{
    unsigned int cpu = smp_processor_id(), min_stime_cpu, max_stime_cpu;
    unsigned int min_cycles_cpu, max_cycles_cpu;
    u64 min_stime, max_stime, dif_stime;
    u64 min_cycles, max_cycles, dif_cycles;
    static u64 sumdif_stime = 0, maxdif_stime = 0;
    static u64 sumdif_cycles = 0, maxdif_cycles = 0;
    static u32 count = 0;
    static DEFINE_SPINLOCK(lock);

    spin_lock(&lock);

    smp_call_function(read_clocks_slave, NULL, 0);

    local_irq_disable();
    cpumask_andnot(&read_clocks_cpumask, &cpu_online_map, cpumask_of(cpu));
    per_cpu(read_clocks_time, cpu) = NOW();
    per_cpu(read_cycles_time, cpu) = get_cycles();
    local_irq_enable();

    while ( !cpumask_empty(&read_clocks_cpumask) )
        cpu_relax();

    min_stime_cpu = max_stime_cpu = min_cycles_cpu = max_cycles_cpu = cpu;
    for_each_online_cpu ( cpu )
    {
        if ( per_cpu(read_clocks_time, cpu) <
             per_cpu(read_clocks_time, min_stime_cpu) )
            min_stime_cpu = cpu;
        if ( per_cpu(read_clocks_time, cpu) >
             per_cpu(read_clocks_time, max_stime_cpu) )
            max_stime_cpu = cpu;
        if ( per_cpu(read_cycles_time, cpu) <
             per_cpu(read_cycles_time, min_cycles_cpu) )
            min_cycles_cpu = cpu;
        if ( per_cpu(read_cycles_time, cpu) >
             per_cpu(read_cycles_time, max_cycles_cpu) )
            max_cycles_cpu = cpu;
    }

    min_stime = per_cpu(read_clocks_time, min_stime_cpu);
    max_stime = per_cpu(read_clocks_time, max_stime_cpu);
    min_cycles = per_cpu(read_cycles_time, min_cycles_cpu);
    max_cycles = per_cpu(read_cycles_time, max_cycles_cpu);

    spin_unlock(&lock);

    dif_stime = max_stime - min_stime;
    if ( dif_stime > maxdif_stime )
        maxdif_stime = dif_stime;
    sumdif_stime += dif_stime;
    dif_cycles = max_cycles - min_cycles;
    if ( dif_cycles > maxdif_cycles )
        maxdif_cycles = dif_cycles;
    sumdif_cycles += dif_cycles;
    count++;
    printk("Synced stime skew: max=%"PRIu64"ns avg=%"PRIu64"ns "
           "samples=%"PRIu32" current=%"PRIu64"ns\n",
           maxdif_stime, sumdif_stime/count, count, dif_stime);
    printk("Synced cycles skew: max=%"PRIu64" avg=%"PRIu64" "
           "samples=%"PRIu32" current=%"PRIu64"\n",
           maxdif_cycles, sumdif_cycles/count, count, dif_cycles);
}

static void cf_check run_all_nonirq_keyhandlers(void *unused)
{
    /* Fire all the non-IRQ-context diagnostic keyhandlers */
    struct keyhandler *h;
    int k;

    console_start_log_everything();

    for ( k = 0; k < ARRAY_SIZE(key_table); k++ )
    {
        process_pending_softirqs();
        h = &key_table[k];
        if ( !h->fn || !h->diagnostic || h->irq_callback )
            continue;
        printk("[%c: %s]\n", k, h->desc);
        h->fn(k);
    }

    console_end_log_everything();
}

static DECLARE_TASKLET(run_all_keyhandlers_tasklet,
                       run_all_nonirq_keyhandlers, NULL);

static void cf_check run_all_keyhandlers(unsigned char key, bool need_context)
{
    struct keyhandler *h;
    unsigned int k;

    watchdog_disable();

    printk("'%c' pressed -> firing all diagnostic keyhandlers\n", key);

    /* Fire all the IRQ-context diangostic keyhandlers now */
    for ( k = 0; k < ARRAY_SIZE(key_table); k++ )
    {
        h = &key_table[k];
        if ( !h->irq_fn || !h->diagnostic || !h->irq_callback )
            continue;
        printk("[%c: %s]\n", k, h->desc);
        h->irq_fn(k, need_context);
    }

    watchdog_enable();

    /* Trigger the others from a tasklet in non-IRQ context */
    tasklet_schedule(&run_all_keyhandlers_tasklet);
}

static void cf_check do_toggle_alt_key(unsigned char key, bool unused)
{
    alt_key_handling = !alt_key_handling;
    printk("'%c' pressed -> using %s key handling\n", key,
           alt_key_handling ? "alternative" : "normal");
}

void __init initialize_keytable(void)
{
    if ( num_present_cpus() > 16 )
    {
        alt_key_handling = 1;
        printk(XENLOG_INFO "Defaulting to alternative key handling; "
               "send 'A' to switch to normal mode.\n");
    }
}

#define CRASHACTION_SIZE  32
static char crash_debug_panic[CRASHACTION_SIZE];
string_runtime_param("crash-debug-panic", crash_debug_panic);
static char crash_debug_hwdom[CRASHACTION_SIZE];
string_runtime_param("crash-debug-hwdom", crash_debug_hwdom);
static char crash_debug_watchdog[CRASHACTION_SIZE];
string_runtime_param("crash-debug-watchdog", crash_debug_watchdog);
#ifdef CONFIG_KEXEC
static char crash_debug_kexeccmd[CRASHACTION_SIZE];
string_runtime_param("crash-debug-kexeccmd", crash_debug_kexeccmd);
#else
#define crash_debug_kexeccmd NULL
#endif
static char crash_debug_debugkey[CRASHACTION_SIZE];
string_runtime_param("crash-debug-debugkey", crash_debug_debugkey);

void keyhandler_crash_action(enum crash_reason reason)
{
    static const char *const crash_action[] = {
        [CRASHREASON_PANIC] = crash_debug_panic,
        [CRASHREASON_HWDOM] = crash_debug_hwdom,
        [CRASHREASON_WATCHDOG] = crash_debug_watchdog,
        [CRASHREASON_KEXECCMD] = crash_debug_kexeccmd,
        [CRASHREASON_DEBUGKEY] = crash_debug_debugkey,
    };
    static bool ignore;
    const char *action;

    /* Some handlers are not functional too early. */
    if ( system_state < SYS_STATE_smp_boot )
        return;

    if ( (unsigned int)reason >= ARRAY_SIZE(crash_action) )
        return;
    action = crash_action[reason];
    if ( !action )
        return;

    /* Avoid recursion. */
    if ( ignore )
        return;
    ignore = true;

    while ( *action )
    {
        if ( *action == '+' )
            mdelay(10);
        else
            handle_keypress(*action, true);
        action++;
    }
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: keyhandler.c === */
/* === BEGIN INLINED: trace.c === */
#include <xen_xen_config.h>
/******************************************************************************
 * common/trace.c
 *
 * Xen Trace Buffer
 *
 * Copyright (C) 2004 by Intel Research Cambridge
 *
 * Authors: Mark Williamson, mark.a.williamson@intel.com
 *          Rob Gardner, rob.gardner@hp.com
 * Date:    October 2005
 *
 * Copyright (C) 2005 Bin Ren
 *
 * The trace buffer code is designed to allow debugging traces of Xen to be
 * generated on UP / SMP machines.  Each trace entry is timestamped so that
 * it's possible to reconstruct a chronological record of trace events.
 */

#include <asm_io.h>
#include <xen_lib.h>
#include <xen_param.h>
#include <xen_sched.h>
#include <xen_smp.h>
#include <xen_trace.h>
#include <xen_errno.h>
#include <xen_event.h>
#include <xen_tasklet.h>
#include <xen_init.h>
#include <xen_mm.h>
#include <xen_percpu.h>
#include <xen_pfn.h>
#include <xen_sections.h>
#include <xen_cpu.h>
#include <asm_atomic.h>
#include <public_sysctl.h>

#ifdef CONFIG_COMPAT
#include <compat/trace.h>
#define xen_t_buf t_buf
CHECK_t_buf;
#undef xen_t_buf
#else
#define compat_t_rec t_rec
#endif

/* opt_tbuf_size: trace buffer size (in pages) for each cpu */
static unsigned int opt_tbuf_size;
static unsigned int opt_tevt_mask;
integer_param("tbuf_size", opt_tbuf_size);
integer_param("tevt_mask", opt_tevt_mask);

/* Pointers to the meta-data objects for all system trace buffers */
static struct t_info *t_info;
static unsigned int t_info_pages;

static DEFINE_PER_CPU_READ_MOSTLY(struct t_buf *, t_bufs);
static DEFINE_PER_CPU_READ_MOSTLY(spinlock_t, t_lock);
static u32 data_size __read_mostly;

/* High water mark for trace buffers; */
/* Send virtual interrupt when buffer level reaches this point */
static u32 t_buf_highwater;

/* Number of records lost due to per-CPU trace buffer being full. */
static DEFINE_PER_CPU(unsigned long, lost_records);
static DEFINE_PER_CPU(unsigned long, lost_records_first_tsc);

/* a flag recording whether initialization has been done */
/* or more properly, if the tbuf subsystem is enabled right now */
bool __read_mostly tb_init_done;

/* which CPUs tracing is enabled on */
static cpumask_t tb_cpu_mask;

/* which tracing events are enabled */
static u32 tb_event_mask = TRC_ALL;

static int cf_check cpu_callback(
    struct notifier_block *nfb, unsigned long action, void *hcpu)
{
    unsigned int cpu = (unsigned long)hcpu;

    if ( action == CPU_UP_PREPARE )
        spin_lock_init(&per_cpu(t_lock, cpu));

    return NOTIFY_DONE;
}

static struct notifier_block cpu_nfb = {
    .notifier_call = cpu_callback
};

static uint32_t calc_tinfo_first_offset(void)
{
    return DIV_ROUND_UP(offsetof(struct t_info, mfn_offset[NR_CPUS]),
                        sizeof(uint32_t));
}

/**
 * calculate_tbuf_size - check to make sure that the proposed size will fit
 * in the currently sized struct t_info and allows prod and cons to
 * reach double the value without overflow.
 * The t_info layout is fixed and cant be changed without breaking xentrace.
 * Initialize t_info_pages based on number of trace pages.
 */
static int calculate_tbuf_size(unsigned int pages, uint16_t t_info_first_offset)
{
    struct t_buf dummy_size;
    typeof(dummy_size.prod) max_size;
    struct t_info dummy_pages;
    typeof(dummy_pages.tbuf_size) max_pages;
    typeof(dummy_pages.mfn_offset[0]) max_mfn_offset;
    unsigned int max_cpus = nr_cpu_ids;
    unsigned int t_info_words;

    /* force maximum value for an unsigned type */
    max_size = -1;
    max_pages = -1;
    max_mfn_offset = -1;

    /* max size holds up to n pages */
    max_size /= PAGE_SIZE;

    if ( max_size < max_pages )
        max_pages = max_size;

    /*
     * max mfn_offset holds up to n pages per cpu
     * The array of mfns for the highest cpu can start at the maximum value
     * mfn_offset can hold. So reduce the number of cpus and also the mfn_offset.
     */
    max_mfn_offset -= t_info_first_offset;
    max_cpus--;
    if ( max_cpus )
        max_mfn_offset /= max_cpus;
    if ( max_mfn_offset < max_pages )
        max_pages = max_mfn_offset;

    if ( pages > max_pages )
    {
        printk(XENLOG_INFO "xentrace: requested number of %u pages "
               "reduced to %u\n",
               pages, max_pages);
        pages = max_pages;
    }

    /*
     * NB this calculation is correct, because t_info_first_offset is
     * in words, not bytes
     */
    t_info_words = nr_cpu_ids * pages + t_info_first_offset;
    t_info_pages = PFN_UP(t_info_words * sizeof(uint32_t));
    printk(XENLOG_INFO "xentrace: requesting %u t_info pages "
           "for %u trace pages on %u cpus\n",
           t_info_pages, pages, nr_cpu_ids);
    return pages;
}

/**
 * alloc_trace_bufs - performs initialization of the per-cpu trace buffers.
 *
 * This function is called at start of day in order to initialize the per-cpu
 * trace buffers.  The trace buffers are then available for debugging use, via
 * the %TRACE_xD macros exported in <xen_trace.h>.
 *
 * This function may also be called later when enabling trace buffers
 * via the SET_SIZE hypercall.
 */
static int alloc_trace_bufs(unsigned int pages)
{
    int i, cpu;
    /* Start after a fixed-size array of NR_CPUS */
    uint32_t *t_info_mfn_list;
    uint16_t t_info_first_offset;
    uint16_t offset;

    if ( t_info )
        return -EBUSY;

    if ( pages == 0 )
        return -EINVAL;

    /* Calculate offset in units of u32 of first mfn */
    t_info_first_offset = calc_tinfo_first_offset();

    pages = calculate_tbuf_size(pages, t_info_first_offset);

    t_info = alloc_xenheap_pages(get_order_from_pages(t_info_pages), 0);
    if ( t_info == NULL )
        goto out_fail;

    memset(t_info, 0, t_info_pages*PAGE_SIZE);

    t_info_mfn_list = (uint32_t *)t_info;

    t_info->tbuf_size = pages;

    /*
     * Allocate buffers for all of the cpus.
     * If any fails, deallocate what you have so far and exit.
     */
    for_each_online_cpu(cpu)
    {
        offset = t_info_first_offset + (cpu * pages);
        t_info->mfn_offset[cpu] = offset;

        for ( i = 0; i < pages; i++ )
        {
            void *p = alloc_xenheap_pages(0, MEMF_bits(32 + PAGE_SHIFT));
            if ( !p )
            {
                printk(XENLOG_INFO "xentrace: memory allocation failed "
                       "on cpu %d after %d pages\n", cpu, i);
                t_info_mfn_list[offset + i] = 0;
                goto out_dealloc;
            }
            t_info_mfn_list[offset + i] = virt_to_mfn(p);
        }
    }

    /*
     * Initialize buffers for all of the cpus.
     */
    for_each_online_cpu(cpu)
    {
        struct t_buf *buf;

        spin_lock_init(&per_cpu(t_lock, cpu));

        offset = t_info->mfn_offset[cpu];

        /* Initialize the buffer metadata */
        per_cpu(t_bufs, cpu) = buf = mfn_to_virt(t_info_mfn_list[offset]);
        buf->cons = buf->prod = 0;

        printk(XENLOG_INFO "xentrace: p%d mfn %x offset %u\n",
                   cpu, t_info_mfn_list[offset], offset);

        /* Now share the trace pages */
        for ( i = 0; i < pages; i++ )
            share_xen_page_with_privileged_guests(
                mfn_to_page(_mfn(t_info_mfn_list[offset + i])), SHARE_rw);
    }

    /* Finally, share the t_info page */
    for(i = 0; i < t_info_pages; i++)
        share_xen_page_with_privileged_guests(
            virt_to_page(t_info) + i, SHARE_ro);

    data_size  = (pages * PAGE_SIZE - sizeof(struct t_buf));
    t_buf_highwater = data_size >> 1; /* 50% high water */
    opt_tbuf_size = pages;

    printk("xentrace: initialised\n");
    smp_wmb(); /* above must be visible before tb_init_done flag set */
    tb_init_done = 1;

    return 0;

out_dealloc:
    for_each_online_cpu(cpu)
    {
        offset = t_info->mfn_offset[cpu];
        if ( !offset )
            continue;
        for ( i = 0; i < pages; i++ )
        {
            uint32_t mfn = t_info_mfn_list[offset + i];
            if ( !mfn )
                break;
            ASSERT(!(mfn_to_page(_mfn(mfn))->count_info & PGC_allocated));
            free_xenheap_pages(mfn_to_virt(mfn), 0);
        }
    }
    free_xenheap_pages(t_info, get_order_from_pages(t_info_pages));
    t_info = NULL;
out_fail:
    printk(XENLOG_WARNING "xentrace: allocation failed! Tracing disabled.\n");
    return -ENOMEM;
}


/**
 * tb_set_size - handle the logic involved with dynamically allocating tbufs
 *
 * This function is called when the SET_SIZE hypercall is done.
 */
static int tb_set_size(unsigned int pages)
{
    /*
     * Setting size is a one-shot operation. It can be done either at
     * boot time or via control tools, but not by both. Once buffers
     * are created they cannot be destroyed.
     */
    if ( opt_tbuf_size && pages != opt_tbuf_size )
    {
        printk(XENLOG_INFO "xentrace: tb_set_size from %d to %d "
               "not implemented\n",
               opt_tbuf_size, pages);
        return -EINVAL;
    }

    return alloc_trace_bufs(pages);
}


/**
 * init_trace_bufs - performs initialization of the per-cpu trace buffers.
 *
 * This function is called at start of day in order to initialize the per-cpu
 * trace buffers.  The trace buffers are then available for debugging use, via
 * the %TRACE_xD macros exported in <xen_trace.h>.
 */
void __init init_trace_bufs(void)
{
    cpumask_setall(&tb_cpu_mask);
    register_cpu_notifier(&cpu_nfb);

    if ( opt_tbuf_size )
    {
        if ( alloc_trace_bufs(opt_tbuf_size) )
        {
            printk("xentrace: allocation size %d failed, disabling\n",
                   opt_tbuf_size);
            opt_tbuf_size = 0;
        }
        else if ( opt_tevt_mask )
        {
            printk("xentrace: Starting tracing, enabling mask %x\n",
                   opt_tevt_mask);
            tb_event_mask = opt_tevt_mask;
            tb_init_done=1;
        }
    }
}

/**
 * tb_control - sysctl operations on trace buffers.
 * @tbc: a pointer to a struct xen_sysctl_tbuf_op to be filled out
 */
int tb_control(struct xen_sysctl_tbuf_op *tbc)
{
    static DEFINE_SPINLOCK(lock);
    int rc = 0;

    spin_lock(&lock);

    switch ( tbc->cmd )
    {
    case XEN_SYSCTL_TBUFOP_get_info:
        tbc->evt_mask   = tb_event_mask;
        tbc->buffer_mfn = t_info ? virt_to_mfn(t_info) : 0;
        tbc->size = t_info_pages * PAGE_SIZE;
        break;
    case XEN_SYSCTL_TBUFOP_set_cpu_mask:
    {
        cpumask_var_t mask;

        rc = xenctl_bitmap_to_cpumask(&mask, &tbc->cpu_mask);
        if ( !rc )
        {
            cpumask_copy(&tb_cpu_mask, mask);
            free_cpumask_var(mask);
        }
    }
        break;
    case XEN_SYSCTL_TBUFOP_set_evt_mask:
        tb_event_mask = tbc->evt_mask;
        break;
    case XEN_SYSCTL_TBUFOP_set_size:
        rc = tb_set_size(tbc->size);
        break;
    case XEN_SYSCTL_TBUFOP_enable:
        /* Enable trace buffers. Check buffers are already allocated. */
        if ( opt_tbuf_size == 0 )
            rc = -EINVAL;
        else
            tb_init_done = 1;
        break;
    case XEN_SYSCTL_TBUFOP_disable:
    {
        /*
         * Disable trace buffers. Just stops new records from being written,
         * does not deallocate any memory.
         */
        int i;

        tb_init_done = 0;
        smp_wmb();
        /* Clear any lost-record info so we don't get phantom lost records next time we
         * start tracing.  Grab the lock to make sure we're not racing anyone.  After this
         * hypercall returns, no more records should be placed into the buffers. */
        for_each_online_cpu(i)
        {
            unsigned long flags;
            spin_lock_irqsave(&per_cpu(t_lock, i), flags);
            per_cpu(lost_records, i)=0;
            spin_unlock_irqrestore(&per_cpu(t_lock, i), flags);
        }
    }
        break;
    default:
        rc = -EINVAL;
        break;
    }

    spin_unlock(&lock);

    return rc;
}

static inline unsigned int calc_rec_size(bool cycles, unsigned int extra)
{
    unsigned int rec_size = 4;

    if ( cycles )
        rec_size += 8;
    rec_size += extra;
    return rec_size;
}

static inline bool bogus(u32 prod, u32 cons)
{
    if ( unlikely(prod & 3) || unlikely(prod >= 2 * data_size) ||
         unlikely(cons & 3) || unlikely(cons >= 2 * data_size) )
    {
        tb_init_done = 0;
        printk(XENLOG_WARNING "trc#%u: bogus prod (%08x) and/or cons (%08x)\n",
               smp_processor_id(), prod, cons);
        return 1;
    }
    return 0;
}

static inline u32 calc_unconsumed_bytes(const struct t_buf *buf)
{
    u32 prod = buf->prod, cons = buf->cons;
    s32 x;

    barrier(); /* must read buf->prod and buf->cons only once */
    if ( bogus(prod, cons) )
        return data_size;

    x = prod - cons;
    if ( x < 0 )
        x += 2*data_size;

    ASSERT(x >= 0);
    ASSERT(x <= data_size);

    return x;
}

static inline u32 calc_bytes_to_wrap(const struct t_buf *buf)
{
    u32 prod = buf->prod, cons = buf->cons;
    s32 x;

    barrier(); /* must read buf->prod and buf->cons only once */
    if ( bogus(prod, cons) )
        return 0;

    x = data_size - prod;
    if ( x <= 0 )
        x += data_size;

    ASSERT(x > 0);
    ASSERT(x <= data_size);

    return x;
}

static inline u32 calc_bytes_avail(const struct t_buf *buf)
{
    return data_size - calc_unconsumed_bytes(buf);
}

static unsigned char *next_record(const struct t_buf *buf, uint32_t *next,
                                 unsigned char **next_page,
                                 uint32_t *offset_in_page)
{
    u32 x = buf->prod, cons = buf->cons;
    uint16_t per_cpu_mfn_offset;
    uint32_t per_cpu_mfn_nr;
    uint32_t *mfn_list;
    uint32_t mfn;
    unsigned char *this_page;

    barrier(); /* must read buf->prod and buf->cons only once */
    *next = x;
    if ( !tb_init_done || bogus(x, cons) )
        return NULL;

    if ( x >= data_size )
        x -= data_size;

    ASSERT(x < data_size);

    /* add leading header to get total offset of next record */
    x += sizeof(struct t_buf);
    *offset_in_page = x & ~PAGE_MASK;

    /* offset into array of mfns */
    per_cpu_mfn_nr = x >> PAGE_SHIFT;
    per_cpu_mfn_offset = t_info->mfn_offset[smp_processor_id()];
    mfn_list = (uint32_t *)t_info;
    mfn = mfn_list[per_cpu_mfn_offset + per_cpu_mfn_nr];
    this_page = mfn_to_virt(mfn);
    if (per_cpu_mfn_nr + 1 >= opt_tbuf_size)
    {
        /* reached end of buffer? */
        *next_page = NULL;
    }
    else
    {
        mfn = mfn_list[per_cpu_mfn_offset + per_cpu_mfn_nr + 1];
        *next_page = mfn_to_virt(mfn);
    }
    return this_page;
}

static inline void __insert_record(struct t_buf *buf,
                                   unsigned long event,
                                   unsigned int extra,
                                   bool cycles,
                                   unsigned int rec_size,
                                   const void *extra_data)
{
    struct t_rec split_rec, *rec;
    uint32_t *dst;
    unsigned char *this_page, *next_page;
    unsigned int extra_word = extra / sizeof(u32);
    unsigned int local_rec_size = calc_rec_size(cycles, extra);
    uint32_t next;
    uint32_t offset;
    uint32_t remaining;

    BUG_ON(local_rec_size != rec_size);
    BUG_ON(extra & 3);

    this_page = next_record(buf, &next, &next_page, &offset);
    if ( !this_page )
        return;

    remaining = PAGE_SIZE - offset;

    if ( unlikely(rec_size > remaining) )
    {
        if ( next_page == NULL )
        {
            /* access beyond end of buffer */
            printk(XENLOG_WARNING
                   "%s: size=%08x prod=%08x cons=%08x rec=%u remaining=%u\n",
                   __func__, data_size, next, buf->cons, rec_size, remaining);
            return;
        }
        rec = &split_rec;
    } else {
        rec = (struct t_rec*)(this_page + offset);
    }

    rec->event = event;
    rec->extra_u32 = extra_word;
    dst = rec->u.nocycles.extra_u32;
    if ( (rec->cycles_included = cycles) != 0 )
    {
        u64 tsc = (u64)get_cycles();
        rec->u.cycles.cycles_lo = (uint32_t)tsc;
        rec->u.cycles.cycles_hi = (uint32_t)(tsc >> 32);
        dst = rec->u.cycles.extra_u32;
    }

    if ( extra_data && extra )
        memcpy(dst, extra_data, extra);

    if ( unlikely(rec_size > remaining) )
    {
        memcpy(this_page + offset, rec, remaining);
        memcpy(next_page, (char *)rec + remaining, rec_size - remaining);
    }

    smp_wmb();

    next += rec_size;
    if ( next >= 2*data_size )
        next -= 2*data_size;
    ASSERT(next < 2*data_size);
    buf->prod = next;
}

static inline void insert_wrap_record(struct t_buf *buf,
                                      unsigned int size)
{
    u32 space_left = calc_bytes_to_wrap(buf);
    unsigned int extra_space = space_left - sizeof(u32);
    bool cycles = false;

    BUG_ON(space_left > size);

    /* We may need to add cycles to take up enough space... */
    if ( (extra_space/sizeof(u32)) > TRACE_EXTRA_MAX )
    {
        cycles = 1;
        extra_space -= sizeof(u64);
        ASSERT((extra_space/sizeof(u32)) <= TRACE_EXTRA_MAX);
    }

    __insert_record(buf, TRC_TRACE_WRAP_BUFFER, extra_space, cycles,
                    space_left, NULL);
}

#define LOST_REC_SIZE (4 + 8 + 16) /* header + tsc + sizeof(struct ed) */

static inline void insert_lost_records(struct t_buf *buf)
{
    struct __packed {
        u32 lost_records;
        u16 did, vid;
        u64 first_tsc;
    } ed;

    ed.vid = current->vcpu_id;
    ed.did = current->domain->domain_id;
    ed.lost_records = this_cpu(lost_records);
    ed.first_tsc = this_cpu(lost_records_first_tsc);

    this_cpu(lost_records) = 0;

    __insert_record(buf, TRC_LOST_RECORDS, sizeof(ed), 1 /* cycles */,
                    LOST_REC_SIZE, &ed);
}

/*
 * Notification is performed in qtasklet to avoid deadlocks with contexts
 * which __trace_var() may be called from (e.g., scheduler critical regions).
 */
static void cf_check trace_notify_dom0(void *unused)
{
    send_global_virq(VIRQ_TBUF);
}
static DECLARE_SOFTIRQ_TASKLET(trace_notify_dom0_tasklet,
                               trace_notify_dom0, NULL);

/**
 * trace - Enters a trace tuple into the trace buffer for the current CPU.
 * @event: the event type being logged
 * @extra: size of additional trace data in bytes
 * @extra_data: pointer to additional trace data
 *
 * Logs a trace record into the appropriate buffer.
 */
void trace(uint32_t event, unsigned int extra, const void *extra_data)
{
    struct t_buf *buf;
    unsigned long flags;
    u32 bytes_to_tail, bytes_to_wrap;
    unsigned int rec_size, total_size;
    bool started_below_highwater;
    bool cycles = event & TRC_HD_CYCLE_FLAG;

    if( !tb_init_done )
        return;

    /*
     * extra data needs to be an exact multiple of uint32_t to prevent the
     * later logic over-reading the object.  Reject out-of-spec records.  Any
     * failure here is an error in the caller.
     */
    if ( extra % sizeof(uint32_t) ||
         extra / sizeof(uint32_t) > TRACE_EXTRA_MAX )
        return printk_once(XENLOG_WARNING
                           "Trace event %#x bad size %u, discarding\n",
                           event, extra);

    if ( (tb_event_mask & event) == 0 )
        return;

    /* match class */
    if ( ((tb_event_mask >> TRC_CLS_SHIFT) & (event >> TRC_CLS_SHIFT)) == 0 )
        return;

    /* then match subclass */
    if ( (((tb_event_mask >> TRC_SUBCLS_SHIFT) & 0xf )
                & ((event >> TRC_SUBCLS_SHIFT) & 0xf )) == 0 )
        return;

    if ( !cpumask_test_cpu(smp_processor_id(), &tb_cpu_mask) )
        return;

    spin_lock_irqsave(&this_cpu(t_lock), flags);

    buf = this_cpu(t_bufs);

    if ( unlikely(!buf) )
    {
        /* Make gcc happy */
        started_below_highwater = 0;
        goto unlock;
    }

    started_below_highwater = (calc_unconsumed_bytes(buf) < t_buf_highwater);

    /* Calculate the record size */
    rec_size = calc_rec_size(cycles, extra);

    /* How many bytes are available in the buffer? */
    bytes_to_tail = calc_bytes_avail(buf);

    /* How many bytes until the next wrap-around? */
    bytes_to_wrap = calc_bytes_to_wrap(buf);

    /*
     * Calculate expected total size to commit this record by
     * doing a dry-run.
     */
    total_size = 0;

    /* First, check to see if we need to include a lost_record.
     */
    if ( this_cpu(lost_records) )
    {
        if ( LOST_REC_SIZE > bytes_to_wrap )
        {
            total_size += bytes_to_wrap;
            bytes_to_wrap = data_size;
        }
        total_size += LOST_REC_SIZE;
        bytes_to_wrap -= LOST_REC_SIZE;

        /* LOST_REC might line up perfectly with the buffer wrap */
        if ( bytes_to_wrap == 0 )
            bytes_to_wrap = data_size;
    }

    if ( rec_size > bytes_to_wrap )
    {
        total_size += bytes_to_wrap;
    }
    total_size += rec_size;

    /* Do we have enough space for everything? */
    if ( total_size > bytes_to_tail )
    {
        if ( ++this_cpu(lost_records) == 1 )
            this_cpu(lost_records_first_tsc)=(u64)get_cycles();
        started_below_highwater = 0;
        goto unlock;
    }

    /*
     * Now, actually write information
     */
    bytes_to_wrap = calc_bytes_to_wrap(buf);

    if ( this_cpu(lost_records) )
    {
        if ( LOST_REC_SIZE > bytes_to_wrap )
        {
            insert_wrap_record(buf, LOST_REC_SIZE);
            bytes_to_wrap = data_size;
        }
        insert_lost_records(buf);
        bytes_to_wrap -= LOST_REC_SIZE;

        /* LOST_REC might line up perfectly with the buffer wrap */
        if ( bytes_to_wrap == 0 )
            bytes_to_wrap = data_size;
    }

    if ( rec_size > bytes_to_wrap )
        insert_wrap_record(buf, rec_size);

    /* Write the original record */
    __insert_record(buf, event, extra, cycles, rec_size, extra_data);

unlock:
    spin_unlock_irqrestore(&this_cpu(t_lock), flags);

    /* Notify trace buffer consumer that we've crossed the high water mark. */
    if ( likely(buf!=NULL)
         && started_below_highwater
         && (calc_unconsumed_bytes(buf) >= t_buf_highwater) )
        tasklet_schedule(&trace_notify_dom0_tasklet);
}

void __trace_hypercall(uint32_t event, unsigned long op,
                       const xen_ulong_t *args)
{
    struct {
        uint32_t op;
        uint32_t args[5];
    } d;
    uint32_t *a = d.args;

    /*
     * In lieu of using __packed above, which gcc9 legitimately doesn't
     * like in combination with the address of d.args[] taken.
     */
    BUILD_BUG_ON(offsetof(typeof(d), args) != sizeof(d.op));

#define APPEND_ARG32(i)                         \
    do {                                        \
        unsigned int i_ = (i);                  \
        *a++ = args[(i_)];                      \
        d.op |= TRC_PV_HYPERCALL_V2_ARG_32(i_); \
    } while( 0 )

    /*
     * This shouldn't happen as @op should be small enough but just in
     * case, warn if the argument bits in the trace record would
     * clobber the hypercall op.
     */
    WARN_ON(op & TRC_PV_HYPERCALL_V2_ARG_MASK);

    d.op = op;

    switch ( op )
    {
    case __HYPERVISOR_mmu_update:
        APPEND_ARG32(1); /* count */
        break;
    case __HYPERVISOR_multicall:
        APPEND_ARG32(1); /* count */
        break;
    case __HYPERVISOR_grant_table_op:
        APPEND_ARG32(0); /* cmd */
        APPEND_ARG32(2); /* count */
        break;
    case __HYPERVISOR_vcpu_op:
        APPEND_ARG32(0); /* cmd */
        APPEND_ARG32(1); /* vcpuid */
        break;
    case __HYPERVISOR_mmuext_op:
        APPEND_ARG32(1); /* count */
        break;
    case __HYPERVISOR_sched_op:
        APPEND_ARG32(0); /* cmd */
        break;
    }

    trace_time(event, sizeof(uint32_t) * (1 + (a - d.args)), &d);
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: trace.c === */
/* === BEGIN INLINED: warning.c === */
#include <xen_xen_config.h>
#include <xen_delay.h>
#include <xen_init.h>
#include <xen_lib.h>
#include <xen_softirq.h>
#include <xen_warning.h>

#define WARNING_ARRAY_SIZE 20
static unsigned int __initdata nr_warnings;
static const char *__initdata warnings[WARNING_ARRAY_SIZE];

void __init warning_add(const char *warning)
{
    if ( nr_warnings >= WARNING_ARRAY_SIZE )
        panic("Too many pieces of warning text\n");

    warnings[nr_warnings] = warning;
    nr_warnings++;
}

void __init warning_print(void)
{
    unsigned int i, j;

    if ( !nr_warnings )
        return;

    printk("***************************************************\n");

    for ( i = 0; i < nr_warnings; i++ )
    {
        printk("%s", warnings[i]);
        printk("***************************************************\n");
        process_pending_softirqs();
    }

    for ( i = 0; i < 3; i++ )
    {
        printk("%u... ", 3 - i);
        for ( j = 0; j < 100; j++ )
        {
            process_pending_softirqs();
            mdelay(10);
        }
    }
    printk("\n");
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: warning.c === */
/* === BEGIN INLINED: virtual_region.c === */
#include <xen_xen_config.h>
/*
 * Copyright (c) 2016 Oracle and/or its affiliates. All rights reserved.
 */

#include <xen_init.h>
#include <xen_kernel.h>
#include <xen_mm.h>
#include <xen_rcupdate.h>
#include <xen_spinlock.h>
#include <xen_virtual_region.h>

extern const struct bug_frame
    __start_bug_frames_0[], __stop_bug_frames_0[],
    __start_bug_frames_1[], __stop_bug_frames_1[],
    __start_bug_frames_2[], __stop_bug_frames_2[],
    __start_bug_frames_3[], __stop_bug_frames_3[];

/*
 * For the built-in regions, the double linked list can be constructed at
 * build time.  Forward-declare the elements and their initialisers.
 */
static struct list_head virtual_region_list;
static struct virtual_region core, core_init;

#define LIST_ENTRY_HEAD() { .next = &core.list,           .prev = &core_init.list }
#define LIST_ENTRY_CORE() { .next = &core_init.list,      .prev = &virtual_region_list }
#define LIST_ENTRY_INIT() { .next = &virtual_region_list, .prev = &core.list }

static struct virtual_region core __read_mostly = {
    .list = LIST_ENTRY_CORE(),
    // .text_start = _stext,
    // .text_end = _etext,
    // .rodata_start = _srodata,
    // .rodata_end = _erodata,

    // .frame = {
    //     { __start_bug_frames_0, __stop_bug_frames_0 },
    //     { __start_bug_frames_1, __stop_bug_frames_1 },
    //     { __start_bug_frames_2, __stop_bug_frames_2 },
    //     { __start_bug_frames_3, __stop_bug_frames_3 },
    // },

#ifdef CONFIG_HAS_EX_TABLE
    .ex = __start___ex_table,
    .ex_end = __stop___ex_table,
#endif
};

/* Becomes irrelevant when __init sections are cleared. */
static struct virtual_region core_init __initdata = {
    .list = LIST_ENTRY_INIT(),
    // .text_start = _sinittext,
    // .text_end = _einittext,

    // .frame = {
    //     { __start_bug_frames_0, __stop_bug_frames_0 },
    //     { __start_bug_frames_1, __stop_bug_frames_1 },
    //     { __start_bug_frames_2, __stop_bug_frames_2 },
    //     { __start_bug_frames_3, __stop_bug_frames_3 },
    // },

// #ifdef CONFIG_HAS_EX_TABLE
//     .ex = __start___ex_table,
//     .ex_end = __stop___ex_table,
// #endif
};

/*
 * RCU locking. Modifications to the list must be done in exclusive mode, and
 * hence need to hold the spinlock.
 *
 * All readers of virtual_region_list MUST use list_for_each_entry_rcu.
 */
static struct list_head virtual_region_list = LIST_ENTRY_HEAD();
static DEFINE_SPINLOCK(virtual_region_lock);
static DEFINE_RCU_READ_LOCK(rcu_virtual_region_lock);

const struct virtual_region *find_text_region(unsigned long addr)
{
    const struct virtual_region *iter, *region = NULL;

    rcu_read_lock(&rcu_virtual_region_lock);
    list_for_each_entry_rcu ( iter, &virtual_region_list, list )
    {
        if ( (void *)addr >= iter->text_start &&
             (void *)addr <  iter->text_end )
        {
            region = iter;
            break;
        }
    }
    rcu_read_unlock(&rcu_virtual_region_lock);

    return region;
}

/*
 * Suggest inline so when !CONFIG_LIVEPATCH the function is not left
 * unreachable after init code is removed.
 */
static void inline remove_virtual_region(struct virtual_region *r)
{
    unsigned long flags;

    spin_lock_irqsave(&virtual_region_lock, flags);
    list_del_rcu(&r->list);
    spin_unlock_irqrestore(&virtual_region_lock, flags);
}

#ifdef CONFIG_LIVEPATCH
void register_virtual_region(struct virtual_region *r)
{
    unsigned long flags;

    spin_lock_irqsave(&virtual_region_lock, flags);
    list_add_tail_rcu(&r->list, &virtual_region_list);
    spin_unlock_irqrestore(&virtual_region_lock, flags);
}

void unregister_virtual_region(struct virtual_region *r)
{
    remove_virtual_region(r);

    /* Assert that no CPU might be using the removed region. */
    rcu_barrier();
}

#ifdef CONFIG_X86
void relax_virtual_region_perms(void)
{
    const struct virtual_region *region;

    rcu_read_lock(&rcu_virtual_region_lock);
    list_for_each_entry_rcu( region, &virtual_region_list, list )
    {
        modify_xen_mappings_lite((unsigned long)region->text_start,
                                 (unsigned long)region->text_end,
                                 PAGE_HYPERVISOR_RWX);
        if ( region->rodata_start )
            modify_xen_mappings_lite((unsigned long)region->rodata_start,
                                     (unsigned long)region->rodata_end,
                                     PAGE_HYPERVISOR_RW);
    }
    rcu_read_unlock(&rcu_virtual_region_lock);
}

void tighten_virtual_region_perms(void)
{
    const struct virtual_region *region;

    rcu_read_lock(&rcu_virtual_region_lock);
    list_for_each_entry_rcu( region, &virtual_region_list, list )
    {
        modify_xen_mappings_lite((unsigned long)region->text_start,
                                 (unsigned long)region->text_end,
                                 PAGE_HYPERVISOR_RX);
        if ( region->rodata_start )
            modify_xen_mappings_lite((unsigned long)region->rodata_start,
                                     (unsigned long)region->rodata_end,
                                     PAGE_HYPERVISOR_RO);
    }
    rcu_read_unlock(&rcu_virtual_region_lock);
}
#endif /* CONFIG_X86 */
#endif /* CONFIG_LIVEPATCH */

void __init unregister_init_virtual_region(void)
{
    BUG_ON(system_state != SYS_STATE_active);

    remove_virtual_region(&core_init);
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: virtual_region.c === */
/* === BEGIN INLINED: pt.c === */
#include <xen_xen_config.h>
/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * xen/arch/arm/mmu/pt.c
 *
 * MMU system page table related functions.
 */

#include <xen_domain_page.h>
#include <xen_init.h>
#include <xen_pfn.h>
#include <xen_sizes.h>
#include <xen_vmap.h>

#include <asm_current.h>
#include <asm_fixmap.h>

#ifdef NDEBUG
static inline void
__attribute__ ((__format__ (__printf__, 1, 2)))
mm_printk(const char *fmt, ...) {}
#else
#define mm_printk(fmt, args...)             \
    do                                      \
    {                                       \
        dprintk(XENLOG_ERR, fmt, ## args);  \
        WARN();                             \
    } while (0)
#endif

#ifdef CONFIG_ARM_64
#define HYP_PT_ROOT_LEVEL 0
#else
#define HYP_PT_ROOT_LEVEL 1
#endif

static lpae_t *xen_map_table(mfn_t mfn)
{
    /*
     * During early boot, map_domain_page() may be unusable. Use the
     * PMAP to map temporarily a page-table.
     */
    if ( system_state == SYS_STATE_early_boot )
        return pmap_map(mfn);

    return map_domain_page(mfn);
}

static void xen_unmap_table(const lpae_t *table)
{
    /*
     * During early boot, xen_map_table() will not use map_domain_page()
     * but the PMAP.
     */
    if ( system_state == SYS_STATE_early_boot )
        pmap_unmap(table);
    else
        unmap_domain_page(table);
}

void dump_pt_walk(paddr_t ttbr, paddr_t addr,
                  unsigned int root_level,
                  unsigned int nr_root_tables)
{
    static const char *level_strs[4] = { "0TH", "1ST", "2ND", "3RD" };
    const mfn_t root_mfn = maddr_to_mfn(ttbr);
    DECLARE_OFFSETS(offsets, addr);
    lpae_t pte, *mapping;
    unsigned int level, root_table;

#ifdef CONFIG_ARM_32
    BUG_ON(root_level < 1);
#endif
    BUG_ON(root_level > 3);

    if ( nr_root_tables > 1 )
    {
        /*
         * Concatenated root-level tables. The table number will be
         * the offset at the previous level. It is not possible to
         * concatenate a level-0 root.
         */
        BUG_ON(root_level == 0);
        root_table = offsets[root_level - 1];
        printk("Using concatenated root table %u\n", root_table);
        if ( root_table >= nr_root_tables )
        {
            printk("Invalid root table offset\n");
            return;
        }
    }
    else
        root_table = 0;

    mapping = xen_map_table(mfn_add(root_mfn, root_table));

    for ( level = root_level; ; level++ )
    {
        if ( offsets[level] > XEN_PT_LPAE_ENTRIES )
            break;

        pte = mapping[offsets[level]];

        printk("%s[0x%03x] = 0x%"PRIx64"\n",
               level_strs[level], offsets[level], pte.bits);

        if ( level == 3 || !pte.walk.valid || !pte.walk.table )
            break;

        /* For next iteration */
        xen_unmap_table(mapping);
        mapping = xen_map_table(lpae_get_mfn(pte));
    }

    xen_unmap_table(mapping);
}

void dump_hyp_walk(vaddr_t addr)
{
    uint64_t ttbr = READ_SYSREG64(TTBR0_EL2);

    printk("Walking Hypervisor VA 0x%"PRIvaddr" "
           "on CPU%d via TTBR 0x%016"PRIx64"\n",
           addr, smp_processor_id(), ttbr);

    dump_pt_walk(ttbr, addr, HYP_PT_ROOT_LEVEL, 1);
}

lpae_t mfn_to_xen_entry(mfn_t mfn, unsigned int attr)
{
    lpae_t e = (lpae_t) {
        .pt = {
            .valid = 1,           /* Mappings are present */
            .table = 0,           /* Set to 1 for links and 4k maps */
            .ai = attr,
            .ns = 1,              /* Hyp mode is in the non-secure world */
            .up = 1,              /* See below */
            .ro = 0,              /* Assume read-write */
            .af = 1,              /* No need for access tracking */
            .ng = 1,              /* Makes TLB flushes easier */
            .contig = 0,          /* Assume non-contiguous */
            .xn = 1,              /* No need to execute outside .text */
            .avail = 0,           /* Reference count for domheap mapping */
        }};
    /*
     * For EL2 stage-1 page table, up (aka AP[1]) is RES1 as the translation
     * regime applies to only one exception level (see D4.4.4 and G4.6.1
     * in ARM DDI 0487B.a). If this changes, remember to update the
     * hard-coded values in head.S too.
     */

    switch ( attr )
    {
    case MT_NORMAL_NC:
        /*
         * ARM ARM: Overlaying the shareability attribute (DDI
         * 0406C.b B3-1376 to 1377)
         *
         * A memory region with a resultant memory type attribute of Normal,
         * and a resultant cacheability attribute of Inner Non-cacheable,
         * Outer Non-cacheable, must have a resultant shareability attribute
         * of Outer Shareable, otherwise shareability is UNPREDICTABLE.
         *
         * On ARMv8 sharability is ignored and explicitly treated as Outer
         * Shareable for Normal Inner Non_cacheable, Outer Non-cacheable.
         */
        e.pt.sh = LPAE_SH_OUTER;
        break;
    case MT_DEVICE_nGnRnE:
    case MT_DEVICE_nGnRE:
        /*
         * Shareability is ignored for non-Normal memory, Outer is as
         * good as anything.
         *
         * On ARMv8 sharability is ignored and explicitly treated as Outer
         * Shareable for any device memory type.
         */
        e.pt.sh = LPAE_SH_OUTER;
        break;
    default:
        e.pt.sh = LPAE_SH_INNER;  /* Xen mappings are SMP coherent */
        break;
    }

    ASSERT(!(mfn_to_maddr(mfn) & ~PADDR_MASK));

    lpae_set_mfn(e, mfn);
    return e;
}

/* Map a 4k page in a fixmap entry */
void set_fixmap(unsigned int map, mfn_t mfn, unsigned int flags)
{
    int res;

    res = map_pages_to_xen(FIXMAP_ADDR(map), mfn, 1, flags);
    BUG_ON(res != 0);
}

/* Remove a mapping from a fixmap entry */
void clear_fixmap(unsigned int map)
{
    int res;

    res = destroy_xen_mappings(FIXMAP_ADDR(map), FIXMAP_ADDR(map) + PAGE_SIZE);
    BUG_ON(res != 0);
}

/*
 * This function should only be used to remap device address ranges
 * TODO: add a check to verify this assumption
 */
void *ioremap_attr(paddr_t start, size_t len, unsigned int attributes)
{
    mfn_t mfn = _mfn(PFN_DOWN(start));
    unsigned int offs = start & (PAGE_SIZE - 1);
    unsigned int nr = PFN_UP(offs + len);
    void *ptr = __vmap(&mfn, nr, 1, 1, attributes, VMAP_DEFAULT);

    if ( ptr == NULL )
        return NULL;

    return ptr + offs;
}


static int create_xen_table(lpae_t *entry)
{
    mfn_t mfn;
    void *p;
    lpae_t pte;

    if ( system_state != SYS_STATE_early_boot )
    {
        struct page_info *pg = alloc_domheap_page(NULL, 0);

        if ( pg == NULL )
            return -ENOMEM;

        mfn = page_to_mfn(pg);
    }
    else
        mfn = alloc_boot_pages(1, 1);

    p = xen_map_table(mfn);
    clear_page(p);
    xen_unmap_table(p);

    pte = mfn_to_xen_entry(mfn, MT_NORMAL);
    pte.pt.table = 1;
    write_pte(entry, pte);
    /*
     * No ISB here. It is deferred to xen_pt_update() as the new table
     * will not be used for hardware translation table access as part of
     * the mapping update.
     */

    return 0;
}

#define XEN_TABLE_MAP_FAILED 0
#define XEN_TABLE_SUPER_PAGE 1
#define XEN_TABLE_NORMAL_PAGE 2

/*
 * Take the currently mapped table, find the corresponding entry,
 * and map the next table, if available.
 *
 * The read_only parameters indicates whether intermediate tables should
 * be allocated when not present.
 *
 * Return values:
 *  XEN_TABLE_MAP_FAILED: Either read_only was set and the entry
 *  was empty, or allocating a new page failed.
 *  XEN_TABLE_NORMAL_PAGE: next level mapped normally
 *  XEN_TABLE_SUPER_PAGE: The next entry points to a superpage.
 */
static int xen_pt_next_level(bool read_only, unsigned int level,
                             lpae_t **table, unsigned int offset)
{
    lpae_t *entry;
    int ret;
    mfn_t mfn;

    entry = *table + offset;

    if ( !lpae_is_valid(*entry) )
    {
        if ( read_only )
            return XEN_TABLE_MAP_FAILED;

        ret = create_xen_table(entry);
        if ( ret )
            return XEN_TABLE_MAP_FAILED;
    }

    /* The function xen_pt_next_level is never called at the 3rd level */
    if ( lpae_is_mapping(*entry, level) )
        return XEN_TABLE_SUPER_PAGE;

    mfn = lpae_get_mfn(*entry);

    xen_unmap_table(*table);
    *table = xen_map_table(mfn);

    return XEN_TABLE_NORMAL_PAGE;
}

/* Sanity check of the entry */
static bool xen_pt_check_entry(lpae_t entry, mfn_t mfn, unsigned int level,
                               unsigned int flags)
{
    /* Sanity check when modifying an entry. */
    if ( (flags & _PAGE_PRESENT) && mfn_eq(mfn, INVALID_MFN) )
    {
        /* We don't allow modifying an invalid entry. */
        if ( !lpae_is_valid(entry) )
        {
            mm_printk("Modifying invalid entry is not allowed.\n");
            return false;
        }

        /* We don't allow modifying a table entry */
        if ( !lpae_is_mapping(entry, level) )
        {
            mm_printk("Modifying a table entry is not allowed.\n");
            return false;
        }

        /* We don't allow changing memory attributes. */
        if ( entry.pt.ai != PAGE_AI_MASK(flags) )
        {
            mm_printk("Modifying memory attributes is not allowed (0x%x -> 0x%x).\n",
                      entry.pt.ai, PAGE_AI_MASK(flags));
            return false;
        }

        /* We don't allow modifying entry with contiguous bit set. */
        if ( entry.pt.contig )
        {
            mm_printk("Modifying entry with contiguous bit set is not allowed.\n");
            return false;
        }
    }
    /* Sanity check when inserting a mapping */
    else if ( flags & _PAGE_PRESENT )
    {
        /* We should be here with a valid MFN. */
        ASSERT(!mfn_eq(mfn, INVALID_MFN));

        /*
         * We don't allow replacing any valid entry.
         *
         * Note that the function xen_pt_update() relies on this
         * assumption and will skip the TLB flush. The function will need
         * to be updated if the check is relaxed.
         */
        if ( lpae_is_valid(entry) )
        {
            if ( lpae_is_mapping(entry, level) )
                mm_printk("Changing MFN for a valid entry is not allowed (%#"PRI_mfn" -> %#"PRI_mfn").\n",
                          mfn_x(lpae_get_mfn(entry)), mfn_x(mfn));
            else
                mm_printk("Trying to replace a table with a mapping.\n");
            return false;
        }
    }
    /* Sanity check when removing a mapping. */
    else if ( (flags & (_PAGE_PRESENT|_PAGE_POPULATE)) == 0 )
    {
        /* We should be here with an invalid MFN. */
        ASSERT(mfn_eq(mfn, INVALID_MFN));

        /* We don't allow removing a table */
        if ( lpae_is_table(entry, level) )
        {
            mm_printk("Removing a table is not allowed.\n");
            return false;
        }

        /* We don't allow removing a mapping with contiguous bit set. */
        if ( entry.pt.contig )
        {
            mm_printk("Removing entry with contiguous bit set is not allowed.\n");
            return false;
        }
    }
    /* Sanity check when populating the page-table. No check so far. */
    else
    {
        ASSERT(flags & _PAGE_POPULATE);
        /* We should be here with an invalid MFN */
        ASSERT(mfn_eq(mfn, INVALID_MFN));
    }

    return true;
}

/* Update an entry at the level @target. */
static int xen_pt_update_entry(mfn_t root, unsigned long virt,
                               mfn_t mfn, unsigned int target,
                               unsigned int flags)
{
    int rc;
    unsigned int level;
    lpae_t *table;
    /*
     * The intermediate page tables are read-only when the MFN is not valid
     * and we are not populating page table.
     * This means we either modify permissions or remove an entry.
     */
    bool read_only = mfn_eq(mfn, INVALID_MFN) && !(flags & _PAGE_POPULATE);
    lpae_t pte, *entry;

    /* convenience aliases */
    DECLARE_OFFSETS(offsets, (paddr_t)virt);

    /* _PAGE_POPULATE and _PAGE_PRESENT should never be set together. */
    ASSERT((flags & (_PAGE_POPULATE|_PAGE_PRESENT)) != (_PAGE_POPULATE|_PAGE_PRESENT));

    table = xen_map_table(root);
    for ( level = HYP_PT_ROOT_LEVEL; level < target; level++ )
    {
        rc = xen_pt_next_level(read_only, level, &table, offsets[level]);
        if ( rc == XEN_TABLE_MAP_FAILED )
        {
            /*
             * We are here because xen_pt_next_level has failed to map
             * the intermediate page table (e.g the table does not exist
             * and the pt is read-only). It is a valid case when
             * removing a mapping as it may not exist in the page table.
             * In this case, just ignore it.
             */
            if ( flags & (_PAGE_PRESENT|_PAGE_POPULATE) )
            {
                mm_printk("%s: Unable to map level %u\n", __func__, level);
                rc = -ENOENT;
                goto out;
            }
            else
            {
                rc = 0;
                goto out;
            }
        }
        else if ( rc != XEN_TABLE_NORMAL_PAGE )
            break;
    }

    if ( level != target )
    {
        mm_printk("%s: Shattering superpage is not supported\n", __func__);
        rc = -EOPNOTSUPP;
        goto out;
    }

    entry = table + offsets[level];

    rc = -EINVAL;
    if ( !xen_pt_check_entry(*entry, mfn, level, flags) )
        goto out;

    /* If we are only populating page-table, then we are done. */
    rc = 0;
    if ( flags & _PAGE_POPULATE )
        goto out;

    /* We are removing the page */
    if ( !(flags & _PAGE_PRESENT) )
        memset(&pte, 0x00, sizeof(pte));
    else
    {
        /* We are inserting a mapping => Create new pte. */
        if ( !mfn_eq(mfn, INVALID_MFN) )
        {
            pte = mfn_to_xen_entry(mfn, PAGE_AI_MASK(flags));

            /*
             * First and second level pages set pte.pt.table = 0, but
             * third level entries set pte.pt.table = 1.
             */
            pte.pt.table = (level == 3);
        }
        else /* We are updating the permission => Copy the current pte. */
            pte = *entry;

        /* Set permission */
        pte.pt.ro = PAGE_RO_MASK(flags);
        pte.pt.xn = PAGE_XN_MASK(flags);
        /* Set contiguous bit */
        pte.pt.contig = !!(flags & _PAGE_CONTIG);
    }

    write_pte(entry, pte);
    /*
     * No ISB or TLB flush here. They are deferred to xen_pt_update()
     * as the entry will not be used as part of the mapping update.
     */

    rc = 0;

out:
    xen_unmap_table(table);

    return rc;
}

/* Return the level where mapping should be done */
static int xen_pt_mapping_level(unsigned long vfn, mfn_t mfn, unsigned long nr,
                                unsigned int flags)
{
    unsigned int level;
    unsigned long mask;

    /*
      * Don't take into account the MFN when removing mapping (i.e
      * MFN_INVALID) to calculate the correct target order.
      *
      * Per the Arm Arm, `vfn` and `mfn` must be both superpage aligned.
      * They are or-ed together and then checked against the size of
      * each level.
      *
      * `left` is not included and checked separately to allow
      * superpage mapping even if it is not properly aligned (the
      * user may have asked to map 2MB + 4k).
      */
     mask = !mfn_eq(mfn, INVALID_MFN) ? mfn_x(mfn) : 0;
     mask |= vfn;

     /*
      * Always use level 3 mapping unless the caller request block
      * mapping.
      */
     if ( likely(!(flags & _PAGE_BLOCK)) )
         level = 3;
     else if ( !(mask & (BIT(FIRST_ORDER, UL) - 1)) &&
               (nr >= BIT(FIRST_ORDER, UL)) )
         level = 1;
     else if ( !(mask & (BIT(SECOND_ORDER, UL) - 1)) &&
               (nr >= BIT(SECOND_ORDER, UL)) )
         level = 2;
     else
         level = 3;

     return level;
}

#define XEN_PT_4K_NR_CONTIG 16

/*
 * Check whether the contiguous bit can be set. Return the number of
 * contiguous entry allowed. If not allowed, return 1.
 */
static unsigned int xen_pt_check_contig(unsigned long vfn, mfn_t mfn,
                                        unsigned int level, unsigned long left,
                                        unsigned int flags)
{
    unsigned long nr_contig;

    /*
     * Allow the contiguous bit to set when the caller requests block
     * mapping.
     */
    if ( !(flags & _PAGE_BLOCK) )
        return 1;

    /*
     * We don't allow to remove mapping with the contiguous bit set.
     * So shortcut the logic and directly return 1.
     */
    if ( mfn_eq(mfn, INVALID_MFN) )
        return 1;

    /*
     * The number of contiguous entries varies depending on the page
     * granularity used. The logic below assumes 4KB.
     */
    BUILD_BUG_ON(PAGE_SIZE != SZ_4K);

    /*
     * In order to enable the contiguous bit, we should have enough entries
     * to map left and both the virtual and physical address should be
     * aligned to the size of 16 translation tables entries.
     */
    nr_contig = BIT(XEN_PT_LEVEL_ORDER(level), UL) * XEN_PT_4K_NR_CONTIG;

    if ( (left < nr_contig) || ((mfn_x(mfn) | vfn) & (nr_contig - 1)) )
        return 1;

    return XEN_PT_4K_NR_CONTIG;
}

static DEFINE_SPINLOCK(xen_pt_lock);

static int xen_pt_update(unsigned long virt,
                         mfn_t mfn,
                         /* const on purpose as it is used for TLB flush */
                         const unsigned long nr_mfns,
                         unsigned int flags)
{
    int rc = 0;
    unsigned long vfn = virt >> PAGE_SHIFT;
    unsigned long left = nr_mfns;

    /*
     * For arm32, page-tables are different on each CPUs. Yet, they share
     * some common mappings. It is assumed that only common mappings
     * will be modified with this function.
     *
     * XXX: Add a check.
     */
    const mfn_t root = maddr_to_mfn(READ_SYSREG64(TTBR0_EL2));

    /*
     * The hardware was configured to forbid mapping both writeable and
     * executable.
     * When modifying/creating mapping (i.e _PAGE_PRESENT is set),
     * prevent any update if this happen.
     */
    if ( (flags & _PAGE_PRESENT) && !PAGE_RO_MASK(flags) &&
         !PAGE_XN_MASK(flags) )
    {
        mm_printk("Mappings should not be both Writeable and Executable.\n");
        return -EINVAL;
    }

    if ( flags & _PAGE_CONTIG )
    {
        mm_printk("_PAGE_CONTIG is an internal only flag.\n");
        return -EINVAL;
    }

    if ( !IS_ALIGNED(virt, PAGE_SIZE) )
    {
        mm_printk("The virtual address is not aligned to the page-size.\n");
        return -EINVAL;
    }

    spin_lock(&xen_pt_lock);

    while ( left )
    {
        unsigned int order, level, nr_contig, new_flags;

        level = xen_pt_mapping_level(vfn, mfn, left, flags);
        order = XEN_PT_LEVEL_ORDER(level);

        ASSERT(left >= BIT(order, UL));

        /*
         * Check if we can set the contiguous mapping and update the
         * flags accordingly.
         */
        nr_contig = xen_pt_check_contig(vfn, mfn, level, left, flags);
        new_flags = flags | ((nr_contig > 1) ? _PAGE_CONTIG : 0);

        for ( ; nr_contig > 0; nr_contig-- )
        {
            rc = xen_pt_update_entry(root, vfn << PAGE_SHIFT, mfn, level,
                                     new_flags);
            if ( rc )
                break;

            vfn += 1U << order;
            if ( !mfn_eq(mfn, INVALID_MFN) )
                mfn = mfn_add(mfn, 1U << order);

            left -= (1U << order);
        }

        if ( rc )
            break;
    }

    /*
     * The TLBs flush can be safely skipped when a mapping is inserted
     * as we don't allow mapping replacement (see xen_pt_check_entry()).
     * Although we still need an ISB to ensure any DSB in
     * write_pte() will complete because the mapping may be used soon
     * after.
     *
     * For all the other cases, the TLBs will be flushed unconditionally
     * even if the mapping has failed. This is because we may have
     * partially modified the PT. This will prevent any unexpected
     * behavior afterwards.
     */
    if ( !((flags & _PAGE_PRESENT) && !mfn_eq(mfn, INVALID_MFN)) )
        flush_xen_tlb_range_va(virt, PAGE_SIZE * nr_mfns);
    else
        isb();

    spin_unlock(&xen_pt_lock);

    return rc;
}

int map_pages_to_xen(unsigned long virt,
                     mfn_t mfn,
                     unsigned long nr_mfns,
                     unsigned int flags)
{
    return xen_pt_update(virt, mfn, nr_mfns, flags);
}

int __init populate_pt_range(unsigned long virt, unsigned long nr_mfns)
{
    return xen_pt_update(virt, INVALID_MFN, nr_mfns, _PAGE_POPULATE);
}

int destroy_xen_mappings(unsigned long s, unsigned long e)
{
    ASSERT(IS_ALIGNED(s, PAGE_SIZE));
    ASSERT(IS_ALIGNED(e, PAGE_SIZE));
    ASSERT(s <= e);
    return xen_pt_update(s, INVALID_MFN, (e - s) >> PAGE_SHIFT, 0);
}

int modify_xen_mappings(unsigned long s, unsigned long e, unsigned int nf)
{
    ASSERT(IS_ALIGNED(s, PAGE_SIZE));
    ASSERT(IS_ALIGNED(e, PAGE_SIZE));
    ASSERT(s <= e);
    return xen_pt_update(s, INVALID_MFN, (e - s) >> PAGE_SHIFT, nf);
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: pt.c === */
/* === BEGIN INLINED: hypfs.c === */
#include <xen_xen_config.h>
/******************************************************************************
 *
 * hypfs.c
 *
 * Simple sysfs-like file system for the hypervisor.
 */

#include <xen_err.h>
#include <xen_guest_access.h>
#include <xen_hypercall.h>
#include <xen_hypfs.h>
#include <xen_lib.h>
#include <xen_param.h>
#include <xen_rwlock.h>
#include <public_hypfs.h>

#ifdef CONFIG_COMPAT
#include <compat/hypfs.h>
CHECK_hypfs_dirlistentry;
#endif

#define DIRENTRY_NAME_OFF offsetof(struct xen_hypfs_dirlistentry, name)
#define DIRENTRY_SIZE(name_len) \
    (DIRENTRY_NAME_OFF +        \
     ROUNDUP((name_len) + 1, alignof(struct xen_hypfs_direntry)))

const struct hypfs_funcs hypfs_dir_funcs = {
    .enter = hypfs_node_enter,
    .exit = hypfs_node_exit,
    .read = hypfs_read_dir,
    .write = hypfs_write_deny,
    .getsize = hypfs_getsize,
    .findentry = hypfs_dir_findentry,
};
const struct hypfs_funcs hypfs_leaf_ro_funcs = {
    .enter = hypfs_node_enter,
    .exit = hypfs_node_exit,
    .read = hypfs_read_leaf,
    .write = hypfs_write_deny,
    .getsize = hypfs_getsize,
    .findentry = hypfs_leaf_findentry,
};
const struct hypfs_funcs hypfs_leaf_wr_funcs = {
    .enter = hypfs_node_enter,
    .exit = hypfs_node_exit,
    .read = hypfs_read_leaf,
    .write = hypfs_write_leaf,
    .getsize = hypfs_getsize,
    .findentry = hypfs_leaf_findentry,
};
const struct hypfs_funcs hypfs_custom_wr_funcs = {
    .enter = hypfs_node_enter,
    .exit = hypfs_node_exit,
    .read = hypfs_read_leaf,
    .write = hypfs_write_custom,
    .getsize = hypfs_getsize,
    .findentry = hypfs_leaf_findentry,
};

static DEFINE_RWLOCK(hypfs_lock);
enum hypfs_lock_state {
    hypfs_unlocked,
    hypfs_read_locked,
    hypfs_write_locked
};
static DEFINE_PER_CPU(enum hypfs_lock_state, hypfs_locked);
static DEFINE_PER_CPU(void *, hypfs_dyndata);

static DEFINE_PER_CPU(const struct hypfs_entry *, hypfs_last_node_entered);

HYPFS_DIR_INIT(hypfs_root, "");

static void hypfs_read_lock(void)
{
    ASSERT(this_cpu(hypfs_locked) != hypfs_write_locked);

    read_lock(&hypfs_lock);
    this_cpu(hypfs_locked) = hypfs_read_locked;
}

static void hypfs_write_lock(void)
{
    ASSERT(this_cpu(hypfs_locked) == hypfs_unlocked);

    write_lock(&hypfs_lock);
    this_cpu(hypfs_locked) = hypfs_write_locked;
}

static void hypfs_unlock(void)
{
    enum hypfs_lock_state locked = this_cpu(hypfs_locked);

    this_cpu(hypfs_locked) = hypfs_unlocked;

    switch ( locked )
    {
    case hypfs_read_locked:
        read_unlock(&hypfs_lock);
        break;
    case hypfs_write_locked:
        write_unlock(&hypfs_lock);
        break;
    default:
        BUG();
    }
}

const struct hypfs_entry *cf_check hypfs_node_enter(
    const struct hypfs_entry *entry)
{
    return entry;
}

void cf_check hypfs_node_exit(const struct hypfs_entry *entry)
{
}

static int node_enter(const struct hypfs_entry *entry)
{
    const struct hypfs_entry **last = &this_cpu(hypfs_last_node_entered);

    entry = entry->funcs->enter(entry);
    if ( IS_ERR(entry) )
        return PTR_ERR(entry);

    ASSERT(entry);
    ASSERT(!*last || *last == entry->parent);

    *last = entry;

    return 0;
}

static void node_exit(const struct hypfs_entry *entry)
{
    const struct hypfs_entry **last = &this_cpu(hypfs_last_node_entered);

    ASSERT(*last == entry);
    *last = entry->parent;

    entry->funcs->exit(entry);
}

static void node_exit_all(void)
{
    const struct hypfs_entry **last = &this_cpu(hypfs_last_node_entered);

    while ( *last )
        node_exit(*last);
}

#undef hypfs_alloc_dyndata
void *hypfs_alloc_dyndata(unsigned long size)
{
    unsigned int cpu = smp_processor_id();
    void **dyndata = &per_cpu(hypfs_dyndata, cpu);

    ASSERT(per_cpu(hypfs_locked, cpu) != hypfs_unlocked);
    ASSERT(*dyndata == NULL);

    *dyndata = xzalloc_array(unsigned char, size);

    return *dyndata;
}

void *hypfs_get_dyndata(void)
{
    void *dyndata = this_cpu(hypfs_dyndata);

    ASSERT(dyndata);

    return dyndata;
}

void hypfs_free_dyndata(void)
{
    void **dyndata = &this_cpu(hypfs_dyndata);

    XFREE(*dyndata);
}

static int add_entry(struct hypfs_entry_dir *parent, struct hypfs_entry *new)
{
    int ret = -ENOENT;
    struct hypfs_entry *e;

    ASSERT(new->funcs->enter);
    ASSERT(new->funcs->exit);
    ASSERT(new->funcs->read);
    ASSERT(new->funcs->write);
    ASSERT(new->funcs->getsize);
    ASSERT(new->funcs->findentry);

    hypfs_write_lock();

    list_for_each_entry ( e, &parent->dirlist, list )
    {
        int cmp = strcmp(e->name, new->name);

        if ( cmp > 0 )
        {
            ret = 0;
            list_add_tail(&new->list, &e->list);
            break;
        }
        if ( cmp == 0 )
        {
            ret = -EEXIST;
            break;
        }
    }

    if ( ret == -ENOENT )
    {
        ret = 0;
        list_add_tail(&new->list, &parent->dirlist);
    }

    if ( !ret )
    {
        unsigned int sz = strlen(new->name);

        parent->e.size += DIRENTRY_SIZE(sz);
        new->parent = &parent->e;
    }

    hypfs_unlock();

    return ret;
}

int hypfs_add_dir(struct hypfs_entry_dir *parent,
                  struct hypfs_entry_dir *dir, bool nofault)
{
    int ret;

    ret = add_entry(parent, &dir->e);
    BUG_ON(nofault && ret);

    return ret;
}

void hypfs_add_dyndir(struct hypfs_entry_dir *parent,
                      struct hypfs_entry_dir *template)
{
    /*
     * As the template is only a placeholder for possibly multiple dynamically
     * generated directories, the link up to its parent can be static, while
     * the "real" children of the parent are to be found via the parent's
     * findentry function only.
     */
    template->e.parent = &parent->e;
}

int hypfs_add_leaf(struct hypfs_entry_dir *parent,
                   struct hypfs_entry_leaf *leaf, bool nofault)
{
    int ret;

    if ( !leaf->u.content )
        ret = -EINVAL;
    else
        ret = add_entry(parent, &leaf->e);
    BUG_ON(nofault && ret);

    return ret;
}

static int hypfs_get_path_user(char *buf,
                               XEN_GUEST_HANDLE_PARAM(const_char) uaddr,
                               unsigned long ulen)
{
    if ( ulen > XEN_HYPFS_MAX_PATHLEN )
        return -EINVAL;

    if ( copy_from_guest(buf, uaddr, ulen) )
        return -EFAULT;

    if ( memchr(buf, 0, ulen) != buf + ulen - 1 )
        return -EINVAL;

    return 0;
}

struct hypfs_entry *cf_check hypfs_leaf_findentry(
    const struct hypfs_entry_dir *dir, const char *name, unsigned int name_len)
{
    return ERR_PTR(-ENOTDIR);
}

struct hypfs_entry *cf_check hypfs_dir_findentry(
    const struct hypfs_entry_dir *dir, const char *name, unsigned int name_len)
{
    struct hypfs_entry *entry;

    list_for_each_entry ( entry, &dir->dirlist, list )
    {
        int cmp = strncmp(name, entry->name, name_len);

        if ( cmp < 0 )
            return ERR_PTR(-ENOENT);

        if ( !cmp && strlen(entry->name) == name_len )
            return entry;
    }

    return ERR_PTR(-ENOENT);
}

static struct hypfs_entry *hypfs_get_entry_rel(struct hypfs_entry_dir *dir,
                                               const char *path)
{
    const char *end;
    struct hypfs_entry *entry;
    unsigned int name_len;
    int ret;

    for ( ; ; )
    {
        if ( dir->e.type != XEN_HYPFS_TYPE_DIR )
            return ERR_PTR(-ENOENT);

        if ( !*path )
            return &dir->e;

        end = strchr(path, '/');
        if ( !end )
            end = strchr(path, '\0');
        name_len = end - path;

        ret = node_enter(&dir->e);
        if ( ret )
            return ERR_PTR(ret);

        entry = dir->e.funcs->findentry(dir, path, name_len);
        if ( IS_ERR(entry) || !*end )
            return entry;

        path = end + 1;
        dir = container_of(entry, struct hypfs_entry_dir, e);
    }

    return ERR_PTR(-ENOENT);
}

static struct hypfs_entry *hypfs_get_entry(const char *path)
{
    if ( path[0] != '/' )
        return ERR_PTR(-EINVAL);

    return hypfs_get_entry_rel(&hypfs_root, path + 1);
}

unsigned int cf_check hypfs_getsize(const struct hypfs_entry *entry)
{
    return entry->size;
}

/*
 * Fill the direntry for a dynamically generated entry. Especially the
 * generated name needs to be kept in sync with hypfs_gen_dyndir_id_entry().
 */
int hypfs_read_dyndir_id_entry(const struct hypfs_entry_dir *template,
                               unsigned int id, bool is_last,
                               XEN_GUEST_HANDLE_PARAM(void) *uaddr)
{
    struct xen_hypfs_dirlistentry direntry;
    char name[HYPFS_DYNDIR_ID_NAMELEN];
    unsigned int e_namelen, e_len;

    e_namelen = snprintf(name, sizeof(name), template->e.name, id);
    if ( e_namelen >= sizeof(name) )
    {
        ASSERT_UNREACHABLE();
        return -ENOBUFS;
    }
    e_len = DIRENTRY_SIZE(e_namelen);
    direntry.e.pad = 0;
    direntry.e.type = template->e.type;
    direntry.e.encoding = template->e.encoding;
    direntry.e.content_len = template->e.funcs->getsize(&template->e);
    direntry.e.max_write_len = template->e.max_size;
    direntry.off_next = is_last ? 0 : e_len;
    if ( copy_to_guest(*uaddr, &direntry, 1) )
        return -EFAULT;
    if ( copy_to_guest_offset(*uaddr, DIRENTRY_NAME_OFF, name,
                              e_namelen + 1) )
        return -EFAULT;

    guest_handle_add_offset(*uaddr, e_len);

    return 0;
}

static const struct hypfs_entry *cf_check hypfs_dyndir_enter(
    const struct hypfs_entry *entry)
{
    const struct hypfs_dyndir_id *data;

    data = hypfs_get_dyndata();

    /* Use template with original enter function. */
    return data->template->e.funcs->enter(&data->template->e);
}

static struct hypfs_entry *cf_check hypfs_dyndir_findentry(
    const struct hypfs_entry_dir *dir, const char *name, unsigned int name_len)
{
    const struct hypfs_dyndir_id *data;

    data = hypfs_get_dyndata();

    /* Use template with original findentry function. */
    return data->template->e.funcs->findentry(data->template, name, name_len);
}

static int cf_check hypfs_read_dyndir(
    const struct hypfs_entry *entry, XEN_GUEST_HANDLE_PARAM(void) uaddr)
{
    const struct hypfs_dyndir_id *data;

    data = hypfs_get_dyndata();

    /* Use template with original read function. */
    return data->template->e.funcs->read(&data->template->e, uaddr);
}

/*
 * Fill dyndata with a dynamically generated entry based on a template
 * and a numerical id.
 * Needs to be kept in sync with hypfs_read_dyndir_id_entry() regarding the
 * name generated.
 */
struct hypfs_entry *hypfs_gen_dyndir_id_entry(
    const struct hypfs_entry_dir *template, unsigned int id, void *data)
{
    struct hypfs_dyndir_id *dyndata;

    dyndata = hypfs_get_dyndata();

    dyndata->template = template;
    dyndata->id = id;
    dyndata->data = data;
    snprintf(dyndata->name, sizeof(dyndata->name), template->e.name, id);
    dyndata->dir = *template;
    dyndata->dir.e.name = dyndata->name;
    dyndata->dir.e.funcs = &dyndata->funcs;
    dyndata->funcs = *template->e.funcs;
    dyndata->funcs.enter = hypfs_dyndir_enter;
    dyndata->funcs.findentry = hypfs_dyndir_findentry;
    dyndata->funcs.read = hypfs_read_dyndir;

    return &dyndata->dir.e;
}

unsigned int hypfs_dynid_entry_size(const struct hypfs_entry *template,
                                    unsigned int id)
{
    return DIRENTRY_SIZE(snprintf(NULL, 0, template->name, id));
}

int cf_check hypfs_read_dir(const struct hypfs_entry *entry,
                            XEN_GUEST_HANDLE_PARAM(void) uaddr)
{
    const struct hypfs_entry_dir *d;
    const struct hypfs_entry *e;
    unsigned int size = entry->funcs->getsize(entry);
    int ret;

    ASSERT(this_cpu(hypfs_locked) != hypfs_unlocked);

    d = container_of(entry, const struct hypfs_entry_dir, e);

    list_for_each_entry ( e, &d->dirlist, list )
    {
        struct xen_hypfs_dirlistentry direntry;
        unsigned int e_namelen = strlen(e->name);
        unsigned int e_len = DIRENTRY_SIZE(e_namelen);

        ret = node_enter(e);
        if ( ret )
            return ret;

        direntry.e.pad = 0;
        direntry.e.type = e->type;
        direntry.e.encoding = e->encoding;
        direntry.e.content_len = e->funcs->getsize(e);
        direntry.e.max_write_len = e->max_size;
        direntry.off_next = list_is_last(&e->list, &d->dirlist) ? 0 : e_len;

        node_exit(e);

        if ( copy_to_guest(uaddr, &direntry, 1) )
            return -EFAULT;

        if ( copy_to_guest_offset(uaddr, DIRENTRY_NAME_OFF,
                                  e->name, e_namelen + 1) )
            return -EFAULT;

        guest_handle_add_offset(uaddr, e_len);

        ASSERT(e_len <= size);
        size -= e_len;
    }

    return 0;
}

int cf_check hypfs_read_leaf(
    const struct hypfs_entry *entry, XEN_GUEST_HANDLE_PARAM(void) uaddr)
{
    const struct hypfs_entry_leaf *l;
    unsigned int size = entry->funcs->getsize(entry);

    ASSERT(this_cpu(hypfs_locked) != hypfs_unlocked);

    l = container_of(entry, const struct hypfs_entry_leaf, e);

    return copy_to_guest(uaddr, l->u.content, size) ?  -EFAULT : 0;
}

static int hypfs_read(const struct hypfs_entry *entry,
                      XEN_GUEST_HANDLE_PARAM(void) uaddr, unsigned long ulen)
{
    struct xen_hypfs_direntry e;
    unsigned int size = entry->funcs->getsize(entry);
    long ret = -EINVAL;

    if ( ulen < sizeof(e) )
        goto out;

    e.pad = 0;
    e.type = entry->type;
    e.encoding = entry->encoding;
    e.content_len = size;
    e.max_write_len = entry->max_size;

    ret = -EFAULT;
    if ( copy_to_guest(uaddr, &e, 1) )
        goto out;

    ret = -ENOBUFS;
    if ( ulen < size + sizeof(e) )
        goto out;

    guest_handle_add_offset(uaddr, sizeof(e));

    ret = entry->funcs->read(entry, uaddr);

 out:
    return ret;
}

int cf_check hypfs_write_leaf(
    struct hypfs_entry_leaf *leaf, XEN_GUEST_HANDLE_PARAM(const_void) uaddr,
    unsigned int ulen)
{
    char *buf;
    int ret;
    struct hypfs_entry *e = &leaf->e;

    ASSERT(this_cpu(hypfs_locked) == hypfs_write_locked);
    ASSERT(leaf->e.max_size);

    if ( ulen > e->max_size )
        return -ENOSPC;

    if ( e->type != XEN_HYPFS_TYPE_STRING &&
         e->type != XEN_HYPFS_TYPE_BLOB && ulen != e->funcs->getsize(e) )
        return -EDOM;

    buf = xmalloc_array(char, ulen);
    if ( !buf )
        return -ENOMEM;

    ret = -EFAULT;
    if ( copy_from_guest(buf, uaddr, ulen) )
        goto out;

    ret = -EINVAL;
    if ( e->type == XEN_HYPFS_TYPE_STRING &&
         e->encoding == XEN_HYPFS_ENC_PLAIN &&
         memchr(buf, 0, ulen) != (buf + ulen - 1) )
        goto out;

    ret = 0;
    memcpy(leaf->u.write_ptr, buf, ulen);
    e->size = ulen;

 out:
    xfree(buf);
    return ret;
}


int cf_check hypfs_write_custom(
    struct hypfs_entry_leaf *leaf, XEN_GUEST_HANDLE_PARAM(const_void) uaddr,
    unsigned int ulen)
{
    struct param_hypfs *p;
    char *buf;
    int ret;

    ASSERT(this_cpu(hypfs_locked) == hypfs_write_locked);
    ASSERT(leaf->e.max_size);

    /* Avoid oversized buffer allocation. */
    if ( ulen > MAX_PARAM_SIZE )
        return -ENOSPC;

    buf = xzalloc_array(char, ulen);
    if ( !buf )
        return -ENOMEM;

    ret = -EFAULT;
    if ( copy_from_guest(buf, uaddr, ulen) )
        goto out;

    ret = -EDOM;
    if ( memchr(buf, 0, ulen) != (buf + ulen - 1) )
        goto out;

    p = container_of(leaf, struct param_hypfs, hypfs);
    ret = p->func(buf);

 out:
    xfree(buf);
    return ret;
}

int cf_check hypfs_write_deny(
    struct hypfs_entry_leaf *leaf, XEN_GUEST_HANDLE_PARAM(const_void) uaddr,
    unsigned int ulen)
{
    return -EACCES;
}

static int hypfs_write(struct hypfs_entry *entry,
                       XEN_GUEST_HANDLE_PARAM(const_void) uaddr,
                       unsigned long ulen)
{
    struct hypfs_entry_leaf *l;

    l = container_of(entry, struct hypfs_entry_leaf, e);

    return entry->funcs->write(l, uaddr, ulen);
}

long do_hypfs_op(
    unsigned int cmd, XEN_GUEST_HANDLE_PARAM(const_char) arg1,
    unsigned long arg2, XEN_GUEST_HANDLE_PARAM(void) arg3, unsigned long arg4)
{
    int ret;
    struct hypfs_entry *entry;
    static char path[XEN_HYPFS_MAX_PATHLEN];

    if ( xsm_hypfs_op(XSM_PRIV) )
        return -EPERM;

    if ( cmd == XEN_HYPFS_OP_get_version )
    {
        if ( !guest_handle_is_null(arg1) || arg2 ||
             !guest_handle_is_null(arg3) || arg4 )
            return -EINVAL;

        return XEN_HYPFS_VERSION;
    }

    if ( cmd == XEN_HYPFS_OP_write_contents )
        hypfs_write_lock();
    else
        hypfs_read_lock();

    ret = hypfs_get_path_user(path, arg1, arg2);
    if ( ret )
        goto out;

    entry = hypfs_get_entry(path);
    if ( IS_ERR(entry) )
    {
        ret = PTR_ERR(entry);
        goto out;
    }

    ret = node_enter(entry);
    if ( ret )
        goto out;

    switch ( cmd )
    {
    case XEN_HYPFS_OP_read:
        ret = hypfs_read(entry, arg3, arg4);
        break;

    case XEN_HYPFS_OP_write_contents:
        ret = hypfs_write(entry, guest_handle_const_cast(arg3, void), arg4);
        break;

    default:
        ret = -EOPNOTSUPP;
        break;
    }

 out:
    node_exit_all();

    hypfs_unlock();

    return ret;
}

/* === END INLINED: hypfs.c === */
/* === BEGIN INLINED: efi_stub.c === */
#include <xen_xen_config.h>
#include <xen_types.h>
#include <xen_efi.h>

bool efi_enabled(unsigned int feature)
{
    return false;
}

/* === END INLINED: efi_stub.c === */
