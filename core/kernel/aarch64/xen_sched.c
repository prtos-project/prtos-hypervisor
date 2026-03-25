/* Xen domain management - consolidated */
/* === BEGIN INLINED: common_domain.c === */
#include <xen_xen_config.h>
/******************************************************************************
 * domain.c
 * 
 * Generic domain-handling functions.
 */

#include <xen_compat.h>
#include <xen_init.h>
#include <xen_lib.h>
#include <xen_ctype.h>
#include <xen_err.h>
#include <xen_param.h>
#include <xen_sched.h>
#include <xen_domain.h>
#include <xen_mm.h>
#include <xen_event.h>
#include <xen_vm_event.h>
#include <xen_time.h>
#include <xen_console.h>
#include <xen_softirq.h>
#include <xen_tasklet.h>
#include <xen_domain_page.h>
#include <xen_rangeset.h>
#include <xen_guest_access.h>
#include <xen_hypercall.h>
#include <xen_delay.h>
#include <xen_shutdown.h>
#include <xen_percpu.h>
#include <xen_multicall.h>
#include <xen_rcupdate.h>
#include <xen_wait.h>
#include <xen_grant_table.h>
#include <xen_xenoprof.h>
#include <xen_irq.h>
#include <xen_argo.h>
#include <asm_p2m.h>
#include <asm_processor.h>
#include <public_sched.h>
#include <public_sysctl.h>
#include <public_vcpu.h>
#include <xsm_xsm.h>
#include <xen_trace.h>
#include <asm_setup.h>

#ifdef CONFIG_X86
#include <asm/guest.h>
#endif


/* opt_dom0_vcpus_pin: If true, dom0 VCPUs are pinned. */
bool opt_dom0_vcpus_pin;
boolean_param("dom0_vcpus_pin", opt_dom0_vcpus_pin);

/* Protect updates/reads (resp.) of domain_list and domain_hash. */
DEFINE_SPINLOCK(domlist_update_lock);
DEFINE_RCU_READ_LOCK(domlist_read_lock);

#define DOMAIN_HASH_SIZE 256
#define DOMAIN_HASH(_id) ((int)(_id)&(DOMAIN_HASH_SIZE-1))
static struct domain *domain_hash[DOMAIN_HASH_SIZE];
struct domain *domain_list;

struct domain *hardware_domain __read_mostly;

#ifdef CONFIG_LATE_HWDOM
domid_t hardware_domid __read_mostly;
integer_param("hardware_dom", hardware_domid);
#endif

/* Private domain structs for DOMID_XEN, DOMID_IO, etc. */
struct domain *__read_mostly dom_xen;
struct domain *__read_mostly dom_io;
#ifdef CONFIG_MEM_SHARING
struct domain *__read_mostly dom_cow;
#endif

struct vcpu *idle_vcpu[4] __read_mostly;

vcpu_info_t dummy_vcpu_info;

bool __read_mostly vmtrace_available;

bool __read_mostly vpmu_is_available;

static void __domain_finalise_shutdown(struct domain *d)
{
    struct vcpu *v;

    BUG_ON(!spin_is_locked(&d->shutdown_lock));

    if ( d->is_shut_down )
        return;

    for_each_vcpu ( d, v )
        if ( !v->paused_for_shutdown )
            return;

    d->is_shut_down = 1;
    if ( (d->shutdown_code == SHUTDOWN_suspend) && d->suspend_evtchn )
        evtchn_send(d, d->suspend_evtchn);
    else
        send_global_virq(VIRQ_DOM_EXC);
}

static void vcpu_check_shutdown(struct vcpu *v)
{
    struct domain *d = v->domain;

    spin_lock(&d->shutdown_lock);

    if ( d->is_shutting_down )
    {
        if ( !v->paused_for_shutdown )
            vcpu_pause_nosync(v);
        v->paused_for_shutdown = 1;
        v->defer_shutdown = 0;
        __domain_finalise_shutdown(d);
    }

    spin_unlock(&d->shutdown_lock);
}

static void vcpu_info_reset(struct vcpu *v)
{
    struct domain *d = v->domain;

    v->vcpu_info_area.map =
        ((v->vcpu_id < XEN_LEGACY_MAX_VCPUS)
         ? (vcpu_info_t *)&shared_info(d, vcpu_info[v->vcpu_id])
         : &dummy_vcpu_info);
}

static void vmtrace_free_buffer(struct vcpu *v)
{
    const struct domain *d = v->domain;
    struct page_info *pg = v->vmtrace.pg;
    unsigned int i;

    if ( !pg )
        return;

    v->vmtrace.pg = NULL;

    for ( i = 0; i < (d->vmtrace_size >> PAGE_SHIFT); i++ )
    {
        put_page_alloc_ref(&pg[i]);
        put_page_and_type(&pg[i]);
    }
}

static int vmtrace_alloc_buffer(struct vcpu *v)
{
    struct domain *d = v->domain;
    struct page_info *pg;
    unsigned int i;

    if ( !d->vmtrace_size )
        return 0;

    pg = alloc_domheap_pages(d, get_order_from_bytes(d->vmtrace_size),
                             MEMF_no_refcount);
    if ( !pg )
        return -ENOMEM;

    for ( i = 0; i < (d->vmtrace_size >> PAGE_SHIFT); i++ )
        if ( unlikely(!get_page_and_type(&pg[i], d, PGT_writable_page)) )
            /*
             * The domain can't possibly know about this page yet, so failure
             * here is a clear indication of something fishy going on.
             */
            goto refcnt_err;

    /*
     * We must only let vmtrace_free_buffer() take any action in the success
     * case when we've taken all the refs it intends to drop.
     */
    v->vmtrace.pg = pg;
    return 0;

 refcnt_err:
    /*
     * We can theoretically reach this point if someone has taken 2^43 refs on
     * the frames in the time the above loop takes to execute, or someone has
     * made a blind decrease reservation hypercall and managed to pick the
     * right mfn.  Free the memory we safely can, and leak the rest.
     */
    while ( i-- )
    {
        put_page_alloc_ref(&pg[i]);
        put_page_and_type(&pg[i]);
    }

    return -ENODATA;
}

/*
 * Release resources held by a vcpu.  There may or may not be live references
 * to the vcpu, and it may or may not be fully constructed.
 *
 * If d->is_dying is DOMDYING_dead, this must not return non-zero.
 */
static int vcpu_teardown(struct vcpu *v)
{
    vmtrace_free_buffer(v);

    return 0;
}

/*
 * Destoy a vcpu once all references to it have been dropped.  Used either
 * from domain_destroy()'s RCU path, or from the vcpu_create() error path
 * before the vcpu is placed on the domain's vcpu list.
 */
static void vcpu_destroy(struct vcpu *v)
{
    free_vcpu_struct(v);
}

struct vcpu *vcpu_create(struct domain *d, unsigned int vcpu_id)
{
    struct vcpu *v;

    /*
     * Sanity check some input expectations:
     * - vcpu_id should be bounded by d->max_vcpus, and not previously
     *   allocated.
     * - VCPUs should be tightly packed and allocated in ascending order,
     *   except for the idle domain which may vary based on PCPU numbering.
     */
    if ( vcpu_id >= d->max_vcpus || d->vcpu[vcpu_id] ||
         (!is_idle_domain(d) && vcpu_id && !d->vcpu[vcpu_id - 1]) )
    {
        ASSERT_UNREACHABLE();
        return NULL;
    }

    if ( (v = alloc_vcpu_struct(d)) == NULL )
        return NULL;

    v->domain = d;
    v->vcpu_id = vcpu_id;
    v->dirty_cpu = VCPU_CPU_CLEAN;

    rwlock_init(&v->virq_lock);

    tasklet_init(&v->continue_hypercall_tasklet, NULL, NULL);

    grant_table_init_vcpu(v);

    if ( is_idle_domain(d) )
    {
        v->runstate.state = RUNSTATE_running;
        v->new_state = RUNSTATE_running;
    }
    else
    {
        v->runstate.state = RUNSTATE_offline;
        v->runstate.state_entry_time = NOW();
        set_bit(_VPF_down, &v->pause_flags);
        vcpu_info_reset(v);
        init_waitqueue_vcpu(v);
    }

    if ( sched_init_vcpu(v) != 0 )
        goto fail_wq;

    if ( vmtrace_alloc_buffer(v) != 0 )
        goto fail_wq;

    if ( arch_vcpu_create(v) != 0 )
        goto fail_sched;

    d->vcpu[vcpu_id] = v;
    if ( vcpu_id != 0 )
    {
        int prev_id = v->vcpu_id - 1;
        while ( (prev_id >= 0) && (d->vcpu[prev_id] == NULL) )
            prev_id--;
        BUG_ON(prev_id < 0);
        v->next_in_list = d->vcpu[prev_id]->next_in_list;
        d->vcpu[prev_id]->next_in_list = v;
    }

    /* Must be called after making new vcpu visible to for_each_vcpu(). */
    vcpu_check_shutdown(v);

    return v;

 fail_sched:
    sched_destroy_vcpu(v);
 fail_wq:
    destroy_waitqueue_vcpu(v);

    /* Must not hit a continuation in this context. */
    if ( vcpu_teardown(v) )
        ASSERT_UNREACHABLE();

    vcpu_destroy(v);

    return NULL;
}

static int late_hwdom_init(struct domain *d)
{
#ifdef CONFIG_LATE_HWDOM
    struct domain *dom0;
    int rv;

    if ( d != hardware_domain || d->domain_id == 0 )
        return 0;

    rv = xsm_init_hardware_domain(XSM_HOOK, d);
    if ( rv )
        return rv;

    printk("Initialising hardware domain %d\n", hardware_domid);

    dom0 = rcu_lock_domain_by_id(0);
    ASSERT(dom0 != NULL);
    /*
     * Hardware resource ranges for domain 0 have been set up from
     * various sources intended to restrict the hardware domain's
     * access.  Apply these ranges to the actual hardware domain.
     *
     * Because the lists are being swapped, a side effect of this
     * operation is that Domain 0's rangesets are cleared.  Since
     * domain 0 should not be accessing the hardware when it constructs
     * a hardware domain, this should not be a problem.  Both lists
     * may be modified after this hypercall returns if a more complex
     * device model is desired.
     */
    rangeset_swap(d->irq_caps, dom0->irq_caps);
    rangeset_swap(d->iomem_caps, dom0->iomem_caps);
#ifdef CONFIG_X86
    rangeset_swap(d->arch.ioport_caps, dom0->arch.ioport_caps);
    setup_io_bitmap(d);
    setup_io_bitmap(dom0);
#endif

    rcu_unlock_domain(dom0);

    iommu_hwdom_init(d);

    return rv;
#else
    return 0;
#endif
}

#ifdef CONFIG_HAS_PIRQ

static unsigned int __read_mostly extra_hwdom_irqs;
#define DEFAULT_EXTRA_DOMU_IRQS 32U
static unsigned int __read_mostly extra_domU_irqs = DEFAULT_EXTRA_DOMU_IRQS;

static int __init cf_check parse_extra_guest_irqs(const char *s)
{
    if ( isdigit(*s) )
        extra_domU_irqs = simple_strtoul(s, &s, 0);
    if ( *s == ',' && isdigit(*++s) )
        extra_hwdom_irqs = simple_strtoul(s, &s, 0);

    return *s ? -EINVAL : 0;
}
custom_param("extra_guest_irqs", parse_extra_guest_irqs);

#endif /* CONFIG_HAS_PIRQ */

static int __init cf_check parse_dom0_param(const char *s)
{
    const char *ss;
    int rc = 0;

    do {
        int ret;

        ss = strchr(s, ',');
        if ( !ss )
            ss = strchr(s, '\0');

        ret = parse_arch_dom0_param(s, ss);
        if ( ret && !rc )
            rc = ret;

        s = ss + 1;
    } while ( *ss );

    return rc;
}
custom_param("dom0", parse_dom0_param);

/*
 * Release resources held by a domain.  There may or may not be live
 * references to the domain, and it may or may not be fully constructed.
 *
 * d->is_dying differing between DOMDYING_dying and DOMDYING_dead can be used
 * to determine if live references to the domain exist, and also whether
 * continuations are permitted.
 *
 * If d->is_dying is DOMDYING_dead, this must not return non-zero.
 */
static int domain_teardown(struct domain *d)
{
    struct vcpu *v;
    int rc;

    BUG_ON(!d->is_dying);

    /*
     * This hypercall can take minutes of wallclock time to complete.  This
     * logic implements a co-routine, stashing state in struct domain across
     * hypercall continuation boundaries.
     */
    switch ( d->teardown.val )
    {
        /*
         * Record the current progress.  Subsequent hypercall continuations
         * will logically restart work from this point.
         *
         * PROGRESS() markers must not be in the middle of loops.  The loop
         * variable isn't preserved across a continuation.  PROGRESS_VCPU()
         * markers may be used in the middle of for_each_vcpu() loops, which
         * preserve v but no other loop variables.
         *
         * To avoid redundant work, there should be a marker before each
         * function which may return -ERESTART.
         */
#define PROGRESS(x)                             \
        d->teardown.val = PROG_ ## x;           \
        fallthrough;                            \
    case PROG_ ## x

#define PROGRESS_VCPU(x)                        \
        d->teardown.val = PROG_vcpu_ ## x;      \
        d->teardown.vcpu = v;                   \
        fallthrough;                            \
    case PROG_vcpu_ ## x:                       \
        v = d->teardown.vcpu

        enum {
            PROG_none,
            PROG_gnttab_mappings,
            PROG_vcpu_teardown,
            PROG_arch_teardown,
            PROG_done,
        };

    case PROG_none:
        BUILD_BUG_ON(PROG_none != 0);

    PROGRESS(gnttab_mappings):
        rc = gnttab_release_mappings(d);
        if ( rc )
            return rc;

        for_each_vcpu ( d, v )
        {
            /* SAF-5-safe MISRA C Rule 16.2: switch label enclosed by for loop */
            PROGRESS_VCPU(teardown);

            rc = vcpu_teardown(v);
            if ( rc )
                return rc;
        }

    PROGRESS(arch_teardown):
        rc = arch_domain_teardown(d);
        if ( rc )
            return rc;

    PROGRESS(done):
        break;

#undef PROGRESS_VCPU
#undef PROGRESS

    default:
        BUG();
    }

    return 0;
}

/*
 * Destroy a domain once all references to it have been dropped.  Used either
 * from the RCU path, or from the domain_create() error path before the domain
 * is inserted into the domlist.
 */
static void _domain_destroy(struct domain *d)
{
    BUG_ON(!d->is_dying);
    BUG_ON(atomic_read(&d->refcnt) != DOMAIN_DESTROYED);

    xfree(d->pbuf);

    argo_destroy(d);

    rangeset_domain_destroy(d);

    free_cpumask_var(d->dirty_cpumask);

    xsm_free_security_domain(d);

    lock_profile_deregister_struct(LOCKPROF_TYPE_PERDOM, d);

    free_domain_struct(d);
}

static int sanitise_domain_config(struct xen_domctl_createdomain *config)
{
    bool hvm = config->flags & XEN_DOMCTL_CDF_hvm;
    bool hap = config->flags & XEN_DOMCTL_CDF_hap;
    bool iommu = config->flags & XEN_DOMCTL_CDF_iommu;
    bool vpmu = config->flags & XEN_DOMCTL_CDF_vpmu;

    if ( config->flags &
         ~(XEN_DOMCTL_CDF_hvm | XEN_DOMCTL_CDF_hap |
           XEN_DOMCTL_CDF_s3_integrity | XEN_DOMCTL_CDF_oos_off |
           XEN_DOMCTL_CDF_xs_domain | XEN_DOMCTL_CDF_iommu |
           XEN_DOMCTL_CDF_nested_virt | XEN_DOMCTL_CDF_vpmu) )
    {
        dprintk(XENLOG_INFO, "Unknown CDF flags %#x\n", config->flags);
        return -EINVAL;
    }

    if ( config->grant_opts & ~XEN_DOMCTL_GRANT_version_mask )
    {
        dprintk(XENLOG_INFO, "Unknown grant options %#x\n", config->grant_opts);
        return -EINVAL;
    }

    if ( config->max_vcpus < 1 )
    {
        dprintk(XENLOG_INFO, "No vCPUS\n");
        return -EINVAL;
    }

    if ( hap && !hvm )
    {
        dprintk(XENLOG_INFO, "HAP requested for non-HVM guest\n");
        return -EINVAL;
    }

    if ( iommu )
    {
        if ( config->iommu_opts & ~XEN_DOMCTL_IOMMU_no_sharept )
        {
            dprintk(XENLOG_INFO, "Unknown IOMMU options %#x\n",
                    config->iommu_opts);
            return -EINVAL;
        }

        if ( !iommu_enabled )
        {
            dprintk(XENLOG_INFO, "IOMMU requested but not available\n");
            return -EINVAL;
        }
    }
    else
    {
        if ( config->iommu_opts )
        {
            dprintk(XENLOG_INFO,
                    "IOMMU options specified but IOMMU not requested\n");
            return -EINVAL;
        }
    }

    if ( config->vmtrace_size && !vmtrace_available )
    {
        dprintk(XENLOG_INFO, "vmtrace requested but not available\n");
        return -EINVAL;
    }

    if ( vpmu && !vpmu_is_available )
    {
        dprintk(XENLOG_INFO, "vpmu requested but cannot be enabled this way\n");
        return -EINVAL;
    }

    return arch_sanitise_domain_config(config);
}

static struct domain local_domain_prtos[4];
static unsigned int local_domain_prtos_index = 0;   
struct domain *domain_create(domid_t domid,
                             struct xen_domctl_createdomain *config,
                             unsigned int flags)
{
    struct domain *d, **pd, *old_hwdom = NULL;
    enum { INIT_watchdog = 1u<<1,
           INIT_evtchn = 1u<<3, INIT_gnttab = 1u<<4, INIT_arch = 1u<<5 };
    int err, init_status = 0;

    if ( config && (err = sanitise_domain_config(config)) )
        return ERR_PTR(err);

    // if ( (d = alloc_domain_struct()) == NULL )
    //     return ERR_PTR(-ENOMEM);
    d = &local_domain_prtos[local_domain_prtos_index++];
    if (local_domain_prtos_index > 4) {
        printk("######### local_domain_prtos_index overflow\n");
        return ERR_PTR(-ENOMEM);
    }
    memset(d, 0, sizeof(*d)); 
    


    /* Sort out our idea of is_system_domain(). */
    d->domain_id = domid;

    /* Holding CDF_* internal flags. */
    d->cdf = flags;

    /* Debug sanity. */
    ASSERT(is_system_domain(d) ? config == NULL : config != NULL);

    if ( config )
    {
        d->options = config->flags;
        d->vmtrace_size = config->vmtrace_size;
    }

    /* Sort out our idea of is_control_domain(). */
    d->is_privileged = flags & CDF_privileged;

    /* Sort out our idea of is_hardware_domain(). */
    if ( domid == 0 || domid == hardware_domid )
    {
        if ( hardware_domid < 0 || hardware_domid >= DOMID_FIRST_RESERVED )
            panic("The value of hardware_dom must be a valid domain ID\n");

        old_hwdom = hardware_domain;
        hardware_domain = d;
    }

    TRACE_TIME(TRC_DOM0_DOM_ADD, d->domain_id);

    lock_profile_register_struct(LOCKPROF_TYPE_PERDOM, d, domid);

    atomic_set(&d->refcnt, 1);
    RCU_READ_LOCK_INIT(&d->rcu_lock);
    rspin_lock_init_prof(d, domain_lock);
    rspin_lock_init_prof(d, page_alloc_lock);
    spin_lock_init(&d->hypercall_deadlock_mutex);
    INIT_PAGE_LIST_HEAD(&d->page_list);
    INIT_PAGE_LIST_HEAD(&d->extra_page_list);
    INIT_PAGE_LIST_HEAD(&d->xenpage_list);
#ifdef CONFIG_STATIC_MEMORY
    INIT_PAGE_LIST_HEAD(&d->resv_page_list);
#endif


    spin_lock_init(&d->node_affinity_lock);
    d->node_affinity = NODE_MASK_ALL;
    d->auto_node_affinity = 1;

    spin_lock_init(&d->shutdown_lock);
    d->shutdown_code = SHUTDOWN_CODE_INVALID;

    spin_lock_init(&d->pbuf_lock);

    rwlock_init(&d->vnuma_rwlock);

#ifdef CONFIG_HAS_PCI
    INIT_LIST_HEAD(&d->pdev_list);
    rwlock_init(&d->pci_lock);
#endif

    /* All error paths can depend on the above setup. */

    /*
     * Allocate d->vcpu[] and set ->max_vcpus up early.  Various per-domain
     * resources want to be sized based on max_vcpus.
     */
    if ( !is_system_domain(d) )
    {
        err = -ENOMEM;
        d->vcpu = xzalloc_array(struct vcpu *, config->max_vcpus);
        if ( !d->vcpu )
            goto fail;

        d->max_vcpus = config->max_vcpus;
    }

    // if ( (err = xsm_alloc_security_domain(d)) != 0 )
    //     goto fail;

    err = -ENOMEM;
    if ( !zalloc_cpumask_var(&d->dirty_cpumask) )
        goto fail;

    rangeset_domain_initialise(d);

    /* DOMID_{XEN,IO,etc} (other than IDLE) are sufficiently constructed. */
    if ( is_system_domain(d) && !is_idle_domain(d) )
        return d;

#ifdef CONFIG_HAS_PIRQ
    if ( !is_idle_domain(d) )
    {
        if ( !is_hardware_domain(d) )
            d->nr_pirqs = nr_static_irqs + extra_domU_irqs;
        else
            d->nr_pirqs = extra_hwdom_irqs ? nr_static_irqs + extra_hwdom_irqs
                                           : arch_hwdom_irqs(d);
        d->nr_pirqs = min(d->nr_pirqs, nr_irqs);

        radix_tree_init(&d->pirq_tree);
    }
#endif

    if ( (err = arch_domain_create(d, config, flags)) != 0 )
        goto fail;
    init_status |= INIT_arch;

    if ( !is_idle_domain(d) )
    {
        /*
         * The assertion helps static analysis tools infer that config cannot
         * be NULL in this branch, which in turn means that it can be safely
         * dereferenced. Therefore, this assertion is not redundant.
         */
        ASSERT(config);

        watchdog_domain_init(d);
        init_status |= INIT_watchdog;

        err = -ENOMEM;
        d->iomem_caps = rangeset_new(d, "I/O Memory", RANGESETF_prettyprint_hex);
        d->irq_caps   = rangeset_new(d, "Interrupts", 0);
        if ( !d->iomem_caps || !d->irq_caps )
            goto fail;

        if ( (err = xsm_domain_create(XSM_HOOK, d, config->ssidref)) != 0 )
            goto fail;

        d->controller_pause_count = 1;
        atomic_inc(&d->pause_count);

        if ( (err = evtchn_init(d, config->max_evtchn_port)) != 0 )
            goto fail;
        init_status |= INIT_evtchn;

        if ( (err = grant_table_init(d, config->max_grant_frames,
                                     config->max_maptrack_frames,
                                     config->grant_opts)) != 0 )
            goto fail;
        init_status |= INIT_gnttab;

        if ( (err = argo_init(d)) != 0 )
            goto fail;

        err = -ENOMEM;

        d->pbuf = xzalloc_array(char, DOMAIN_PBUF_SIZE);
        if ( !d->pbuf )
            goto fail;

        if ( (err = sched_init_domain(d, config->cpupool_id)) != 0 )
            goto fail;

        if ( (err = late_hwdom_init(d)) != 0 )
            goto fail;

        /*
         * Must not fail beyond this point, as our caller doesn't know whether
         * the domain has been entered into domain_list or not.
         */

        spin_lock(&domlist_update_lock);
        pd = &domain_list; /* NB. domain_list maintained in order of domid. */
        for ( pd = &domain_list; *pd != NULL; pd = &(*pd)->next_in_list )
            if ( (*pd)->domain_id > d->domain_id )
                break;
        d->next_in_list = *pd;
        d->next_in_hashbucket = domain_hash[DOMAIN_HASH(domid)];
        rcu_assign_pointer(*pd, d);
        rcu_assign_pointer(domain_hash[DOMAIN_HASH(domid)], d);
        spin_unlock(&domlist_update_lock);

        memcpy(d->handle, config->handle, sizeof(d->handle));
    }

    return d;

 fail:
    ASSERT(err < 0);      /* Sanity check paths leading here. */
    err = err ?: -EILSEQ; /* Release build safety. */

    d->is_dying = DOMDYING_dead;
    if ( hardware_domain == d )
        hardware_domain = old_hwdom;
    atomic_set(&d->refcnt, DOMAIN_DESTROYED);

    sched_destroy_domain(d);

    if ( d->max_vcpus )
    {
        d->max_vcpus = 0;
        XFREE(d->vcpu);
    }
    if ( init_status & INIT_arch )
        arch_domain_destroy(d);
    if ( init_status & INIT_gnttab )
        grant_table_destroy(d);
    if ( init_status & INIT_evtchn )
    {
        evtchn_destroy(d);
        evtchn_destroy_final(d);
#ifdef CONFIG_HAS_PIRQ
        radix_tree_destroy(&d->pirq_tree, free_pirq_struct);
#endif
    }
    if ( init_status & INIT_watchdog )
        watchdog_domain_destroy(d);

    /* Must not hit a continuation in this context. */
    if ( domain_teardown(d) )
        ASSERT_UNREACHABLE();

    _domain_destroy(d);

    return ERR_PTR(err);
}

void __init setup_system_domains(void)
{
    /*
     * Initialise our DOMID_XEN domain.
     * Any Xen-heap pages that we will allow to be mapped will have
     * their domain field set to dom_xen.
     * Hidden PCI devices will also be associated with this domain
     * (but be [partly] controlled by Dom0 nevertheless).
     */
    dom_xen = domain_create(DOMID_XEN, NULL, 0);
    if ( IS_ERR(dom_xen) )
        panic("Failed to create d[XEN]: %ld\n", PTR_ERR(dom_xen));

#ifdef CONFIG_HAS_PIRQ
    /* Bound-check values passed via "extra_guest_irqs=". */
    {
        unsigned int n = max(arch_hwdom_irqs(dom_xen), nr_static_irqs);

        if ( extra_hwdom_irqs > n - nr_static_irqs )
        {
            extra_hwdom_irqs = n - nr_static_irqs;
            printk(XENLOG_WARNING "hwdom IRQs bounded to %u\n", n);
        }
        if ( extra_domU_irqs >
             max(DEFAULT_EXTRA_DOMU_IRQS, n - nr_static_irqs) )
        {
            extra_domU_irqs = n - nr_static_irqs;
            printk(XENLOG_WARNING "domU IRQs bounded to %u\n", n);
        }
    }
#endif

    /*
     * Initialise our DOMID_IO domain.
     * This domain owns I/O pages that are within the range of the page_info
     * array. Mappings occur at the priv of the caller.
     * Quarantined PCI devices will be associated with this domain.
     *
     * DOMID_IO is also the default owner of memory pre-shared among multiple
     * domains at boot time.
     */
    dom_io = domain_create(DOMID_IO, NULL, 0);
    if ( IS_ERR(dom_io) )
        panic("Failed to create d[IO]: %ld\n", PTR_ERR(dom_io));

#ifdef CONFIG_MEM_SHARING
    /*
     * Initialise our COW domain.
     * This domain owns sharable pages.
     */
    dom_cow = domain_create(DOMID_COW, NULL, 0);
    if ( IS_ERR(dom_cow) )
        panic("Failed to create d[COW]: %ld\n", PTR_ERR(dom_cow));
#endif
}

int domain_set_node_affinity(struct domain *d, const nodemask_t *affinity)
{
    /* Being disjoint with the system is just wrong. */
    if ( !nodes_intersects(*affinity, node_online_map) )
        return -EINVAL;

    spin_lock(&d->node_affinity_lock);

    /*
     * Being/becoming explicitly affine to all nodes is not particularly
     * useful. Let's take it as the `reset node affinity` command.
     */
    if ( nodes_full(*affinity) )
    {
        d->auto_node_affinity = 1;
        goto out;
    }

    d->auto_node_affinity = 0;
    d->node_affinity = *affinity;

out:
    spin_unlock(&d->node_affinity_lock);

    domain_update_node_affinity(d);

    return 0;
}

/* rcu_read_lock(&domlist_read_lock) must be held. */
static struct domain *domid_to_domain(domid_t dom)
{
    struct domain *d;

    for ( d = rcu_dereference(domain_hash[DOMAIN_HASH(dom)]);
          d != NULL;
          d = rcu_dereference(d->next_in_hashbucket) )
    {
        if ( d->domain_id == dom )
            return d;
    }

    return NULL;
}

struct domain *get_domain_by_id(domid_t dom)
{
    struct domain *d;

    rcu_read_lock(&domlist_read_lock);

    d = domid_to_domain(dom);
    if ( d && unlikely(!get_domain(d)) )
        d = NULL;

    rcu_read_unlock(&domlist_read_lock);

    return d;
}


struct domain *rcu_lock_domain_by_id(domid_t dom)
{
    struct domain *d;

    rcu_read_lock(&domlist_read_lock);

    d = domid_to_domain(dom);
    if ( d )
        rcu_lock_domain(d);

    rcu_read_unlock(&domlist_read_lock);

    return d;
}

struct domain *knownalive_domain_from_domid(domid_t dom)
{
    struct domain *d;

    rcu_read_lock(&domlist_read_lock);

    d = domid_to_domain(dom);

    rcu_read_unlock(&domlist_read_lock);

    return d;
}

struct domain *rcu_lock_domain_by_any_id(domid_t dom)
{
    if ( dom == DOMID_SELF )
        return rcu_lock_current_domain();
    return rcu_lock_domain_by_id(dom);
}

int rcu_lock_remote_domain_by_id(domid_t dom, struct domain **d)
{
    if ( (*d = rcu_lock_domain_by_id(dom)) == NULL )
        return -ESRCH;

    if ( *d == current->domain )
    {
        rcu_unlock_domain(*d);
        return -EPERM;
    }

    return 0;
}


int domain_kill(struct domain *d)
{
    int rc = 0;
    struct vcpu *v;

    if ( d == current->domain )
        return -EINVAL;

    /* Protected by domctl_lock. */
    switch ( d->is_dying )
    {
    case DOMDYING_alive:
        domain_pause(d);
        d->is_dying = DOMDYING_dying;
        rspin_barrier(&d->domain_lock);
        argo_destroy(d);
        vnuma_destroy(d->vnuma);
        domain_set_outstanding_pages(d, 0);
        /* fallthrough */
    case DOMDYING_dying:
        rc = domain_teardown(d);
        if ( rc )
            break;
        rc = evtchn_destroy(d);
        if ( rc )
            break;
        rc = domain_relinquish_resources(d);
        if ( rc != 0 )
            break;
        if ( cpupool_move_domain(d, cpupool0) )
            return -ERESTART;
        for_each_vcpu ( d, v )
        {
            unmap_guest_area(v, &v->vcpu_info_area);
            unmap_guest_area(v, &v->runstate_guest_area);
        }
        d->is_dying = DOMDYING_dead;
        /* Mem event cleanup has to go here because the rings 
         * have to be put before we call put_domain. */
        vm_event_cleanup(d);
        put_domain(d);
        send_global_virq(VIRQ_DOM_EXC);
        /* fallthrough */
    case DOMDYING_dead:
        break;
    }

    return rc;
}


void __domain_crash(struct domain *d)
{
    if ( d->is_shutting_down )
    {
        /* Print nothing: the domain is already shutting down. */
    }
    else if ( d == current->domain )
    {
        printk("Domain %d (vcpu#%d) crashed on cpu#%d:\n",
               d->domain_id, current->vcpu_id, smp_processor_id());
        show_execution_state(guest_cpu_user_regs());
    }
    else
    {
        printk("Domain %d reported crashed by domain %d on cpu#%d:\n",
               d->domain_id, current->domain->domain_id, smp_processor_id());
    }

    domain_shutdown(d, SHUTDOWN_crash);
}


int domain_shutdown(struct domain *d, u8 reason)
{
    struct vcpu *v;

#ifdef CONFIG_X86
    if ( pv_shim )
        return pv_shim_shutdown(reason);
#endif

    spin_lock(&d->shutdown_lock);

    if ( d->shutdown_code == SHUTDOWN_CODE_INVALID )
        d->shutdown_code = reason;
    reason = d->shutdown_code;

    if ( is_hardware_domain(d) )
        hwdom_shutdown(reason);

    if ( d->is_shutting_down )
    {
        spin_unlock(&d->shutdown_lock);
        return 0;
    }

    d->is_shutting_down = 1;

    smp_mb(); /* set shutdown status /then/ check for per-cpu deferrals */

    for_each_vcpu ( d, v )
    {
        if ( reason == SHUTDOWN_crash )
            v->defer_shutdown = 0;
        else if ( v->defer_shutdown )
            continue;
        vcpu_pause_nosync(v);
        v->paused_for_shutdown = 1;
    }

    arch_domain_shutdown(d);

    __domain_finalise_shutdown(d);

    spin_unlock(&d->shutdown_lock);

    return 0;
}

void domain_resume(struct domain *d)
{
    struct vcpu *v;

    /*
     * Some code paths assume that shutdown status does not get reset under
     * their feet (e.g., some assertions make this assumption).
     */
    domain_pause(d);

    spin_lock(&d->shutdown_lock);

    d->is_shutting_down = d->is_shut_down = 0;
    d->shutdown_code = SHUTDOWN_CODE_INVALID;

    for_each_vcpu ( d, v )
    {
        if ( v->paused_for_shutdown )
            vcpu_unpause(v);
        v->paused_for_shutdown = 0;
    }

    spin_unlock(&d->shutdown_lock);

    domain_unpause(d);
}


void vcpu_end_shutdown_deferral(struct vcpu *v)
{
    v->defer_shutdown = 0;
    smp_mb(); /* clear deferral status /then/ check for shutdown */
    if ( unlikely(v->domain->is_shutting_down) )
        vcpu_check_shutdown(v);
}

/* Complete domain destroy after RCU readers are not holding old references. */
static void cf_check complete_domain_destroy(struct rcu_head *head)
{
    struct domain *d = container_of(head, struct domain, rcu);
    struct vcpu *v;
    int i;

    /*
     * Flush all state for the vCPU previously having run on the current CPU.
     * This is in particular relevant for x86 HVM ones on VMX, so that this
     * flushing of state won't happen from the TLB flush IPI handler behind
     * the back of a vmx_vmcs_enter() / vmx_vmcs_exit() section.
     */
    sync_local_execstate();

    for ( i = d->max_vcpus - 1; i >= 0; i-- )
    {
        if ( (v = d->vcpu[i]) == NULL )
            continue;
        tasklet_kill(&v->continue_hypercall_tasklet);
        arch_vcpu_destroy(v);
        sched_destroy_vcpu(v);
        destroy_waitqueue_vcpu(v);
    }

    grant_table_destroy(d);

    arch_domain_destroy(d);

    watchdog_domain_destroy(d);

    sched_destroy_domain(d);

    /* Free page used by xen oprofile buffer. */
#ifdef CONFIG_XENOPROF
    free_xenoprof_pages(d);
#endif

#ifdef CONFIG_MEM_PAGING
    xfree(d->vm_event_paging);
#endif
    xfree(d->vm_event_monitor);
#ifdef CONFIG_MEM_SHARING
    xfree(d->vm_event_share);
#endif

    for ( i = d->max_vcpus - 1; i >= 0; i-- )
        if ( (v = d->vcpu[i]) != NULL )
            vcpu_destroy(v);

    if ( d->target != NULL )
        put_domain(d->target);

    evtchn_destroy_final(d);

#ifdef CONFIG_HAS_PIRQ
    radix_tree_destroy(&d->pirq_tree, free_pirq_struct);
#endif

    xfree(d->vcpu);

    _domain_destroy(d);

    send_global_virq(VIRQ_DOM_EXC);
}

/* Release resources belonging to task @p. */
void domain_destroy(struct domain *d)
{
    struct domain **pd;

    BUG_ON(!d->is_dying);

    /* May be already destroyed, or get_domain() can race us. */
    if ( atomic_cmpxchg(&d->refcnt, 0, DOMAIN_DESTROYED) != 0 )
        return;

    TRACE_TIME(TRC_DOM0_DOM_REM, d->domain_id);

    /* Delete from task list and task hashtable. */
    spin_lock(&domlist_update_lock);
    pd = &domain_list;
    while ( *pd != d ) 
        pd = &(*pd)->next_in_list;
    rcu_assign_pointer(*pd, d->next_in_list);
    pd = &domain_hash[DOMAIN_HASH(d->domain_id)];
    while ( *pd != d ) 
        pd = &(*pd)->next_in_hashbucket;
    rcu_assign_pointer(*pd, d->next_in_hashbucket);
    spin_unlock(&domlist_update_lock);

    /* Schedule RCU asynchronous completion of domain destroy. */
    call_rcu(&d->rcu, complete_domain_destroy);
}

void vcpu_pause(struct vcpu *v)
{
    ASSERT(v != current);
    atomic_inc(&v->pause_count);
    vcpu_sleep_sync(v);
}

void vcpu_pause_nosync(struct vcpu *v)
{
    atomic_inc(&v->pause_count);
    vcpu_sleep_nosync(v);
}

void vcpu_unpause(struct vcpu *v)
{
    if ( atomic_dec_and_test(&v->pause_count) )
        vcpu_wake(v);
}



static void _domain_pause(struct domain *d, bool sync)
{
    struct vcpu *v;

    atomic_inc(&d->pause_count);

    if ( sync )
        for_each_vcpu ( d, v )
            vcpu_sleep_sync(v);
    else
        for_each_vcpu ( d, v )
            vcpu_sleep_nosync(v);

    arch_domain_pause(d);
}

void domain_pause(struct domain *d)
{
    ASSERT(d != current->domain);
    _domain_pause(d, true /* sync */);
}

void domain_pause_nosync(struct domain *d)
{
    _domain_pause(d, false /* nosync */);
}

void domain_unpause(struct domain *d)
{
    struct vcpu *v;

    arch_domain_unpause(d);

    if ( atomic_dec_and_test(&d->pause_count) )
        for_each_vcpu( d, v )
            vcpu_wake(v);
}

static int _domain_pause_by_systemcontroller(struct domain *d, bool sync)
{
    int old, new, prev = d->controller_pause_count;

    do
    {
        old = prev;
        new = old + 1;

        /*
         * Limit the toolstack pause count to an arbitrary 255 to prevent the
         * toolstack overflowing d->pause_count with many repeated hypercalls.
         */
        if ( new > 255 )
            return -EOVERFLOW;

        prev = cmpxchg(&d->controller_pause_count, old, new);
    } while ( prev != old );

    _domain_pause(d, sync);

    return 0;
}

int domain_pause_by_systemcontroller(struct domain *d)
{
    return _domain_pause_by_systemcontroller(d, true /* sync */);
}


int domain_unpause_by_systemcontroller(struct domain *d)
{
    int old, new, prev = d->controller_pause_count;

    do
    {
        old = prev;
        new = old - 1;

        if ( new < 0 )
            return -EINVAL;

        prev = cmpxchg(&d->controller_pause_count, old, new);
    } while ( prev != old );

    /*
     * d->controller_pause_count is initialised to 1, and the toolstack is
     * responsible for making one unpause hypercall when it wishes the guest
     * to start running.
     *
     * All other toolstack operations should make a pair of pause/unpause
     * calls and rely on the reference counting here.
     *
     * Creation is considered finished when the controller reference count
     * first drops to 0.
     */
    if ( new == 0 && !d->creation_finished )
    {
        d->creation_finished = true;
        arch_domain_creation_finished(d);
    }

    domain_unpause(d);

    return 0;
}



int domain_soft_reset(struct domain *d, bool resuming)
{
    struct vcpu *v;
    int rc;

    spin_lock(&d->shutdown_lock);
    for_each_vcpu ( d, v )
        if ( !v->paused_for_shutdown )
        {
            spin_unlock(&d->shutdown_lock);
            return -EINVAL;
        }
    spin_unlock(&d->shutdown_lock);

    rc = evtchn_reset(d, resuming);
    if ( rc )
        return rc;

    grant_table_warn_active_grants(d);

    argo_soft_reset(d);

    for_each_vcpu ( d, v )
    {
        set_xen_guest_handle(runstate_guest(v), NULL);
        unmap_guest_area(v, &v->vcpu_info_area);
        unmap_guest_area(v, &v->runstate_guest_area);
    }

    rc = arch_domain_soft_reset(d);
    if ( !rc )
        domain_resume(d);
    else
        domain_crash(d);

    return rc;
}

int vcpu_reset(struct vcpu *v)
{
    struct domain *d = v->domain;
    int rc;

    vcpu_pause(v);
    domain_lock(d);

    set_bit(_VPF_in_reset, &v->pause_flags);
    rc = arch_vcpu_reset(v);
    if ( rc )
        goto out_unlock;

    set_bit(_VPF_down, &v->pause_flags);

    clear_bit(v->vcpu_id, d->poll_mask);
    v->poll_evtchn = 0;

    v->fpu_initialised = 0;
    v->fpu_dirtied     = 0;
    v->is_initialised  = 0;
    if ( v->affinity_broken & VCPU_AFFINITY_OVERRIDE )
        vcpu_temporary_affinity(v, 4, VCPU_AFFINITY_OVERRIDE);
    if ( v->affinity_broken & VCPU_AFFINITY_WAIT )
        vcpu_temporary_affinity(v, 4, VCPU_AFFINITY_WAIT);
    clear_bit(_VPF_blocked, &v->pause_flags);
    clear_bit(_VPF_in_reset, &v->pause_flags);

 out_unlock:
    domain_unlock(v->domain);
    vcpu_unpause(v);

    return rc;
}

int map_guest_area(struct vcpu *v, paddr_t gaddr, unsigned int size,
                   struct guest_area *area,
                   void (*populate)(void *dst, struct vcpu *v))
{
    struct domain *d = v->domain;
    void *map = NULL;
    struct page_info *pg = NULL;
    int rc = 0;

    if ( ~gaddr ) /* Map (i.e. not just unmap)? */
    {
        unsigned long gfn = PFN_DOWN(gaddr);
        unsigned int align;
        p2m_type_t p2mt;

        if ( gfn != PFN_DOWN(gaddr + size - 1) )
            return -ENXIO;

#ifdef CONFIG_COMPAT
        if ( has_32bit_shinfo(d) )
            align = alignof(compat_ulong_t);
        else
#endif
            align = alignof(xen_ulong_t);
        if ( !IS_ALIGNED(gaddr, align) )
            return -ENXIO;

        rc = check_get_page_from_gfn(d, _gfn(gfn), false, &p2mt, &pg);
        if ( rc )
            return rc;

        if ( !get_page_type(pg, PGT_writable_page) )
        {
            put_page(pg);
            return -EACCES;
        }

        map = __map_domain_page_global(pg);
        if ( !map )
        {
            put_page_and_type(pg);
            return -ENOMEM;
        }
        map += PAGE_OFFSET(gaddr);
    }

    if ( v != current )
    {
        if ( !spin_trylock(&d->hypercall_deadlock_mutex) )
        {
            rc = -ERESTART;
            goto unmap;
        }

        vcpu_pause(v);

        spin_unlock(&d->hypercall_deadlock_mutex);
    }

    domain_lock(d);

    /* No re-registration of the vCPU info area. */
    if ( area != &v->vcpu_info_area || !area->pg )
    {
        if ( map && populate )
            populate(map, v);

        SWAP(area->pg, pg);
        SWAP(area->map, map);
    }
    else
        rc = -EBUSY;

    domain_unlock(d);

    /* Set pending flags /after/ new vcpu_info pointer was set. */
    if ( area == &v->vcpu_info_area && !rc )
    {
        /*
         * Mark everything as being pending just to make sure nothing gets
         * lost.  The domain will get a spurious event, but it can cope.
         */
#ifdef CONFIG_COMPAT
        if ( !has_32bit_shinfo(d) )
        {
            vcpu_info_t *info = area->map;

            /* For VCPUOP_register_vcpu_info handling in common_vcpu_op(). */
            BUILD_BUG_ON(sizeof(*info) != sizeof(info->compat));
            write_atomic(&info->native.evtchn_pending_sel, ~0);
        }
        else
#endif
            write_atomic(&vcpu_info(v, evtchn_pending_sel), ~0);
        vcpu_mark_events_pending(v);

        force_update_vcpu_system_time(v);
    }

    if ( v != current )
        vcpu_unpause(v);

 unmap:
    if ( pg )
    {
        unmap_domain_page_global((void *)((unsigned long)map & PAGE_MASK));
        put_page_and_type(pg);
    }

    return rc;
}

/*
 * This is only intended to be used for domain cleanup (or more generally only
 * with at least the respective vCPU, if it's not the current one, reliably
 * paused).
 */
void unmap_guest_area(struct vcpu *v, struct guest_area *area)
{
    struct domain *d = v->domain;
    void *map;
    struct page_info *pg;

    if ( v != current )
        ASSERT(atomic_read(&v->pause_count) | atomic_read(&d->pause_count));

    domain_lock(d);
    map = area->map;
    if ( area == &v->vcpu_info_area )
        vcpu_info_reset(v);
    else
        area->map = NULL;
    pg = area->pg;
    area->pg = NULL;
    domain_unlock(d);

    if ( pg )
    {
        unmap_domain_page_global((void *)((unsigned long)map & PAGE_MASK));
        put_page_and_type(pg);
    }
}


/* Update per-VCPU guest runstate shared memory area (if registered). */
bool update_runstate_area(struct vcpu *v)
{
    bool rc;
    struct guest_memory_policy policy = { };
    void __user *guest_handle = NULL;
    struct vcpu_runstate_info runstate = v->runstate;
    struct vcpu_runstate_info *map = v->runstate_guest_area.map;

    if ( map )
    {
        uint64_t *pset;
#ifdef CONFIG_COMPAT
        struct compat_vcpu_runstate_info *cmap = NULL;

        if ( v->runstate_guest_area_compat )
            cmap = (void *)map;
#endif

        /*
         * NB: No VM_ASSIST(v->domain, runstate_update_flag) check here.
         *     Always using that updating model.
         */
#ifdef CONFIG_COMPAT
        if ( cmap )
            pset = &cmap->state_entry_time;
        else
#endif
            pset = &map->state_entry_time;
        runstate.state_entry_time |= XEN_RUNSTATE_UPDATE;
        write_atomic(pset, runstate.state_entry_time);
        smp_wmb();

#ifdef CONFIG_COMPAT
        if ( cmap )
            XLAT_vcpu_runstate_info(cmap, &runstate);
        else
#endif
            *map = runstate;

        smp_wmb();
        runstate.state_entry_time &= ~XEN_RUNSTATE_UPDATE;
        write_atomic(pset, runstate.state_entry_time);

        return true;
    }

    if ( guest_handle_is_null(runstate_guest(v)) )
        return true;

    update_guest_memory_policy(v, &policy);

    if ( VM_ASSIST(v->domain, runstate_update_flag) )
    {
#ifdef CONFIG_COMPAT
        guest_handle = has_32bit_shinfo(v->domain)
            ? &v->runstate_guest.compat.p->state_entry_time + 1
            : &v->runstate_guest.native.p->state_entry_time + 1;
#else
        guest_handle = &v->runstate_guest.p->state_entry_time + 1;
#endif
        guest_handle--;
        runstate.state_entry_time |= XEN_RUNSTATE_UPDATE;
        __raw_copy_to_guest(guest_handle,
                            (void *)(&runstate.state_entry_time + 1) - 1, 1);
        smp_wmb();
    }

#ifdef CONFIG_COMPAT
    if ( has_32bit_shinfo(v->domain) )
    {
        struct compat_vcpu_runstate_info info;

        XLAT_vcpu_runstate_info(&info, &runstate);
        __copy_to_guest(v->runstate_guest.compat, &info, 1);
        rc = true;
    }
    else
#endif
        rc = __copy_to_guest(runstate_guest(v), &runstate, 1) !=
             sizeof(runstate);

    if ( guest_handle )
    {
        runstate.state_entry_time &= ~XEN_RUNSTATE_UPDATE;
        smp_wmb();
        __raw_copy_to_guest(guest_handle,
                            (void *)(&runstate.state_entry_time + 1) - 1, 1);
    }

    update_guest_memory_policy(v, &policy);

    return rc;
}

/*
 * This makes sure that the vcpu_info is always pointing at a valid piece of
 * memory, and it sets a pending event to make sure that a pending event
 * doesn't get missed.
 */
static void cf_check
vcpu_info_populate(void *map, struct vcpu *v)
{
    vcpu_info_t *info = map;

    if ( v->vcpu_info_area.map == &dummy_vcpu_info )
    {
        memset(info, 0, sizeof(*info));
#ifdef XEN_HAVE_PV_UPCALL_MASK
        __vcpu_info(v, info, evtchn_upcall_mask) = 1;
#endif
    }
    else
        memcpy(info, v->vcpu_info_area.map, sizeof(*info));
}

static void cf_check
runstate_area_populate(void *map, struct vcpu *v)
{
#ifdef CONFIG_PV
    if ( is_pv_vcpu(v) )
        v->arch.pv.need_update_runstate_area = false;
#endif

#ifdef CONFIG_COMPAT
    v->runstate_guest_area_compat = false;
#endif

    if ( v == current )
    {
        struct vcpu_runstate_info *info = map;

        *info = v->runstate;
    }
}

long common_vcpu_op(int cmd, struct vcpu *v, XEN_GUEST_HANDLE_PARAM(void) arg)
{
    long rc = 0;
    struct domain *d = v->domain;
    unsigned int vcpuid = v->vcpu_id;

    switch ( cmd )
    {
    case VCPUOP_initialise:
        if ( is_pv_domain(d) && v->vcpu_info_area.map == &dummy_vcpu_info )
            return -EINVAL;

        rc = arch_initialise_vcpu(v, arg);
        if ( rc == -ERESTART )
            rc = hypercall_create_continuation(__HYPERVISOR_vcpu_op, "iih",
                                               cmd, vcpuid, arg);

        break;

    case VCPUOP_up:
#ifdef CONFIG_X86
        if ( pv_shim )
            rc = continue_hypercall_on_cpu(0, pv_shim_cpu_up, v);
        else
#endif
        {
            bool wake = false;

            domain_lock(d);
            if ( !v->is_initialised )
                rc = -EINVAL;
            else
                wake = test_and_clear_bit(_VPF_down, &v->pause_flags);
            domain_unlock(d);
            if ( wake )
                vcpu_wake(v);
        }

        break;

    case VCPUOP_down:
        for_each_vcpu ( d, v )
            if ( v->vcpu_id != vcpuid && !test_bit(_VPF_down, &v->pause_flags) )
            {
               rc = 1;
               break;
            }

        if ( !rc ) /* Last vcpu going down? */
        {
            domain_shutdown(d, SHUTDOWN_poweroff);
            break;
        }

        rc = 0;
        v = d->vcpu[vcpuid];

#ifdef CONFIG_X86
        if ( pv_shim )
            rc = continue_hypercall_on_cpu(0, pv_shim_cpu_down, v);
        else
#endif
            if ( !test_and_set_bit(_VPF_down, &v->pause_flags) )
                vcpu_sleep_nosync(v);

        break;

    case VCPUOP_is_up:
        rc = !(v->pause_flags & VPF_down);
        break;

    case VCPUOP_get_runstate_info:
    {
        struct vcpu_runstate_info runstate;
        vcpu_runstate_get(v, &runstate);
        if ( copy_to_guest(arg, &runstate, 1) )
            rc = -EFAULT;
        break;
    }

    case VCPUOP_set_periodic_timer:
    {
        struct vcpu_set_periodic_timer set;

        if ( copy_from_guest(&set, arg, 1) )
            return -EFAULT;

        if ( set.period_ns < MILLISECS(1) )
            return -EINVAL;

        if ( set.period_ns > STIME_DELTA_MAX )
            return -EINVAL;

        vcpu_set_periodic_timer(v, set.period_ns);

        break;
    }

    case VCPUOP_stop_periodic_timer:
        vcpu_set_periodic_timer(v, 0);
        break;

    case VCPUOP_set_singleshot_timer:
    {
        struct vcpu_set_singleshot_timer set;

        if ( v != current )
            return -EINVAL;

        if ( copy_from_guest(&set, arg, 1) )
            return -EFAULT;

        if ( set.timeout_abs_ns < NOW() )
        {
            /*
             * Simplify the logic if the timeout has already expired and just
             * inject the event.
             */
            stop_timer(&v->singleshot_timer);
            send_timer_event(v);
            break;
        }

        migrate_timer(&v->singleshot_timer, smp_processor_id());
        set_timer(&v->singleshot_timer, set.timeout_abs_ns);

        break;
    }

    case VCPUOP_stop_singleshot_timer:
        if ( v != current )
            return -EINVAL;

        stop_timer(&v->singleshot_timer);

        break;

    case VCPUOP_register_vcpu_info:
    {
        struct vcpu_register_vcpu_info info;
        paddr_t gaddr;

        rc = -EFAULT;
        if ( copy_from_guest(&info, arg, 1) )
            break;

        rc = -EINVAL;
        gaddr = gfn_to_gaddr(_gfn(info.mfn)) + info.offset;
        if ( !~gaddr ||
             gfn_x(gaddr_to_gfn(gaddr)) != info.mfn )
            break;

        /* Preliminary check only; see map_guest_area(). */
        rc = -EBUSY;
        if ( v->vcpu_info_area.pg )
            break;

        /* See the BUILD_BUG_ON() in vcpu_info_populate(). */
        rc = map_guest_area(v, gaddr, sizeof(vcpu_info_t),
                            &v->vcpu_info_area, vcpu_info_populate);
        if ( rc == -ERESTART )
            rc = hypercall_create_continuation(__HYPERVISOR_vcpu_op, "iih",
                                               cmd, vcpuid, arg);

        break;
    }

    case VCPUOP_register_runstate_memory_area:
    {
        struct vcpu_register_runstate_memory_area area;
        struct vcpu_runstate_info runstate;

        rc = -EFAULT;
        if ( copy_from_guest(&area, arg, 1) )
            break;

        if ( !guest_handle_okay(area.addr.h, 1) )
            break;

        rc = 0;
        runstate_guest(v) = area.addr.h;

        if ( v == current )
        {
            __copy_to_guest(runstate_guest(v), &v->runstate, 1);
        }
        else
        {
            vcpu_runstate_get(v, &runstate);
            __copy_to_guest(runstate_guest(v), &runstate, 1);
        }

        break;
    }

    case VCPUOP_register_runstate_phys_area:
    {
        struct vcpu_register_runstate_memory_area area;

        rc = -ENOSYS;
        if ( 0 /* TODO: Dom's XENFEAT_runstate_phys_area setting */ )
            break;

        rc = -EFAULT;
        if ( copy_from_guest(&area.addr.p, arg, 1) )
            break;

        rc = map_guest_area(v, area.addr.p,
                            sizeof(struct vcpu_runstate_info),
                            &v->runstate_guest_area,
                            runstate_area_populate);
        if ( rc == -ERESTART )
            rc = hypercall_create_continuation(__HYPERVISOR_vcpu_op, "iih",
                                               cmd, vcpuid, arg);

        break;
    }

    default:
        rc = -ENOSYS;
        break;
    }

    return rc;
}

#ifdef arch_vm_assist_valid_mask
long do_vm_assist(unsigned int cmd, unsigned int type)
{
    struct domain *currd = current->domain;
    const unsigned long valid = arch_vm_assist_valid_mask(currd);

    if ( type >= BITS_PER_LONG || !test_bit(type, &valid) )
        return -EINVAL;

    switch ( cmd )
    {
    case VMASST_CMD_enable:
        set_bit(type, &currd->vm_assist);
        return 0;

    case VMASST_CMD_disable:
        clear_bit(type, &currd->vm_assist);
        return 0;
    }

    return -ENOSYS;
}
#endif

#ifdef CONFIG_HAS_PIRQ

struct pirq *pirq_get_info(struct domain *d, int pirq)
{
    struct pirq *info = pirq_info(d, pirq);

    if ( !info && (info = alloc_pirq_struct(d)) != NULL )
    {
        info->pirq = pirq;
        if ( radix_tree_insert(&d->pirq_tree, pirq, info) )
        {
            free_pirq_struct(info);
            info = NULL;
        }
    }

    return info;
}

static void cf_check _free_pirq_struct(struct rcu_head *head)
{
    xfree(container_of(head, struct pirq, rcu_head));
}

void cf_check free_pirq_struct(void *ptr)
{
    struct pirq *pirq = ptr;

    call_rcu(&pirq->rcu_head, _free_pirq_struct);
}

#endif /* CONFIG_HAS_PIRQ */

struct migrate_info {
    long (*func)(void *data);
    void *data;
    struct vcpu *vcpu;
    unsigned int cpu;
    unsigned int nest;
};

static DEFINE_PER_CPU(struct migrate_info *, continue_info);

static void cf_check continue_hypercall_tasklet_handler(void *data)
{
    struct migrate_info *info = data;
    struct vcpu *v = info->vcpu;
    long res = -EINVAL;

    /* Wait for vcpu to sleep so that we can access its register state. */
    vcpu_sleep_sync(v);

    this_cpu(continue_info) = info;

    if ( likely(info->cpu == smp_processor_id()) )
        res = info->func(info->data);

    arch_hypercall_tasklet_result(v, res);

    this_cpu(continue_info) = NULL;

    if ( info->nest-- == 0 )
    {
        xfree(info);
        vcpu_unpause(v);
        put_domain(v->domain);
    }
}

int continue_hypercall_on_cpu(
    unsigned int cpu, long (*func)(void *data), void *data)
{
    struct migrate_info *info;

    if ( (cpu >= nr_cpu_ids) || !cpu_online(cpu) )
        return -EINVAL;

    info = this_cpu(continue_info);
    if ( info == NULL )
    {
        struct vcpu *curr = current;

        info = xmalloc(struct migrate_info);
        if ( info == NULL )
            return -ENOMEM;

        info->vcpu = curr;
        info->nest = 0;

        tasklet_kill(&curr->continue_hypercall_tasklet);
        tasklet_init(&curr->continue_hypercall_tasklet,
                     continue_hypercall_tasklet_handler, info);

        get_knownalive_domain(curr->domain);
        vcpu_pause_nosync(curr);
    }
    else
    {
        BUG_ON(info->nest != 0);
        info->nest++;
    }

    info->func = func;
    info->data = data;
    info->cpu  = cpu;

    tasklet_schedule_on_cpu(&info->vcpu->continue_hypercall_tasklet, cpu);

    /* Dummy return value will be overwritten by tasklet. */
    return 0;
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

/* === END INLINED: common_domain.c === */
/* === BEGIN INLINED: domain.c === */
#include <xen_xen_config.h>
#include <xen_sched.h>

#include <asm_domain.h>
#include <asm_processor.h>

#include <public_xen.h>

/* C(hyp,user), hyp is Xen internal name, user is user API name. */

#define ALLREGS \
    C(x0,x0);   C(x1,x1);   C(x2,x2);   C(x3,x3);   \
    C(x4,x4);   C(x5,x5);   C(x6,x6);   C(x7,x7);   \
    C(x8,x8);   C(x9,x9);   C(x10,x10); C(x11,x11); \
    C(x12,x12); C(x13,x13); C(x14,x14); C(x15,x15); \
    C(x16,x16); C(x17,x17); C(x18,x18); C(x19,x19); \
    C(x20,x20); C(x21,x21); C(x22,x22); C(x23,x23); \
    C(x24,x24); C(x25,x25); C(x26,x26); C(x27,x27); \
    C(x28,x28); C(fp,x29);  C(lr,x30);  C(pc,pc64); \
    C(cpsr, cpsr); C(spsr_el1, spsr_el1)

#define ALLREGS32 C(spsr_fiq, spsr_fiq); C(spsr_irq,spsr_irq); \
                  C(spsr_und,spsr_und); C(spsr_abt,spsr_abt)

#define ALLREGS64 C(sp_el0,sp_el0); C(sp_el1,sp_el1); C(elr_el1,elr_el1)

void vcpu_regs_hyp_to_user(const struct vcpu *vcpu,
                           struct vcpu_guest_core_regs *regs)
{
#define C(hyp,user) regs->user = vcpu->arch.cpu_info->guest_cpu_user_regs.hyp
    ALLREGS;
    if ( is_32bit_domain(vcpu->domain) )
    {
        ALLREGS32;
    }
    else
    {
        ALLREGS64;
    }
#undef C
}

void vcpu_regs_user_to_hyp(struct vcpu *vcpu,
                           const struct vcpu_guest_core_regs *regs)
{
#define C(hyp,user) vcpu->arch.cpu_info->guest_cpu_user_regs.hyp = regs->user
    ALLREGS;
    if ( is_32bit_domain(vcpu->domain) )
    {
        ALLREGS32;
    }
    else
    {
        ALLREGS64;
    }
#undef C
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: domain.c === */
/* === BEGIN INLINED: domain_build.c === */
#include <xen_xen_config.h>
/* SPDX-License-Identifier: GPL-2.0 */
#include <xen_init.h>
#include <xen_compile.h>
#include <xen_lib.h>
#include <xen_mm.h>
#include <xen_param.h>
#include <xen_domain_page.h>
#include <xen_sched.h>
#include <xen_sizes.h>
#include <asm_irq.h>
#include <asm_regs.h>
#include <xen_errno.h>
#include <xen_err.h>
#include <xen_device_tree.h>
#include <xen_libfdt_libfdt.h>
#include <xen_guest_access.h>
#include <xen_iocap.h>
#include <xen_acpi.h>
#include <xen_vmap.h>
#include <xen_warning.h>
#include <asm_generic_device.h>
#include <asm_kernel.h>
#include <asm_setup.h>
#include <asm_tee_tee.h>
#include <asm_pci.h>
#include <asm_platform.h>
#include <asm_psci.h>
#include <asm_setup.h>
#include <asm_arm64_sve.h>
#include <asm_cpufeature.h>
#include <asm_dom0less-build.h>
#include <asm_domain_build.h>
#include <asm_static-shmem.h>
#include <xen_event.h>

#include <xen_irq.h>
#include <xen_grant_table.h>
#include <asm_grant_table.h>
#include <xen_serial.h>

static unsigned int __initdata opt_dom0_max_vcpus;
integer_param("dom0_max_vcpus", opt_dom0_max_vcpus);

/*
 * If true, the extended regions support is enabled for dom0 and
 * dom0less domUs.
 */
static bool __initdata opt_ext_regions = true;
boolean_param("ext_regions", opt_ext_regions);

static u64 __initdata dom0_mem;
static bool __initdata dom0_mem_set;

static int __init parse_dom0_mem(const char *s)
{
    dom0_mem_set = true;

    dom0_mem = parse_size_and_unit(s, &s);

    return *s ? -EINVAL : 0;
}
custom_param("dom0_mem", parse_dom0_mem);

int __init parse_arch_dom0_param(const char *s, const char *e)
{
    long long val;

    if ( !parse_signed_integer("sve", s, e, &val) )
    {
#ifdef CONFIG_ARM64_SVE
        if ( (val >= INT_MIN) && (val <= INT_MAX) )
            opt_dom0_sve = val;
        else
            printk(XENLOG_INFO "'sve=%lld' value out of range!\n", val);

        return 0;
#else
        panic("'sve' property found, but CONFIG_ARM64_SVE not selected\n");
#endif
    }

    return -EINVAL;
}

/* Override macros from asm/page.h to make them work with mfn_t */
#undef virt_to_mfn
#define virt_to_mfn(va) _mfn(__virt_to_mfn(va))

//#define DEBUG_11_ALLOCATION
#ifdef DEBUG_11_ALLOCATION
# define D11PRINT(fmt, args...) printk(XENLOG_DEBUG fmt, ##args)
#else
# define D11PRINT(fmt, args...) do {} while ( 0 )
#endif

/*
 * Amount of extra space required to dom0's device tree.  No new nodes
 * are added (yet) but one terminating reserve map entry (16 bytes) is
 * added.
 */
#define DOM0_FDT_EXTRA_SIZE (128 + sizeof(struct fdt_reserve_entry))

unsigned int __init dom0_max_vcpus(void)
{
    if ( opt_dom0_max_vcpus == 0 )
    {
        ASSERT(cpupool0);
        opt_dom0_max_vcpus = cpumask_weight(cpupool_valid_cpus(cpupool0));
    }
    if ( opt_dom0_max_vcpus > MAX_VIRT_CPUS )
        opt_dom0_max_vcpus = MAX_VIRT_CPUS;

    return opt_dom0_max_vcpus;
}

struct vcpu *__init alloc_dom0_vcpu0(struct domain *dom0)
{
    return vcpu_create(dom0, 0);
}

unsigned int __init get_allocation_size(paddr_t size)
{
    /*
     * get_order_from_bytes returns the order greater than or equal to
     * the given size, but we need less than or equal. Adding one to
     * the size pushes an evenly aligned size into the next order, so
     * we can then unconditionally subtract 1 from the order which is
     * returned.
     */
    return get_order_from_bytes(size + 1) - 1;
}

/*
 * Insert the given pages into a memory bank, banks are ordered by address.
 *
 * Returns false if the memory would be below bank 0 or we have run
 * out of banks. In this case it will free the pages.
 */
static bool __init insert_11_bank(struct domain *d,
                                  struct kernel_info *kinfo,
                                  struct page_info *pg,
                                  unsigned int order)
{
    struct membanks *mem = kernel_info_get_mem(kinfo);
    unsigned int i;
    int res;
    mfn_t smfn;
    paddr_t start, size;

    smfn = page_to_mfn(pg);
    start = mfn_to_maddr(smfn);
    size = pfn_to_paddr(1UL << order);

    D11PRINT("Allocated %#"PRIpaddr"-%#"PRIpaddr" (%ldMB/%ldMB, order %d)\n",
             start, start + size,
             1UL << (order + PAGE_SHIFT - 20),
             /* Don't want format this as PRIpaddr (16 digit hex) */
             (unsigned long)(kinfo->unassigned_mem >> 20),
             order);

    if ( mem->nr_banks > 0 &&
         size < MB(128) &&
         start + size < mem->bank[0].start )
    {
        D11PRINT("Allocation below bank 0 is too small, not using\n");
        goto fail;
    }

    res = guest_physmap_add_page(d, _gfn(mfn_x(smfn)), smfn, order);
    if ( res )
        panic("Failed map pages to DOM0: %d\n", res);

    kinfo->unassigned_mem -= size;

    if ( mem->nr_banks == 0 )
    {
        mem->bank[0].start = start;
        mem->bank[0].size = size;
        mem->nr_banks = 1;
        return true;
    }

    for( i = 0; i < mem->nr_banks; i++ )
    {
        struct membank *bank = &mem->bank[i];

        /* If possible merge new memory into the start of the bank */
        if ( bank->start == start+size )
        {
            bank->start = start;
            bank->size += size;
            return true;
        }

        /* If possible merge new memory onto the end of the bank */
        if ( start == bank->start + bank->size )
        {
            bank->size += size;
            return true;
        }

        /*
         * Otherwise if it is below this bank insert new memory in a
         * new bank before this one. If there was a lower bank we
         * could have inserted the memory into/before we would already
         * have done so, so this must be the right place.
         */
        if ( start + size < bank->start && mem->nr_banks < mem->max_banks )
        {
            memmove(bank + 1, bank,
                    sizeof(*bank) * (mem->nr_banks - i));
            mem->nr_banks++;
            bank->start = start;
            bank->size = size;
            return true;
        }
    }

    if ( i == mem->nr_banks && mem->nr_banks < mem->max_banks )
    {
        struct membank *bank = &mem->bank[mem->nr_banks];

        bank->start = start;
        bank->size = size;
        mem->nr_banks++;
        return true;
    }

    /* If we get here then there are no more banks to fill. */

fail:
    free_domheap_pages(pg, order);
    return false;
}

/*
 * This is all pretty horrible.
 *
 * Requirements:
 *
 * 1. The dom0 kernel should be loaded within the first 128MB of RAM. This
 *    is necessary at least for Linux zImage kernels, which are all we
 *    support today.
 * 2. We want to put the dom0 kernel, ramdisk and DTB in the same
 *    bank. Partly this is just easier for us to deal with, but also
 *    the ramdisk and DTB must be placed within a certain proximity of
 *    the kernel within RAM.
 * 3. For dom0 we want to place as much of the RAM as we reasonably can
 *    below 4GB, so that it can be used by non-LPAE enabled kernels (32-bit).
 * 4. Some devices assigned to dom0 can only do 32-bit DMA access or
 *    even be more restricted. We want to allocate as much of the RAM
 *    as we reasonably can that can be accessed from all the devices..
 * 5. For 32-bit dom0 the kernel must be located below 4GB.
 * 6. We want to have a few largers banks rather than many smaller ones.
 *
 * For the first two requirements we need to make sure that the lowest
 * bank is sufficiently large.
 *
 * For convenience we also sort the banks by physical address.
 *
 * The memory allocator does not really give us the flexibility to
 * meet these requirements directly. So instead of proceed as follows:
 *
 * We first allocate the largest allocation we can as low as we
 * can. This then becomes the first bank. This bank must be at least
 * 128MB (or dom0_mem if that is smaller).
 *
 * Then we start allocating more memory, trying to allocate the
 * largest possible size and trying smaller sizes until we
 * successfully allocate something.
 *
 * We then try and insert this memory in to the list of banks. If it
 * can be merged into an existing bank then this is trivial.
 *
 * If the new memory is before the first bank (and cannot be merged into it)
 * and is at least 128M then we allow it, otherwise we give up. Since the
 * allocator prefers to allocate high addresses first and the first bank has
 * already been allocated to be as low as possible this likely means we
 * wouldn't have been able to allocate much more memory anyway.
 *
 * Otherwise we insert a new bank. If we've reached MAX_NR_BANKS then
 * we give up.
 *
 * For 32-bit domain we require that the initial allocation for the
 * first bank is part of the low mem. For 64-bit, the first bank is preferred
 * to be allocated in the low mem. Then for subsequent allocation, we
 * initially allocate memory only from low mem. Once that runs out out
 * (as described above) we allow higher allocations and continue until
 * that runs out (or we have allocated sufficient dom0 memory).
 */
static void __init allocate_memory_11(struct domain *d,
                                      struct kernel_info *kinfo)
{
    const unsigned int min_low_order =
        get_order_from_bytes(min_t(paddr_t, dom0_mem, MB(128)));
    const unsigned int min_order = get_order_from_bytes(MB(4));
    struct membanks *mem = kernel_info_get_mem(kinfo);
    struct page_info *pg;
    unsigned int order = get_allocation_size(kinfo->unassigned_mem);
    unsigned int i;

    bool lowmem = true;
    unsigned int lowmem_bitsize = min(32U, arch_get_dma_bitsize());
    unsigned int bits;

    /*
     * TODO: Implement memory bank allocation when DOM0 is not direct
     * mapped
     */
    BUG_ON(!is_domain_direct_mapped(d));

    printk("Allocating 1:1 mappings totalling %ldMB for dom0:\n",
           /* Don't want format this as PRIpaddr (16 digit hex) */
           (unsigned long)(kinfo->unassigned_mem >> 20));

    mem->nr_banks = 0;

    /*
     * First try and allocate the largest thing we can as low as
     * possible to be bank 0.
     */
    while ( order >= min_low_order )
    {
        for ( bits = order ; bits <= lowmem_bitsize; bits++ )
        {
            pg = alloc_domheap_pages(d, order, MEMF_bits(bits));
            if ( pg != NULL )
            {
                if ( !insert_11_bank(d, kinfo, pg, order) )
                    BUG(); /* Cannot fail for first bank */

                goto got_bank0;
            }
        }
        order--;
    }

    /* Failed to allocate bank0 in the lowmem region. */
    if ( is_32bit_domain(d) )
        panic("Unable to allocate first memory bank\n");

    /* Try to allocate memory from above the lowmem region */
    printk(XENLOG_INFO "No bank has been allocated below %u-bit.\n",
           lowmem_bitsize);
    lowmem = false;

 got_bank0:

    /*
     * If we failed to allocate bank0 in the lowmem region,
     * continue allocating from above the lowmem and fill in banks.
     */
    order = get_allocation_size(kinfo->unassigned_mem);
    while ( kinfo->unassigned_mem && mem->nr_banks < mem->max_banks )
    {
        pg = alloc_domheap_pages(d, order,
                                 lowmem ? MEMF_bits(lowmem_bitsize) : 0);
        if ( !pg )
        {
            order --;

            if ( lowmem && order < min_low_order)
            {
                D11PRINT("Failed at min_low_order, allow high allocations\n");
                order = get_allocation_size(kinfo->unassigned_mem);
                lowmem = false;
                continue;
            }
            if ( order >= min_order )
                continue;

            /* No more we can do */
            break;
        }

        if ( !insert_11_bank(d, kinfo, pg, order) )
        {
            if ( mem->nr_banks == mem->max_banks )
                /* Nothing more we can do. */
                break;

            if ( lowmem )
            {
                D11PRINT("Allocation below bank 0, allow high allocations\n");
                order = get_allocation_size(kinfo->unassigned_mem);
                lowmem = false;
                continue;
            }
            else
            {
                D11PRINT("Allocation below bank 0\n");
                break;
            }
        }

        /*
         * Success, next time around try again to get the largest order
         * allocation possible.
         */
        order = get_allocation_size(kinfo->unassigned_mem);
    }

    if ( kinfo->unassigned_mem )
        /* Don't want format this as PRIpaddr (16 digit hex) */
        panic("Failed to allocate requested dom0 memory. %ldMB unallocated\n",
              (unsigned long)kinfo->unassigned_mem >> 20);

    for( i = 0; i < mem->nr_banks; i++ )
    {
        printk("BANK[%d] %#"PRIpaddr"-%#"PRIpaddr" (%ldMB)\n",
               i,
               mem->bank[i].start,
               mem->bank[i].start + mem->bank[i].size,
               /* Don't want format this as PRIpaddr (16 digit hex) */
               (unsigned long)(mem->bank[i].size >> 20));
    }
}

#ifdef CONFIG_DOM0LESS_BOOT
bool __init allocate_domheap_memory(struct domain *d, paddr_t tot_size,
                                    alloc_domheap_mem_cb cb, void *extra)
{
    unsigned int max_order = UINT_MAX;

    while ( tot_size > 0 )
    {
        unsigned int order = get_allocation_size(tot_size);
        struct page_info *pg;

        order = min(max_order, order);

        pg = alloc_domheap_pages(d, order, 0);
        if ( !pg )
        {
            /*
             * If we can't allocate one page, then it is unlikely to
             * succeed in the next iteration. So bail out.
             */
            if ( !order )
                return false;

            /*
             * If we can't allocate memory with order, then it is
             * unlikely to succeed in the next iteration.
             * Record the order - 1 to avoid re-trying.
             */
            max_order = order - 1;
            continue;
        }

        if ( !cb(d, pg, order, extra) )
            return false;

        tot_size -= (1ULL << (PAGE_SHIFT + order));
    }

    return true;
}

static bool __init guest_map_pages(struct domain *d, struct page_info *pg,
                                   unsigned int order, void *extra)
{
    gfn_t *sgfn = (gfn_t *)extra;
    int res;

    BUG_ON(!sgfn);
    res = guest_physmap_add_page(d, *sgfn, page_to_mfn(pg), order);
    if ( res )
    {
        dprintk(XENLOG_ERR, "Failed map pages to DOMU: %d", res);
        return false;
    }

    *sgfn = gfn_add(*sgfn, 1UL << order);

    return true;
}

bool __init allocate_bank_memory(struct kernel_info *kinfo, gfn_t sgfn,
                                 paddr_t tot_size)
{
    struct membanks *mem = kernel_info_get_mem(kinfo);
    struct domain *d = kinfo->d;
    struct membank *bank;

    /*
     * allocate_bank_memory can be called with a tot_size of zero for
     * the second memory bank. It is not an error and we can safely
     * avoid creating a zero-size memory bank.
     */
    if ( tot_size == 0 )
        return true;

    bank = &mem->bank[mem->nr_banks];
    bank->start = gfn_to_gaddr(sgfn);
    bank->size = tot_size;

    /*
     * Allocate pages from the heap until tot_size is zero and map them to the
     * guest using guest_map_pages, passing the starting gfn as extra parameter
     * for the map operation.
     */
    if ( !allocate_domheap_memory(d, tot_size, guest_map_pages, &sgfn) )
        return false;

    mem->nr_banks++;
    kinfo->unassigned_mem -= bank->size;

    return true;
}
#endif

/*
 * When PCI passthrough is available we want to keep the
 * "linux,pci-domain" in sync for every host bridge.
 *
 * Xen may not have a driver for all the host bridges. So we have
 * to write an heuristic to detect whether a device node describes
 * a host bridge.
 *
 * The current heuristic assumes that a device is a host bridge
 * if the type is "pci" and then parent type is not "pci".
 */
static int __init handle_linux_pci_domain(struct kernel_info *kinfo,
                                          const struct dt_device_node *node)
{
    uint16_t segment;
    int res;

    if ( !is_pci_passthrough_enabled() )
        return 0;

    if ( !dt_device_type_is_equal(node, "pci") )
        return 0;

    if ( node->parent && dt_device_type_is_equal(node->parent, "pci") )
        return 0;

    if ( dt_find_property(node, "linux,pci-domain", NULL) )
        return 0;

    /* Allocate and create the linux,pci-domain */
    res = pci_get_host_bridge_segment(node, &segment);
    if ( res < 0 )
    {
        res = pci_get_new_domain_nr();
        if ( res < 0 )
        {
            printk(XENLOG_DEBUG "Can't assign PCI segment to %s\n",
                   node->full_name);
            return -FDT_ERR_NOTFOUND;
        }

        segment = res;
        printk(XENLOG_DEBUG "Assigned segment %d to %s\n",
               segment, node->full_name);
    }

    return fdt_property_cell(kinfo->fdt, "linux,pci-domain", segment);
}

static int __init write_properties(struct domain *d, struct kernel_info *kinfo,
                                   const struct dt_device_node *node)
{
    const char *bootargs = NULL;
    const struct dt_property *prop, *status = NULL;
    int res = 0;
    int had_dom0_bootargs = 0;
    struct dt_device_node *iommu_node;

    if ( kinfo->cmdline && kinfo->cmdline[0] )
        bootargs = &kinfo->cmdline[0];

    /*
     * We always skip the IOMMU device when creating DT for hwdom if there is
     * an appropriate driver for it in Xen (device_get_class(iommu_node)
     * returns DEVICE_IOMMU).
     * We should also skip the IOMMU specific properties of the master device
     * behind that IOMMU in order to avoid exposing an half complete IOMMU
     * bindings to hwdom.
     * Use "iommu_node" as an indicator of the master device which properties
     * should be skipped.
     */
    iommu_node = dt_parse_phandle(node, "iommus", 0);
    if ( !iommu_node )
        iommu_node = dt_parse_phandle(node, "iommu-map", 1);
    if ( iommu_node && device_get_class(iommu_node) != DEVICE_IOMMU )
        iommu_node = NULL;

    dt_for_each_property_node (node, prop)
    {
        const void *prop_data = prop->value;
        u32 prop_len = prop->length;

        /*
         * In chosen node:
         *
         * * remember xen,dom0-bootargs if we don't already have
         *   bootargs (from module #1, above).
         * * remove bootargs,  xen,dom0-bootargs, xen,xen-bootargs,
         *   xen,static-heap, linux,initrd-start and linux,initrd-end.
         * * remove stdout-path.
         * * remove bootargs, linux,uefi-system-table,
         *   linux,uefi-mmap-start, linux,uefi-mmap-size,
         *   linux,uefi-mmap-desc-size, and linux,uefi-mmap-desc-ver
         *   (since EFI boot is not currently supported in dom0).
         */
        if ( dt_node_path_is_equal(node, "/chosen") )
        {
            if ( dt_property_name_is_equal(prop, "xen,static-heap") ||
                 dt_property_name_is_equal(prop, "xen,xen-bootargs") ||
                 dt_property_name_is_equal(prop, "linux,initrd-start") ||
                 dt_property_name_is_equal(prop, "linux,initrd-end") ||
                 dt_property_name_is_equal(prop, "stdout-path") ||
                 dt_property_name_is_equal(prop, "linux,uefi-system-table") ||
                 dt_property_name_is_equal(prop, "linux,uefi-mmap-start") ||
                 dt_property_name_is_equal(prop, "linux,uefi-mmap-size") ||
                 dt_property_name_is_equal(prop, "linux,uefi-mmap-desc-size") ||
                 dt_property_name_is_equal(prop, "linux,uefi-mmap-desc-ver"))
                continue;

            if ( dt_property_name_is_equal(prop, "xen,dom0-bootargs") )
            {
                had_dom0_bootargs = 1;
                bootargs = prop->value;
                continue;
            }
            if ( dt_property_name_is_equal(prop, "bootargs") )
            {
                if ( !bootargs  && !had_dom0_bootargs )
                    bootargs = prop->value;
                continue;
            }
        }

        /* Don't expose the property "xen,passthrough" to the guest */
        if ( dt_property_name_is_equal(prop, "xen,passthrough") )
            continue;

        /* Remember and skip the status property as Xen may modify it later */
        if ( dt_property_name_is_equal(prop, "status") )
        {
            status = prop;
            continue;
        }

        if ( iommu_node )
        {
            /* Don't expose IOMMU specific properties to hwdom */
            if ( dt_property_name_is_equal(prop, "iommus") )
                continue;

            if ( dt_property_name_is_equal(prop, "iommu-map") )
                continue;

            if ( dt_property_name_is_equal(prop, "iommu-map-mask") )
                continue;
        }

        res = fdt_property(kinfo->fdt, prop->name, prop_data, prop_len);

        if ( res )
            return res;
    }

    res = handle_linux_pci_domain(kinfo, node);

    if ( res )
        return res;

    /*
     * Override the property "status" to disable the device when it's
     * marked for passthrough.
     */
    if ( dt_device_for_passthrough(node) )
        res = fdt_property_string(kinfo->fdt, "status", "disabled");
    else if ( status )
        res = fdt_property(kinfo->fdt, "status", status->value,
                           status->length);

    if ( res )
        return res;

    if ( dt_node_path_is_equal(node, "/chosen") )
    {
        const struct bootmodule *initrd = kinfo->initrd_bootmodule;

        if ( bootargs )
        {
            res = fdt_property(kinfo->fdt, "bootargs", bootargs,
                               strlen(bootargs) + 1);
            if ( res )
                return res;
        }

        /*
         * If the bootloader provides an initrd, we must create a placeholder
         * for the initrd properties. The values will be replaced later.
         */
        if ( initrd && initrd->size )
        {
            u64 a = 0;
            res = fdt_property(kinfo->fdt, "linux,initrd-start", &a, sizeof(a));
            if ( res )
                return res;

            res = fdt_property(kinfo->fdt, "linux,initrd-end", &a, sizeof(a));
            if ( res )
                return res;
        }
    }

    return 0;
}

void __init set_interrupt(gic_interrupt_t interrupt,
                          unsigned int irq,
                          unsigned int cpumask,
                          unsigned int level)
{
    __be32 *cells = interrupt;
    bool is_ppi = !!(irq < 32);

    BUG_ON(irq < 16);
    irq -= (is_ppi) ? 16: 32; /* PPIs start at 16, SPIs at 32 */

    /* See linux Documentation/devicetree/bindings/interrupt-controller/arm,gic.txt */
    dt_set_cell(&cells, 1, is_ppi); /* is a PPI? */
    dt_set_cell(&cells, 1, irq);
    dt_set_cell(&cells, 1, (cpumask << 8) | level);
}

/*
 * Helper to set interrupts for a node in the flat device tree.
 * It needs 2 property:
 *  "interrupts": contains the list of interrupts
 *  "interrupt-parent": link to the GIC
 */
static int __init fdt_property_interrupts(const struct kernel_info *kinfo,
                                          gic_interrupt_t *intr,
                                          unsigned int num_irq)
{
    int res;

    res = fdt_property(kinfo->fdt, "interrupts",
                       intr, sizeof(intr[0]) * num_irq);
    if ( res )
        return res;

    res = fdt_property_cell(kinfo->fdt, "interrupt-parent",
                            kinfo->phandle_gic);

    return res;
}

/*
 * Wrapper to convert physical address from paddr_t to uint64_t and
 * invoke fdt_begin_node(). This is required as the physical address
 * provided as part of node name should not contain any leading
 * zeroes. Thus, one should use PRIx64 (instead of PRIpaddr) to append
 * unit (which contains the physical address) with name to generate a
 * node name.
 */
int __init domain_fdt_begin_node(void *fdt, const char *name, uint64_t unit)
{
    /*
     * The size of the buffer to hold the longest possible string (i.e.
     * interrupt-controller@ + a 64-bit number + \0).
     */
    char buf[38];
    int ret;

    /* ePAPR 3.4 */
    ret = snprintf(buf, sizeof(buf), "%s@%"PRIx64, name, unit);

    if ( ret >= sizeof(buf) )
    {
        printk(XENLOG_ERR
               "Insufficient buffer. Minimum size required is %d\n",
               (ret + 1));

        return -FDT_ERR_TRUNCATED;
    }

    return fdt_begin_node(fdt, buf);
}

int __init make_memory_node(const struct kernel_info *kinfo, int addrcells,
                            int sizecells, const struct membanks *mem)
{
    void *fdt = kinfo->fdt;
    unsigned int i;
    int res, reg_size = addrcells + sizecells;
    int nr_cells = 0;
    __be32 reg[DT_MEM_NODE_REG_RANGE_SIZE];
    __be32 *cells;

    if ( mem->nr_banks == 0 )
        return -ENOENT;

    /* find the first memory range that is reserved for device (or firmware) */
    for ( i = 0; i < mem->nr_banks &&
                 (mem->bank[i].type != MEMBANK_DEFAULT); i++ )
        ;

    if ( i == mem->nr_banks )
        return 0;

    dt_dprintk("Create memory node\n");

    res = domain_fdt_begin_node(fdt, "memory", mem->bank[i].start);
    if ( res )
        return res;

    res = fdt_property_string(fdt, "device_type", "memory");
    if ( res )
        return res;

    cells = &reg[0];
    for ( ; i < mem->nr_banks; i++ )
    {
        u64 start = mem->bank[i].start;
        u64 size = mem->bank[i].size;

        if ( (mem->bank[i].type == MEMBANK_STATIC_DOMAIN) ||
             (mem->bank[i].type == MEMBANK_FDT_RESVMEM) )
            continue;

        nr_cells += reg_size;
        BUG_ON(nr_cells >= ARRAY_SIZE(reg));
        dt_child_set_range(&cells, addrcells, sizecells, start, size);
    }

    /*
     * static shared memory banks need to be listed as /memory node, so when
     * this function is handling the normal memory, add the banks.
     */
    if ( mem == kernel_info_get_mem_const(kinfo) )
        shm_mem_node_fill_reg_range(kinfo, reg, &nr_cells, addrcells,
                                    sizecells);

    for ( cells = reg, i = 0; cells < reg + nr_cells; i++, cells += reg_size )
    {
        uint64_t start = dt_read_number(cells, addrcells);
        uint64_t size = dt_read_number(cells + addrcells, sizecells);

        dt_dprintk("  Bank %u: %#"PRIx64"->%#"PRIx64"\n",
                   i, start, start + size);
    }

    dt_dprintk("(reg size %d, nr cells %d)\n", reg_size, nr_cells);

    res = fdt_property(fdt, "reg", reg, nr_cells * sizeof(*reg));
    if ( res )
        return res;

    res = fdt_end_node(fdt);

    return res;
}

int __init add_ext_regions(unsigned long s_gfn, unsigned long e_gfn,
                           void *data)
{
    struct membanks *ext_regions = data;
    paddr_t start, size;
    paddr_t s = pfn_to_paddr(s_gfn);
    paddr_t e = pfn_to_paddr(e_gfn);

    if ( ext_regions->nr_banks >= ext_regions->max_banks )
        return 0;

    /*
     * Both start and size of the extended region should be 2MB aligned to
     * potentially allow superpage mapping.
     */
    start = (s + SZ_2M - 1) & ~(SZ_2M - 1);
    if ( start > e )
        return 0;

    /*
     * e is actually "end-1" because it is called by rangeset functions
     * which are inclusive of the last address.
     */
    e += 1;
    size = (e - start) & ~(SZ_2M - 1);

    /*
     * Reasonable size. Not too little to pick up small ranges which are
     * not quite useful but increase bookkeeping and not too large
     * to skip a large proportion of unused address space.
     */
    if ( size < MB(64) )
        return 0;

    ext_regions->bank[ext_regions->nr_banks].start = start;
    ext_regions->bank[ext_regions->nr_banks].size = size;
    ext_regions->nr_banks++;

    return 0;
}

/*
 * Find unused regions of Host address space which can be exposed to Dom0
 * as extended regions for the special memory mappings. In order to calculate
 * regions we exclude every region assigned to Dom0 from the Host RAM:
 * - domain RAM
 * - reserved-memory
 * - static shared memory
 * - grant table space
 */
static int __init find_unallocated_memory(const struct kernel_info *kinfo,
                                          struct membanks *ext_regions)
{
    const struct membanks *mem = bootinfo_get_mem();
    const struct membanks *mem_banks[] = {
        kernel_info_get_mem_const(kinfo),
        bootinfo_get_reserved_mem(),
#ifdef CONFIG_STATIC_SHM
        bootinfo_get_shmem(),
#endif
    };
    struct rangeset *unalloc_mem;
    paddr_t start, end;
    unsigned int i, j;
    int res;

    dt_dprintk("Find unallocated memory for extended regions\n");

    unalloc_mem = rangeset_new(NULL, NULL, 0);
    if ( !unalloc_mem )
        return -ENOMEM;

    /* Start with all available RAM */
    for ( i = 0; i < mem->nr_banks; i++ )
    {
        start = mem->bank[i].start;
        end = mem->bank[i].start + mem->bank[i].size;
        res = rangeset_add_range(unalloc_mem, PFN_DOWN(start),
                                 PFN_DOWN(end - 1));
        if ( res )
        {
            printk(XENLOG_ERR "Failed to add: %#"PRIpaddr"->%#"PRIpaddr"\n",
                   start, end);
            goto out;
        }
    }

    /*
     * Exclude the following regions:
     * 1) Remove RAM assigned to Dom0
     * 2) Remove reserved memory
     * 3) Remove static shared memory (when the feature is enabled)
     */
    for ( i = 0; i < ARRAY_SIZE(mem_banks); i++ )
        for ( j = 0; j < mem_banks[i]->nr_banks; j++ )
        {
            start = mem_banks[i]->bank[j].start;

            /* Shared memory banks can contain INVALID_PADDR as start */
            if ( INVALID_PADDR == start )
                continue;

            end = mem_banks[i]->bank[j].start + mem_banks[i]->bank[j].size;
            res = rangeset_remove_range(unalloc_mem, PFN_DOWN(start),
                                        PFN_DOWN(end - 1));
            if ( res )
            {
                printk(XENLOG_ERR
                       "Failed to add: %#"PRIpaddr"->%#"PRIpaddr", error %d\n",
                       start, end, res);
                goto out;
            }
        }

    /* Remove grant table region */
    if ( kinfo->gnttab_size )
    {
        start = kinfo->gnttab_start;
        end = kinfo->gnttab_start + kinfo->gnttab_size;
        res = rangeset_remove_range(unalloc_mem, PFN_DOWN(start),
                                    PFN_DOWN(end - 1));
        if ( res )
        {
            printk(XENLOG_ERR "Failed to remove: %#"PRIpaddr"->%#"PRIpaddr"\n",
                   start, end);
            goto out;
        }
    }

    start = 0;
    end = (1ULL << p2m_ipa_bits) - 1;
    res = rangeset_report_ranges(unalloc_mem, PFN_DOWN(start), PFN_DOWN(end),
                                 add_ext_regions, ext_regions);
    if ( res )
        ext_regions->nr_banks = 0;
    else if ( !ext_regions->nr_banks )
        res = -ENOENT;

out:
    rangeset_destroy(unalloc_mem);

    return res;
}

static int __init handle_pci_range(const struct dt_device_node *dev,
                                   uint64_t addr, uint64_t len, void *data)
{
    struct rangeset *mem_holes = data;
    paddr_t start, end;
    int res;

    if ( (addr != (paddr_t)addr) || (((paddr_t)~0 - addr) < len) )
    {
        printk(XENLOG_ERR "%s: [0x%"PRIx64", 0x%"PRIx64"] exceeds the maximum allowed PA width (%u bits)",
               dt_node_full_name(dev), addr, (addr + len), PADDR_BITS);
        return -ERANGE;
    }

    start = addr & PAGE_MASK;
    end = PAGE_ALIGN(addr + len);
    res = rangeset_remove_range(mem_holes, PFN_DOWN(start), PFN_DOWN(end - 1));
    if ( res )
    {
        printk(XENLOG_ERR "Failed to remove: %#"PRIpaddr"->%#"PRIpaddr"\n",
               start, end);
        return res;
    }

    return 0;
}

/*
 * Find the holes in the Host DT which can be exposed to Dom0 as extended
 * regions for the special memory mappings. In order to calculate regions
 * we exclude every addressable memory region described by "reg" and "ranges"
 * properties from the maximum possible addressable physical memory range:
 * - MMIO
 * - Host RAM
 * - PCI aperture
 * - Static shared memory regions, which are described by special property
 *   "xen,shared-mem"
 */
static int __init find_memory_holes(const struct kernel_info *kinfo,
                                    struct membanks *ext_regions)
{
    struct dt_device_node *np;
    struct rangeset *mem_holes;
    paddr_t start, end;
    unsigned int i;
    int res;

    dt_dprintk("Find memory holes for extended regions\n");

    mem_holes = rangeset_new(NULL, NULL, 0);
    if ( !mem_holes )
        return -ENOMEM;

    /* Start with maximum possible addressable physical memory range */
    start = 0;
    end = (1ULL << p2m_ipa_bits) - 1;
    res = rangeset_add_range(mem_holes, PFN_DOWN(start), PFN_DOWN(end));
    if ( res )
    {
        printk(XENLOG_ERR "Failed to add: %#"PRIpaddr"->%#"PRIpaddr"\n",
               start, end);
        goto out;
    }

    /* Remove static shared memory regions */
    res = remove_shm_from_rangeset(kinfo, mem_holes);
    if ( res )
        goto out;

    /*
     * Remove regions described by "reg" and "ranges" properties where
     * the memory is addressable (MMIO, RAM, PCI BAR, etc).
     */
    dt_for_each_device_node( dt_host, np )
    {
        unsigned int naddr;
        paddr_t addr, size;

        naddr = dt_number_of_address(np);

        for ( i = 0; i < naddr; i++ )
        {
            res = dt_device_get_paddr(np, i, &addr, &size);
            if ( res )
            {
                printk(XENLOG_ERR "Unable to retrieve address %u for %s\n",
                       i, dt_node_full_name(np));
                goto out;
            }

            start = addr & PAGE_MASK;
            end = PAGE_ALIGN(addr + size);
            res = rangeset_remove_range(mem_holes, PFN_DOWN(start),
                                        PFN_DOWN(end - 1));
            if ( res )
            {
                printk(XENLOG_ERR "Failed to remove: %#"PRIpaddr"->%#"PRIpaddr"\n",
                       start, end);
                goto out;
            }
        }

        if ( dt_device_type_is_equal(np, "pci") )
        {
            /*
             * The ranges property in this context describes the PCI host
             * bridge aperture. It shall be absent if no addresses are mapped
             * through the bridge.
             */
            if ( !dt_get_property(np, "ranges", NULL) )
                continue;

            res = dt_for_each_range(np, &handle_pci_range, mem_holes);
            if ( res )
                goto out;
        }
    }

    start = 0;
    end = (1ULL << p2m_ipa_bits) - 1;
    res = rangeset_report_ranges(mem_holes, PFN_DOWN(start), PFN_DOWN(end),
                                 add_ext_regions,  ext_regions);
    if ( res )
        ext_regions->nr_banks = 0;
    else if ( !ext_regions->nr_banks )
        res = -ENOENT;

out:
    rangeset_destroy(mem_holes);

    return res;
}

static int __init find_domU_holes(const struct kernel_info *kinfo,
                                  struct membanks *ext_regions)
{
    unsigned int i;
    uint64_t bankend;
    const uint64_t bankbase[] = GUEST_RAM_BANK_BASES;
    const uint64_t banksize[] = GUEST_RAM_BANK_SIZES;
    const struct membanks *kinfo_mem = kernel_info_get_mem_const(kinfo);
    int res = -ENOENT;

    for ( i = 0; i < GUEST_RAM_BANKS; i++ )
    {
        struct membank *ext_bank = &(ext_regions->bank[ext_regions->nr_banks]);

        ext_bank->start = ROUNDUP(bankbase[i] + kinfo_mem->bank[i].size, SZ_2M);

        bankend = ~0ULL >> (64 - p2m_ipa_bits);
        bankend = min(bankend, bankbase[i] + banksize[i] - 1);
        if ( bankend > ext_bank->start )
            ext_bank->size = bankend - ext_bank->start + 1;

        /* 64MB is the minimum size of an extended region */
        if ( ext_bank->size < MB(64) )
            continue;
        ext_regions->nr_banks++;
        res = 0;
    }

    if ( res )
        return res;

    return remove_shm_holes_for_domU(kinfo, ext_regions);
}

int __init make_hypervisor_node(struct domain *d,
                                const struct kernel_info *kinfo,
                                int addrcells, int sizecells)
{
    const char compat[] =
        "xen,xen-" XEN_VERSION_STRING "\0"
        "xen,xen";
    __be32 *reg, *cells;
    gic_interrupt_t intr;
    int res;
    void *fdt = kinfo->fdt;
    struct membanks *ext_regions = NULL;
    unsigned int i, nr_ext_regions;

    dt_dprintk("Create hypervisor node\n");

    /*
     * Sanity-check address sizes, since addresses and sizes which do
     * not take up exactly 4 or 8 bytes are not supported.
     */
    if ((addrcells != 1 && addrcells != 2) ||
        (sizecells != 1 && sizecells != 2))
        panic("Cannot cope with this size\n");

    /* See linux Documentation/devicetree/bindings/arm/xen.txt */
    res = fdt_begin_node(fdt, "hypervisor");
    if ( res )
        return res;

    /* Cannot use fdt_property_string due to embedded nulls */
    res = fdt_property(fdt, "compatible", compat, sizeof(compat));
    if ( res )
        return res;

    if ( !opt_ext_regions )
    {
        printk(XENLOG_INFO "%pd: extended regions support is disabled\n", d);
        nr_ext_regions = 0;
    }
    else if ( is_32bit_domain(d) )
    {
        printk(XENLOG_WARNING
               "%pd: extended regions not supported for 32-bit guests\n", d);
        nr_ext_regions = 0;
    }
    else
    {
        ext_regions = xzalloc_flex_struct(struct membanks, bank, NR_MEM_BANKS);
        if ( !ext_regions )
            return -ENOMEM;

        ext_regions->max_banks = NR_MEM_BANKS;

        if ( is_domain_direct_mapped(d) )
        {
            if ( !is_iommu_enabled(d) )
                res = find_unallocated_memory(kinfo, ext_regions);
            else
                res = find_memory_holes(kinfo, ext_regions);
        }
        else
        {
            res = find_domU_holes(kinfo, ext_regions);
        }

        if ( res )
            printk(XENLOG_WARNING "%pd: failed to allocate extended regions\n",
                   d);
        nr_ext_regions = ext_regions->nr_banks;
    }

    reg = xzalloc_array(__be32, (nr_ext_regions + 1) * (addrcells + sizecells));
    if ( !reg )
    {
        xfree(ext_regions);
        return -ENOMEM;
    }

    /* reg 0 is grant table space */
    cells = &reg[0];
    dt_child_set_range(&cells, addrcells, sizecells,
                       kinfo->gnttab_start, kinfo->gnttab_size);
    /* reg 1...N are extended regions */
    for ( i = 0; i < nr_ext_regions; i++ )
    {
        u64 start = ext_regions->bank[i].start;
        u64 size = ext_regions->bank[i].size;

        printk("%pd: extended region %d: %#"PRIx64"->%#"PRIx64"\n",
               d, i, start, start + size);

        dt_child_set_range(&cells, addrcells, sizecells, start, size);
    }

    res = fdt_property(fdt, "reg", reg,
                       dt_cells_to_size(addrcells + sizecells) *
                       (nr_ext_regions + 1));
    xfree(ext_regions);
    xfree(reg);

    if ( res )
        return res;

    BUG_ON(d->arch.evtchn_irq == 0);

    /*
     * Interrupt event channel upcall:
     *  - Active-low level-sensitive
     *  - All CPUs
     *  TODO: Handle properly the cpumask;
     */
    set_interrupt(intr, d->arch.evtchn_irq, 0xf, DT_IRQ_TYPE_LEVEL_LOW);
    res = fdt_property_interrupts(kinfo, &intr, 1);
    if ( res )
        return res;

    res = fdt_end_node(fdt);

    return res;
}

int __init make_psci_node(void *fdt)
{
    int res;
    const char compat[] =
        "arm,psci-1.0""\0"
        "arm,psci-0.2""\0"
        "arm,psci";

    dt_dprintk("Create PSCI node\n");

    /* See linux Documentation/devicetree/bindings/arm/psci.txt */
    res = fdt_begin_node(fdt, "psci");
    if ( res )
        return res;

    res = fdt_property(fdt, "compatible", compat, sizeof(compat));
    if ( res )
        return res;

    res = fdt_property_string(fdt, "method", "hvc");
    if ( res )
        return res;

    res = fdt_property_cell(fdt, "cpu_off", PSCI_cpu_off);
    if ( res )
        return res;

    res = fdt_property_cell(fdt, "cpu_on", PSCI_cpu_on);
    if ( res )
        return res;

    res = fdt_end_node(fdt);

    return res;
}

int __init make_cpus_node(const struct domain *d, void *fdt)
{
    int res;
    const struct dt_device_node *cpus = dt_find_node_by_path("/cpus");
    const struct dt_device_node *npcpu;
    unsigned int cpu;
    const void *compatible = NULL;
    u32 len;
    /* Placeholder for cpu@ + a 32-bit hexadecimal number + \0 */
    char buf[13];
    u32 clock_frequency;
    /* Keep the compiler happy with -Og */
    bool clock_valid = false;
    uint64_t mpidr_aff;

    dt_dprintk("Create cpus node\n");

    if ( !cpus )
    {
        dprintk(XENLOG_ERR, "Missing /cpus node in the device tree?\n");
        return -ENOENT;
    }

    /*
     * Get the compatible property of CPUs from the device tree.
     * We are assuming that all CPUs are the same so we are just look
     * for the first one.
     * TODO: Handle compatible per VCPU
     */
    dt_for_each_child_node(cpus, npcpu)
    {
        if ( dt_device_type_is_equal(npcpu, "cpu") )
        {
            compatible = dt_get_property(npcpu, "compatible", &len);
            clock_valid = dt_property_read_u32(npcpu, "clock-frequency",
                                            &clock_frequency);
            break;
        }
    }

    if ( !compatible )
    {
        dprintk(XENLOG_ERR, "Can't find cpu in the device tree?\n");
        return -ENOENT;
    }

    /* See Linux Documentation/devicetree/booting-without-of.txt
     * section III.5.b
     */
    res = fdt_begin_node(fdt, "cpus");
    if ( res )
        return res;

    res = fdt_property_cell(fdt, "#address-cells", 1);
    if ( res )
        return res;

    res = fdt_property_cell(fdt, "#size-cells", 0);
    if ( res )
        return res;

    for ( cpu = 0; cpu < d->max_vcpus; cpu++ )
    {
        /*
         * According to ARM CPUs bindings, the reg field should match
         * the MPIDR's affinity bits. We will use AFF0 and AFF1 when
         * constructing the reg value of the guest at the moment, for it
         * is enough for the current max vcpu number.
         *
         * We only deal with AFF{0, 1, 2} stored in bits [23:0] at the
         * moment.
         */
        mpidr_aff = vcpuid_to_vaffinity(cpu);
        if ( (mpidr_aff & ~GENMASK_ULL(23, 0)) != 0 )
        {
            printk(XENLOG_ERR "Unable to handle MPIDR AFFINITY 0x%"PRIx64"\n",
                   mpidr_aff);
            return -EINVAL;
        }

        dt_dprintk("Create cpu@%"PRIx64" (logical CPUID: %d) node\n",
                   mpidr_aff, cpu);

        /*
         * We use PRIx64 because mpidr_aff is a 64bit integer. However,
         * only bits [23:0] are used, thus, we are sure it will fit in
         * buf.
         */
        snprintf(buf, sizeof(buf), "cpu@%"PRIx64, mpidr_aff);
        res = fdt_begin_node(fdt, buf);
        if ( res )
            return res;

        res = fdt_property(fdt, "compatible", compatible, len);
        if ( res )
            return res;

        res = fdt_property_string(fdt, "device_type", "cpu");
        if ( res )
            return res;

        res = fdt_property_cell(fdt, "reg", mpidr_aff);
        if ( res )
            return res;

        if ( clock_valid )
        {
            res = fdt_property_cell(fdt, "clock-frequency", clock_frequency);
            if ( res )
                return res;
        }

        if ( is_64bit_domain(d) )
        {
            res = fdt_property_string(fdt, "enable-method", "psci");
            if ( res )
                return res;
        }

        res = fdt_end_node(fdt);
        if ( res )
            return res;
    }

    res = fdt_end_node(fdt);

    return res;
}

static int __init make_gic_node(const struct domain *d, void *fdt,
                                const struct dt_device_node *node)
{
    const struct dt_device_node *gic = dt_interrupt_controller;
    int res = 0;
    const void *addrcells, *sizecells;
    u32 addrcells_len, sizecells_len;

    /*
     * Xen currently supports only a single GIC. Discard any secondary
     * GIC entries.
     */
    if ( node != dt_interrupt_controller )
    {
        dt_dprintk("  Skipping (secondary GIC)\n");
        return 0;
    }

    dt_dprintk("Create gic node\n");

    res = fdt_begin_node(fdt, "interrupt-controller");
    if ( res )
        return res;

    /*
     * The value of the property "phandle" in the property "interrupts"
     * to know on which interrupt controller the interrupt is wired.
     */
    if ( gic->phandle )
    {
        dt_dprintk("  Set phandle = 0x%x\n", gic->phandle);
        res = fdt_property_cell(fdt, "phandle", gic->phandle);
        if ( res )
            return res;
    }

    addrcells = dt_get_property(gic, "#address-cells", &addrcells_len);
    if ( addrcells )
    {
        res = fdt_property(fdt, "#address-cells", addrcells, addrcells_len);
        if ( res )
            return res;
    }

    sizecells = dt_get_property(gic, "#size-cells", &sizecells_len);
    if ( sizecells )
    {
        res = fdt_property(fdt, "#size-cells", sizecells, sizecells_len);
        if ( res )
            return res;
    }

    res = fdt_property_cell(fdt, "#interrupt-cells", 3);
    if ( res )
        return res;

    res = fdt_property(fdt, "interrupt-controller", NULL, 0);
    if ( res )
        return res;

    res = gic_make_hwdom_dt_node(d, node, fdt);
    if ( res )
        return res;

    res = fdt_end_node(fdt);

    return res;
}

int __init make_timer_node(const struct kernel_info *kinfo)
{
    void *fdt = kinfo->fdt;
    static const struct dt_device_match timer_ids[] __initconst =
    {
        DT_MATCH_TIMER,
        { /* sentinel */ },
    };
    struct dt_device_node *dev;
    int res;
    unsigned int irq[MAX_TIMER_PPI];
    gic_interrupt_t intrs[3];
    u32 clock_frequency;
    bool clock_valid;

    dt_dprintk("Create timer node\n");

    dev = dt_find_matching_node(NULL, timer_ids);
    if ( !dev )
    {
        dprintk(XENLOG_ERR, "Missing timer node in the device tree?\n");
        return -FDT_ERR_XEN(ENOENT);
    }

    res = fdt_begin_node(fdt, "timer");
    if ( res )
        return res;

    if ( !is_64bit_domain(kinfo->d) )
        res = fdt_property_string(fdt, "compatible", "arm,armv7-timer");
    else
        res = fdt_property_string(fdt, "compatible", "arm,armv8-timer");
    if ( res )
        return res;

    /*
     * The timer IRQ is emulated by Xen.
     * It always exposes an active-low level-sensitive interrupt.
     */

    if ( is_hardware_domain(kinfo->d) )
    {
        irq[TIMER_PHYS_SECURE_PPI] = timer_get_irq(TIMER_PHYS_SECURE_PPI);
        irq[TIMER_PHYS_NONSECURE_PPI] =
                                    timer_get_irq(TIMER_PHYS_NONSECURE_PPI);
        irq[TIMER_VIRT_PPI] = timer_get_irq(TIMER_VIRT_PPI);
    }
    else
    {
        irq[TIMER_PHYS_SECURE_PPI] = GUEST_TIMER_PHYS_S_PPI;
        irq[TIMER_PHYS_NONSECURE_PPI] = GUEST_TIMER_PHYS_NS_PPI;
        irq[TIMER_VIRT_PPI] = GUEST_TIMER_VIRT_PPI;
    }
    dt_dprintk("  Secure interrupt %u\n", irq[TIMER_PHYS_SECURE_PPI]);
    set_interrupt(intrs[0], irq[TIMER_PHYS_SECURE_PPI],
                  0xf, DT_IRQ_TYPE_LEVEL_LOW);
    dt_dprintk("  Non secure interrupt %u\n", irq[TIMER_PHYS_NONSECURE_PPI]);
    set_interrupt(intrs[1], irq[TIMER_PHYS_NONSECURE_PPI],
                  0xf, DT_IRQ_TYPE_LEVEL_LOW);
    dt_dprintk("  Virt interrupt %u\n", irq[TIMER_VIRT_PPI]);
    set_interrupt(intrs[2], irq[TIMER_VIRT_PPI], 0xf, DT_IRQ_TYPE_LEVEL_LOW);

    res = fdt_property_interrupts(kinfo, intrs, 3);
    if ( res )
        return res;

    clock_valid = dt_property_read_u32(dev, "clock-frequency",
                                       &clock_frequency);
    if ( clock_valid )
    {
        res = fdt_property_cell(fdt, "clock-frequency", clock_frequency);
        if ( res )
            return res;
    }

    res = fdt_end_node(fdt);

    return res;
}

/*
 * This function is used as part of the device tree generation for Dom0
 * on ACPI systems, and DomUs started directly from Xen based on device
 * tree information.
 */
int __init make_chosen_node(const struct kernel_info *kinfo)
{
    int res;
    const char *bootargs = NULL;
    const struct bootmodule *initrd = kinfo->initrd_bootmodule;
    void *fdt = kinfo->fdt;

    dt_dprintk("Create chosen node\n");
    res = fdt_begin_node(fdt, "chosen");
    if ( res )
        return res;

    if ( kinfo->cmdline && kinfo->cmdline[0] )
    {
        bootargs = &kinfo->cmdline[0];
        res = fdt_property(fdt, "bootargs", bootargs, strlen(bootargs) + 1);
        if ( res )
           return res;
    }

    /*
     * If the bootloader provides an initrd, we must create a placeholder
     * for the initrd properties. The values will be replaced later.
     */
    if ( initrd && initrd->size )
    {
        u64 a = 0;
        res = fdt_property(kinfo->fdt, "linux,initrd-start", &a, sizeof(a));
        if ( res )
            return res;

        res = fdt_property(kinfo->fdt, "linux,initrd-end", &a, sizeof(a));
        if ( res )
            return res;
    }

    res = fdt_end_node(fdt);

    return res;
}

static int __init handle_node(struct domain *d, struct kernel_info *kinfo,
                              struct dt_device_node *node,
                              p2m_type_t p2mt)
{
    static const struct dt_device_match skip_matches[] __initconst =
    {
        DT_MATCH_COMPATIBLE("xen,domain"),
        DT_MATCH_COMPATIBLE("xen,domain-shared-memory-v1"),
        DT_MATCH_COMPATIBLE("xen,evtchn-v1"),
        DT_MATCH_COMPATIBLE("xen,xen"),
        DT_MATCH_COMPATIBLE("xen,multiboot-module"),
        DT_MATCH_COMPATIBLE("multiboot,module"),
        DT_MATCH_COMPATIBLE("arm,psci"),
        DT_MATCH_COMPATIBLE("arm,psci-0.2"),
        DT_MATCH_COMPATIBLE("arm,psci-1.0"),
        DT_MATCH_COMPATIBLE("arm,cortex-a7-pmu"),
        DT_MATCH_COMPATIBLE("arm,cortex-a15-pmu"),
        DT_MATCH_COMPATIBLE("arm,cortex-a53-edac"),
        DT_MATCH_COMPATIBLE("arm,armv8-pmuv3"),
        DT_MATCH_PATH("/cpus"),
        DT_MATCH_TYPE("memory"),
        /* The memory mapped timer is not supported by Xen. */
        DT_MATCH_COMPATIBLE("arm,armv7-timer-mem"),
        { /* sentinel */ },
    };
    static const struct dt_device_match timer_matches[] __initconst =
    {
        DT_MATCH_TIMER,
        { /* sentinel */ },
    };
    static const struct dt_device_match reserved_matches[] __initconst =
    {
        DT_MATCH_PATH("/psci"),
        DT_MATCH_PATH("/memory"),
        DT_MATCH_PATH("/hypervisor"),
        { /* sentinel */ },
    };
    static __initdata bool res_mem_node_found = false;
    struct dt_device_node *child;
    int res, i, nirq, irq_id;
    const char *name;
    const char *path;

    path = dt_node_full_name(node);

    dt_dprintk("handle %s\n", path);

    /* Skip theses nodes and the sub-nodes */
    if ( dt_match_node(skip_matches, node) )
    {
        dt_dprintk("  Skip it (matched)\n");
        return 0;
    }
    if ( platform_device_is_blacklisted(node) )
    {
        dt_dprintk("  Skip it (blacklisted)\n");
        return 0;
    }

    /*
     * Replace these nodes with our own. Note that the original may be
     * used_by DOMID_XEN so this check comes first.
     */
    if ( device_get_class(node) == DEVICE_INTERRUPT_CONTROLLER )
        return make_gic_node(d, kinfo->fdt, node);
    if ( dt_match_node(timer_matches, node) )
        return make_timer_node(kinfo);

    /* Skip nodes used by Xen */
    if ( dt_device_used_by(node) == DOMID_XEN )
    {
        dt_dprintk("  Skip it (used by Xen)\n");
        return 0;
    }

    /*
     * Even if the IOMMU device is not used by Xen, it should not be
     * passthrough to DOM0
     */
    if ( device_get_class(node) == DEVICE_IOMMU )
    {
        dt_dprintk(" IOMMU, skip it\n");
        return 0;
    }

    /*
     * The vGIC does not support routing hardware PPIs to guest. So
     * we need to skip any node using PPIs.
     */
    nirq = dt_number_of_irq(node);

    for ( i = 0 ; i < nirq ; i++ )
    {
        irq_id = platform_get_irq(node, i);

        /* PPIs ranges from ID 16 to 31 */
        if ( irq_id >= 16 && irq_id < 32 )
        {
            dt_dprintk(" Skip it (using PPIs)\n");
            return 0;
        }
    }

    /*
     * Xen is using some path for its own purpose. Warn if a node
     * already exists with the same path.
     */
    if ( dt_match_node(reserved_matches, node) )
        printk(XENLOG_WARNING
               "WARNING: Path %s is reserved, skip the node as we may re-use the path.\n",
               path);

    res = handle_device(d, node, p2mt, NULL, NULL);
    if ( res)
        return res;

    /*
     * The property "name" is used to have a different name on older FDT
     * version. We want to keep the name retrieved during the tree
     * structure creation, that is store in the node path.
     */
    name = strrchr(path, '/');
    name = name ? name + 1 : path;

    res = fdt_begin_node(kinfo->fdt, name);
    if ( res )
        return res;

    res = write_properties(d, kinfo, node);
    if ( res )
        return res;

    if ( dt_node_path_is_equal(node, "/reserved-memory") )
    {
        res_mem_node_found = true;
        /*
         * Avoid duplicate /reserved-memory nodes in Device Tree, so add the
         * static shared memory nodes there.
         */
        res = make_shm_resv_memory_node(kinfo, dt_n_addr_cells(node),
                                        dt_n_size_cells(node));
        if ( res )
            return res;
    }

    for ( child = node->child; child != NULL; child = child->sibling )
    {
        res = handle_node(d, kinfo, child, p2mt);
        if ( res )
            return res;
    }

    if ( node == dt_host )
    {
        const struct membanks *reserved_mem = bootinfo_get_reserved_mem();
        int addrcells = dt_child_n_addr_cells(node);
        int sizecells = dt_child_n_size_cells(node);

        /*
         * It is safe to allocate the event channel here because all the
         * PPIs used by the hardware domain have been registered.
         */
        evtchn_allocate(d);

        /*
         * The hypervisor node should always be created after all nodes
         * from the host DT have been parsed.
         */
        res = make_hypervisor_node(d, kinfo, addrcells, sizecells);
        if ( res )
            return res;

        res = make_psci_node(kinfo->fdt);
        if ( res )
            return res;

        res = make_cpus_node(d, kinfo->fdt);
        if ( res )
            return res;

        res = make_memory_node(kinfo, addrcells, sizecells,
                               kernel_info_get_mem(kinfo));
        if ( res )
            return res;

        /*
         * Create a second memory node to store the ranges covering
         * reserved-memory regions.
         */
        if ( reserved_mem->nr_banks > 0 )
        {
            res = make_memory_node(kinfo, addrcells, sizecells, reserved_mem);
            if ( res )
                return res;
        }

        if ( !res_mem_node_found )
        {
            res = make_resv_memory_node(kinfo, addrcells, sizecells);
            if ( res )
                return res;
        }
    }

    res = fdt_end_node(kinfo->fdt);

    return res;
}

static int __init prepare_dtb_hwdom(struct domain *d, struct kernel_info *kinfo)
{
    const p2m_type_t default_p2mt = p2m_mmio_direct_c;
    const void *fdt;
    int new_size;
    int ret;

    ASSERT(dt_host && (dt_host->sibling == NULL));

    kinfo->phandle_gic = dt_interrupt_controller->phandle;
    fdt = device_tree_flattened;

    new_size = fdt_totalsize(fdt) + DOM0_FDT_EXTRA_SIZE;
    kinfo->fdt = xmalloc_bytes(new_size);
    if ( kinfo->fdt == NULL )
        return -ENOMEM;

    ret = fdt_create(kinfo->fdt, new_size);
    if ( ret < 0 )
        goto err;

    fdt_finish_reservemap(kinfo->fdt);

    ret = handle_node(d, kinfo, dt_host, default_p2mt);
    if ( ret )
        goto err;

    ret = fdt_finish(kinfo->fdt);
    if ( ret < 0 )
        goto err;

    return 0;

  err:
    printk("Device tree generation failed (%d).\n", ret);
    xfree(kinfo->fdt);
    return -EINVAL;
}

static void __init dtb_load(struct kernel_info *kinfo)
{
    unsigned long left;

    printk("Loading %pd DTB to 0x%"PRIpaddr"-0x%"PRIpaddr"\n",
           kinfo->d, kinfo->dtb_paddr,
           kinfo->dtb_paddr + fdt_totalsize(kinfo->fdt));

    left = copy_to_guest_phys_flush_dcache(kinfo->d, kinfo->dtb_paddr,
                                           kinfo->fdt,
                                           fdt_totalsize(kinfo->fdt));

    if ( left != 0 )
        panic("Unable to copy the DTB to %pd memory (left = %lu bytes)\n",
              kinfo->d, left);
    xfree(kinfo->fdt);
}

static void __init initrd_load(struct kernel_info *kinfo)
{
    const struct bootmodule *mod = kinfo->initrd_bootmodule;
    paddr_t load_addr = kinfo->initrd_paddr;
    paddr_t paddr, len;
    int node;
    int res;
    __be32 val[2];
    __be32 *cellp;
    void __iomem *initrd;

    if ( !mod || !mod->size )
        return;

    paddr = mod->start;
    len = mod->size;

    printk("Loading %pd initrd from %"PRIpaddr" to 0x%"PRIpaddr"-0x%"PRIpaddr"\n",
           kinfo->d, paddr, load_addr, load_addr + len);

    /* Fix up linux,initrd-start and linux,initrd-end in /chosen */
    node = fdt_path_offset(kinfo->fdt, "/chosen");
    if ( node < 0 )
        panic("Cannot find the /chosen node\n");

    cellp = (__be32 *)val;
    dt_set_cell(&cellp, ARRAY_SIZE(val), load_addr);
    res = fdt_setprop_inplace(kinfo->fdt, node, "linux,initrd-start",
                              val, sizeof(val));
    if ( res )
        panic("Cannot fix up \"linux,initrd-start\" property\n");

    cellp = (__be32 *)val;
    dt_set_cell(&cellp, ARRAY_SIZE(val), load_addr + len);
    res = fdt_setprop_inplace(kinfo->fdt, node, "linux,initrd-end",
                              val, sizeof(val));
    if ( res )
        panic("Cannot fix up \"linux,initrd-end\" property\n");

    initrd = ioremap_wc(paddr, len);
    if ( !initrd )
        panic("Unable to map the hwdom initrd\n");

    res = copy_to_guest_phys_flush_dcache(kinfo->d, load_addr,
                                          initrd, len);
    if ( res != 0 )
        panic("Unable to copy the initrd in the hwdom memory\n");

    iounmap(initrd);
}

/*
 * Allocate the event channel PPIs and setup the HVM_PARAM_CALLBACK_IRQ.
 * The allocated IRQ will be found in d->arch.evtchn_irq.
 *
 * Note that this should only be called once all PPIs used by the
 * hardware domain have been registered.
 */
void __init evtchn_allocate(struct domain *d)
{
    int res;
    u64 val;

    res = vgic_allocate_ppi(d);
    if ( res < 0 )
        panic("Unable to allocate a PPI for the event channel interrupt\n");

    d->arch.evtchn_irq = res;

    printk("Allocating PPI %u for event channel interrupt\n",
           d->arch.evtchn_irq);

    /* Set the value of domain param HVM_PARAM_CALLBACK_IRQ */
    val = MASK_INSR(HVM_PARAM_CALLBACK_TYPE_PPI,
                    HVM_PARAM_CALLBACK_IRQ_TYPE_MASK);
    /* Active-low level-sensitive  */
    val |= MASK_INSR(HVM_PARAM_CALLBACK_TYPE_PPI_FLAG_LOW_LEVEL,
                     HVM_PARAM_CALLBACK_TYPE_PPI_FLAG_MASK);
    val |= d->arch.evtchn_irq;
    d->arch.hvm.params[HVM_PARAM_CALLBACK_IRQ] = val;
}

static void __init find_gnttab_region(struct domain *d,
                                      struct kernel_info *kinfo)
{
//     /*
//      * The region used by Xen on the memory will never be mapped in DOM0
//      * memory layout. Therefore it can be used for the grant table.
//      *
//      * Only use the text section as it's always present and will contain
//      * enough space for a large grant table
//      */
//     kinfo->gnttab_start = __pa(_stext);
//     kinfo->gnttab_size = gnttab_dom0_frames() << PAGE_SHIFT;

// #ifdef CONFIG_ARM_32
//     /*
//      * The gnttab region must be under 4GB in order to work with DOM0
//      * using short page table.
//      * In practice it's always the case because Xen is always located
//      * below 4GB, but be safe.
//      */
//     BUG_ON((kinfo->gnttab_start + kinfo->gnttab_size) > GB(4));
// #endif

//     printk("Grant table range: %#"PRIpaddr"-%#"PRIpaddr"\n",
//            kinfo->gnttab_start, kinfo->gnttab_start + kinfo->gnttab_size);
}

int __init construct_domain(struct domain *d, struct kernel_info *kinfo)
{
    unsigned int i;
    struct vcpu *v = d->vcpu[0];
    struct cpu_user_regs *regs = &v->arch.cpu_info->guest_cpu_user_regs;

    BUG_ON(d->vcpu[0] == NULL);
    BUG_ON(v->is_initialised);

#ifdef CONFIG_ARM_64
    /* if aarch32 mode is not supported at EL1 do not allow 32-bit domain */
    if ( !(cpu_has_el1_32) && kinfo->type == DOMAIN_32BIT )
    {
        printk("Platform does not support 32-bit domain\n");
        return -EINVAL;
    }

    if ( is_sve_domain(d) && (kinfo->type == DOMAIN_32BIT) )
    {
        printk("SVE is not available for 32-bit domain\n");
        return -EINVAL;
    }

    if ( is_64bit_domain(d) )
        vcpu_switch_to_aarch64_mode(v);

#endif

    /*
     * kernel_load will determine the placement of the kernel as well
     * as the initrd & fdt in RAM, so call it first.
     */
    kernel_load(kinfo);
    /* initrd_load will fix up the fdt, so call it before dtb_load */
    initrd_load(kinfo);
    dtb_load(kinfo);

    memset(regs, 0, sizeof(*regs));

    regs->pc = (register_t)kinfo->entry;

    if ( is_32bit_domain(d) )
    {
        regs->cpsr = PSR_GUEST32_INIT;

        /* FROM LINUX head.S
         *
         * Kernel startup entry point.
         * ---------------------------
         *
         * This is normally called from the decompressor code.  The requirements
         * are: MMU = off, D-cache = off, I-cache = dont care, r0 = 0,
         * r1 = machine nr, r2 = atags or dtb pointer.
         *...
         */
        regs->r0 = 0U; /* SBZ */
        regs->r1 = 0xffffffffU; /* We use DTB therefore no machine id */
        regs->r2 = kinfo->dtb_paddr;
    }
#ifdef CONFIG_ARM_64
    else
    {
        regs->cpsr = PSR_GUEST64_INIT;
        /* From linux/Documentation/arm64/booting.txt */
        regs->x0 = kinfo->dtb_paddr;
        regs->x1 = 0; /* Reserved for future use */
        regs->x2 = 0; /* Reserved for future use */
        regs->x3 = 0; /* Reserved for future use */
    }
#endif

    for ( i = 1; i < d->max_vcpus; i++ )
    {
        if ( vcpu_create(d, i) == NULL )
        {
            printk("Failed to allocate d%dv%d\n", d->domain_id, i);
            break;
        }

        if ( is_64bit_domain(d) )
            vcpu_switch_to_aarch64_mode(d->vcpu[i]);
    }

    domain_update_node_affinity(d);

    v->is_initialised = 1;
    clear_bit(_VPF_down, &v->pause_flags);

    return 0;
}

static int __init construct_dom0(struct domain *d)
{
    struct kernel_info kinfo = KERNEL_INFO_INIT;
    int rc;

    /* Sanity! */
    BUG_ON(d->domain_id != 0);

    printk("*** LOADING DOMAIN 0 ***\n");

    /* The ordering of operands is to work around a clang5 issue. */
    if ( CONFIG_DOM0_MEM[0] && !dom0_mem_set )
        parse_dom0_mem(CONFIG_DOM0_MEM);

    if ( dom0_mem <= 0 )
    {
        warning_add("PLEASE SPECIFY dom0_mem PARAMETER - USING 512M FOR NOW\n");
        dom0_mem = MB(512);
    }

    iommu_hwdom_init(d);

    d->max_pages = dom0_mem >> PAGE_SHIFT;

    kinfo.unassigned_mem = dom0_mem;
    kinfo.d = d;

    rc = kernel_probe(&kinfo, NULL);
    if ( rc < 0 )
        return rc;

#ifdef CONFIG_ARM_64
    /* type must be set before allocate_memory */
    d->arch.type = kinfo.type;
#endif
    allocate_memory_11(d, &kinfo);
    find_gnttab_region(d, &kinfo);

    rc = process_shm_chosen(d, &kinfo);
    if ( rc < 0 )
        return rc;

    /* Map extra GIC MMIO, irqs and other hw stuffs to dom0. */
    rc = gic_map_hwdom_extra_mappings(d);
    if ( rc < 0 )
        return rc;

    rc = platform_specific_mapping(d);
    if ( rc < 0 )
        return rc;

    if ( acpi_disabled )
    {
        rc = prepare_dtb_hwdom(d, &kinfo);
        if ( rc < 0 )
            return rc;
#ifdef CONFIG_HAS_PCI
        rc = pci_host_bridge_mappings(d);
#endif
    }
    else
        rc = prepare_acpi(d, &kinfo);

    if ( rc < 0 )
        return rc;

    return construct_domain(d, &kinfo);
}


/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: domain_build.c === */
/* === BEGIN INLINED: dom0less-build.c === */
#include <xen_xen_config.h>
/* SPDX-License-Identifier: GPL-2.0-only */
#include <xen_device_tree.h>
#include <xen_err.h>
#include <xen_event.h>
#include <xen_grant_table.h>
#include <xen_iocap.h>
#include <xen_libfdt_libfdt.h>
#include <xen_sched.h>
#include <xen_serial.h>
#include <xen_sizes.h>
#include <xen_vmap.h>

#include <asm_arm64_sve.h>
#include <asm_dom0less-build.h>
#include <asm_domain_build.h>
#include <asm_static-memory.h>
#include <asm_static-shmem.h>


static void __init allocate_memory(struct domain *d, struct kernel_info *kinfo)
{
    struct membanks *mem = kernel_info_get_mem(kinfo);
    unsigned int i;
    paddr_t bank_size;

    printk(XENLOG_INFO "Allocating mappings totalling %ldMB for %pd:\n",
           /* Don't want format this as PRIpaddr (16 digit hex) */
           (unsigned long)(kinfo->unassigned_mem >> 20), d);

    mem->nr_banks = 0;
    bank_size = MIN(GUEST_RAM0_SIZE, kinfo->unassigned_mem);
    if ( !allocate_bank_memory(kinfo, gaddr_to_gfn(GUEST_RAM0_BASE),
                               bank_size) )
        goto fail;

    bank_size = MIN(GUEST_RAM1_SIZE, kinfo->unassigned_mem);
    if ( !allocate_bank_memory(kinfo, gaddr_to_gfn(GUEST_RAM1_BASE),
                               bank_size) )
        goto fail;

    if ( kinfo->unassigned_mem )
        goto fail;

    for( i = 0; i < mem->nr_banks; i++ )
    {
        printk(XENLOG_INFO "%pd BANK[%d] %#"PRIpaddr"-%#"PRIpaddr" (%ldMB)\n",
               d,
               i,
               mem->bank[i].start,
               mem->bank[i].start + mem->bank[i].size,
               /* Don't want format this as PRIpaddr (16 digit hex) */
               (unsigned long)(mem->bank[i].size >> 20));
    }

    return;

fail:
    panic("Failed to allocate requested domain memory."
          /* Don't want format this as PRIpaddr (16 digit hex) */
          " %ldKB unallocated. Fix the VMs configurations.\n",
          (unsigned long)kinfo->unassigned_mem >> 10);
}

#ifdef CONFIG_VGICV2

#endif

#ifdef CONFIG_GICV3
static int __init make_gicv3_domU_node(struct kernel_info *kinfo)
{
    void *fdt = kinfo->fdt;
    int res = 0;
    __be32 *reg, *cells;
    const struct domain *d = kinfo->d;
    unsigned int i, len = 0;

    res = domain_fdt_begin_node(fdt, "interrupt-controller",
                                vgic_dist_base(&d->arch.vgic));
    if ( res )
        return res;

    res = fdt_property_cell(fdt, "#address-cells", 0);
    if ( res )
        return res;

    res = fdt_property_cell(fdt, "#interrupt-cells", 3);
    if ( res )
        return res;

    res = fdt_property(fdt, "interrupt-controller", NULL, 0);
    if ( res )
        return res;

    res = fdt_property_string(fdt, "compatible", "arm,gic-v3");
    if ( res )
        return res;

    /* reg specifies all re-distributors and Distributor. */
    len = (GUEST_ROOT_ADDRESS_CELLS + GUEST_ROOT_SIZE_CELLS) *
          (d->arch.vgic.nr_regions + 1) * sizeof(__be32);
    reg = xmalloc_bytes(len);
    if ( reg == NULL )
        return -ENOMEM;
    cells = reg;

    dt_child_set_range(&cells, GUEST_ROOT_ADDRESS_CELLS, GUEST_ROOT_SIZE_CELLS,
                       vgic_dist_base(&d->arch.vgic), GUEST_GICV3_GICD_SIZE);

    for ( i = 0; i < d->arch.vgic.nr_regions; i++ )
        dt_child_set_range(&cells,
                           GUEST_ROOT_ADDRESS_CELLS, GUEST_ROOT_SIZE_CELLS,
                           d->arch.vgic.rdist_regions[i].base,
                           d->arch.vgic.rdist_regions[i].size);

    res = fdt_property(fdt, "reg", reg, len);
    xfree(reg);
    if (res)
        return res;

    res = fdt_property_cell(fdt, "linux,phandle", kinfo->phandle_gic);
    if (res)
        return res;

    res = fdt_property_cell(fdt, "phandle", kinfo->phandle_gic);
    if (res)
        return res;

    res = fdt_end_node(fdt);

    return res;
}
#endif

static int __init make_gic_domU_node(struct kernel_info *kinfo)
{
    switch ( kinfo->d->arch.vgic.version )
    {
#ifdef CONFIG_GICV3
    case GIC_V3:
        return make_gicv3_domU_node(kinfo);
#endif
#ifdef CONFIG_VGICV2
#endif
    default:
        panic("Unsupported GIC version\n");
    }
}

#ifdef CONFIG_SBSA_VUART_CONSOLE
static int __init make_vpl011_uart_node(struct kernel_info *kinfo)
{
    void *fdt = kinfo->fdt;
    int res;
    gic_interrupt_t intr;
    __be32 reg[GUEST_ROOT_ADDRESS_CELLS + GUEST_ROOT_SIZE_CELLS];
    __be32 *cells;
    struct domain *d = kinfo->d;

    res = domain_fdt_begin_node(fdt, "sbsa-uart", d->arch.vpl011.base_addr);
    if ( res )
        return res;

    res = fdt_property_string(fdt, "compatible", "arm,sbsa-uart");
    if ( res )
        return res;

    cells = &reg[0];
    dt_child_set_range(&cells, GUEST_ROOT_ADDRESS_CELLS,
                       GUEST_ROOT_SIZE_CELLS, d->arch.vpl011.base_addr,
                       GUEST_PL011_SIZE);

    res = fdt_property(fdt, "reg", reg, sizeof(reg));
    if ( res )
        return res;

    set_interrupt(intr, d->arch.vpl011.virq, 0xf, DT_IRQ_TYPE_LEVEL_HIGH);

    res = fdt_property(fdt, "interrupts", intr, sizeof (intr));
    if ( res )
        return res;

    res = fdt_property_cell(fdt, "interrupt-parent",
                            kinfo->phandle_gic);
    if ( res )
        return res;

    /* Use a default baud rate of 115200. */
    fdt_property_u32(fdt, "current-speed", 115200);

    res = fdt_end_node(fdt);
    if ( res )
        return res;

    return 0;
}
#endif

/*
 * Scan device tree properties for passthrough specific information.
 * Returns < 0 on error
 *         0 on success
 */
static int __init handle_passthrough_prop(struct kernel_info *kinfo,
                                          const struct fdt_property *xen_reg,
                                          const struct fdt_property *xen_path,
                                          bool xen_force,
                                          uint32_t address_cells,
                                          uint32_t size_cells)
{
    const __be32 *cell;
    unsigned int i, len;
    struct dt_device_node *node;
    int res;
    paddr_t mstart, size, gstart;

    /* xen,reg specifies where to map the MMIO region */
    cell = (const __be32 *)xen_reg->data;
    len = fdt32_to_cpu(xen_reg->len) / ((address_cells * 2 + size_cells) *
                                        sizeof(uint32_t));

    for ( i = 0; i < len; i++ )
    {
        device_tree_get_reg(&cell, address_cells, size_cells,
                            &mstart, &size);
        gstart = dt_next_cell(address_cells, &cell);

        if ( gstart & ~PAGE_MASK || mstart & ~PAGE_MASK || size & ~PAGE_MASK )
        {
            printk(XENLOG_ERR
                   "DomU passthrough config has not page aligned addresses/sizes\n");
            return -EINVAL;
        }

        res = iomem_permit_access(kinfo->d, paddr_to_pfn(mstart),
                                  paddr_to_pfn(PAGE_ALIGN(mstart + size - 1)));
        if ( res )
        {
            printk(XENLOG_ERR "Unable to permit to dom%d access to"
                   " 0x%"PRIpaddr" - 0x%"PRIpaddr"\n",
                   kinfo->d->domain_id,
                   mstart & PAGE_MASK, PAGE_ALIGN(mstart + size) - 1);
            return res;
        }

        res = map_regions_p2mt(kinfo->d,
                               gaddr_to_gfn(gstart),
                               PFN_DOWN(size),
                               maddr_to_mfn(mstart),
                               p2m_mmio_direct_dev);
        if ( res < 0 )
        {
            printk(XENLOG_ERR
                   "Failed to map %"PRIpaddr" to the guest at%"PRIpaddr"\n",
                   mstart, gstart);
            return -EFAULT;
        }
    }

    /*
     * If xen_force, we let the user assign a MMIO region with no
     * associated path.
     */
    if ( xen_path == NULL )
        return xen_force ? 0 : -EINVAL;

    /*
     * xen,path specifies the corresponding node in the host DT.
     * Both interrupt mappings and IOMMU settings are based on it,
     * as they are done based on the corresponding host DT node.
     */
    node = dt_find_node_by_path(xen_path->data);
    if ( node == NULL )
    {
        printk(XENLOG_ERR "Couldn't find node %s in host_dt!\n",
               xen_path->data);
        return -EINVAL;
    }

    res = map_device_irqs_to_domain(kinfo->d, node, true, NULL);
    if ( res < 0 )
        return res;

    res = iommu_add_dt_device(node);
    if ( res < 0 )
        return res;

    /* If xen_force, we allow assignment of devices without IOMMU protection. */
    if ( xen_force && !dt_device_is_protected(node) )
        return 0;

    return iommu_assign_dt_device(kinfo->d, node);
}

static int __init handle_prop_pfdt(struct kernel_info *kinfo,
                                   const void *pfdt, int nodeoff,
                                   uint32_t address_cells, uint32_t size_cells,
                                   bool scan_passthrough_prop)
{
    void *fdt = kinfo->fdt;
    int propoff, nameoff, res;
    const struct fdt_property *prop, *xen_reg = NULL, *xen_path = NULL;
    const char *name;
    bool found, xen_force = false;

    for ( propoff = fdt_first_property_offset(pfdt, nodeoff);
          propoff >= 0;
          propoff = fdt_next_property_offset(pfdt, propoff) )
    {
        if ( !(prop = fdt_get_property_by_offset(pfdt, propoff, NULL)) )
            return -FDT_ERR_INTERNAL;

        found = false;
        nameoff = fdt32_to_cpu(prop->nameoff);
        name = fdt_string(pfdt, nameoff);

        if ( scan_passthrough_prop )
        {
            if ( dt_prop_cmp("xen,reg", name) == 0 )
            {
                xen_reg = prop;
                found = true;
            }
            else if ( dt_prop_cmp("xen,path", name) == 0 )
            {
                xen_path = prop;
                found = true;
            }
            else if ( dt_prop_cmp("xen,force-assign-without-iommu",
                                  name) == 0 )
            {
                xen_force = true;
                found = true;
            }
        }

        /*
         * Copy properties other than the ones above: xen,reg, xen,path,
         * and xen,force-assign-without-iommu.
         */
        if ( !found )
        {
            res = fdt_property(fdt, name, prop->data, fdt32_to_cpu(prop->len));
            if ( res )
                return res;
        }
    }

    /*
     * Only handle passthrough properties if both xen,reg and xen,path
     * are present, or if xen,force-assign-without-iommu is specified.
     */
    if ( xen_reg != NULL && (xen_path != NULL || xen_force) )
    {
        res = handle_passthrough_prop(kinfo, xen_reg, xen_path, xen_force,
                                      address_cells, size_cells);
        if ( res < 0 )
        {
            printk(XENLOG_ERR "Failed to assign device to %pd\n", kinfo->d);
            return res;
        }
    }
    else if ( (xen_path && !xen_reg) || (xen_reg && !xen_path && !xen_force) )
    {
        printk(XENLOG_ERR "xen,reg or xen,path missing for %pd\n",
               kinfo->d);
        return -EINVAL;
    }

    /* FDT_ERR_NOTFOUND => There is no more properties for this node */
    return ( propoff != -FDT_ERR_NOTFOUND ) ? propoff : 0;
}

static int __init scan_pfdt_node(struct kernel_info *kinfo, const void *pfdt,
                                 int nodeoff,
                                 uint32_t address_cells, uint32_t size_cells,
                                 bool scan_passthrough_prop)
{
    int rc = 0;
    void *fdt = kinfo->fdt;
    int node_next;

    rc = fdt_begin_node(fdt, fdt_get_name(pfdt, nodeoff, NULL));
    if ( rc )
        return rc;

    rc = handle_prop_pfdt(kinfo, pfdt, nodeoff, address_cells, size_cells,
                          scan_passthrough_prop);
    if ( rc )
        return rc;

    address_cells = device_tree_get_u32(pfdt, nodeoff, "#address-cells",
                                        DT_ROOT_NODE_ADDR_CELLS_DEFAULT);
    size_cells = device_tree_get_u32(pfdt, nodeoff, "#size-cells",
                                     DT_ROOT_NODE_SIZE_CELLS_DEFAULT);

    node_next = fdt_first_subnode(pfdt, nodeoff);
    while ( node_next > 0 )
    {
        rc = scan_pfdt_node(kinfo, pfdt, node_next, address_cells, size_cells,
                            scan_passthrough_prop);
        if ( rc )
            return rc;

        node_next = fdt_next_subnode(pfdt, node_next);
    }

    return fdt_end_node(fdt);
}

static int __init check_partial_fdt(void *pfdt, size_t size)
{
    int res;

    if ( fdt_magic(pfdt) != FDT_MAGIC )
    {
        dprintk(XENLOG_ERR, "Partial FDT is not a valid Flat Device Tree");
        return -EINVAL;
    }

    res = fdt_check_header(pfdt);
    if ( res )
    {
        dprintk(XENLOG_ERR, "Failed to check the partial FDT (%d)", res);
        return -EINVAL;
    }

    if ( fdt_totalsize(pfdt) > size )
    {
        dprintk(XENLOG_ERR, "Partial FDT totalsize is too big");
        return -EINVAL;
    }

    return 0;
}

static int __init domain_handle_dtb_bootmodule(struct domain *d,
                                               struct kernel_info *kinfo)
{
    void *pfdt;
    int res, node_next;

    pfdt = ioremap_cache(kinfo->dtb_bootmodule->start,
                         kinfo->dtb_bootmodule->size);
    if ( pfdt == NULL )
        return -EFAULT;

    res = check_partial_fdt(pfdt, kinfo->dtb_bootmodule->size);
    if ( res < 0 )
        goto out;

    for ( node_next = fdt_first_subnode(pfdt, 0);
          node_next > 0;
          node_next = fdt_next_subnode(pfdt, node_next) )
    {
        const char *name = fdt_get_name(pfdt, node_next, NULL);

        if ( name == NULL )
            continue;

        /*
         * Only scan /gic /aliases /passthrough, ignore the rest.
         * They don't have to be parsed in order.
         *
         * Take the GIC phandle value from the special /gic node in the
         * DTB fragment.
         */
        if ( dt_node_cmp(name, "gic") == 0 )
        {
            kinfo->phandle_gic = fdt_get_phandle(pfdt, node_next);
            continue;
        }

        if ( dt_node_cmp(name, "aliases") == 0 )
        {
            res = scan_pfdt_node(kinfo, pfdt, node_next,
                                 DT_ROOT_NODE_ADDR_CELLS_DEFAULT,
                                 DT_ROOT_NODE_SIZE_CELLS_DEFAULT,
                                 false);
            if ( res )
                goto out;
            continue;
        }
        if ( dt_node_cmp(name, "passthrough") == 0 )
        {
            res = scan_pfdt_node(kinfo, pfdt, node_next,
                                 DT_ROOT_NODE_ADDR_CELLS_DEFAULT,
                                 DT_ROOT_NODE_SIZE_CELLS_DEFAULT,
                                 true);
            if ( res )
                goto out;
            continue;
        }
    }

 out:
    iounmap(pfdt);

    return res;
}

/*
 * The max size for DT is 2MB. However, the generated DT is small (not including
 * domU passthrough DT nodes whose size we account separately), 4KB are enough
 * for now, but we might have to increase it in the future.
 */
#define DOMU_DTB_SIZE 4096
static int __init prepare_dtb_domU(struct domain *d, struct kernel_info *kinfo)
{
    int addrcells, sizecells;
    int ret, fdt_size = DOMU_DTB_SIZE;

    kinfo->phandle_gic = GUEST_PHANDLE_GIC;
    kinfo->gnttab_start = GUEST_GNTTAB_BASE;
    kinfo->gnttab_size = GUEST_GNTTAB_SIZE;

    addrcells = GUEST_ROOT_ADDRESS_CELLS;
    sizecells = GUEST_ROOT_SIZE_CELLS;

    /* Account for domU passthrough DT size */
    if ( kinfo->dtb_bootmodule )
        fdt_size += kinfo->dtb_bootmodule->size;

    /* Cap to max DT size if needed */
    fdt_size = min(fdt_size, SZ_2M);

    kinfo->fdt = xmalloc_bytes(fdt_size);
    if ( kinfo->fdt == NULL )
        return -ENOMEM;

    ret = fdt_create(kinfo->fdt, fdt_size);
    if ( ret < 0 )
        goto err;

    ret = fdt_finish_reservemap(kinfo->fdt);
    if ( ret < 0 )
        goto err;

    ret = fdt_begin_node(kinfo->fdt, "");
    if ( ret < 0 )
        goto err;

    ret = fdt_property_cell(kinfo->fdt, "#address-cells", addrcells);
    if ( ret )
        goto err;

    ret = fdt_property_cell(kinfo->fdt, "#size-cells", sizecells);
    if ( ret )
        goto err;

    ret = make_chosen_node(kinfo);
    if ( ret )
        goto err;

    ret = make_psci_node(kinfo->fdt);
    if ( ret )
        goto err;

    ret = make_cpus_node(d, kinfo->fdt);
    if ( ret )
        goto err;

    ret = make_memory_node(kinfo, addrcells, sizecells,
                           kernel_info_get_mem(kinfo));
    if ( ret )
        goto err;

    ret = make_resv_memory_node(kinfo, addrcells, sizecells);
    if ( ret )
        goto err;

    /*
     * domain_handle_dtb_bootmodule has to be called before the rest of
     * the device tree is generated because it depends on the value of
     * the field phandle_gic.
     */
    if ( kinfo->dtb_bootmodule )
    {
        ret = domain_handle_dtb_bootmodule(d, kinfo);
        if ( ret )
            goto err;
    }

    ret = make_gic_domU_node(kinfo);
    if ( ret )
        goto err;

    ret = make_timer_node(kinfo);
    if ( ret )
        goto err;

    if ( kinfo->vpl011 )
    {
        ret = -EINVAL;
#ifdef CONFIG_SBSA_VUART_CONSOLE
        ret = make_vpl011_uart_node(kinfo);
#endif
        if ( ret )
            goto err;
    }

    if ( kinfo->dom0less_feature & DOM0LESS_ENHANCED_NO_XS )
    {
        ret = make_hypervisor_node(d, kinfo, addrcells, sizecells);
        if ( ret )
            goto err;
    }

    ret = fdt_end_node(kinfo->fdt);
    if ( ret < 0 )
        goto err;

    ret = fdt_finish(kinfo->fdt);
    if ( ret < 0 )
        goto err;

    return 0;

  err:
    printk("Device tree generation failed (%d).\n", ret);
    xfree(kinfo->fdt);

    return -EINVAL;
}

static unsigned long __init domain_p2m_pages(unsigned long maxmem_kb,
                                             unsigned int smp_cpus)
{
    /*
     * Keep in sync with libxl__get_required_paging_memory().
     * 256 pages (1MB) per vcpu, plus 1 page per MiB of RAM for the P2M map,
     * plus 128 pages to cover extended regions.
     */
    unsigned long memkb = 4 * (256 * smp_cpus + (maxmem_kb / 1024) + 128);

    BUILD_BUG_ON(PAGE_SIZE != SZ_4K);

    return DIV_ROUND_UP(memkb, 1024) << (20 - PAGE_SHIFT);
}

static int __init alloc_xenstore_evtchn(struct domain *d)
{
    evtchn_alloc_unbound_t alloc;
    int rc;

    alloc.dom = d->domain_id;
    alloc.remote_dom = hardware_domain->domain_id;
    rc = evtchn_alloc_unbound(&alloc, 0);
    if ( rc )
    {
        printk("Failed allocating event channel for domain\n");
        return rc;
    }

    d->arch.hvm.params[HVM_PARAM_STORE_EVTCHN] = alloc.port;

    return 0;
}

static int __init construct_domU(struct domain *d,
                                 const struct dt_device_node *node)
{
    struct kernel_info kinfo = KERNEL_INFO_INIT;
    const char *dom0less_enhanced;
    int rc;
    u64 mem;
    u32 p2m_mem_mb;
    unsigned long p2m_pages;

    rc = dt_property_read_u64(node, "memory", &mem);
    if ( !rc )
    {
        printk("Error building DomU: cannot read \"memory\" property\n");
        return -EINVAL;
    }
    kinfo.unassigned_mem = (paddr_t)mem * SZ_1K;

    rc = dt_property_read_u32(node, "xen,domain-p2m-mem-mb", &p2m_mem_mb);
    /* If xen,domain-p2m-mem-mb is not specified, use the default value. */
    p2m_pages = rc ?
                p2m_mem_mb << (20 - PAGE_SHIFT) :
                domain_p2m_pages(mem, d->max_vcpus);

    spin_lock(&d->arch.paging.lock);
    rc = p2m_set_allocation(d, p2m_pages, NULL);
    spin_unlock(&d->arch.paging.lock);
    if ( rc != 0 )
        return rc;

    printk("*** LOADING DOMU cpus=%u memory=%#"PRIx64"KB ***\n",
           d->max_vcpus, mem);

    kinfo.vpl011 = dt_property_read_bool(node, "vpl011");

    rc = dt_property_read_string(node, "xen,enhanced", &dom0less_enhanced);
    if ( rc == -EILSEQ ||
         rc == -ENODATA ||
         (rc == 0 && !strcmp(dom0less_enhanced, "enabled")) )
    {
        if ( hardware_domain )
            kinfo.dom0less_feature = DOM0LESS_ENHANCED;
        else
            panic("At the moment, Xenstore support requires dom0 to be present\n");
    }
    else if ( rc == 0 && !strcmp(dom0less_enhanced, "no-xenstore") )
        kinfo.dom0less_feature = DOM0LESS_ENHANCED_NO_XS;

    if ( vcpu_create(d, 0) == NULL )
        return -ENOMEM;

    d->max_pages = ((paddr_t)mem * SZ_1K) >> PAGE_SHIFT;

    kinfo.d = d;

    rc = kernel_probe(&kinfo, node);
    if ( rc < 0 )
        return rc;

#ifdef CONFIG_ARM_64
    /* type must be set before allocate memory */
    d->arch.type = kinfo.type;
#endif
    if ( !dt_find_property(node, "xen,static-mem", NULL) )
        allocate_memory(d, &kinfo);
    else if ( !is_domain_direct_mapped(d) )
        allocate_static_memory(d, &kinfo, node);
    else
        assign_static_memory_11(d, &kinfo, node);

    rc = process_shm(d, &kinfo, node);
    if ( rc < 0 )
        return rc;

    /*
     * Base address and irq number are needed when creating vpl011 device
     * tree node in prepare_dtb_domU, so initialization on related variables
     * shall be done first.
     */
    if ( kinfo.vpl011 )
    {
        rc = domain_vpl011_init(d, NULL);
        if ( rc < 0 )
            return rc;
    }

    rc = prepare_dtb_domU(d, &kinfo);
    if ( rc < 0 )
        return rc;

    rc = construct_domain(d, &kinfo);
    if ( rc < 0 )
        return rc;

    if ( kinfo.dom0less_feature & DOM0LESS_XENSTORE )
    {
        ASSERT(hardware_domain);
        rc = alloc_xenstore_evtchn(d);
        if ( rc < 0 )
            return rc;
        d->arch.hvm.params[HVM_PARAM_STORE_PFN] = ~0ULL;
    }

    return rc;
}

void __init create_domUs(void)
{
    struct dt_device_node *node;
    const char *dom0less_iommu;
    bool iommu = false;
    const struct dt_device_node *cpupool_node,
                                *chosen = dt_find_node_by_path("/chosen");

    BUG_ON(chosen == NULL);
    dt_for_each_child_node(chosen, node)
    {
        struct domain *d;
        struct xen_domctl_createdomain d_cfg = {
            .arch.gic_version = XEN_DOMCTL_CONFIG_GIC_NATIVE,
            .flags = XEN_DOMCTL_CDF_hvm | XEN_DOMCTL_CDF_hap,
            /*
             * The default of 1023 should be sufficient for guests because
             * on ARM we don't bind physical interrupts to event channels.
             * The only use of the evtchn port is inter-domain communications.
             * 1023 is also the default value used in libxl.
             */
            .max_evtchn_port = 1023,
            .max_grant_frames = -1,
            .max_maptrack_frames = -1,
            .grant_opts = XEN_DOMCTL_GRANT_version(opt_gnttab_max_version),
        };
        unsigned int flags = 0U;
        uint32_t val;
        int rc;

        if ( !dt_device_is_compatible(node, "xen,domain") )
            continue;

        if ( (max_init_domid + 1) >= DOMID_FIRST_RESERVED )
            panic("No more domain IDs available\n");

        if ( dt_find_property(node, "xen,static-mem", NULL) )
            flags |= CDF_staticmem;

        if ( dt_property_read_bool(node, "direct-map") )
        {
            if ( !(flags & CDF_staticmem) )
                panic("direct-map is not valid for domain %s without static allocation.\n",
                      dt_node_name(node));

            flags |= CDF_directmap;
        }

        if ( !dt_property_read_u32(node, "cpus", &d_cfg.max_vcpus) )
            panic("Missing property 'cpus' for domain %s\n",
                  dt_node_name(node));

        if ( !dt_property_read_string(node, "passthrough", &dom0less_iommu) &&
             !strcmp(dom0less_iommu, "enabled") )
            iommu = true;

        if ( iommu_enabled &&
             (iommu || dt_find_compatible_node(node, NULL,
                                               "multiboot,device-tree")) )
            d_cfg.flags |= XEN_DOMCTL_CDF_iommu;

        if ( !dt_property_read_u32(node, "nr_spis", &d_cfg.arch.nr_spis) )
        {
            int vpl011_virq = GUEST_VPL011_SPI;

            d_cfg.arch.nr_spis = gic_number_lines() - 32;

            /*
             * The VPL011 virq is GUEST_VPL011_SPI, unless direct-map is
             * set, in which case it'll match the hardware.
             *
             * Since the domain is not yet created, we can't use
             * d->arch.vpl011.irq. So the logic to find the vIRQ has to
             * be hardcoded.
             * The logic here shall be consistent with the one in
             * domain_vpl011_init().
             */
            if ( flags & CDF_directmap )
            {
                vpl011_virq = serial_irq(SERHND_DTUART);
                if ( vpl011_virq < 0 )
                    panic("Error getting IRQ number for this serial port %d\n",
                          SERHND_DTUART);
            }

            /*
             * vpl011 uses one emulated SPI. If vpl011 is requested, make
             * sure that we allocate enough SPIs for it.
             */
            if ( dt_property_read_bool(node, "vpl011") )
                d_cfg.arch.nr_spis = MAX(d_cfg.arch.nr_spis,
                                         vpl011_virq - 32 + 1);
        }

        /* Get the optional property domain-cpupool */
        cpupool_node = dt_parse_phandle(node, "domain-cpupool", 0);
        if ( cpupool_node )
        {
            int pool_id = btcpupools_get_domain_pool_id(cpupool_node);
            if ( pool_id < 0 )
                panic("Error getting cpupool id from domain-cpupool (%d)\n",
                      pool_id);
            d_cfg.cpupool_id = pool_id;
        }

        if ( dt_property_read_u32(node, "max_grant_version", &val) )
            d_cfg.grant_opts = XEN_DOMCTL_GRANT_version(val);

        if ( dt_property_read_u32(node, "max_grant_frames", &val) )
        {
            if ( val > INT32_MAX )
                panic("max_grant_frames (%"PRIu32") overflow\n", val);
            d_cfg.max_grant_frames = val;
        }

        if ( dt_property_read_u32(node, "max_maptrack_frames", &val) )
        {
            if ( val > INT32_MAX )
                panic("max_maptrack_frames (%"PRIu32") overflow\n", val);
            d_cfg.max_maptrack_frames = val;
        }

        if ( dt_get_property(node, "sve", &val) )
        {
#ifdef CONFIG_ARM64_SVE
            unsigned int sve_vl_bits;
            bool ret = false;

            if ( !val )
            {
                /* Property found with no value, means max HW VL supported */
                ret = sve_domctl_vl_param(-1, &sve_vl_bits);
            }
            else
            {
                if ( dt_property_read_u32(node, "sve", &val) )
                    ret = sve_domctl_vl_param(val, &sve_vl_bits);
                else
                    panic("Error reading 'sve' property\n");
            }

            if ( ret )
                d_cfg.arch.sve_vl = sve_encode_vl(sve_vl_bits);
            else
                panic("SVE vector length error\n");
#else
            panic("'sve' property found, but CONFIG_ARM64_SVE not selected\n");
#endif
        }

        /*
         * The variable max_init_domid is initialized with zero, so here it's
         * very important to use the pre-increment operator to call
         * domain_create() with a domid > 0. (domid == 0 is reserved for Dom0)
         */
        d = domain_create(++max_init_domid, &d_cfg, flags);
        if ( IS_ERR(d) )
            panic("Error creating domain %s (rc = %ld)\n",
                  dt_node_name(node), PTR_ERR(d));

        d->is_console = true;
        dt_device_set_used_by(node, d->domain_id);

        rc = construct_domU(d, node);
        if ( rc )
            panic("Could not set up domain %s (rc = %d)\n",
                  dt_node_name(node), rc);
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

/* === END INLINED: dom0less-build.c === */
/* Xen scheduler policies removed - PRTOS uses its own cyclic scheduler */
/* === BEGIN INLINED: core.c === */
#include <xen_xen_config.h>
/****************************************************************************
 * (C) 2002-2003 - Rolf Neugebauer - Intel Research Cambridge
 * (C) 2002-2003 University of Cambridge
 * (C) 2004      - Mark Williamson - Intel Research Cambridge
 ****************************************************************************
 *
 *        File: common/schedule.c
 *      Author: Rolf Neugebauer & Keir Fraser
 *              Updated for generic API by Mark Williamson
 *
 * Description: Generic CPU scheduling code
 *              implements support functionality for the Xen scheduler API.
 *
 */

#ifndef COMPAT
#include <xen_init.h>
#include <xen_lib.h>
#include <xen_param.h>
#include <xen_sched.h>
#include <xen_domain.h>
#include <xen_delay.h>
#include <xen_event.h>
#include <xen_time.h>
#include <xen_timer.h>
#include <xen_perfc.h>
#include <xen_softirq.h>
#include <xen_trace.h>
#include <xen_mm.h>
#include <xen_err.h>
#include <xen_guest_access.h>
#include <xen_hypercall.h>
#include <xen_multicall.h>
#include <xen_cpu.h>
#include <xen_preempt.h>
#include <xen_event.h>
#include <public_sched.h>
#include <xsm_xsm.h>
#include <xen_err.h>

#include "private.h"

#ifdef CONFIG_XEN_GUEST
#include <asm/guest.h>
#else
#define pv_shim false
#endif

/* opt_sched: scheduler - default to configured value */
static char __initdata opt_sched[10] = CONFIG_SCHED_DEFAULT;
string_param("sched", opt_sched);

/* if sched_smt_power_savings is set,
 * scheduler will give preferrence to partially idle package compared to
 * the full idle package, when picking pCPU to schedule vCPU.
 */
bool sched_smt_power_savings;
boolean_param("sched_smt_power_savings", sched_smt_power_savings);

/* Default scheduling rate limit: 1ms
 * The behavior when sched_ratelimit_us is greater than sched_credit_tslice_ms is undefined
 * */
int sched_ratelimit_us = SCHED_DEFAULT_RATELIMIT_US;
integer_param("sched_ratelimit_us", sched_ratelimit_us);

/* Number of vcpus per struct sched_unit. */
bool __read_mostly sched_disable_smt_switching;
cpumask_t sched_res_mask;

/* Common lock for free cpus. */
static DEFINE_SPINLOCK(sched_free_cpu_lock);

/* Various timer handlers. */
static void cf_check s_timer_fn(void *unused);
static void cf_check vcpu_periodic_timer_fn(void *data);
static void cf_check vcpu_singleshot_timer_fn(void *data);
static void cf_check poll_timer_fn(void *data);

/* This is global for now so that private implementations can reach it */
DEFINE_PER_CPU_READ_MOSTLY(struct sched_resource *, sched_res);
static DEFINE_PER_CPU_READ_MOSTLY(unsigned int, sched_res_idx);
DEFINE_RCU_READ_LOCK(sched_res_rculock);

/* Scratch space for cpumasks. */
DEFINE_PER_CPU(cpumask_t, cpumask_scratch);

/* How many urgent vcpus. */
DEFINE_PER_CPU(atomic_t, sched_urgent_count);

/* PRTOS uses its own cyclic scheduler - no Xen scheduler policies registered */

static struct scheduler __read_mostly operations;

static bool scheduler_active;

static void sched_set_affinity(
    struct sched_unit *unit, const cpumask_t *hard, const cpumask_t *soft);

static struct sched_resource *cf_check
sched_idle_res_pick(const struct scheduler *ops, const struct sched_unit *unit)
{
    return unit->res;
}

static void *cf_check
sched_idle_alloc_udata(const struct scheduler *ops, struct sched_unit *unit,
                       void *dd)
{
    /* Any non-NULL pointer is fine here. */
    return ZERO_BLOCK_PTR;
}

static void cf_check
sched_idle_free_udata(const struct scheduler *ops, void *priv)
{
}

static void cf_check sched_idle_schedule(
    const struct scheduler *ops, struct sched_unit *unit, s_time_t now,
    bool tasklet_work_scheduled)
{
    const unsigned int cpu = smp_processor_id();

    unit->next_time = -1;
    unit->next_task = sched_idle_unit(cpu);
}

static struct scheduler sched_idle_ops = {
    .name           = "Idle Scheduler",
    .opt_name       = "idle",
    .sched_data     = NULL,

    .pick_resource  = sched_idle_res_pick,
    .do_schedule    = sched_idle_schedule,

    .alloc_udata    = sched_idle_alloc_udata,
    .free_udata     = sched_idle_free_udata,
};

static inline struct vcpu *unit2vcpu_cpu(const struct sched_unit *unit,
                                         unsigned int cpu)
{
    unsigned int idx = unit->unit_id + per_cpu(sched_res_idx, cpu);
    const struct domain *d = unit->domain;

    return (idx < d->max_vcpus) ? d->vcpu[idx] : NULL;
}

static inline struct vcpu *sched_unit2vcpu_cpu(const struct sched_unit *unit,
                                               unsigned int cpu)
{
    struct vcpu *v = unit2vcpu_cpu(unit, cpu);

    return (v && v->new_state == RUNSTATE_running) ? v : idle_vcpu[cpu];
}

static inline struct scheduler *dom_scheduler(const struct domain *d)
{
    if ( likely(d->cpupool != NULL) )
        return d->cpupool->sched;

    /*
     * If d->cpupool is NULL, this is the idle domain. This is special
     * because the idle domain does not really belong to any cpupool, and,
     * hence, does not really have a scheduler.
     *
     * This is (should be!) only called like this for allocating the idle
     * vCPUs for the first time, during boot, in which case what we want
     * is the default scheduler that has been, choosen at boot.
     */
    ASSERT(is_idle_domain(d));
    return &operations;
}

static inline struct scheduler *unit_scheduler(const struct sched_unit *unit)
{
    const struct domain *d = unit->domain;

    if ( likely(d->cpupool != NULL) )
        return d->cpupool->sched;

    /*
     * If d->cpupool is NULL, this is a unit of the idle domain. And this
     * case is special because the idle domain does not really belong to
     * a cpupool and, hence, doesn't really have a scheduler). In fact, its
     * units (may) run on pCPUs which are in different pools, with different
     * schedulers.
     *
     * What we want, in this case, is the scheduler of the pCPU where this
     * particular idle unit is running. And, since unit->res never changes
     * for idle units, it is safe to use it, with no locks, to figure that out.
     */

    ASSERT(is_idle_domain(d));
    return unit->res->scheduler;
}

static inline struct scheduler *vcpu_scheduler(const struct vcpu *v)
{
    return unit_scheduler(v->sched_unit);
}
#define VCPU2ONLINE(_v) cpupool_domain_master_cpumask((_v)->domain)

static inline void trace_runstate_change(const struct vcpu *v, int new_state)
{
    struct { uint16_t vcpu, domain; } d;
    uint32_t event;

    if ( likely(!tb_init_done) )
        return;

    d.vcpu = v->vcpu_id;
    d.domain = v->domain->domain_id;

    event = TRC_SCHED_RUNSTATE_CHANGE;
    event |= ( v->runstate.state & 0x3 ) << 8;
    event |= ( new_state & 0x3 ) << 4;

    trace_time(event, sizeof(d), &d);
}

static inline void trace_continue_running(const struct vcpu *v)
{
    struct { uint16_t vcpu, domain; } d;

    if ( likely(!tb_init_done) )
        return;

    d.vcpu = v->vcpu_id;
    d.domain = v->domain->domain_id;

    trace_time(TRC_SCHED_CONTINUE_RUNNING, sizeof(d), &d);
}

static inline void vcpu_urgent_count_update(struct vcpu *v)
{
    if ( is_idle_vcpu(v) )
        return;

    if ( unlikely(v->is_urgent) )
    {
        if ( !(v->pause_flags & VPF_blocked) ||
             !test_bit(v->vcpu_id, v->domain->poll_mask) )
        {
            v->is_urgent = 0;
            atomic_dec(&per_cpu(sched_urgent_count, v->processor));
        }
    }
    else
    {
        if ( unlikely(v->pause_flags & VPF_blocked) &&
             unlikely(test_bit(v->vcpu_id, v->domain->poll_mask)) )
        {
            v->is_urgent = 1;
            atomic_inc(&per_cpu(sched_urgent_count, v->processor));
        }
    }
}

static inline void vcpu_runstate_change(
    struct vcpu *v, int new_state, s_time_t new_entry_time)
{
    s_time_t delta;
    struct sched_unit *unit = v->sched_unit;

    ASSERT(spin_is_locked(get_sched_res(v->processor)->schedule_lock));
    if ( v->runstate.state == new_state )
        return;

    vcpu_urgent_count_update(v);

    trace_runstate_change(v, new_state);

    if ( !is_idle_vcpu(v) )
    {
        unit->runstate_cnt[v->runstate.state]--;
        unit->runstate_cnt[new_state]++;
    }

    delta = new_entry_time - v->runstate.state_entry_time;
    if ( delta > 0 )
    {
        v->runstate.time[v->runstate.state] += delta;
        v->runstate.state_entry_time = new_entry_time;
    }

    v->runstate.state = new_state;
}


void vcpu_runstate_get(const struct vcpu *v,
                       struct vcpu_runstate_info *runstate)
{
    spinlock_t *lock;
    s_time_t delta;
    struct sched_unit *unit;

    rcu_read_lock(&sched_res_rculock);

    /*
     * Be careful in case of an idle vcpu: the assignment to a unit might
     * change even with the scheduling lock held, so be sure to use the
     * correct unit for locking in order to avoid triggering an ASSERT() in
     * the unlock function.
     */
    unit = is_idle_vcpu(v) ? get_sched_res(v->processor)->sched_unit_idle
                           : v->sched_unit;
    lock = likely(v == current) ? NULL : unit_schedule_lock_irq(unit);
    memcpy(runstate, &v->runstate, sizeof(*runstate));
    delta = NOW() - runstate->state_entry_time;
    if ( delta > 0 )
        runstate->time[runstate->state] += delta;

    if ( unlikely(lock != NULL) )
        unit_schedule_unlock_irq(lock, unit);

    rcu_read_unlock(&sched_res_rculock);
}

uint64_t get_cpu_idle_time(unsigned int cpu)
{
    struct vcpu_runstate_info state = { 0 };
    const struct vcpu *v = idle_vcpu[cpu];

    if ( cpu_online(cpu) && get_sched_res(cpu) )
        vcpu_runstate_get(v, &state);

    return state.time[RUNSTATE_running];
}

/*
 * If locks are different, take the one with the lower address first.
 * This avoids dead- or live-locks when this code is running on both
 * cpus at the same time.
 */
static always_inline void sched_spin_lock_double(
    spinlock_t *lock1, spinlock_t *lock2, unsigned long *flags)
{
    /*
     * In order to avoid extra overhead, use the locking primitives without the
     * speculation barrier, and introduce a single barrier here.
     */
    if ( lock1 == lock2 )
    {
        *flags = _spin_lock_irqsave(lock1);
    }
    else if ( lock1 < lock2 )
    {
        *flags = _spin_lock_irqsave(lock1);
        _spin_lock(lock2);
    }
    else
    {
        *flags = _spin_lock_irqsave(lock2);
        _spin_lock(lock1);
    }
    block_lock_speculation();
}

static void sched_spin_unlock_double(spinlock_t *lock1, spinlock_t *lock2,
                                     unsigned long flags)
{
    if ( lock1 != lock2 )
        spin_unlock(lock2);
    spin_unlock_irqrestore(lock1, flags);
}

static void sched_free_unit_mem(struct sched_unit *unit)
{
    struct sched_unit *prev_unit;
    struct domain *d = unit->domain;

    if ( d->sched_unit_list == unit )
        d->sched_unit_list = unit->next_in_list;
    else
    {
        for_each_sched_unit ( d, prev_unit )
        {
            if ( prev_unit->next_in_list == unit )
            {
                prev_unit->next_in_list = unit->next_in_list;
                break;
            }
        }
    }

    free_cpumask_var(unit->cpu_hard_affinity);
    free_cpumask_var(unit->cpu_hard_affinity_saved);
    free_cpumask_var(unit->cpu_soft_affinity);

    xfree(unit);
}

static void sched_free_unit(struct sched_unit *unit, struct vcpu *v)
{
    const struct vcpu *vunit;
    unsigned int cnt = 0;

    /* Don't count to be released vcpu, might be not in vcpu list yet. */
    for_each_sched_unit_vcpu ( unit, vunit )
        if ( vunit != v )
            cnt++;

    v->sched_unit = NULL;
    unit->runstate_cnt[v->runstate.state]--;

    if ( unit->vcpu_list == v )
        unit->vcpu_list = v->next_in_list;

    if ( !cnt )
        sched_free_unit_mem(unit);
}

static void sched_unit_add_vcpu(struct sched_unit *unit, struct vcpu *v)
{
    v->sched_unit = unit;

    /* All but idle vcpus are allocated with sequential vcpu_id. */
    if ( !unit->vcpu_list || unit->vcpu_list->vcpu_id > v->vcpu_id )
    {
        unit->vcpu_list = v;
        /*
         * unit_id is always the same as lowest vcpu_id of unit.
         * This is used for stopping for_each_sched_unit_vcpu() loop and in
         * order to support cpupools with different granularities.
         */
        unit->unit_id = v->vcpu_id;
    }
    unit->runstate_cnt[v->runstate.state]++;
}

static struct sched_unit local_sched_unit_prtos[NR_CPUS];
static int local_sched_unit_prtos_index = 0;

static struct sched_unit *sched_alloc_unit_mem(void)
{
    struct sched_unit *unit;

    // unit = xzalloc(struct sched_unit);
    // if ( !unit )
    //     return NULL;

    unit = &local_sched_unit_prtos[local_sched_unit_prtos_index++];
    memset(unit, 0, sizeof(*unit));
    if (local_sched_unit_prtos_index > NR_CPUS) {
        printk("######### local_sched_unit_prtos_index overflow\n");
        return NULL;
    }

    if ( !zalloc_cpumask_var(&unit->cpu_hard_affinity) ||
         !zalloc_cpumask_var(&unit->cpu_hard_affinity_saved) ||
         !zalloc_cpumask_var(&unit->cpu_soft_affinity) )
    {
        sched_free_unit_mem(unit);
        unit = NULL;
    }

    return unit;
}

static void sched_domain_insert_unit(struct sched_unit *unit, struct domain *d)
{
    struct sched_unit **prev_unit;

    unit->domain = d;

    for ( prev_unit = &d->sched_unit_list; *prev_unit;
          prev_unit = &(*prev_unit)->next_in_list )
        if ( (*prev_unit)->next_in_list &&
             (*prev_unit)->next_in_list->unit_id > unit->unit_id )
            break;

    unit->next_in_list = *prev_unit;
    *prev_unit = unit;
}

static struct sched_unit *sched_alloc_unit(struct vcpu *v)
{
    struct sched_unit *unit;
    struct domain *d = v->domain;
    unsigned int gran = cpupool_get_granularity(d->cpupool);

    for_each_sched_unit ( d, unit )
        if ( unit->unit_id / gran == v->vcpu_id / gran )
            break;

    if ( unit )
    {
        sched_unit_add_vcpu(unit, v);
        return unit;
    }

    if ( (unit = sched_alloc_unit_mem()) == NULL )
        return NULL;

    sched_unit_add_vcpu(unit, v);
    sched_domain_insert_unit(unit, d);

    return unit;
}

static unsigned int sched_select_initial_cpu(const struct vcpu *v)
{
    const struct domain *d = v->domain;
    nodeid_t node;
    spinlock_t *lock;
    unsigned long flags;
    unsigned int cpu_ret, cpu = smp_processor_id();
    cpumask_t *cpus = cpumask_scratch_cpu(cpu);

    lock = pcpu_schedule_lock_irqsave(cpu, &flags);
    cpumask_clear(cpus);
    for_each_node_mask ( node, d->node_affinity )
        cpumask_or(cpus, cpus, &node_to_cpumask(node));
    cpumask_and(cpus, cpus, d->cpupool->cpu_valid);
    if ( cpumask_empty(cpus) )
        cpumask_copy(cpus, d->cpupool->cpu_valid);

    if ( v->vcpu_id == 0 )
        cpu_ret = cpumask_first(cpus);
    else
    {
        /* We can rely on previous vcpu being available. */
        ASSERT(!is_idle_domain(d));

        cpu_ret = cpumask_cycle(d->vcpu[v->vcpu_id - 1]->processor, cpus);
    }

    pcpu_schedule_unlock_irqrestore(lock, flags, cpu);

    return cpu_ret;
}

int sched_init_vcpu(struct vcpu *v)
{
    const struct domain *d = v->domain;
    struct sched_unit *unit;
    unsigned int processor;

    if ( (unit = sched_alloc_unit(v)) == NULL )
        return 1;

    if ( is_idle_domain(d) )
        processor = v->vcpu_id;
    else
        processor = sched_select_initial_cpu(v);

    /* Initialise the per-vcpu timers. */
    spin_lock_init(&v->periodic_timer_lock);
    init_timer(&v->periodic_timer, vcpu_periodic_timer_fn, v, processor);
    init_timer(&v->singleshot_timer, vcpu_singleshot_timer_fn, v, processor);
    init_timer(&v->poll_timer, poll_timer_fn, v, processor);

    /* If this is not the first vcpu of the unit we are done. */
    if ( unit->priv != NULL )
    {
        v->processor = processor;
        return 0;
    }

    rcu_read_lock(&sched_res_rculock);

    /* The first vcpu of an unit can be set via sched_set_res(). */
    sched_set_res(unit, get_sched_res(processor));

    unit->priv = sched_alloc_udata(dom_scheduler(d), unit, d->sched_priv);
    if ( unit->priv == NULL )
    {
        sched_free_unit(unit, v);
        rcu_read_unlock(&sched_res_rculock);
        return 1;
    }

    if ( is_idle_domain(d) )
    {
        /* Idle vCPUs are always pinned onto their respective pCPUs */
        sched_set_affinity(unit, cpumask_of(processor), &cpumask_all);
    }
    else if ( pv_shim && v->vcpu_id == 0 )
    {
        /*
         * PV-shim: vcpus are pinned 1:1. Initially only 1 cpu is online,
         * others will be dealt with when onlining them. This avoids pinning
         * a vcpu to a not yet online cpu here.
         */
        sched_set_affinity(unit, cpumask_of(0), cpumask_of(0));
    }
    else if ( d->domain_id == 0 && opt_dom0_vcpus_pin )
    {
        /*
         * If dom0_vcpus_pin is specified, dom0 vCPUs are pinned 1:1 to
         * their respective pCPUs too.
         */
        sched_set_affinity(unit, cpumask_of(processor), &cpumask_all);
    }
#ifdef CONFIG_X86
    else if ( d->domain_id == 0 )
    {
        /*
         * In absence of dom0_vcpus_pin instead, the hard and soft affinity of
         * dom0 is controlled by the (x86 only) dom0_nodes parameter. At this
         * point it has been parsed and decoded into the dom0_cpus mask.
         *
         * Note that we always honor what user explicitly requested, for both
         * hard and soft affinity, without doing any dynamic computation of
         * either of them.
         */
        if ( !dom0_affinity_relaxed )
            sched_set_affinity(unit, &dom0_cpus, &cpumask_all);
        else
            sched_set_affinity(unit, &cpumask_all, &dom0_cpus);
    }
#endif
    else
        sched_set_affinity(unit, &cpumask_all, &cpumask_all);

    /* Idle VCPUs are scheduled immediately, so don't put them in runqueue. */
    if ( is_idle_domain(d) )
    {
        get_sched_res(v->processor)->curr = unit;
        get_sched_res(v->processor)->sched_unit_idle = unit;
        v->is_running = true;
        unit->is_running = true;
        unit->state_entry_time = NOW();
    }
    else
    {
        sched_insert_unit(dom_scheduler(d), unit);
    }

    rcu_read_unlock(&sched_res_rculock);

    return 0;
}

static void vcpu_move_irqs(struct vcpu *v)
{
    arch_move_irqs(v);
    evtchn_move_pirqs(v);
}

static void sched_move_irqs(const struct sched_unit *unit)
{
    struct vcpu *v;

    for_each_sched_unit_vcpu ( unit, v )
        vcpu_move_irqs(v);
}

static void sched_move_domain_cleanup(const struct scheduler *ops,
                                      struct sched_unit *units,
                                      void *domdata)
{
    struct sched_unit *unit, *old_unit;

    for ( unit = units; unit; )
    {
        if ( unit->priv )
            sched_free_udata(ops, unit->priv);
        old_unit = unit;
        unit = unit->next_in_list;
        xfree(old_unit);
    }

    sched_free_domdata(ops, domdata);
}

/*
 * Move a domain from one cpupool to another.
 *
 * A domain with any vcpu having temporary affinity settings will be denied
 * to move. Hard and soft affinities will be reset.
 *
 * In order to support cpupools with different scheduling granularities all
 * scheduling units are replaced by new ones.
 *
 * The complete move is done in the following steps:
 * - check prerequisites (no vcpu with temporary affinities)
 * - allocate all new data structures (scheduler specific domain data, unit
 *   memory, scheduler specific unit data)
 * - pause domain
 * - temporarily move all (old) units to the same scheduling resource (this
 *   makes the final resource assignment easier in case the new cpupool has
 *   a larger granularity than the old one, as the scheduling locks for all
 *   vcpus must be held for that operation)
 * - remove old units from scheduling
 * - set new cpupool and scheduler domain data pointers in struct domain
 * - switch all vcpus to new units, still assigned to the old scheduling
 *   resource
 * - migrate all new units to scheduling resources of the new cpupool
 * - unpause the domain
 * - free the old memory (scheduler specific domain data, unit memory,
 *   scheduler specific unit data)
 */
int sched_move_domain(struct domain *d, struct cpupool *c)
{
    struct vcpu *v;
    struct sched_unit *unit, *old_unit;
    struct sched_unit *new_units = NULL, *old_units;
    struct sched_unit **unit_ptr = &new_units;
    unsigned int new_cpu, unit_idx;
    void *domdata;
    struct scheduler *old_ops = dom_scheduler(d);
    void *old_domdata;
    unsigned int gran = cpupool_get_granularity(c);
    unsigned int n_units = d->vcpu[0] ? DIV_ROUND_UP(d->max_vcpus, gran) : 0;

    for_each_vcpu ( d, v )
    {
        if ( v->affinity_broken )
            return -EBUSY;
    }

    rcu_read_lock(&sched_res_rculock);

    domdata = sched_alloc_domdata(c->sched, d);
    if ( IS_ERR(domdata) )
    {
        rcu_read_unlock(&sched_res_rculock);

        return PTR_ERR(domdata);
    }

    for ( unit_idx = 0; unit_idx < n_units; unit_idx++ )
    {
        unit = sched_alloc_unit_mem();
        if ( unit )
        {
            /* Initialize unit for sched_alloc_udata() to work. */
            unit->domain = d;
            unit->unit_id = unit_idx * gran;
            unit->vcpu_list = d->vcpu[unit->unit_id];
            unit->priv = sched_alloc_udata(c->sched, unit, domdata);
            *unit_ptr = unit;
        }

        if ( !unit || !unit->priv )
        {
            sched_move_domain_cleanup(c->sched, new_units, domdata);
            rcu_read_unlock(&sched_res_rculock);

            return -ENOMEM;
        }

        unit_ptr = &unit->next_in_list;
    }

    domain_pause(d);

    old_domdata = d->sched_priv;
    old_units = d->sched_unit_list;

    /*
     * Remove all units from the old scheduler, and temporarily move them to
     * the same processor to make locking easier when moving the new units to
     * new processors.
     */
    new_cpu = cpumask_first(d->cpupool->cpu_valid);
    for_each_sched_unit ( d, unit )
    {
        spinlock_t *lock;

        sched_remove_unit(old_ops, unit);

        lock = unit_schedule_lock_irq(unit);
        sched_set_res(unit, get_sched_res(new_cpu));
        spin_unlock_irq(lock);
    }

    d->cpupool = c;
    d->sched_priv = domdata;

    unit = new_units;
    for_each_vcpu ( d, v )
    {
        old_unit = v->sched_unit;
        if ( unit->unit_id + gran == v->vcpu_id )
            unit = unit->next_in_list;

        unit->state_entry_time = old_unit->state_entry_time;
        unit->runstate_cnt[v->runstate.state]++;
        /* Temporarily use old resource assignment */
        unit->res = get_sched_res(new_cpu);

        v->sched_unit = unit;
    }

    d->sched_unit_list = new_units;

    new_cpu = cpumask_first(c->cpu_valid);
    for_each_sched_unit ( d, unit )
    {
        spinlock_t *lock;
        unsigned int unit_cpu = new_cpu;

        for_each_sched_unit_vcpu ( unit, v )
        {
            migrate_timer(&v->periodic_timer, new_cpu);
            migrate_timer(&v->singleshot_timer, new_cpu);
            migrate_timer(&v->poll_timer, new_cpu);
            new_cpu = cpumask_cycle(new_cpu, c->cpu_valid);
        }

        lock = unit_schedule_lock_irq(unit);

        sched_set_affinity(unit, &cpumask_all, &cpumask_all);

        sched_set_res(unit, get_sched_res(unit_cpu));
        /*
         * With v->processor modified we must not
         * - make any further changes assuming we hold the scheduler lock,
         * - use unit_schedule_unlock_irq().
         */
        spin_unlock_irq(lock);

        if ( !d->is_dying )
            sched_move_irqs(unit);

        sched_insert_unit(c->sched, unit);
    }

    domain_update_node_affinity(d);

    domain_unpause(d);

    sched_move_domain_cleanup(old_ops, old_units, old_domdata);

    rcu_read_unlock(&sched_res_rculock);

    return 0;
}

void sched_destroy_vcpu(struct vcpu *v)
{
    struct sched_unit *unit = v->sched_unit;

    kill_timer(&v->periodic_timer);
    kill_timer(&v->singleshot_timer);
    kill_timer(&v->poll_timer);
    if ( test_and_clear_bool(v->is_urgent) )
        atomic_dec(&per_cpu(sched_urgent_count, v->processor));
    /*
     * Vcpus are being destroyed top-down. So being the first vcpu of an unit
     * is the same as being the only one.
     */
    if ( unit->vcpu_list == v )
    {
        rcu_read_lock(&sched_res_rculock);

        sched_remove_unit(vcpu_scheduler(v), unit);
        sched_free_udata(vcpu_scheduler(v), unit->priv);
        sched_free_unit(unit, v);

        rcu_read_unlock(&sched_res_rculock);
    }
}

int sched_init_domain(struct domain *d, unsigned int poolid)
{
    void *sdom;
    int ret;

    ASSERT(d->cpupool == NULL);
    ASSERT(d->domain_id < DOMID_FIRST_RESERVED);

    if ( (ret = cpupool_add_domain(d, poolid)) )
        return ret;

    SCHED_STAT_CRANK(dom_init);
    TRACE_TIME(TRC_SCHED_DOM_ADD, d->domain_id);

    rcu_read_lock(&sched_res_rculock);

    sdom = sched_alloc_domdata(dom_scheduler(d), d);

    rcu_read_unlock(&sched_res_rculock);

    if ( IS_ERR(sdom) )
        return PTR_ERR(sdom);

    d->sched_priv = sdom;

    return 0;
}

void sched_destroy_domain(struct domain *d)
{
    ASSERT(d->domain_id < DOMID_FIRST_RESERVED);

    if ( d->cpupool )
    {
        SCHED_STAT_CRANK(dom_destroy);
        TRACE_TIME(TRC_SCHED_DOM_REM, d->domain_id);

        rcu_read_lock(&sched_res_rculock);

        sched_free_domdata(dom_scheduler(d), d->sched_priv);
        d->sched_priv = NULL;

        rcu_read_unlock(&sched_res_rculock);

        cpupool_rm_domain(d);
    }
}

static void vcpu_sleep_nosync_locked(struct vcpu *v)
{
    struct sched_unit *unit = v->sched_unit;

    ASSERT(spin_is_locked(get_sched_res(v->processor)->schedule_lock));

    if ( likely(!vcpu_runnable(v)) )
    {
        if ( v->runstate.state == RUNSTATE_runnable )
            vcpu_runstate_change(v, RUNSTATE_offline, NOW());

        /* Only put unit to sleep in case all vcpus are not runnable. */
        if ( likely(!unit_runnable(unit)) )
            sched_sleep(unit_scheduler(unit), unit);
        else if ( unit_running(unit) > 1 && v->is_running &&
                  !v->force_context_switch )
        {
            v->force_context_switch = true;
            cpu_raise_softirq(v->processor, SCHED_SLAVE_SOFTIRQ);
        }
    }
}

void vcpu_sleep_nosync(struct vcpu *v)
{
    unsigned long flags;
    spinlock_t *lock;

    TRACE_TIME(TRC_SCHED_SLEEP, v->domain->domain_id, v->vcpu_id);

    rcu_read_lock(&sched_res_rculock);

    lock = unit_schedule_lock_irqsave(v->sched_unit, &flags);

    vcpu_sleep_nosync_locked(v);

    unit_schedule_unlock_irqrestore(lock, flags, v->sched_unit);

    rcu_read_unlock(&sched_res_rculock);
}

void vcpu_sleep_sync(struct vcpu *v)
{
    vcpu_sleep_nosync(v);

    while ( !vcpu_runnable(v) && v->is_running )
        cpu_relax();

    sync_vcpu_execstate(v);
}

void vcpu_wake(struct vcpu *v)
{
    unsigned long flags;
    spinlock_t *lock;
    struct sched_unit *unit = v->sched_unit;

    TRACE_TIME(TRC_SCHED_WAKE, v->domain->domain_id, v->vcpu_id);

    rcu_read_lock(&sched_res_rculock);

    lock = unit_schedule_lock_irqsave(unit, &flags);

    if ( likely(vcpu_runnable(v)) )
    {
        if ( v->runstate.state >= RUNSTATE_blocked )
            vcpu_runstate_change(v, RUNSTATE_runnable, NOW());
        /*
         * Call sched_wake() unconditionally, even if unit is running already.
         * We might have not been de-scheduled after vcpu_sleep_nosync_locked()
         * and are now to be woken up again.
         */
        sched_wake(unit_scheduler(unit), unit);
        if ( unit->is_running && !v->is_running && !v->force_context_switch )
        {
            v->force_context_switch = true;
            cpu_raise_softirq(v->processor, SCHED_SLAVE_SOFTIRQ);
        }
    }
    else if ( !(v->pause_flags & VPF_blocked) )
    {
        if ( v->runstate.state == RUNSTATE_blocked )
            vcpu_runstate_change(v, RUNSTATE_offline, NOW());
    }

    unit_schedule_unlock_irqrestore(lock, flags, unit);

    rcu_read_unlock(&sched_res_rculock);
}

void vcpu_unblock(struct vcpu *v)
{
    if ( !test_and_clear_bit(_VPF_blocked, &v->pause_flags) )
        return;

    /* Polling period ends when a VCPU is unblocked. */
    if ( unlikely(v->poll_evtchn != 0) )
    {
        v->poll_evtchn = 0;
        /*
         * We *must* re-clear _VPF_blocked to avoid racing other wakeups of
         * this VCPU (and it then going back to sleep on poll_mask).
         * Test-and-clear is idiomatic and ensures clear_bit not reordered.
         */
        if ( test_and_clear_bit(v->vcpu_id, v->domain->poll_mask) )
            clear_bit(_VPF_blocked, &v->pause_flags);
    }

    vcpu_wake(v);
}

/*
 * Do the actual movement of an unit from old to new CPU. Locks for *both*
 * CPUs needs to have been taken already when calling this!
 */
static void sched_unit_move_locked(struct sched_unit *unit,
                                   unsigned int new_cpu)
{
    unsigned int old_cpu = unit->res->master_cpu;
    const struct vcpu *v;

    rcu_read_lock(&sched_res_rculock);

    /*
     * Transfer urgency status to new CPU before switching CPUs, as
     * once the switch occurs, v->is_urgent is no longer protected by
     * the per-CPU scheduler lock we are holding.
     */
    for_each_sched_unit_vcpu ( unit, v )
    {
        if ( unlikely(v->is_urgent) && (old_cpu != new_cpu) )
        {
            atomic_inc(&per_cpu(sched_urgent_count, new_cpu));
            atomic_dec(&per_cpu(sched_urgent_count, old_cpu));
        }
    }

    /*
     * Actual CPU switch to new CPU.  This is safe because the lock
     * pointer can't change while the current lock is held.
     */
    sched_migrate(unit_scheduler(unit), unit, new_cpu);

    rcu_read_unlock(&sched_res_rculock);
}

/*
 * Initiating migration
 *
 * In order to migrate, we need the unit in question to have stopped
 * running and have called sched_sleep() (to take it off any
 * runqueues, for instance); and if it is currently running, it needs
 * to be scheduled out.  Finally, we need to hold the scheduling locks
 * for both the processor we're migrating from, and the processor
 * we're migrating to.
 *
 * In order to avoid deadlock while satisfying the final requirement,
 * we must release any scheduling lock we hold, then try to grab both
 * locks we want, then double-check to make sure that what we started
 * to do hasn't been changed in the mean time.
 *
 * These steps are encapsulated in the following two functions; they
 * should be called like this:
 *
 *     lock = unit_schedule_lock_irq(unit);
 *     sched_unit_migrate_start(unit);
 *     unit_schedule_unlock_irq(lock, unit)
 *     sched_unit_migrate_finish(unit);
 *
 * sched_unit_migrate_finish() will do the work now if it can, or simply
 * return if it can't (because unit is still running); in that case
 * sched_unit_migrate_finish() will be called by unit_context_saved().
 */
static void sched_unit_migrate_start(struct sched_unit *unit)
{
    struct vcpu *v;

    for_each_sched_unit_vcpu ( unit, v )
    {
        set_bit(_VPF_migrating, &v->pause_flags);
        vcpu_sleep_nosync_locked(v);
    }
}

static void sched_unit_migrate_finish(struct sched_unit *unit)
{
    unsigned long flags;
    unsigned int old_cpu, new_cpu;
    spinlock_t *old_lock, *new_lock;
    bool pick_called = false;
    struct vcpu *v;

    /*
     * If the unit is currently running, this will be handled by
     * unit_context_saved(); and in any case, if the bit is cleared, then
     * someone else has already done the work so we don't need to.
     */
    if ( unit->is_running )
        return;
    for_each_sched_unit_vcpu ( unit, v )
        if ( !test_bit(_VPF_migrating, &v->pause_flags) )
            return;

    old_cpu = new_cpu = unit->res->master_cpu;
    for ( ; ; )
    {
        /*
         * We need another iteration if the pre-calculated lock addresses
         * are not correct any longer after evaluating old and new cpu holding
         * the locks.
         */
        old_lock = get_sched_res(old_cpu)->schedule_lock;
        new_lock = get_sched_res(new_cpu)->schedule_lock;

        sched_spin_lock_double(old_lock, new_lock, &flags);

        old_cpu = unit->res->master_cpu;
        if ( old_lock == get_sched_res(old_cpu)->schedule_lock )
        {
            /*
             * If we selected a CPU on the previosu iteration, check if it
             * remains suitable for running this vCPU.
             */
            if ( pick_called &&
                 (new_lock == get_sched_res(new_cpu)->schedule_lock) &&
                 cpumask_test_cpu(new_cpu, unit->cpu_hard_affinity) &&
                 cpumask_test_cpu(new_cpu, unit->domain->cpupool->cpu_valid) )
                break;

            /* Select a new CPU. */
            new_cpu = sched_pick_resource(unit_scheduler(unit),
                                          unit)->master_cpu;
            if ( (new_lock == get_sched_res(new_cpu)->schedule_lock) &&
                 cpumask_test_cpu(new_cpu, unit->domain->cpupool->cpu_valid) )
                break;
            pick_called = true;
        }
        else
        {
            /*
             * We do not hold the scheduler lock appropriate for this vCPU.
             * Thus we cannot select a new CPU on this iteration. Try again.
             */
            pick_called = false;
        }

        sched_spin_unlock_double(old_lock, new_lock, flags);
    }

    /*
     * NB. Check of v->running happens /after/ setting migration flag
     * because they both happen in (different) spinlock regions, and those
     * regions are strictly serialised.
     */
    if ( unit->is_running )
    {
        sched_spin_unlock_double(old_lock, new_lock, flags);
        return;
    }
    for_each_sched_unit_vcpu ( unit, v )
    {
        if ( !test_and_clear_bit(_VPF_migrating, &v->pause_flags) )
        {
            sched_spin_unlock_double(old_lock, new_lock, flags);
            return;
        }
    }

    sched_unit_move_locked(unit, new_cpu);

    sched_spin_unlock_double(old_lock, new_lock, flags);

    if ( old_cpu != new_cpu )
        sched_move_irqs(unit);

    /* Wake on new CPU. */
    for_each_sched_unit_vcpu ( unit, v )
        vcpu_wake(v);
}

static bool sched_check_affinity_broken(const struct sched_unit *unit)
{
    const struct vcpu *v;

    for_each_sched_unit_vcpu ( unit, v )
        if ( v->affinity_broken )
            return true;

    return false;
}

/*
 * This function is used by cpu_hotplug code via cpu notifier chain
 * and from cpupools to switch schedulers on a cpu.
 * Caller must get domlist_read_lock.
 */
int cpu_disable_scheduler(unsigned int cpu)
{
    struct domain *d;
    const struct cpupool *c;
    int ret = 0;

    rcu_read_lock(&sched_res_rculock);

    c = get_sched_res(cpu)->cpupool;
    if ( c == NULL )
        goto out;

    for_each_domain_in_cpupool ( d, c )
    {
        struct sched_unit *unit;

        for_each_sched_unit ( d, unit )
        {
            unsigned long flags;
            spinlock_t *lock = unit_schedule_lock_irqsave(unit, &flags);

            if ( !cpumask_intersects(unit->cpu_hard_affinity, c->cpu_valid) &&
                 cpumask_test_cpu(cpu, unit->cpu_hard_affinity) )
            {
                if ( sched_check_affinity_broken(unit) )
                {
                    /* The unit is temporarily pinned, can't move it. */
                    unit_schedule_unlock_irqrestore(lock, flags, unit);
                    ret = -EADDRINUSE;
                    break;
                }

                printk(XENLOG_DEBUG "Breaking affinity for %pv\n",
                       unit->vcpu_list);

                sched_set_affinity(unit, &cpumask_all, NULL);
            }

            if ( unit->res != get_sched_res(cpu) )
            {
                /* The unit is not on this cpu, so we can move on. */
                unit_schedule_unlock_irqrestore(lock, flags, unit);
                continue;
            }

            /* If it is on this cpu, we must send it away.
             * We are doing some cpupool manipulations:
             *  * we want to call the scheduler, and let it re-evaluation
             *    the placement of the vcpu, taking into account the new
             *    cpupool configuration;
             *  * the scheduler will always find a suitable solution, or
             *    things would have failed before getting in here.
             */
            sched_unit_migrate_start(unit);
            unit_schedule_unlock_irqrestore(lock, flags, unit);
            sched_unit_migrate_finish(unit);

            /*
             * The only caveat, in this case, is that if a vcpu active in
             * the hypervisor isn't migratable. In this case, the caller
             * should try again after releasing and reaquiring all locks.
             */
            if ( unit->res == get_sched_res(cpu) )
                ret = -EAGAIN;
        }
    }

out:
    rcu_read_unlock(&sched_res_rculock);

    return ret;
}

static int cpu_disable_scheduler_check(unsigned int cpu)
{
    struct domain *d;
    const struct vcpu *v;
    const struct cpupool *c;

    c = get_sched_res(cpu)->cpupool;
    if ( c == NULL )
        return 0;

    for_each_domain_in_cpupool ( d, c )
        for_each_vcpu ( d, v )
            if ( v->affinity_broken )
                return -EADDRINUSE;

    return 0;
}

/*
 * Called after a cpu has come up again in a suspend/resume cycle.
 * Migrate all timers for this cpu (they have been migrated to cpu 0 when the
 * cpu was going down).
 * Note that only timers related to a physical cpu are migrated, not the ones
 * related to a vcpu or domain.
 */
void sched_migrate_timers(unsigned int cpu)
{
    struct sched_resource *sr;

    rcu_read_lock(&sched_res_rculock);

    sr = get_sched_res(cpu);

    /*
     * Note that on a system with parked cpus (e.g. smt=0 on Intel cpus) this
     * will be called for the parked cpus, too, so the case for no scheduling
     * resource being available must be considered.
     */
    if ( sr && sr->master_cpu == cpu )
    {
        migrate_timer(&sr->s_timer, cpu);
        sched_move_timers(sr->scheduler, sr);
    }

    rcu_read_unlock(&sched_res_rculock);
}

/*
 * In general, this must be called with the scheduler lock held, because the
 * adjust_affinity hook may want to modify the vCPU state. However, when the
 * vCPU is being initialized (either for dom0 or domU) there is no risk of
 * races, and it's fine to not take the look (we're talking about
 * sched_setup_dom0_vcpus() an sched_init_vcpu()).
 */
static void sched_set_affinity(
    struct sched_unit *unit, const cpumask_t *hard, const cpumask_t *soft)
{
    rcu_read_lock(&sched_res_rculock);
    sched_adjust_affinity(dom_scheduler(unit->domain), unit, hard, soft);
    rcu_read_unlock(&sched_res_rculock);

    if ( hard )
        cpumask_copy(unit->cpu_hard_affinity, hard);
    if ( soft )
        cpumask_copy(unit->cpu_soft_affinity, soft);

    unit->soft_aff_effective = !cpumask_subset(unit->cpu_hard_affinity,
                                               unit->cpu_soft_affinity) &&
                               cpumask_intersects(unit->cpu_soft_affinity,
                                                  unit->cpu_hard_affinity);
}

static int vcpu_set_affinity(
    struct vcpu *v, const cpumask_t *affinity, const cpumask_t *which)
{
    struct sched_unit *unit = v->sched_unit;
    spinlock_t *lock;
    int ret = 0;

    rcu_read_lock(&sched_res_rculock);

    lock = unit_schedule_lock_irq(unit);

    if ( v->affinity_broken )
        ret = -EBUSY;
    else
    {
        /*
         * Tell the scheduler we changes something about affinity,
         * and ask to re-evaluate vcpu placement.
         */
        if ( which == unit->cpu_hard_affinity )
        {
            sched_set_affinity(unit, affinity, NULL);
        }
        else
        {
            ASSERT(which == unit->cpu_soft_affinity);
            sched_set_affinity(unit, NULL, affinity);
        }
        sched_unit_migrate_start(unit);
    }

    unit_schedule_unlock_irq(lock, unit);

    domain_update_node_affinity(v->domain);

    sched_unit_migrate_finish(unit);

    rcu_read_unlock(&sched_res_rculock);

    return ret;
}

int vcpu_set_hard_affinity(struct vcpu *v, const cpumask_t *affinity)
{
    cpumask_t *online;

    online = VCPU2ONLINE(v);
    if ( !cpumask_intersects(online, affinity) )
        return -EINVAL;

    return vcpu_set_affinity(v, affinity, v->sched_unit->cpu_hard_affinity);
}

static int vcpu_set_soft_affinity(struct vcpu *v, const cpumask_t *affinity)
{
    return vcpu_set_affinity(v, affinity, v->sched_unit->cpu_soft_affinity);
}

/* Block the currently-executing domain until a pertinent event occurs. */
void vcpu_block(void)
{
    struct vcpu *v = current;

    set_bit(_VPF_blocked, &v->pause_flags);

    smp_mb__after_atomic();

    arch_vcpu_block(v);

    /* Check for events /after/ blocking: avoids wakeup waiting race. */
    if ( local_events_need_delivery() )
    {
        clear_bit(_VPF_blocked, &v->pause_flags);
    }
    else
    {
        TRACE_TIME(TRC_SCHED_BLOCK, v->domain->domain_id, v->vcpu_id);
        raise_softirq(SCHEDULE_SOFTIRQ);
    }
}

static void vcpu_block_enable_events(void)
{
    local_event_delivery_enable();
    vcpu_block();
}

static long do_poll(const struct sched_poll *sched_poll)
{
    struct vcpu   *v = current;
    struct domain *d = v->domain;
    evtchn_port_t  port = 0;
    long           rc;
    unsigned int   i;

    /* Fairly arbitrary limit. */
    if ( sched_poll->nr_ports > 128 )
        return -EINVAL;

    if ( !guest_handle_okay(sched_poll->ports, sched_poll->nr_ports) )
        return -EFAULT;

    set_bit(_VPF_blocked, &v->pause_flags);
    v->poll_evtchn = -1;
    set_bit(v->vcpu_id, d->poll_mask);

    arch_vcpu_block(v);

#ifndef CONFIG_X86 /* set_bit() implies mb() on x86 */
    /* Check for events /after/ setting flags: avoids wakeup waiting race. */
    smp_mb();

    /*
     * Someone may have seen we are blocked but not that we are polling, or
     * vice versa. We are certainly being woken, so clean up and bail. Beyond
     * this point others can be guaranteed to clean up for us if they wake us.
     */
    rc = 0;
    if ( (v->poll_evtchn == 0) ||
         !test_bit(_VPF_blocked, &v->pause_flags) ||
         !test_bit(v->vcpu_id, d->poll_mask) )
        goto out;
#endif

    rc = 0;
    if ( local_events_need_delivery() )
        goto out;

    for ( i = 0; i < sched_poll->nr_ports; i++ )
    {
        rc = -EFAULT;
        if ( __copy_from_guest_offset(&port, sched_poll->ports, i, 1) )
            goto out;

        rc = evtchn_port_poll(d, port);
        if ( rc )
        {
            if ( rc > 0 )
                rc = 0;
            goto out;
        }
    }

    if ( sched_poll->nr_ports == 1 )
        v->poll_evtchn = port;

    if ( sched_poll->timeout != 0 )
        set_timer(&v->poll_timer, sched_poll->timeout);

    TRACE_TIME(TRC_SCHED_BLOCK, d->domain_id, v->vcpu_id);
    raise_softirq(SCHEDULE_SOFTIRQ);

    return 0;

 out:
    v->poll_evtchn = 0;
    clear_bit(v->vcpu_id, d->poll_mask);
    clear_bit(_VPF_blocked, &v->pause_flags);
    return rc;
}

/* Voluntarily yield the processor for this allocation. */
long vcpu_yield(void)
{
    struct vcpu * v=current;
    spinlock_t *lock;

    rcu_read_lock(&sched_res_rculock);

    lock = unit_schedule_lock_irq(v->sched_unit);
    sched_yield(vcpu_scheduler(v), v->sched_unit);
    unit_schedule_unlock_irq(lock, v->sched_unit);

    rcu_read_unlock(&sched_res_rculock);

    SCHED_STAT_CRANK(vcpu_yield);

    TRACE_TIME(TRC_SCHED_YIELD, current->domain->domain_id, current->vcpu_id);
    raise_softirq(SCHEDULE_SOFTIRQ);
    return 0;
}

static void cf_check domain_watchdog_timeout(void *data)
{
    struct domain *d = data;

    if ( d->is_shutting_down || d->is_dying )
        return;

    printk("Watchdog timer fired for domain %u\n", d->domain_id);
    domain_shutdown(d, SHUTDOWN_watchdog);
}

static long domain_watchdog(struct domain *d, uint32_t id, uint32_t timeout)
{
    if ( id > NR_DOMAIN_WATCHDOG_TIMERS )
        return -EINVAL;

    spin_lock(&d->watchdog_lock);

    if ( id == 0 )
    {
        for ( id = 0; id < NR_DOMAIN_WATCHDOG_TIMERS; id++ )
        {
            if ( test_and_set_bit(id, &d->watchdog_inuse_map) )
                continue;
            set_timer(&d->watchdog_timer[id], NOW() + SECONDS(timeout));
            break;
        }
        spin_unlock(&d->watchdog_lock);
        return id == NR_DOMAIN_WATCHDOG_TIMERS ? -ENOSPC : id + 1;
    }

    id -= 1;
    if ( !test_bit(id, &d->watchdog_inuse_map) )
    {
        spin_unlock(&d->watchdog_lock);
        return -EINVAL;
    }

    if ( timeout == 0 )
    {
        stop_timer(&d->watchdog_timer[id]);
        clear_bit(id, &d->watchdog_inuse_map);
    }
    else
    {
        set_timer(&d->watchdog_timer[id], NOW() + SECONDS(timeout));
    }

    spin_unlock(&d->watchdog_lock);
    return 0;
}

void watchdog_domain_init(struct domain *d)
{
    unsigned int i;

    spin_lock_init(&d->watchdog_lock);

    d->watchdog_inuse_map = 0;

    for ( i = 0; i < NR_DOMAIN_WATCHDOG_TIMERS; i++ )
        init_timer(&d->watchdog_timer[i], domain_watchdog_timeout, d, 0);
}

void watchdog_domain_destroy(struct domain *d)
{
    unsigned int i;

    for ( i = 0; i < NR_DOMAIN_WATCHDOG_TIMERS; i++ )
        kill_timer(&d->watchdog_timer[i]);
}

/*
 * Pin a vcpu temporarily to a specific CPU (or restore old pinning state if
 * cpu is NR_CPUS).
 * Temporary pinning can be done due to two reasons, which may be nested:
 * - VCPU_AFFINITY_OVERRIDE (requested by guest): is allowed to fail in case
 *   of a conflict (e.g. in case cpupool doesn't include requested CPU, or
 *   another conflicting temporary pinning is already in effect.
 * - VCPU_AFFINITY_WAIT (called by wait_event()): only used to pin vcpu to the
 *   CPU it is just running on. Can't fail if used properly.
 */
int vcpu_temporary_affinity(struct vcpu *v, unsigned int cpu, uint8_t reason)
{
    struct sched_unit *unit = v->sched_unit;
    spinlock_t *lock;
    int ret = -EINVAL;
    bool migrate;

    rcu_read_lock(&sched_res_rculock);

    lock = unit_schedule_lock_irq(unit);

    if ( cpu == NR_CPUS )
    {
        if ( v->affinity_broken & reason )
        {
            ret = 0;
            v->affinity_broken &= ~reason;
        }
        if ( !ret && !sched_check_affinity_broken(unit) )
            sched_set_affinity(unit, unit->cpu_hard_affinity_saved, NULL);
    }
    else if ( cpu < nr_cpu_ids )
    {
        if ( (v->affinity_broken & reason) ||
             (sched_check_affinity_broken(unit) && v->processor != cpu) )
            ret = -EBUSY;
        else if ( cpumask_test_cpu(cpu, VCPU2ONLINE(v)) )
        {
            if ( !sched_check_affinity_broken(unit) )
            {
                cpumask_copy(unit->cpu_hard_affinity_saved,
                             unit->cpu_hard_affinity);
                sched_set_affinity(unit, cpumask_of(cpu), NULL);
            }
            v->affinity_broken |= reason;
            ret = 0;
        }
    }

    migrate = !ret && !cpumask_test_cpu(v->processor, unit->cpu_hard_affinity);
    if ( migrate )
        sched_unit_migrate_start(unit);

    unit_schedule_unlock_irq(lock, unit);

    if ( migrate )
        sched_unit_migrate_finish(unit);

    rcu_read_unlock(&sched_res_rculock);

    return ret;
}

static inline
int vcpuaffinity_params_invalid(const struct xen_domctl_vcpuaffinity *vcpuaff)
{
    return vcpuaff->flags == 0 ||
           ((vcpuaff->flags & XEN_VCPUAFFINITY_HARD) &&
            guest_handle_is_null(vcpuaff->cpumap_hard.bitmap)) ||
           ((vcpuaff->flags & XEN_VCPUAFFINITY_SOFT) &&
            guest_handle_is_null(vcpuaff->cpumap_soft.bitmap));
}

int vcpu_affinity_domctl(struct domain *d, uint32_t cmd,
                         struct xen_domctl_vcpuaffinity *vcpuaff)
{
    struct vcpu *v;
    const struct sched_unit *unit;
    int ret = 0;

    if ( vcpuaff->vcpu >= d->max_vcpus )
        return -EINVAL;

    if ( (v = d->vcpu[vcpuaff->vcpu]) == NULL )
        return -ESRCH;

    if ( vcpuaffinity_params_invalid(vcpuaff) )
        return -EINVAL;

    unit = v->sched_unit;

    if ( cmd == XEN_DOMCTL_setvcpuaffinity )
    {
        cpumask_var_t new_affinity, old_affinity;
        cpumask_t *online = cpupool_domain_master_cpumask(v->domain);

        /*
         * We want to be able to restore hard affinity if we are trying
         * setting both and changing soft affinity (which happens later,
         * when hard affinity has been succesfully chaged already) fails.
         */
        if ( !alloc_cpumask_var(&old_affinity) )
            return -ENOMEM;

        cpumask_copy(old_affinity, unit->cpu_hard_affinity);

        if ( !alloc_cpumask_var(&new_affinity) )
        {
            free_cpumask_var(old_affinity);
            return -ENOMEM;
        }

        /* Undo a stuck SCHED_pin_override? */
        if ( vcpuaff->flags & XEN_VCPUAFFINITY_FORCE )
            vcpu_temporary_affinity(v, NR_CPUS, VCPU_AFFINITY_OVERRIDE);

        ret = 0;

        /*
         * We both set a new affinity and report back to the caller what
         * the scheduler will be effectively using.
         */
        if ( vcpuaff->flags & XEN_VCPUAFFINITY_HARD )
        {
            ret = xenctl_bitmap_to_bitmap(cpumask_bits(new_affinity),
                                          &vcpuaff->cpumap_hard, nr_cpu_ids);
            if ( !ret )
                ret = vcpu_set_hard_affinity(v, new_affinity);
            if ( ret )
                goto setvcpuaffinity_out;

            /*
             * For hard affinity, what we return is the intersection of
             * cpupool's online mask and the new hard affinity.
             */
            cpumask_and(new_affinity, online, unit->cpu_hard_affinity);
            ret = cpumask_to_xenctl_bitmap(&vcpuaff->cpumap_hard, new_affinity);
        }
        if ( vcpuaff->flags & XEN_VCPUAFFINITY_SOFT )
        {
            ret = xenctl_bitmap_to_bitmap(cpumask_bits(new_affinity),
                                          &vcpuaff->cpumap_soft, nr_cpu_ids);
            if ( !ret)
                ret = vcpu_set_soft_affinity(v, new_affinity);
            if ( ret )
            {
                /*
                 * Since we're returning error, the caller expects nothing
                 * happened, so we rollback the changes to hard affinity
                 * (if any).
                 */
                if ( vcpuaff->flags & XEN_VCPUAFFINITY_HARD )
                    vcpu_set_hard_affinity(v, old_affinity);
                goto setvcpuaffinity_out;
            }

            /*
             * For soft affinity, we return the intersection between the
             * new soft affinity, the cpupool's online map and the (new)
             * hard affinity.
             */
            cpumask_and(new_affinity, new_affinity, online);
            cpumask_and(new_affinity, new_affinity, unit->cpu_hard_affinity);
            ret = cpumask_to_xenctl_bitmap(&vcpuaff->cpumap_soft, new_affinity);
        }

 setvcpuaffinity_out:
        free_cpumask_var(new_affinity);
        free_cpumask_var(old_affinity);
    }
    else
    {
        if ( vcpuaff->flags & XEN_VCPUAFFINITY_HARD )
            ret = cpumask_to_xenctl_bitmap(&vcpuaff->cpumap_hard,
                                           unit->cpu_hard_affinity);
        if ( vcpuaff->flags & XEN_VCPUAFFINITY_SOFT )
            ret = cpumask_to_xenctl_bitmap(&vcpuaff->cpumap_soft,
                                           unit->cpu_soft_affinity);
    }

    return ret;
}

bool alloc_affinity_masks(struct affinity_masks *affinity)
{
    if ( !alloc_cpumask_var(&affinity->hard) )
        return false;
    if ( !alloc_cpumask_var(&affinity->soft) )
    {
        free_cpumask_var(affinity->hard);
        return false;
    }

    return true;
}

void free_affinity_masks(struct affinity_masks *affinity)
{
    free_cpumask_var(affinity->soft);
    free_cpumask_var(affinity->hard);
}

void domain_update_node_aff(struct domain *d, struct affinity_masks *affinity)
{
    struct affinity_masks masks;
    cpumask_t *dom_affinity;
    const cpumask_t *online;
    struct sched_unit *unit;
    unsigned int cpu;

    /* Do we have vcpus already? If not, no need to update node-affinity. */
    if ( !d->vcpu || !d->vcpu[0] )
        return;

    if ( !affinity )
    {
        affinity = &masks;
        if ( !alloc_affinity_masks(affinity) )
            return;
    }

    cpumask_clear(affinity->hard);
    cpumask_clear(affinity->soft);

    online = cpupool_domain_master_cpumask(d);

    spin_lock(&d->node_affinity_lock);

    /*
     * If d->auto_node_affinity is true, let's compute the domain's
     * node-affinity and update d->node_affinity accordingly. if false,
     * just leave d->auto_node_affinity alone.
     */
    if ( d->auto_node_affinity )
    {
        /*
         * We want the narrowest possible set of pcpus (to get the narowest
         * possible set of nodes). What we need is the cpumask of where the
         * domain can run (the union of the hard affinity of all its vcpus),
         * and the full mask of where it would prefer to run (the union of
         * the soft affinity of all its various vcpus). Let's build them.
         */
        for_each_sched_unit ( d, unit )
        {
            cpumask_or(affinity->hard, affinity->hard, unit->cpu_hard_affinity);
            cpumask_or(affinity->soft, affinity->soft, unit->cpu_soft_affinity);
        }
        /* Filter out non-online cpus */
        cpumask_and(affinity->hard, affinity->hard, online);
        ASSERT(!cpumask_empty(affinity->hard));
        /* And compute the intersection between hard, online and soft */
        cpumask_and(affinity->soft, affinity->soft, affinity->hard);

        /*
         * If not empty, the intersection of hard, soft and online is the
         * narrowest set we want. If empty, we fall back to hard&online.
         */
        dom_affinity = cpumask_empty(affinity->soft) ? affinity->hard
                                                     : affinity->soft;

        nodes_clear(d->node_affinity);
        for_each_cpu ( cpu, dom_affinity )
            node_set(cpu_to_node(cpu), d->node_affinity);
    }

    spin_unlock(&d->node_affinity_lock);

    if ( affinity == &masks )
        free_affinity_masks(affinity);
}

typedef long ret_t;

#endif /* !COMPAT */

ret_t do_sched_op(int cmd, XEN_GUEST_HANDLE_PARAM(void) arg)
{
    ret_t ret = 0;

    switch ( cmd )
    {
    case SCHEDOP_yield:
    {
        ret = vcpu_yield();
        break;
    }

    case SCHEDOP_block:
    {
        vcpu_block_enable_events();
        break;
    }

    case SCHEDOP_shutdown:
    {
        struct sched_shutdown sched_shutdown;

        ret = -EFAULT;
        if ( copy_from_guest(&sched_shutdown, arg, 1) )
            break;

        TRACE_TIME(TRC_SCHED_SHUTDOWN, current->domain->domain_id,
                   current->vcpu_id, sched_shutdown.reason);
        ret = domain_shutdown(current->domain, (u8)sched_shutdown.reason);

        break;
    }

    case SCHEDOP_shutdown_code:
    {
        struct sched_shutdown sched_shutdown;
        struct domain *d = current->domain;

        ret = -EFAULT;
        if ( copy_from_guest(&sched_shutdown, arg, 1) )
            break;

        TRACE_TIME(TRC_SCHED_SHUTDOWN_CODE, d->domain_id, current->vcpu_id,
                   sched_shutdown.reason);

        spin_lock(&d->shutdown_lock);
        if ( d->shutdown_code == SHUTDOWN_CODE_INVALID )
            d->shutdown_code = (u8)sched_shutdown.reason;
        spin_unlock(&d->shutdown_lock);

        ret = 0;
        break;
    }

    case SCHEDOP_poll:
    {
        struct sched_poll sched_poll;

        ret = -EFAULT;
        if ( copy_from_guest(&sched_poll, arg, 1) )
            break;

        ret = do_poll(&sched_poll);

        break;
    }

    case SCHEDOP_remote_shutdown:
    {
        struct domain *d;
        struct sched_remote_shutdown sched_remote_shutdown;

        ret = -EFAULT;
        if ( copy_from_guest(&sched_remote_shutdown, arg, 1) )
            break;

        ret = -ESRCH;
        d = rcu_lock_domain_by_id(sched_remote_shutdown.domain_id);
        if ( d == NULL )
            break;

        ret = xsm_schedop_shutdown(XSM_DM_PRIV, current->domain, d);
        if ( likely(!ret) )
            domain_shutdown(d, sched_remote_shutdown.reason);

        rcu_unlock_domain(d);

        break;
    }

    case SCHEDOP_watchdog:
    {
        struct sched_watchdog sched_watchdog;

        ret = -EFAULT;
        if ( copy_from_guest(&sched_watchdog, arg, 1) )
            break;

        ret = domain_watchdog(
            current->domain, sched_watchdog.id, sched_watchdog.timeout);
        break;
    }

    case SCHEDOP_pin_override:
    {
        struct sched_pin_override sched_pin_override;
        unsigned int cpu;

        ret = -EPERM;
        if ( !is_hardware_domain(current->domain) )
            break;

        ret = -EFAULT;
        if ( copy_from_guest(&sched_pin_override, arg, 1) )
            break;

        ret = -EINVAL;
        if ( sched_pin_override.pcpu >= NR_CPUS )
           break;

        cpu = sched_pin_override.pcpu < 0 ? NR_CPUS : sched_pin_override.pcpu;
        ret = vcpu_temporary_affinity(current, cpu, VCPU_AFFINITY_OVERRIDE);

        break;
    }

    default:
        ret = -ENOSYS;
        break;
    }

    return ret;
}

#ifndef COMPAT


/* scheduler_id - fetch ID of current scheduler */
int scheduler_id(void)
{
    return operations.sched_id;
}

/* Adjust scheduling parameter for a given domain. */
long sched_adjust(struct domain *d, struct xen_domctl_scheduler_op *op)
{
    long ret;

    ret = xsm_domctl_scheduler_op(XSM_HOOK, d, op->cmd);
    if ( ret )
        return ret;

    if ( op->sched_id != dom_scheduler(d)->sched_id )
        return -EINVAL;

    switch ( op->cmd )
    {
    case XEN_DOMCTL_SCHEDOP_putinfo:
    case XEN_DOMCTL_SCHEDOP_getinfo:
    case XEN_DOMCTL_SCHEDOP_putvcpuinfo:
    case XEN_DOMCTL_SCHEDOP_getvcpuinfo:
        break;
    default:
        return -EINVAL;
    }

    /* NB: the pluggable scheduler code needs to take care
     * of locking by itself. */
    rcu_read_lock(&sched_res_rculock);

    if ( (ret = sched_adjust_dom(dom_scheduler(d), d, op)) == 0 )
        TRACE_TIME(TRC_SCHED_ADJDOM, d->domain_id);

    rcu_read_unlock(&sched_res_rculock);

    return ret;
}

long sched_adjust_global(struct xen_sysctl_scheduler_op *op)
{
    struct cpupool *pool;
    int rc;

    rc = xsm_sysctl_scheduler_op(XSM_HOOK, op->cmd);
    if ( rc )
        return rc;

    if ( (op->cmd != XEN_SYSCTL_SCHEDOP_putinfo) &&
         (op->cmd != XEN_SYSCTL_SCHEDOP_getinfo) )
        return -EINVAL;

    pool = cpupool_get_by_id(op->cpupool_id);
    if ( pool == NULL )
        return -ESRCH;

    rcu_read_lock(&sched_res_rculock);

    rc = ((op->sched_id == pool->sched->sched_id)
          ? sched_adjust_cpupool(pool->sched, op) : -EINVAL);

    rcu_read_unlock(&sched_res_rculock);

    cpupool_put(pool);

    return rc;
}

static void vcpu_periodic_timer_work_locked(struct vcpu *v)
{
    s_time_t now;
    s_time_t periodic_next_event;

    now = NOW();
    periodic_next_event = v->periodic_last_event + v->periodic_period;

    if ( now >= periodic_next_event )
    {
        send_timer_event(v);
        v->periodic_last_event = now;
        periodic_next_event = now + v->periodic_period;
    }

    migrate_timer(&v->periodic_timer, v->processor);
    set_timer(&v->periodic_timer, periodic_next_event);
}

static void vcpu_periodic_timer_work(struct vcpu *v)
{
    if ( v->periodic_period == 0 )
        return;

    spin_lock(&v->periodic_timer_lock);
    if ( v->periodic_period )
        vcpu_periodic_timer_work_locked(v);
    spin_unlock(&v->periodic_timer_lock);
}

/*
 * Set the periodic timer of a vcpu.
 */
void vcpu_set_periodic_timer(struct vcpu *v, s_time_t value)
{
    spin_lock(&v->periodic_timer_lock);

    stop_timer(&v->periodic_timer);

    v->periodic_period = value;
    if ( value )
        vcpu_periodic_timer_work_locked(v);

    spin_unlock(&v->periodic_timer_lock);
}

static void sched_switch_units(struct sched_resource *sr,
                               struct sched_unit *next, struct sched_unit *prev,
                               s_time_t now)
{
    unsigned int cpu;

    ASSERT(unit_running(prev));

    if ( prev != next )
    {
        sr->curr = next;
        sr->prev = prev;

        TRACE_TIME(TRC_SCHED_SWITCH_INFPREV, prev->domain->domain_id,
                   prev->unit_id, now - prev->state_entry_time);
        TRACE_TIME(TRC_SCHED_SWITCH_INFNEXT, next->domain->domain_id, next->unit_id,
                   (next->vcpu_list->runstate.state == RUNSTATE_runnable) ?
                   (now - next->state_entry_time) : 0, prev->next_time);
        TRACE_TIME(TRC_SCHED_SWITCH, prev->domain->domain_id, prev->unit_id,
                   next->domain->domain_id, next->unit_id);

        ASSERT(!unit_running(next));

        /*
         * NB. Don't add any trace records from here until the actual context
         * switch, else lost_records resume will not work properly.
         */

        ASSERT(!next->is_running);
        next->is_running = true;
        next->state_entry_time = now;

        if ( is_idle_unit(prev) )
        {
            prev->runstate_cnt[RUNSTATE_running] = 0;
            prev->runstate_cnt[RUNSTATE_runnable] = sr->granularity;
        }
        if ( is_idle_unit(next) )
        {
            next->runstate_cnt[RUNSTATE_running] = sr->granularity;
            next->runstate_cnt[RUNSTATE_runnable] = 0;
        }
    }

    for_each_cpu ( cpu, sr->cpus )
    {
        struct vcpu *vprev = get_cpu_current(cpu);
        struct vcpu *vnext = sched_unit2vcpu_cpu(next, cpu);

        if ( vprev != vnext || vprev->runstate.state != vnext->new_state )
        {
            vcpu_runstate_change(vprev,
                ((vprev->pause_flags & VPF_blocked) ? RUNSTATE_blocked :
                 (vcpu_runnable(vprev) ? RUNSTATE_runnable : RUNSTATE_offline)),
                now);
            vcpu_runstate_change(vnext, vnext->new_state, now);
        }

        vnext->is_running = true;

        if ( is_idle_vcpu(vnext) )
            vnext->sched_unit = next;
    }
}

static bool sched_tasklet_check_cpu(unsigned int cpu)
{
    unsigned long *tasklet_work = &per_cpu(tasklet_work_to_do, cpu);

    switch ( *tasklet_work )
    {
    case TASKLET_enqueued:
        set_bit(_TASKLET_scheduled, tasklet_work);
        /* fallthrough */
    case TASKLET_enqueued|TASKLET_scheduled:
        return true;
    case TASKLET_scheduled:
        clear_bit(_TASKLET_scheduled, tasklet_work);
        /* fallthrough */
    case 0:
        /* return false; */
        break;
    default:
        BUG();
    }

    return false;
}

static bool sched_tasklet_check(unsigned int cpu)
{
    bool tasklet_work_scheduled = false;
    const cpumask_t *mask = get_sched_res(cpu)->cpus;
    unsigned int cpu_iter;

    for_each_cpu ( cpu_iter, mask )
        if ( sched_tasklet_check_cpu(cpu_iter) )
            tasklet_work_scheduled = true;

    return tasklet_work_scheduled;
}

static struct sched_unit *do_schedule(struct sched_unit *prev, s_time_t now,
                                      unsigned int cpu)
{
    struct sched_resource *sr = get_sched_res(cpu);
    struct scheduler *sched = sr->scheduler;
    struct sched_unit *next;

    /* get policy-specific decision on scheduling... */
    sched->do_schedule(sched, prev, now, sched_tasklet_check(cpu));

    next = prev->next_task;

    if ( prev->next_time >= 0 ) /* -ve means no limit */
        set_timer(&sr->s_timer, now + prev->next_time);

    sched_switch_units(sr, next, prev, now);

    return next;
}

static void vcpu_context_saved(struct vcpu *vprev, struct vcpu *vnext)
{
    /* Clear running flag /after/ writing context to memory. */
    smp_wmb();

    if ( vprev != vnext )
        vprev->is_running = false;
}

static void unit_context_saved(struct sched_resource *sr)
{
    struct sched_unit *unit = sr->prev;

    if ( !unit )
        return;

    unit->is_running = false;
    unit->state_entry_time = NOW();
    sr->prev = NULL;

    /* Check for migration request /after/ clearing running flag. */
    smp_mb();

    sched_context_saved(unit_scheduler(unit), unit);

    /* Idle never migrates and idle vcpus might belong to other units. */
    if ( !is_idle_unit(unit) )
        sched_unit_migrate_finish(unit);
}

/*
 * Rendezvous on end of context switch.
 * As no lock is protecting this rendezvous function we need to use atomic
 * access functions on the counter.
 * The counter will be 0 in case no rendezvous is needed. For the rendezvous
 * case it is initialised to the number of cpus to rendezvous plus 1. Each
 * member entering decrements the counter. The last one will decrement it to
 * 1 and perform the final needed action in that case (call of
 * unit_context_saved()), and then set the counter to zero. The other members
 * will wait until the counter becomes zero until they proceed.
 */
void sched_context_switched(struct vcpu *vprev, struct vcpu *vnext)
{
    struct sched_unit *next = vnext->sched_unit;
    struct sched_resource *sr;

    rcu_read_lock(&sched_res_rculock);

    sr = get_sched_res(smp_processor_id());

    if ( atomic_read(&next->rendezvous_out_cnt) )
    {
        int cnt = atomic_dec_return(&next->rendezvous_out_cnt);

        vcpu_context_saved(vprev, vnext);

        /* Call unit_context_saved() before releasing other waiters. */
        if ( cnt == 1 )
        {
            unit_context_saved(sr);
            atomic_set(&next->rendezvous_out_cnt, 0);
        }
        else
            while ( atomic_read(&next->rendezvous_out_cnt) )
                cpu_relax();
    }
    else
    {
        vcpu_context_saved(vprev, vnext);
        if ( sr->granularity == 1 )
            unit_context_saved(sr);
    }

    if ( is_idle_vcpu(vprev) && vprev != vnext )
        vprev->sched_unit = sr->sched_unit_idle;

    rcu_read_unlock(&sched_res_rculock);
}

/*
 * Switch to a new context or keep the current one running.
 * On x86 it won't return, so it needs to drop the still held sched_res_rculock.
 */
static void sched_context_switch(struct vcpu *vprev, struct vcpu *vnext,
                                 bool reset_idle_unit, s_time_t now)
{
    if ( unlikely(vprev == vnext) )
    {
        TRACE_TIME(TRC_SCHED_SWITCH_INFCONT,
                   vnext->domain->domain_id, vnext->sched_unit->unit_id,
                   now - vprev->runstate.state_entry_time,
                   vprev->sched_unit->next_time);
        sched_context_switched(vprev, vnext);

        /*
         * We are switching from a non-idle to an idle unit.
         * A vcpu of the idle unit might have been running before due to
         * the guest vcpu being blocked. We must adjust the unit of the idle
         * vcpu which might have been set to the guest's one.
         */
        if ( reset_idle_unit )
            vnext->sched_unit =
                get_sched_res(smp_processor_id())->sched_unit_idle;

        rcu_read_unlock(&sched_res_rculock);

        trace_continue_running(vnext);
        return continue_running(vprev);
    }

    SCHED_STAT_CRANK(sched_ctx);

    stop_timer(&vprev->periodic_timer);

    if ( vnext->sched_unit->migrated )
        vcpu_move_irqs(vnext);

    vcpu_periodic_timer_work(vnext);

    rcu_read_unlock(&sched_res_rculock);

    context_switch(vprev, vnext);
}

/*
 * Force a context switch of a single vcpu of an unit.
 * Might be called either if a vcpu of an already running unit is woken up
 * or if a vcpu of a running unit is put asleep with other vcpus of the same
 * unit still running.
 * Returns either NULL if v is already in the correct state or the vcpu to
 * run next.
 */
static struct vcpu *sched_force_context_switch(struct vcpu *vprev,
                                               struct vcpu *v,
                                               unsigned int cpu, s_time_t now)
{
    v->force_context_switch = false;

    if ( vcpu_runnable(v) == v->is_running )
        return NULL;

    if ( vcpu_runnable(v) )
    {
        if ( is_idle_vcpu(vprev) )
        {
            vcpu_runstate_change(vprev, RUNSTATE_runnable, now);
            vprev->sched_unit = get_sched_res(cpu)->sched_unit_idle;
        }
        vcpu_runstate_change(v, RUNSTATE_running, now);
    }
    else
    {
        /* Make sure not to switch last vcpu of an unit away. */
        if ( unit_running(v->sched_unit) == 1 )
            return NULL;

        v->new_state = vcpu_runstate_blocked(v);
        vcpu_runstate_change(v, v->new_state, now);
        v = sched_unit2vcpu_cpu(vprev->sched_unit, cpu);
        if ( v != vprev )
        {
            if ( is_idle_vcpu(vprev) )
            {
                vcpu_runstate_change(vprev, RUNSTATE_runnable, now);
                vprev->sched_unit = get_sched_res(cpu)->sched_unit_idle;
            }
            else
            {
                v->sched_unit = vprev->sched_unit;
                vcpu_runstate_change(v, RUNSTATE_running, now);
            }
        }
    }

    /* This vcpu will be switched to. */
    v->is_running = true;

    /* Make sure not to loose another slave call. */
    raise_softirq(SCHED_SLAVE_SOFTIRQ);

    return v;
}

/*
 * Rendezvous before taking a scheduling decision.
 * Called with schedule lock held, so all accesses to the rendezvous counter
 * can be normal ones (no atomic accesses needed).
 * The counter is initialized to the number of cpus to rendezvous initially.
 * Each cpu entering will decrement the counter. In case the counter becomes
 * zero do_schedule() is called and the rendezvous counter for leaving
 * context_switch() is set. All other members will wait until the counter is
 * becoming zero, dropping the schedule lock in between.
 * Either returns the new unit to run, or NULL if no context switch is
 * required or (on Arm) has already been performed. If NULL is returned
 * sched_res_rculock has been dropped.
 */
static struct sched_unit *sched_wait_rendezvous_in(struct sched_unit *prev,
                                                   spinlock_t **lock, int cpu,
                                                   s_time_t now)
{
    struct sched_unit *next;
    struct vcpu *v;
    struct sched_resource *sr = get_sched_res(cpu);
    unsigned int gran = sr->granularity;

    if ( !--prev->rendezvous_in_cnt )
    {
        next = do_schedule(prev, now, cpu);
        atomic_set(&next->rendezvous_out_cnt, gran + 1);
        return next;
    }

    v = unit2vcpu_cpu(prev, cpu);
    while ( prev->rendezvous_in_cnt )
    {
        if ( v && v->force_context_switch )
        {
            struct vcpu *vprev = current;

            v = sched_force_context_switch(vprev, v, cpu, now);

            if ( v )
            {
                /* We'll come back another time, so adjust rendezvous_in_cnt. */
                prev->rendezvous_in_cnt++;
                atomic_set(&prev->rendezvous_out_cnt, 0);

                pcpu_schedule_unlock_irq(*lock, cpu);

                sched_context_switch(vprev, v, false, now);

                return NULL;     /* ARM only. */
            }

            v = unit2vcpu_cpu(prev, cpu);
        }
        /*
         * Check for any work to be done which might need cpu synchronization.
         * This is either pending RCU work, or tasklet work when coming from
         * idle. It is mandatory that RCU softirqs are of higher priority
         * than scheduling ones as otherwise a deadlock might occur.
         * In order to avoid deadlocks we can't do that here, but have to
         * schedule the previous vcpu again, which will lead to the desired
         * processing to be done.
         * Undo the rendezvous_in_cnt decrement and schedule another call of
         * sched_slave().
         */
        BUILD_BUG_ON(RCU_SOFTIRQ > SCHED_SLAVE_SOFTIRQ ||
                     RCU_SOFTIRQ > SCHEDULE_SOFTIRQ);
        if ( rcu_pending(cpu) ||
             (is_idle_unit(prev) && sched_tasklet_check_cpu(cpu)) )
        {
            struct vcpu *vprev = current;

            prev->rendezvous_in_cnt++;
            atomic_set(&prev->rendezvous_out_cnt, 0);

            pcpu_schedule_unlock_irq(*lock, cpu);

            raise_softirq(SCHED_SLAVE_SOFTIRQ);
            sched_context_switch(vprev, vprev, false, now);

            return NULL;         /* ARM only. */
        }

        pcpu_schedule_unlock_irq(*lock, cpu);

        cpu_relax();

        *lock = pcpu_schedule_lock_irq(cpu);

        /*
         * Check for scheduling resource switched. This happens when we are
         * moved away from our cpupool and cpus are subject of the idle
         * scheduler now.
         *
         * This is also a bail out case when scheduler_disable() has been
         * called.
         */
        if ( unlikely(sr != get_sched_res(cpu) || !scheduler_active) )
        {
            ASSERT(is_idle_unit(prev));
            atomic_set(&prev->next_task->rendezvous_out_cnt, 0);
            prev->rendezvous_in_cnt = 0;
            pcpu_schedule_unlock_irq(*lock, cpu);
            rcu_read_unlock(&sched_res_rculock);
            return NULL;
        }
    }

    return prev->next_task;
}

static void cf_check sched_slave(void)
{
    struct vcpu          *v, *vprev = current;
    struct sched_unit    *prev = vprev->sched_unit, *next;
    s_time_t              now;
    spinlock_t           *lock;
    bool                  needs_softirq = false;
    unsigned int          cpu = smp_processor_id();

    ASSERT_NOT_IN_ATOMIC();

    rcu_read_lock(&sched_res_rculock);

    lock = pcpu_schedule_lock_irq(cpu);

    now = NOW();

    v = unit2vcpu_cpu(prev, cpu);
    if ( v && v->force_context_switch )
    {
        v = sched_force_context_switch(vprev, v, cpu, now);

        if ( v )
        {
            pcpu_schedule_unlock_irq(lock, cpu);

            sched_context_switch(vprev, v, false, now);

            return;
        }

        needs_softirq = true;
    }

    if ( !prev->rendezvous_in_cnt )
    {
        pcpu_schedule_unlock_irq(lock, cpu);

        rcu_read_unlock(&sched_res_rculock);

        /* Check for failed forced context switch. */
        if ( needs_softirq )
            raise_softirq(SCHEDULE_SOFTIRQ);

        return;
    }

    stop_timer(&get_sched_res(cpu)->s_timer);

    next = sched_wait_rendezvous_in(prev, &lock, cpu, now);
    if ( !next )
        return;

    pcpu_schedule_unlock_irq(lock, cpu);

    sched_context_switch(vprev, sched_unit2vcpu_cpu(next, cpu),
                         is_idle_unit(next) && !is_idle_unit(prev), now);
}

/*
 * The main function
 * - deschedule the current domain (scheduler independent).
 * - pick a new domain (scheduler dependent).
 */
static void cf_check schedule(void)
{
    struct vcpu          *vnext, *vprev = current;
    struct sched_unit    *prev = vprev->sched_unit, *next = NULL;
    s_time_t              now;
    struct sched_resource *sr;
    spinlock_t           *lock;
    int cpu = smp_processor_id();
    unsigned int          gran;

    ASSERT_NOT_IN_ATOMIC();

    SCHED_STAT_CRANK(sched_run);

    rcu_read_lock(&sched_res_rculock);

    lock = pcpu_schedule_lock_irq(cpu);

    sr = get_sched_res(cpu);
    gran = sr->granularity;

    if ( prev->rendezvous_in_cnt )
    {
        /*
         * We have a race: sched_slave() should be called, so raise a softirq
         * in order to re-enter schedule() later and call sched_slave() now.
         */
        pcpu_schedule_unlock_irq(lock, cpu);

        rcu_read_unlock(&sched_res_rculock);

        raise_softirq(SCHEDULE_SOFTIRQ);
        return sched_slave();
    }

    stop_timer(&sr->s_timer);

    now = NOW();

    if ( gran > 1 )
    {
        cpumask_t *mask = cpumask_scratch_cpu(cpu);

        prev->rendezvous_in_cnt = gran;
        cpumask_andnot(mask, sr->cpus, cpumask_of(cpu));
        cpumask_raise_softirq(mask, SCHED_SLAVE_SOFTIRQ);
        next = sched_wait_rendezvous_in(prev, &lock, cpu, now);
        if ( !next )
            return;
    }
    else
    {
        prev->rendezvous_in_cnt = 0;
        next = do_schedule(prev, now, cpu);
        atomic_set(&next->rendezvous_out_cnt, 0);
    }

    pcpu_schedule_unlock_irq(lock, cpu);

    vnext = sched_unit2vcpu_cpu(next, cpu);
    sched_context_switch(vprev, vnext,
                         !is_idle_unit(prev) && is_idle_unit(next), now);
}

/* The scheduler timer: force a run through the scheduler */
static void cf_check s_timer_fn(void *unused)
{
    raise_softirq(SCHEDULE_SOFTIRQ);
    SCHED_STAT_CRANK(sched_irq);
}

/* Per-VCPU periodic timer function: sends a virtual timer interrupt. */
static void cf_check vcpu_periodic_timer_fn(void *data)
{
    struct vcpu *v = data;
    vcpu_periodic_timer_work(v);
}

/* Per-VCPU single-shot timer function: sends a virtual timer interrupt. */
static void cf_check vcpu_singleshot_timer_fn(void *data)
{
    struct vcpu *v = data;
    send_timer_event(v);
}

/* SCHEDOP_poll timeout callback. */
static void cf_check poll_timer_fn(void *data)
{
    struct vcpu *v = data;

    if ( test_and_clear_bit(v->vcpu_id, v->domain->poll_mask) )
        vcpu_unblock(v);
}

static struct sched_resource *sched_alloc_res(void)
{
    struct sched_resource *sr;

    sr = xzalloc(struct sched_resource);
    if ( sr == NULL )
        return NULL;
    if ( !zalloc_cpumask_var(&sr->cpus) )
    {
        xfree(sr);
        return NULL;
    }
    return sr;
}


static struct sched_resource local_sched_resource_prtos[NR_CPUS];
static int local_sched_resource_prtos_index = 0;
static int cpu_schedule_up(unsigned int cpu)
{
    struct sched_resource *sr;

    // sr = sched_alloc_res();
    // if ( sr == NULL )
    //     return -ENOMEM;
    sr = &local_sched_resource_prtos[local_sched_resource_prtos_index++];
    memset(sr, 0, sizeof(*sr));
    if (local_sched_resource_prtos_index > NR_CPUS) {
        printk("######### local_sched_resource_prtos_index overflow\n");
        local_sched_resource_prtos_index = 0;
        return -ENOMEM;
    }

    if (!zalloc_cpumask_var(&sr->cpus)) {
        printk("######### zalloc_cpumask_var failed\n");
        return -ENOMEM;
    }

    sr->master_cpu = cpu;
    cpumask_copy(sr->cpus, cpumask_of(cpu));
    set_sched_res(cpu, sr);

    sr->scheduler = &sched_idle_ops;
    spin_lock_init(&sr->_lock);
    sr->schedule_lock = &sched_free_cpu_lock;
    init_timer(&sr->s_timer, s_timer_fn, NULL, cpu);
    atomic_set(&per_cpu(sched_urgent_count, cpu), 0);

    /* We start with cpu granularity. */
    sr->granularity = 1;

    cpumask_set_cpu(cpu, &sched_res_mask);

    /* Boot CPU is dealt with later in scheduler_init(). */
    if ( cpu == 0 )
        return 0;

    /*
     * Guard in particular against the compiler suspecting out-of-bounds
     * array accesses below when NR_CPUS=1.
     */
    BUG_ON(cpu >= NR_CPUS);

    if ( idle_vcpu[cpu] == NULL )
        vcpu_create(idle_vcpu[0]->domain, cpu);
    else
        idle_vcpu[cpu]->sched_unit->res = sr;

    if ( idle_vcpu[cpu] == NULL )
        return -ENOMEM;

    idle_vcpu[cpu]->sched_unit->rendezvous_in_cnt = 0;

    /*
     * No need to allocate any scheduler data, as cpus coming online are
     * free initially and the idle scheduler doesn't need any data areas
     * allocated.
     */

    sr->curr = idle_vcpu[cpu]->sched_unit;
    sr->sched_unit_idle = idle_vcpu[cpu]->sched_unit;

    sr->sched_priv = NULL;

    return 0;
}

static void cf_check sched_res_free(struct rcu_head *head)
{
    struct sched_resource *sr = container_of(head, struct sched_resource, rcu);

    free_cpumask_var(sr->cpus);
    if ( sr->sched_unit_idle )
        sched_free_unit_mem(sr->sched_unit_idle);
    xfree(sr);
}

static void cpu_schedule_down(unsigned int cpu)
{
    struct sched_resource *sr;

    rcu_read_lock(&sched_res_rculock);

    sr = get_sched_res(cpu);

    kill_timer(&sr->s_timer);

    cpumask_clear_cpu(cpu, &sched_res_mask);
    set_sched_res(cpu, NULL);

    /* Keep idle unit. */
    sr->sched_unit_idle = NULL;
    call_rcu(&sr->rcu, sched_res_free);

    rcu_read_unlock(&sched_res_rculock);
}

void sched_rm_cpu(unsigned int cpu)
{
    int rc;

    rcu_read_lock(&domlist_read_lock);
    rc = cpu_disable_scheduler(cpu);
    BUG_ON(rc);
    rcu_read_unlock(&domlist_read_lock);
    cpu_schedule_down(cpu);
}

static int cf_check cpu_schedule_callback(
    struct notifier_block *nfb, unsigned long action, void *hcpu)
{
    unsigned int cpu = (unsigned long)hcpu;
    int rc = 0;

    /*
     * All scheduler related suspend/resume handling needed is done in
     * cpupool.c.
     */
    if ( system_state > SYS_STATE_active )
        return NOTIFY_DONE;

    rcu_read_lock(&sched_res_rculock);

    /*
     * From the scheduler perspective, bringing up a pCPU requires
     * allocating and initializing the per-pCPU scheduler specific data,
     * as well as "registering" this pCPU to the scheduler (which may
     * involve modifying some scheduler wide data structures).
     * As new pCPUs always start as "free" cpus with the minimal idle
     * scheduler being in charge, we don't need any of that.
     *
     * On the other hand, at teardown, we need to reverse what has been done
     * during initialization, and then free the per-pCPU specific data. A
     * pCPU brought down is not forced through "free" cpus, so here we need to
     * use the appropriate hooks.
     *
     * This happens by calling the deinit_pdata and free_pdata hooks, in this
     * order. If no per-pCPU memory was allocated, there is no need to
     * provide an implementation of free_pdata. deinit_pdata may, however,
     * be necessary/useful in this case too (e.g., it can undo something done
     * on scheduler wide data structure during switch_sched). Both deinit_pdata
     * and free_pdata are called during CPU_DEAD.
     *
     * If something goes wrong during bringup, we go to CPU_UP_CANCELLED.
     */
    switch ( action )
    {
    case CPU_UP_PREPARE:
        rc = cpu_schedule_up(cpu);
        break;
    case CPU_DOWN_PREPARE:
        rcu_read_lock(&domlist_read_lock);
        rc = cpu_disable_scheduler_check(cpu);
        rcu_read_unlock(&domlist_read_lock);
        break;
    case CPU_DEAD:
        sched_rm_cpu(cpu);
        break;
    case CPU_UP_CANCELED:
        cpu_schedule_down(cpu);
        break;
    default:
        break;
    }

    rcu_read_unlock(&sched_res_rculock);

    return notifier_from_errno(rc);
}

int cpu_schedule_up_prtos(unsigned int cpu) {
    return cpu_schedule_up(cpu);
}

static struct notifier_block cpu_schedule_nfb = {
    .notifier_call = cpu_schedule_callback
};

const cpumask_t *sched_get_opt_cpumask(enum sched_gran opt, unsigned int cpu)
{
    const cpumask_t *mask;

    switch ( opt )
    {
    case SCHED_GRAN_cpu:
        mask = cpumask_of(cpu);
        break;
    case SCHED_GRAN_core:
        mask = per_cpu(cpu_sibling_mask, cpu);
        break;
    case SCHED_GRAN_socket:
        mask = per_cpu(cpu_core_mask, cpu);
        break;
    default:
        ASSERT_UNREACHABLE();
        return NULL;
    }

    return mask;
}

static void cf_check schedule_dummy(void)
{
    sched_tasklet_check_cpu(smp_processor_id());
}


void scheduler_enable(void)
{
    open_softirq(SCHEDULE_SOFTIRQ, schedule);
    open_softirq(SCHED_SLAVE_SOFTIRQ, sched_slave);
    scheduler_active = true;
}

static inline
const struct scheduler *__init sched_get_by_name(const char *sched_name)
{
    /* PRTOS: only the idle scheduler is available */
    return &sched_idle_ops;
}


/* Initialise the data structures. */
void __init scheduler_init(void)
{
    struct domain *idle_domain;
    int i;

    scheduler_enable();

    /*
     * PRTOS uses its own cyclic table-driven scheduler (sched.c).
     * Use the built-in idle scheduler for Xen domain infrastructure only.
     */
    operations = sched_idle_ops;

    if ( cpu_schedule_up(0) )
        BUG();
    register_cpu_notifier(&cpu_schedule_nfb);

    printk("Using scheduler: %s (%s) [PRTOS cyclic scheduler active]\n",
           operations.name, operations.opt_name);

    /*
     * The idle dom is created privileged to ensure unrestricted access during
     * setup and will be demoted by xsm_set_system_active() when setup is
     * complete.
     */
    idle_domain = domain_create(DOMID_IDLE, NULL, CDF_privileged);
    BUG_ON(IS_ERR(idle_domain));
    BUG_ON(nr_cpu_ids > ARRAY_SIZE(idle_vcpu));
    idle_domain->vcpu = idle_vcpu;
    idle_domain->max_vcpus = nr_cpu_ids;
    if ( vcpu_create(idle_domain, 0) == NULL )
        BUG();

    rcu_read_lock(&sched_res_rculock);

    get_sched_res(0)->curr = idle_vcpu[0]->sched_unit;
    get_sched_res(0)->sched_unit_idle = idle_vcpu[0]->sched_unit;

    rcu_read_unlock(&sched_res_rculock);
}

/*
 * Move a pCPU from free cpus (running the idle scheduler) to a cpupool
 * using any "real" scheduler.
 * The cpu is still marked as "free" and not yet valid for its cpupool.
 */
int schedule_cpu_add(unsigned int cpu, struct cpupool *c)
{
    struct vcpu *idle;
    void *ppriv, *vpriv;
    struct scheduler *new_ops = c->sched;
    struct sched_resource *sr;
    spinlock_t *old_lock, *new_lock;
    unsigned long flags;
    int ret = 0;

    rcu_read_lock(&sched_res_rculock);

    sr = get_sched_res(cpu);

    ASSERT(cpumask_test_cpu(cpu, &cpupool_free_cpus));
    ASSERT(!cpumask_test_cpu(cpu, c->cpu_valid));
    ASSERT(get_sched_res(cpu)->cpupool == NULL);

    /*
     * To setup the cpu for the new scheduler we need:
     *  - a valid instance of per-CPU scheduler specific data, as it is
     *    allocated by sched_alloc_pdata(). Note that we do not want to
     *    initialize it yet, as that will be done by the target scheduler,
     *    in sched_switch_sched(), in proper ordering and with locking.
     *  - a valid instance of per-vCPU scheduler specific data, for the idle
     *    vCPU of cpu. That is what the target scheduler will use for the
     *    sched_priv field of the per-vCPU info of the idle domain.
     */
    idle = idle_vcpu[cpu];
    ppriv = sched_alloc_pdata(new_ops, cpu);
    if ( IS_ERR(ppriv) )
    {
        ret = PTR_ERR(ppriv);
        goto out;
    }

    vpriv = sched_alloc_udata(new_ops, idle->sched_unit,
                              idle->domain->sched_priv);
    if ( vpriv == NULL )
    {
        sched_free_pdata(new_ops, ppriv, cpu);
        ret = -ENOMEM;
        goto out;
    }

    /*
     * The actual switch, including the rerouting of the scheduler lock to
     * whatever new_ops prefers, needs to happen in one critical section,
     * protected by old_ops' lock, or races are possible.
     * It is, in fact, the lock of the idle scheduler that we are taking.
     * But that is ok as anyone trying to schedule on this cpu will spin until
     * when we release that lock (bottom of this function). When he'll get the
     * lock --thanks to the loop inside *_schedule_lock() functions-- he'll
     * notice that the lock itself changed, and retry acquiring the new one
     * (which will be the correct, remapped one, at that point).
     */
    old_lock = pcpu_schedule_lock_irqsave(cpu, &flags);

    if ( cpupool_get_granularity(c) > 1 )
    {
        const cpumask_t *mask;
        unsigned int cpu_iter, idx = 0;
        struct sched_unit *master_unit;
        struct sched_resource *sr_old;

        /*
         * We need to merge multiple idle_vcpu units and sched_resource structs
         * into one. As the free cpus all share the same lock we are fine doing
         * that now. The worst which could happen would be someone waiting for
         * the lock, thus dereferencing sched_res->schedule_lock. This is the
         * reason we are freeing struct sched_res via call_rcu() to avoid the
         * lock pointer suddenly disappearing.
         */
        mask = sched_get_opt_cpumask(c->gran, cpu);
        master_unit = idle_vcpu[cpu]->sched_unit;

        for_each_cpu ( cpu_iter, mask )
        {
            if ( idx )
                cpumask_clear_cpu(cpu_iter, &sched_res_mask);

            per_cpu(sched_res_idx, cpu_iter) = idx++;

            if ( cpu == cpu_iter )
                continue;

            sr_old = get_sched_res(cpu_iter);
            kill_timer(&sr_old->s_timer);
            idle_vcpu[cpu_iter]->sched_unit = master_unit;
            master_unit->runstate_cnt[RUNSTATE_running]++;
            set_sched_res(cpu_iter, sr);
            cpumask_set_cpu(cpu_iter, sr->cpus);

            call_rcu(&sr_old->rcu, sched_res_free);
        }
    }

    new_lock = sched_switch_sched(new_ops, cpu, ppriv, vpriv);

    sr->scheduler = new_ops;
    sr->sched_priv = ppriv;
    sr->granularity = cpupool_get_granularity(c);
    sr->cpupool = c;

    /*
     * Reroute the lock to the per pCPU lock as /last/ thing. In fact,
     * if it is free (and it can be) we want that anyone that manages
     * taking it, finds all the initializations we've done above in place.
     */
    smp_wmb();
    sr->schedule_lock = new_lock;

    /* _Not_ pcpu_schedule_unlock(): schedule_lock has changed! */
    spin_unlock_irqrestore(old_lock, flags);

    /* The  cpu is added to a pool, trigger it to go pick up some work */
    cpu_raise_softirq(cpu, SCHEDULE_SOFTIRQ);

out:
    rcu_read_unlock(&sched_res_rculock);

    return ret;
}

/*
 * Allocate all memory needed for free_cpu_rm_data(), as allocations cannot
 * be made in stop_machine() context.
 *
 * Between alloc_cpu_rm_data() and the real cpu removal action the relevant
 * contents of struct sched_resource can't change, as the cpu in question is
 * locked against any other movement to or from cpupools, and the data copied
 * by alloc_cpu_rm_data() is modified only in case the cpu in question is
 * being moved from or to a cpupool.
 */
struct cpu_rm_data *alloc_cpu_rm_data(unsigned int cpu, bool aff_alloc)
{
    struct cpu_rm_data *data;
    const struct sched_resource *sr;
    unsigned int idx;

    rcu_read_lock(&sched_res_rculock);

    sr = get_sched_res(cpu);
    data = xmalloc_flex_struct(struct cpu_rm_data, sr, sr->granularity - 1);
    if ( !data )
        goto out;

    if ( aff_alloc )
    {
        if ( !alloc_affinity_masks(&data->affinity) )
        {
            XFREE(data);
            goto out;
        }
    }
    else
        memset(&data->affinity, 0, sizeof(data->affinity));

    data->old_ops = sr->scheduler;
    data->vpriv_old = idle_vcpu[cpu]->sched_unit->priv;
    data->ppriv_old = sr->sched_priv;

    for ( idx = 0; idx < sr->granularity - 1; idx++ )
    {
        data->sr[idx] = sched_alloc_res();
        if ( data->sr[idx] )
        {
            data->sr[idx]->sched_unit_idle = sched_alloc_unit_mem();
            if ( !data->sr[idx]->sched_unit_idle )
            {
                sched_res_free(&data->sr[idx]->rcu);
                data->sr[idx] = NULL;
            }
        }
        if ( !data->sr[idx] )
        {
            while ( idx > 0 )
                sched_res_free(&data->sr[--idx]->rcu);
            free_affinity_masks(&data->affinity);
            XFREE(data);
            goto out;
        }

        data->sr[idx]->curr = data->sr[idx]->sched_unit_idle;
        data->sr[idx]->scheduler = &sched_idle_ops;
        data->sr[idx]->granularity = 1;

        /* We want the lock not to change when replacing the resource. */
        data->sr[idx]->schedule_lock = sr->schedule_lock;
    }

 out:
    rcu_read_unlock(&sched_res_rculock);

    return data;
}

void free_cpu_rm_data(struct cpu_rm_data *mem, unsigned int cpu)
{
    sched_free_udata(mem->old_ops, mem->vpriv_old);
    sched_free_pdata(mem->old_ops, mem->ppriv_old, cpu);
    free_affinity_masks(&mem->affinity);

    xfree(mem);
}

/*
 * Remove a pCPU from its cpupool. Its scheduler becomes &sched_idle_ops
 * (the idle scheduler).
 * The cpu is already marked as "free" and not valid any longer for its
 * cpupool.
 */
int schedule_cpu_rm(unsigned int cpu, struct cpu_rm_data *data)
{
    struct sched_resource *sr;
    struct sched_unit *unit;
    spinlock_t *old_lock;
    unsigned long flags;
    int idx = 0;
    unsigned int cpu_iter;
    bool free_data = !data;

    if ( !data )
        data = alloc_cpu_rm_data(cpu, false);
    if ( !data )
        return -ENOMEM;

    rcu_read_lock(&sched_res_rculock);

    sr = get_sched_res(cpu);

    ASSERT(sr->granularity);
    ASSERT(sr->cpupool != NULL);
    ASSERT(cpumask_test_cpu(cpu, &cpupool_free_cpus));
    ASSERT(!cpumask_test_cpu(cpu, sr->cpupool->cpu_valid));

    /* See comment in schedule_cpu_add() regarding lock switching. */
    old_lock = pcpu_schedule_lock_irqsave(cpu, &flags);

    for_each_cpu ( cpu_iter, sr->cpus )
    {
        per_cpu(sched_res_idx, cpu_iter) = 0;
        if ( cpu_iter == cpu )
        {
            unit = idle_vcpu[cpu_iter]->sched_unit;
            unit->priv = NULL;
            atomic_set(&unit->next_task->rendezvous_out_cnt, 0);
            unit->rendezvous_in_cnt = 0;
        }
        else
        {
            /* Initialize unit. */
            unit = data->sr[idx]->sched_unit_idle;
            unit->res = data->sr[idx];
            unit->is_running = true;
            sched_unit_add_vcpu(unit, idle_vcpu[cpu_iter]);
            sched_domain_insert_unit(unit, idle_vcpu[cpu_iter]->domain);

            /* Adjust cpu masks of resources (old and new). */
            cpumask_clear_cpu(cpu_iter, sr->cpus);
            cpumask_set_cpu(cpu_iter, data->sr[idx]->cpus);
            cpumask_set_cpu(cpu_iter, &sched_res_mask);

            /* Init timer. */
            init_timer(&data->sr[idx]->s_timer, s_timer_fn, NULL, cpu_iter);

            /* Last resource initializations and insert resource pointer. */
            data->sr[idx]->master_cpu = cpu_iter;
            set_sched_res(cpu_iter, data->sr[idx]);

            /* Last action: set the new lock pointer. */
            smp_mb();
            data->sr[idx]->schedule_lock = &sched_free_cpu_lock;

            idx++;
        }
    }
    sr->scheduler = &sched_idle_ops;
    sr->sched_priv = NULL;
    sr->granularity = 1;
    sr->cpupool = NULL;

    smp_mb();
    sr->schedule_lock = &sched_free_cpu_lock;

    /* _Not_ pcpu_schedule_unlock(): schedule_lock may have changed! */
    spin_unlock_irqrestore(old_lock, flags);

    sched_deinit_pdata(data->old_ops, data->ppriv_old, cpu);

    rcu_read_unlock(&sched_res_rculock);
    if ( free_data )
        free_cpu_rm_data(data, cpu);

    return 0;
}

struct scheduler *scheduler_get_default(void)
{
    return &operations;
}

struct scheduler *scheduler_alloc(unsigned int sched_id)
{
    struct scheduler *sched;

    /* PRTOS: only the idle scheduler is available */
    if ( (sched = xmalloc(struct scheduler)) == NULL )
        return ERR_PTR(-ENOMEM);
    memcpy(sched, &sched_idle_ops, sizeof(*sched));
    return sched;
}

void scheduler_free(struct scheduler *sched)
{
    BUG_ON(sched == &operations);
    sched_deinit(sched);
    xfree(sched);
}

void schedule_dump(struct cpupool *c)
{
    unsigned int      i, j;
    struct scheduler *sched;
    cpumask_t        *cpus;

    /* Locking, if necessary, must be handled withing each scheduler */

    rcu_read_lock(&sched_res_rculock);

    if ( c != NULL )
    {
        sched = c->sched;
        cpus = c->res_valid;
        printk("Scheduler: %s (%s)\n", sched->name, sched->opt_name);
        sched_dump_settings(sched);
    }
    else
    {
        sched = &operations;
        cpus = &cpupool_free_cpus;
    }

    printk("CPUs info:\n");
    for_each_cpu (i, cpus)
    {
        struct sched_resource *sr = get_sched_res(i);
        unsigned long flags;
        spinlock_t *lock;

        lock = pcpu_schedule_lock_irqsave(i, &flags);

        printk("CPU[%02d] current=%pv, curr=%pv, prev=%pv\n", i,
               get_cpu_current(i), sr->curr ? sr->curr->vcpu_list : NULL,
               sr->prev ? sr->prev->vcpu_list : NULL);
        for_each_cpu (j, sr->cpus)
            if ( i != j )
                printk("CPU[%02d] current=%pv\n", j, get_cpu_current(j));

        pcpu_schedule_unlock_irqrestore(lock, flags, i);

        sched_dump_cpu_state(sched, i);
    }

    rcu_read_unlock(&sched_res_rculock);
}

void wait(void)
{
    schedule();
}

#ifdef CONFIG_X86
void __init sched_setup_dom0_vcpus(struct domain *d)
{
    unsigned int i;

    for ( i = 1; i < d->max_vcpus; i++ )
        vcpu_create(d, i);

    domain_update_node_affinity(d);
}
#endif

#ifdef CONFIG_COMPAT
#include "compat.c"
#endif

#endif /* !COMPAT */

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: core.c === */
/* === BEGIN INLINED: cpupool.c === */
#include <xen_xen_config.h>
/******************************************************************************
 * cpupool.c
 * 
 * Generic cpupool-handling functions.
 *
 * Cpupools are a feature to have configurable scheduling domains. Each
 * cpupool runs an own scheduler on a dedicated set of physical cpus.
 * A domain is bound to one cpupool at any time, but it can be moved to
 * another cpupool.
 *
 * (C) 2009, Juergen Gross, Fujitsu Technology Solutions
 */

#include <xen_cpu.h>
#include <xen_cpumask.h>
#include <xen_guest_access.h>
#include <xen_hypfs.h>
#include <xen_init.h>
#include <xen_keyhandler.h>
#include <xen_lib.h>
#include <xen_list.h>
#include <xen_param.h>
#include <xen_percpu.h>
#include <xen_sched.h>
#include <xen_warning.h>

#include "private.h"

struct cpupool *cpupool0;                /* Initial cpupool with Dom0 */
cpumask_t cpupool_free_cpus;             /* cpus not in any cpupool */

static LIST_HEAD(cpupool_list);          /* linked list, sorted by poolid */
static unsigned int n_cpupools;

static int cpupool_moving_cpu = -1;
static struct cpupool *cpupool_cpu_moving = NULL;
static cpumask_t cpupool_locked_cpus;

/* This lock nests inside sysctl or hypfs lock. */
static DEFINE_SPINLOCK(cpupool_lock);

static enum sched_gran __read_mostly opt_sched_granularity = SCHED_GRAN_cpu;
static unsigned int __read_mostly sched_granularity = 1;

#define SCHED_GRAN_NAME_LEN  8
struct sched_gran_name {
    enum sched_gran mode;
    char name[SCHED_GRAN_NAME_LEN];
};

static const struct sched_gran_name sg_name[] = {
    {SCHED_GRAN_cpu, "cpu"},
    {SCHED_GRAN_core, "core"},
    {SCHED_GRAN_socket, "socket"},
};

static const char *sched_gran_get_name(enum sched_gran mode)
{
    const char *name = "";
    unsigned int i;

    for ( i = 0; i < ARRAY_SIZE(sg_name); i++ )
    {
        if ( mode == sg_name[i].mode )
        {
            name = sg_name[i].name;
            break;
        }
    }

    return name;
}

static void sched_gran_print(enum sched_gran mode, unsigned int gran)
{
    printk("Scheduling granularity: %s, %u CPU%s per sched-resource\n",
           sched_gran_get_name(mode), gran, gran == 1 ? "" : "s");
}

#ifdef CONFIG_HAS_SCHED_GRANULARITY
static int sched_gran_get(const char *str, enum sched_gran *mode)
{
    unsigned int i;

    for ( i = 0; i < ARRAY_SIZE(sg_name); i++ )
    {
        if ( strcmp(sg_name[i].name, str) == 0 )
        {
            *mode = sg_name[i].mode;
            return 0;
        }
    }

    return -EINVAL;
}

static int __init cf_check sched_select_granularity(const char *str)
{
    return sched_gran_get(str, &opt_sched_granularity);
}
custom_param("sched-gran", sched_select_granularity);
#elif defined(CONFIG_HYPFS)
static int sched_gran_get(const char *str, enum sched_gran *mode)
{
    return -EINVAL;
}
#endif

static unsigned int cpupool_check_granularity(enum sched_gran mode)
{
    unsigned int cpu;
    unsigned int siblings, gran = 0;

    if ( mode == SCHED_GRAN_cpu )
        return 1;

    for_each_online_cpu ( cpu )
    {
        siblings = cpumask_weight(sched_get_opt_cpumask(mode, cpu));
        if ( gran == 0 )
            gran = siblings;
        else if ( gran != siblings )
            return 0;
    }

    return gran;
}

/* Setup data for selected scheduler granularity. */
static void __init cpupool_gran_init(void)
{
    unsigned int gran = 0;
    const char *fallback = NULL;

    while ( gran == 0 )
    {
        gran = cpupool_check_granularity(opt_sched_granularity);

        if ( gran == 0 )
        {
            switch ( opt_sched_granularity )
            {
            case SCHED_GRAN_core:
                opt_sched_granularity = SCHED_GRAN_cpu;
                fallback = "Asymmetric cpu configuration.\n"
                           "Falling back to sched-gran=cpu.\n";
                break;
            case SCHED_GRAN_socket:
                opt_sched_granularity = SCHED_GRAN_core;
                fallback = "Asymmetric cpu configuration.\n"
                           "Falling back to sched-gran=core.\n";
                break;
            default:
                ASSERT_UNREACHABLE();
                break;
            }
        }
    }

    if ( fallback )
        warning_add(fallback);

    if ( opt_sched_granularity != SCHED_GRAN_cpu )
        sched_disable_smt_switching = true;

    sched_granularity = gran;
    sched_gran_print(opt_sched_granularity, sched_granularity);
}

unsigned int cpupool_get_granularity(const struct cpupool *c)
{
    return c ? c->sched_gran : 1;
}

static void free_cpupool_struct(struct cpupool *c)
{
    if ( c )
    {
        free_cpumask_var(c->res_valid);
        free_cpumask_var(c->cpu_valid);
    }
    xfree(c);
}

static struct cpupool *alloc_cpupool_struct(void)
{
    struct cpupool *c = xzalloc(struct cpupool);

    if ( !c )
        return NULL;

    if ( !zalloc_cpumask_var(&c->cpu_valid) ||
         !zalloc_cpumask_var(&c->res_valid) )
    {
        free_cpupool_struct(c);
        c = NULL;
    }

    return c;
}

/*
 * find a cpupool by it's id. to be called with cpupool lock held
 * if exact is not specified, the first cpupool with an id larger or equal to
 * the searched id is returned
 * returns NULL if not found.
 */
static struct cpupool *__cpupool_find_by_id(unsigned int id, bool exact)
{
    struct cpupool *q;

    ASSERT(spin_is_locked(&cpupool_lock));

    list_for_each_entry(q, &cpupool_list, list)
        if ( q->cpupool_id == id || (!exact && q->cpupool_id > id) )
            return q;

    return NULL;
}

static struct cpupool *cpupool_find_by_id(unsigned int poolid)
{
    return __cpupool_find_by_id(poolid, true);
}

static struct cpupool *__cpupool_get_by_id(unsigned int poolid, bool exact)
{
    struct cpupool *c;
    spin_lock(&cpupool_lock);
    c = __cpupool_find_by_id(poolid, exact);
    if ( c != NULL )
        atomic_inc(&c->refcnt);
    spin_unlock(&cpupool_lock);
    return c;
}

struct cpupool *cpupool_get_by_id(unsigned int poolid)
{
    return __cpupool_get_by_id(poolid, true);
}

static struct cpupool *cpupool_get_next_by_id(unsigned int poolid)
{
    return __cpupool_get_by_id(poolid, false);
}

void cpupool_put(struct cpupool *pool)
{
    if ( !atomic_dec_and_test(&pool->refcnt) )
        return;
    scheduler_free(pool->sched);
    free_cpupool_struct(pool);
}

/*
 * create a new cpupool with specified poolid and scheduler
 * returns pointer to new cpupool structure if okay, NULL else
 * possible failures:
 * - no memory
 * - poolid already used
 * - unknown scheduler
 */
static struct cpupool *cpupool_create(unsigned int poolid,
                                      unsigned int sched_id)
{
    struct cpupool *c;
    struct cpupool *q;
    int ret;

    if ( (c = alloc_cpupool_struct()) == NULL )
        return ERR_PTR(-ENOMEM);

    /* One reference for caller, one reference for cpupool_destroy(). */
    atomic_set(&c->refcnt, 2);

    debugtrace_printk("cpupool_create(pool=%u,sched=%u)\n", poolid, sched_id);

    spin_lock(&cpupool_lock);

    /* Don't allow too many cpupools. */
    if ( n_cpupools >= 2 * nr_cpu_ids )
    {
        ret = -ENOSPC;
        goto unlock;
    }
    n_cpupools++;

    if ( poolid != CPUPOOLID_NONE )
    {
        q = __cpupool_find_by_id(poolid, false);
        if ( !q )
            list_add_tail(&c->list, &cpupool_list);
        else
        {
            list_add_tail(&c->list, &q->list);
            if ( q->cpupool_id == poolid )
            {
                ret = -EEXIST;
                goto err;
            }
        }

        c->cpupool_id = poolid;
    }
    else
    {
        /* Cpupool 0 is created with specified id at boot and never removed. */
        ASSERT(!list_empty(&cpupool_list));

        q = list_last_entry(&cpupool_list, struct cpupool, list);
        /* In case of wrap search for first free id. */
        if ( q->cpupool_id == CPUPOOLID_NONE - 1 )
        {
            list_for_each_entry(q, &cpupool_list, list)
                if ( q->cpupool_id + 1 != list_next_entry(q, list)->cpupool_id )
                    break;
        }

        list_add(&c->list, &q->list);

        c->cpupool_id = q->cpupool_id + 1;
    }

    c->sched = scheduler_alloc(sched_id);
    if ( IS_ERR(c->sched) )
    {
        ret = PTR_ERR(c->sched);
        goto err;
    }

    c->sched->cpupool = c;
    c->gran = opt_sched_granularity;
    c->sched_gran = sched_granularity;

    spin_unlock(&cpupool_lock);

    debugtrace_printk("Created cpupool %u with scheduler %s (%s)\n",
                      c->cpupool_id, c->sched->name, c->sched->opt_name);

    return c;

 err:
    list_del(&c->list);
    n_cpupools--;

 unlock:
    spin_unlock(&cpupool_lock);

    free_cpupool_struct(c);

    return ERR_PTR(ret);
}
/*
 * destroys the given cpupool
 * returns 0 on success, 1 else
 * possible failures:
 * - pool still in use
 * - cpus still assigned to pool
 */
static int cpupool_destroy(struct cpupool *c)
{
    spin_lock(&cpupool_lock);

    if ( (c->n_dom != 0) || cpumask_weight(c->cpu_valid) )
    {
        spin_unlock(&cpupool_lock);
        return -EBUSY;
    }

    n_cpupools--;
    list_del(&c->list);

    spin_unlock(&cpupool_lock);

    cpupool_put(c);

    debugtrace_printk("cpupool_destroy(pool=%u)\n", c->cpupool_id);
    return 0;
}

/*
 * Move domain to another cpupool
 */
static int cpupool_move_domain_locked(struct domain *d, struct cpupool *c)
{
    int ret;

    if ( unlikely(d->cpupool == c) )
        return 0;

    d->cpupool->n_dom--;
    ret = sched_move_domain(d, c);
    if ( ret )
        d->cpupool->n_dom++;
    else
        c->n_dom++;

    return ret;
}
int cpupool_move_domain(struct domain *d, struct cpupool *c)
{
    int ret;

    spin_lock(&cpupool_lock);

    ret = cpupool_move_domain_locked(d, c);

    spin_unlock(&cpupool_lock);

    return ret;
}

/* Update affinities of all domains in a cpupool. */
static void cpupool_update_node_affinity(const struct cpupool *c,
                                         struct affinity_masks *masks)
{
    struct affinity_masks local_masks;
    struct domain *d;

    if ( !masks )
    {
        if ( !alloc_affinity_masks(&local_masks) )
            return;
        masks = &local_masks;
    }

    rcu_read_lock(&domlist_read_lock);

    for_each_domain_in_cpupool(d, c)
        domain_update_node_aff(d, masks);

    rcu_read_unlock(&domlist_read_lock);

    if ( masks == &local_masks )
        free_affinity_masks(masks);
}

/*
 * assign a specific cpu to a cpupool
 * cpupool_lock must be held
 */
static int cpupool_assign_cpu_locked(struct cpupool *c, unsigned int cpu)
{
    int ret;
    const cpumask_t *cpus;

    cpus = sched_get_opt_cpumask(c->gran, cpu);

    if ( (cpupool_moving_cpu == cpu) && (c != cpupool_cpu_moving) )
        return -EADDRNOTAVAIL;
    ret = schedule_cpu_add(cpumask_first(cpus), c);
    if ( ret )
        return ret;

    rcu_read_lock(&sched_res_rculock);

    cpumask_andnot(&cpupool_free_cpus, &cpupool_free_cpus, cpus);
    if (cpupool_moving_cpu == cpu)
    {
        cpupool_moving_cpu = -1;
        cpupool_put(cpupool_cpu_moving);
        cpupool_cpu_moving = NULL;
    }
    cpumask_or(c->cpu_valid, c->cpu_valid, cpus);
    cpumask_and(c->res_valid, c->cpu_valid, &sched_res_mask);

    rcu_read_unlock(&sched_res_rculock);

    cpupool_update_node_affinity(c, NULL);

    return 0;
}

static int cpupool_unassign_cpu_finish(struct cpupool *c,
                                       struct cpu_rm_data *mem)
{
    int cpu = cpupool_moving_cpu;
    const cpumask_t *cpus;
    struct affinity_masks *masks = mem ? &mem->affinity : NULL;
    int ret;

    if ( c != cpupool_cpu_moving )
        return -EADDRNOTAVAIL;

    rcu_read_lock(&domlist_read_lock);
    ret = cpu_disable_scheduler(cpu);
    rcu_read_unlock(&domlist_read_lock);

    rcu_read_lock(&sched_res_rculock);
    cpus = get_sched_res(cpu)->cpus;
    cpumask_or(&cpupool_free_cpus, &cpupool_free_cpus, cpus);

    /*
     * cpu_disable_scheduler() returning an error doesn't require resetting
     * cpupool_free_cpus' cpu bit. All error cases should be of temporary
     * nature and tools will retry the operation. Even if the number of
     * retries may be limited, the in-between state can easily be repaired
     * by adding the cpu to the cpupool again.
     */
    if ( !ret )
    {
        ret = schedule_cpu_rm(cpu, mem);
        if ( ret )
            cpumask_andnot(&cpupool_free_cpus, &cpupool_free_cpus, cpus);
        else
        {
            cpupool_moving_cpu = -1;
            cpupool_put(cpupool_cpu_moving);
            cpupool_cpu_moving = NULL;
        }
    }
    rcu_read_unlock(&sched_res_rculock);

    cpupool_update_node_affinity(c, masks);

    return ret;
}

static int cpupool_unassign_cpu_start(struct cpupool *c, unsigned int cpu)
{
    int ret;
    struct domain *d;
    const cpumask_t *cpus;

    spin_lock(&cpupool_lock);
    ret = -EADDRNOTAVAIL;
    if ( ((cpupool_moving_cpu != -1) || !cpumask_test_cpu(cpu, c->cpu_valid))
         && (cpu != cpupool_moving_cpu) )
        goto out;

    ret = 0;
    rcu_read_lock(&sched_res_rculock);
    cpus = get_sched_res(cpu)->cpus;

    if ( (c->n_dom > 0) &&
         (cpumask_weight(c->cpu_valid) == cpumask_weight(cpus)) &&
         (cpu != cpupool_moving_cpu) )
    {
        rcu_read_lock(&domlist_read_lock);
        for_each_domain_in_cpupool(d, c)
        {
            if ( !d->is_dying && system_state == SYS_STATE_active )
            {
                ret = -EBUSY;
                break;
            }
            ret = cpupool_move_domain_locked(d, cpupool0);
            if ( ret )
                break;
        }
        rcu_read_unlock(&domlist_read_lock);
        if ( ret )
            goto out_rcu;
    }
    cpupool_moving_cpu = cpu;
    atomic_inc(&c->refcnt);
    cpupool_cpu_moving = c;
    cpumask_andnot(c->cpu_valid, c->cpu_valid, cpus);
    cpumask_and(c->res_valid, c->cpu_valid, &sched_res_mask);

 out_rcu:
    rcu_read_unlock(&sched_res_rculock);
 out:
    spin_unlock(&cpupool_lock);

    return ret;
}

static long cf_check cpupool_unassign_cpu_helper(void *info)
{
    struct cpupool *c = info;
    long ret;

    debugtrace_printk("cpupool_unassign_cpu(pool=%u,cpu=%d)\n",
                      cpupool_cpu_moving->cpupool_id, cpupool_moving_cpu);
    spin_lock(&cpupool_lock);

    ret = cpupool_unassign_cpu_finish(c, NULL);

    spin_unlock(&cpupool_lock);
    debugtrace_printk("cpupool_unassign_cpu ret=%ld\n", ret);

    return ret;
}

/*
 * unassign a specific cpu from a cpupool
 * we must be sure not to run on the cpu to be unassigned! to achieve this
 * the main functionality is performed via continue_hypercall_on_cpu on a
 * specific cpu.
 * if the cpu to be removed is the last one of the cpupool no active domain
 * must be bound to the cpupool. dying domains are moved to cpupool0 as they
 * might be zombies.
 * possible failures:
 * - last cpu and still active domains in cpupool
 * - cpu just being unplugged
 * - Attempt to remove boot cpu from cpupool0
 */
static int cpupool_unassign_cpu(struct cpupool *c, unsigned int cpu)
{
    int work_cpu;
    int ret;
    unsigned int master_cpu;

    debugtrace_printk("cpupool_unassign_cpu(pool=%u,cpu=%d)\n",
                      c->cpupool_id, cpu);

    /*
     * Cpu0 must remain in cpupool0, otherwise some operations like moving cpus
     * between cpupools, cpu hotplug, destroying cpupools, shutdown of the host,
     * might not work in a sane way.
     */
    if ( (!c->cpupool_id && !cpu) || !cpu_online(cpu) )
        return -EINVAL;

    master_cpu = sched_get_resource_cpu(cpu);
    ret = cpupool_unassign_cpu_start(c, master_cpu);
    if ( ret )
    {
        debugtrace_printk("cpupool_unassign_cpu(pool=%u,cpu=%d) ret %d\n",
                          c->cpupool_id, cpu, ret);
        return ret;
    }

    work_cpu = sched_get_resource_cpu(smp_processor_id());
    if ( work_cpu == master_cpu )
    {
        work_cpu = cpumask_first(cpupool0->cpu_valid);
        if ( work_cpu == master_cpu )
            work_cpu = cpumask_last(cpupool0->cpu_valid);
    }
    return continue_hypercall_on_cpu(work_cpu, cpupool_unassign_cpu_helper, c);
}

/*
 * add a new domain to a cpupool
 * possible failures:
 * - pool does not exist
 * - no cpu assigned to pool
 */
int cpupool_add_domain(struct domain *d, unsigned int poolid)
{
    struct cpupool *c;
    int rc;
    int n_dom = 0;

    spin_lock(&cpupool_lock);
    c = cpupool_find_by_id(poolid);
    if ( c == NULL )
        rc = -ESRCH;
    else if ( !cpumask_weight(c->cpu_valid) )
        rc = -ENODEV;
    else
    {
        c->n_dom++;
        n_dom = c->n_dom;
        d->cpupool = c;
        rc = 0;
    }
    spin_unlock(&cpupool_lock);
    debugtrace_printk("cpupool_add_domain(dom=%d,pool=%u) n_dom %d rc %d\n",
                      d->domain_id, poolid, n_dom, rc);
    return rc;
}

/*
 * remove a domain from a cpupool
 */
void cpupool_rm_domain(struct domain *d)
{
    unsigned int cpupool_id;
    int n_dom;

    if ( d->cpupool == NULL )
        return;
    spin_lock(&cpupool_lock);
    cpupool_id = d->cpupool->cpupool_id;
    d->cpupool->n_dom--;
    n_dom = d->cpupool->n_dom;
    d->cpupool = NULL;
    spin_unlock(&cpupool_lock);
    debugtrace_printk("cpupool_rm_domain(dom=%d,pool=%u) n_dom %d\n",
                      d->domain_id, cpupool_id, n_dom);
    return;
}

/*
 * Called to add a cpu to a pool. CPUs being hot-plugged are added to pool0,
 * as they must have been in there when unplugged.
 */
static int cpupool_cpu_add(unsigned int cpu)
{
    int ret = 0;
    const cpumask_t *cpus;

    spin_lock(&cpupool_lock);
    cpumask_clear_cpu(cpu, &cpupool_locked_cpus);
    cpumask_set_cpu(cpu, &cpupool_free_cpus);

    /*
     * If we are not resuming, we are hot-plugging cpu, and in which case
     * we add it to pool0, as it certainly was there when hot-unplagged
     * (or unplugging would have failed) and that is the default behavior
     * anyway.
     */
    rcu_read_lock(&sched_res_rculock);
    get_sched_res(cpu)->cpupool = NULL;

    cpus = sched_get_opt_cpumask(cpupool0->gran, cpu);
    if ( cpumask_subset(cpus, &cpupool_free_cpus) &&
         cpumask_weight(cpus) == cpupool_get_granularity(cpupool0) )
        ret = cpupool_assign_cpu_locked(cpupool0, cpu);

    rcu_read_unlock(&sched_res_rculock);

    spin_unlock(&cpupool_lock);

    return ret;
}

/*
 * This function is called in stop_machine context, so we can be sure no
 * non-idle vcpu is active on the system.
 */
static void cpupool_cpu_remove(unsigned int cpu, struct cpu_rm_data *mem)
{
    int ret;

    ASSERT(is_idle_vcpu(current));

    if ( !cpumask_test_cpu(cpu, &cpupool_free_cpus) )
    {
        ret = cpupool_unassign_cpu_finish(cpupool0, mem);
        BUG_ON(ret);
    }
    cpumask_clear_cpu(cpu, &cpupool_free_cpus);
}

/*
 * Called before a CPU is being removed from the system.
 * Removing a CPU is allowed for free CPUs or CPUs in Pool-0 (those are moved
 * to free cpus actually before removing them).
 * The CPU is locked, to forbid adding it again to another cpupool.
 */
static int cpupool_cpu_remove_prologue(unsigned int cpu)
{
    int ret = 0;
    cpumask_t *cpus;
    unsigned int master_cpu;

    spin_lock(&cpupool_lock);

    rcu_read_lock(&sched_res_rculock);
    cpus = get_sched_res(cpu)->cpus;
    master_cpu = sched_get_resource_cpu(cpu);
    if ( cpumask_intersects(cpus, &cpupool_locked_cpus) )
        ret = -EBUSY;
    else
        cpumask_set_cpu(cpu, &cpupool_locked_cpus);
    rcu_read_unlock(&sched_res_rculock);

    spin_unlock(&cpupool_lock);

    if ( ret )
        return  ret;

    if ( cpumask_test_cpu(master_cpu, cpupool0->cpu_valid) )
    {
        /* Cpupool0 is populated only after all cpus are up. */
        ASSERT(system_state == SYS_STATE_active);

        ret = cpupool_unassign_cpu_start(cpupool0, master_cpu);
    }
    else if ( !cpumask_test_cpu(master_cpu, &cpupool_free_cpus) )
        ret = -ENODEV;

    return ret;
}

/*
 * Called during resume for all cpus which didn't come up again. The cpu must
 * be removed from the cpupool it is assigned to. In case a cpupool will be
 * left without cpu we move all domains of that cpupool to cpupool0.
 * As we are called with all domains still frozen there is no need to take the
 * cpupool lock here.
 */
static void cpupool_cpu_remove_forced(unsigned int cpu)
{
    struct cpupool *c;
    int ret;
    unsigned int master_cpu = sched_get_resource_cpu(cpu);

    list_for_each_entry(c, &cpupool_list, list)
    {
        if ( cpumask_test_cpu(master_cpu, c->cpu_valid) )
        {
            ret = cpupool_unassign_cpu_start(c, master_cpu);
            BUG_ON(ret);
            ret = cpupool_unassign_cpu_finish(c, NULL);
            BUG_ON(ret);
        }
    }

    cpumask_clear_cpu(cpu, &cpupool_free_cpus);

    rcu_read_lock(&sched_res_rculock);
    sched_rm_cpu(cpu);
    rcu_read_unlock(&sched_res_rculock);
}

/*
 * do cpupool related sysctl operations
 */
int cpupool_do_sysctl(struct xen_sysctl_cpupool_op *op)
{
    int ret = 0;
    struct cpupool *c;

    switch ( op->op )
    {

    case XEN_SYSCTL_CPUPOOL_OP_CREATE:
    {
        unsigned int poolid;

        poolid = (op->cpupool_id == XEN_SYSCTL_CPUPOOL_PAR_ANY) ?
            CPUPOOLID_NONE: op->cpupool_id;
        c = cpupool_create(poolid, op->sched_id);
        if ( IS_ERR(c) )
            ret = PTR_ERR(c);
        else
        {
            op->cpupool_id = c->cpupool_id;
            cpupool_put(c);
        }
    }
    break;

    case XEN_SYSCTL_CPUPOOL_OP_DESTROY:
    {
        c = cpupool_get_by_id(op->cpupool_id);
        ret = -ENOENT;
        if ( c == NULL )
            break;
        ret = cpupool_destroy(c);
        cpupool_put(c);
    }
    break;

    case XEN_SYSCTL_CPUPOOL_OP_INFO:
    {
        c = cpupool_get_next_by_id(op->cpupool_id);
        ret = -ENOENT;
        if ( c == NULL )
            break;
        op->cpupool_id = c->cpupool_id;
        op->sched_id = c->sched->sched_id;
        op->n_dom = c->n_dom;
        ret = cpumask_to_xenctl_bitmap(&op->cpumap, c->cpu_valid);
        cpupool_put(c);
    }
    break;

    case XEN_SYSCTL_CPUPOOL_OP_ADDCPU:
    {
        unsigned int cpu;
        const cpumask_t *cpus;

        cpu = op->cpu;
        debugtrace_printk("cpupool_assign_cpu(pool=%u,cpu=%u)\n",
                          op->cpupool_id, cpu);

        spin_lock(&cpupool_lock);

        c = cpupool_find_by_id(op->cpupool_id);
        ret = -ENOENT;
        if ( c == NULL )
            goto addcpu_out;
        if ( cpu == XEN_SYSCTL_CPUPOOL_PAR_ANY )
        {
            for_each_cpu ( cpu, &cpupool_free_cpus )
            {
                cpus = sched_get_opt_cpumask(c->gran, cpu);
                if ( cpumask_subset(cpus, &cpupool_free_cpus) )
                    break;
            }
            ret = -ENODEV;
            if ( cpu >= nr_cpu_ids )
                goto addcpu_out;
        }
        ret = -EINVAL;
        if ( cpu >= nr_cpu_ids )
            goto addcpu_out;
        ret = -ENODEV;
        if ( !cpu_online(cpu) )
            goto addcpu_out;
        cpus = sched_get_opt_cpumask(c->gran, cpu);
        if ( !cpumask_subset(cpus, &cpupool_free_cpus) ||
             cpumask_intersects(cpus, &cpupool_locked_cpus) )
            goto addcpu_out;
        ret = cpupool_assign_cpu_locked(c, cpu);

    addcpu_out:
        spin_unlock(&cpupool_lock);
        debugtrace_printk("cpupool_assign_cpu(pool=%u,cpu=%u) ret %d\n",
                          op->cpupool_id, cpu, ret);

    }
    break;

    case XEN_SYSCTL_CPUPOOL_OP_RMCPU:
    {
        unsigned int cpu;

        c = cpupool_get_by_id(op->cpupool_id);
        ret = -ENOENT;
        if ( c == NULL )
            break;
        cpu = op->cpu;
        if ( cpu == XEN_SYSCTL_CPUPOOL_PAR_ANY )
            cpu = cpumask_last(c->cpu_valid);
        ret = (cpu < nr_cpu_ids) ? cpupool_unassign_cpu(c, cpu) : -EINVAL;
        cpupool_put(c);
    }
    break;

    case XEN_SYSCTL_CPUPOOL_OP_MOVEDOMAIN:
    {
        struct domain *d;

        ret = rcu_lock_remote_domain_by_id(op->domid, &d);
        if ( ret )
            break;
        if ( d->cpupool == NULL )
        {
            ret = -EINVAL;
            rcu_unlock_domain(d);
            break;
        }
        if ( op->cpupool_id == d->cpupool->cpupool_id )
        {
            ret = 0;
            rcu_unlock_domain(d);
            break;
        }
        debugtrace_printk("cpupool move_domain(dom=%d)->pool=%u\n",
                          d->domain_id, op->cpupool_id);
        ret = -ENOENT;
        spin_lock(&cpupool_lock);

        c = cpupool_find_by_id(op->cpupool_id);
        if ( (c != NULL) && cpumask_weight(c->cpu_valid) )
            ret = cpupool_move_domain_locked(d, c);

        spin_unlock(&cpupool_lock);
        debugtrace_printk("cpupool move_domain(dom=%d)->pool=%u ret %d\n",
                          d->domain_id, op->cpupool_id, ret);
        rcu_unlock_domain(d);
    }
    break;

    case XEN_SYSCTL_CPUPOOL_OP_FREEINFO:
    {
        ret = cpumask_to_xenctl_bitmap(
            &op->cpumap, &cpupool_free_cpus);
    }
    break;

    default:
        ret = -ENOSYS;
        break;
    }

    return ret;
}

unsigned int cpupool_get_id(const struct domain *d)
{
    return d->cpupool ? d->cpupool->cpupool_id : CPUPOOLID_NONE;
}

const cpumask_t *cpupool_valid_cpus(const struct cpupool *pool)
{
    return pool->cpu_valid;
}

void cf_check dump_runq(unsigned char key)
{
    s_time_t         now = NOW();
    struct cpupool *c;

    spin_lock(&cpupool_lock);

    printk("sched_smt_power_savings: %s\n",
            sched_smt_power_savings? "enabled":"disabled");
    printk("NOW=%"PRI_stime"\n", now);

    printk("Online Cpus: %*pbl\n", CPUMASK_PR(&cpu_online_map));
    if ( !cpumask_empty(&cpupool_free_cpus) )
    {
        printk("Free Cpus: %*pbl\n", CPUMASK_PR(&cpupool_free_cpus));
        schedule_dump(NULL);
    }

    list_for_each_entry(c, &cpupool_list, list)
    {
        printk("Cpupool %u:\n", c->cpupool_id);
        printk("Cpus: %*pbl\n", CPUMASK_PR(c->cpu_valid));
        sched_gran_print(c->gran, cpupool_get_granularity(c));
        schedule_dump(c);
    }

    spin_unlock(&cpupool_lock);
}

static int cf_check cpu_callback(
    struct notifier_block *nfb, unsigned long action, void *hcpu)
{
    static struct cpu_rm_data *mem;

    unsigned int cpu = (unsigned long)hcpu;
    int rc = 0;

    switch ( action )
    {
    case CPU_DOWN_FAILED:
        if ( system_state <= SYS_STATE_active )
        {
            if ( mem )
            {
                free_cpu_rm_data(mem, cpu);
                mem = NULL;
            }
            rc = cpupool_cpu_add(cpu);
        }
        break;
    case CPU_ONLINE:
        if ( system_state <= SYS_STATE_active )
            rc = cpupool_cpu_add(cpu);
        else
            sched_migrate_timers(cpu);
        break;
    case CPU_DOWN_PREPARE:
        /* Suspend/Resume don't change assignments of cpus to cpupools. */
        if ( system_state <= SYS_STATE_active )
        {
            rc = cpupool_cpu_remove_prologue(cpu);
            if ( !rc )
            {
                ASSERT(!mem);
                mem = alloc_cpu_rm_data(cpu, true);
                rc = mem ? 0 : -ENOMEM;
            }
        }
        break;
    case CPU_DYING:
        /* Suspend/Resume don't change assignments of cpus to cpupools. */
        if ( system_state <= SYS_STATE_active )
        {
            ASSERT(mem);
            cpupool_cpu_remove(cpu, mem);
        }
        break;
    case CPU_DEAD:
        if ( system_state <= SYS_STATE_active )
        {
            ASSERT(mem);
            free_cpu_rm_data(mem, cpu);
            mem = NULL;
        }
        break;
    case CPU_RESUME_FAILED:
        cpupool_cpu_remove_forced(cpu);
        break;
    default:
        break;
    }

    return notifier_from_errno(rc);
}

static struct notifier_block cpu_nfb = {
    .notifier_call = cpu_callback
};

#ifdef CONFIG_HYPFS

static HYPFS_DIR_INIT(cpupool_pooldir, "%u");

static int cf_check cpupool_dir_read(
    const struct hypfs_entry *entry, XEN_GUEST_HANDLE_PARAM(void) uaddr)
{
    int ret = 0;
    struct cpupool *c;
    struct hypfs_dyndir_id *data;

    data = hypfs_get_dyndata();

    list_for_each_entry(c, &cpupool_list, list)
    {
        data->id = c->cpupool_id;
        data->data = c;

        ret = hypfs_read_dyndir_id_entry(&cpupool_pooldir, c->cpupool_id,
                                         list_is_last(&c->list, &cpupool_list),
                                         &uaddr);
        if ( ret )
            break;
    }

    return ret;
}

static unsigned int cf_check cpupool_dir_getsize(
    const struct hypfs_entry *entry)
{
    const struct cpupool *c;
    unsigned int size = 0;

    list_for_each_entry(c, &cpupool_list, list)
        size += hypfs_dynid_entry_size(entry, c->cpupool_id);

    return size;
}

static const struct hypfs_entry *cf_check cpupool_dir_enter(
    const struct hypfs_entry *entry)
{
    struct hypfs_dyndir_id *data;

    data = hypfs_alloc_dyndata(struct hypfs_dyndir_id);
    if ( !data )
        return ERR_PTR(-ENOMEM);
    data->id = CPUPOOLID_NONE;

    spin_lock(&cpupool_lock);

    return entry;
}

static void cf_check cpupool_dir_exit(const struct hypfs_entry *entry)
{
    spin_unlock(&cpupool_lock);

    hypfs_free_dyndata();
}

static struct hypfs_entry *cf_check cpupool_dir_findentry(
    const struct hypfs_entry_dir *dir, const char *name, unsigned int name_len)
{
    unsigned long id;
    const char *end;
    struct cpupool *cpupool;

    id = simple_strtoul(name, &end, 10);
    if ( end != name + name_len || id > UINT_MAX )
        return ERR_PTR(-ENOENT);

    cpupool = __cpupool_find_by_id(id, true);

    if ( !cpupool )
        return ERR_PTR(-ENOENT);

    return hypfs_gen_dyndir_id_entry(&cpupool_pooldir, id, cpupool);
}

static int cf_check cpupool_gran_read(
    const struct hypfs_entry *entry, XEN_GUEST_HANDLE_PARAM(void) uaddr)
{
    const struct hypfs_dyndir_id *data;
    const struct cpupool *cpupool;
    const char *gran;

    data = hypfs_get_dyndata();
    cpupool = data->data;
    ASSERT(cpupool);

    gran = sched_gran_get_name(cpupool->gran);

    if ( !*gran )
        return -ENOENT;

    return copy_to_guest(uaddr, gran, strlen(gran) + 1) ? -EFAULT : 0;
}

static unsigned int cf_check hypfs_gran_getsize(const struct hypfs_entry *entry)
{
    const struct hypfs_dyndir_id *data;
    const struct cpupool *cpupool;
    const char *gran;

    data = hypfs_get_dyndata();
    cpupool = data->data;
    ASSERT(cpupool);

    gran = sched_gran_get_name(cpupool->gran);

    return strlen(gran) + 1;
}

static int cf_check cpupool_gran_write(
    struct hypfs_entry_leaf *leaf, XEN_GUEST_HANDLE_PARAM(const_void) uaddr,
    unsigned int ulen)
{
    const struct hypfs_dyndir_id *data;
    struct cpupool *cpupool;
    enum sched_gran gran;
    unsigned int sched_gran = 0;
    char name[SCHED_GRAN_NAME_LEN];
    int ret = 0;

    if ( ulen > SCHED_GRAN_NAME_LEN )
        return -ENOSPC;

    if ( copy_from_guest(name, uaddr, ulen) )
        return -EFAULT;

    if ( memchr(name, 0, ulen) == (name + ulen - 1) )
        sched_gran = sched_gran_get(name, &gran) ?
                     0 : cpupool_check_granularity(gran);
    if ( sched_gran == 0 )
        return -EINVAL;

    data = hypfs_get_dyndata();
    cpupool = data->data;
    ASSERT(cpupool);

    /* Guarded by the cpupool_lock taken in cpupool_dir_enter(). */
    if ( !cpumask_empty(cpupool->cpu_valid) )
        ret = -EBUSY;
    else
    {
        cpupool->gran = gran;
        cpupool->sched_gran = sched_gran;
    }

    return ret;
}

static const struct hypfs_funcs cpupool_gran_funcs = {
    .enter = hypfs_node_enter,
    .exit = hypfs_node_exit,
    .read = cpupool_gran_read,
    .write = cpupool_gran_write,
    .getsize = hypfs_gran_getsize,
    .findentry = hypfs_leaf_findentry,
};

static HYPFS_VARSIZE_INIT(cpupool_gran, XEN_HYPFS_TYPE_STRING, "sched-gran",
                          SCHED_GRAN_NAME_LEN, &cpupool_gran_funcs);
static char granstr[SCHED_GRAN_NAME_LEN] = {
    [0 ... SCHED_GRAN_NAME_LEN - 2] = '?',
    [SCHED_GRAN_NAME_LEN - 1] = 0
};

static const struct hypfs_funcs cpupool_dir_funcs = {
    .enter = cpupool_dir_enter,
    .exit = cpupool_dir_exit,
    .read = cpupool_dir_read,
    .write = hypfs_write_deny,
    .getsize = cpupool_dir_getsize,
    .findentry = cpupool_dir_findentry,
};

static HYPFS_DIR_INIT_FUNC(cpupool_dir, "cpupool", &cpupool_dir_funcs);

static void cpupool_hypfs_init(void)
{
    hypfs_add_dir(&hypfs_root, &cpupool_dir, true);
    hypfs_add_dyndir(&cpupool_dir, &cpupool_pooldir);
    hypfs_string_set_reference(&cpupool_gran, granstr);
    hypfs_add_leaf(&cpupool_pooldir, &cpupool_gran, true);
}

#else /* CONFIG_HYPFS */

static void cpupool_hypfs_init(void)
{
}

#endif /* CONFIG_HYPFS */

struct cpupool *__init cpupool_create_pool(unsigned int pool_id, int sched_id)
{
    struct cpupool *pool;

    if ( sched_id < 0 )
        sched_id = scheduler_get_default()->sched_id;

    pool = cpupool_create(pool_id, sched_id);

    BUG_ON(IS_ERR(pool));
    cpupool_put(pool);

    return pool;
}

static int __init cf_check cpupool_init(void)
{
    unsigned int cpu;

    cpupool_gran_init();

    cpupool_hypfs_init();

    register_cpu_notifier(&cpu_nfb);

    btcpupools_dtb_parse();

    btcpupools_allocate_pools();

    spin_lock(&cpupool_lock);

    cpumask_copy(&cpupool_free_cpus, &cpu_online_map);

    for_each_cpu ( cpu, &cpupool_free_cpus )
    {
        unsigned int pool_id = btcpupools_get_cpupool_id(cpu);
        struct cpupool *pool = cpupool_find_by_id(pool_id);

        ASSERT(pool);
        cpupool_assign_cpu_locked(pool, cpu);
    }

    spin_unlock(&cpupool_lock);

    return 0;
}
__initcall(cpupool_init);

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: cpupool.c === */
