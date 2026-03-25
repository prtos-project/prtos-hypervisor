/* Xen event channels & hypercalls - consolidated */
/* === BEGIN INLINED: event_2l.c === */
#include <xen_xen_config.h>
/*
 * Event channel port operations.
 *
 * Copyright (c) 2003-2006, K A Fraser.
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2 or later.  See the file COPYING for more details.
 */

/* --- event_channel.h (inlined) --- */
#ifndef __EVENT_CHANNEL_PRIV_H__
#define __EVENT_CHANNEL_PRIV_H__

#include <xen_event.h>

static inline unsigned int max_evtchns(const struct domain *d)
{
    return d->evtchn_fifo ? EVTCHN_FIFO_NR_CHANNELS
                          : BITS_PER_EVTCHN_WORD(d) * BITS_PER_EVTCHN_WORD(d);
}

static inline bool evtchn_is_busy(const struct domain *d,
                                  const struct evtchn *evtchn)
{
    return d->evtchn_port_ops->is_busy &&
           d->evtchn_port_ops->is_busy(d, evtchn);
}

static inline void evtchn_port_unmask(struct domain *d,
                                      struct evtchn *evtchn)
{
    if ( evtchn_usable(evtchn) )
        d->evtchn_port_ops->unmask(d, evtchn);
}

static inline int evtchn_port_set_priority(struct domain *d,
                                           struct evtchn *evtchn,
                                           unsigned int priority)
{
    if ( !d->evtchn_port_ops->set_priority )
        return -ENOSYS;
    if ( !evtchn_usable(evtchn) )
        return -EACCES;
    return d->evtchn_port_ops->set_priority(d, evtchn, priority);
}

static inline void evtchn_port_print_state(struct domain *d,
                                           const struct evtchn *evtchn)
{
    d->evtchn_port_ops->print_state(d, evtchn);
}

void evtchn_2l_init(struct domain *d);

int evtchn_fifo_init_control(struct evtchn_init_control *init_control);
int evtchn_fifo_expand_array(const struct evtchn_expand_array *expand_array);
void evtchn_fifo_destroy(struct domain *d);

#endif /* __EVENT_CHANNEL_PRIV_H__ */
/* --- end event_channel.h --- */

#include <xen_init.h>
#include <xen_lib.h>
#include <xen_errno.h>
#include <xen_sched.h>

#include <asm_guest_atomics.h>

static void cf_check evtchn_2l_set_pending(
    struct vcpu *v, struct evtchn *evtchn)
{
    struct domain *d = v->domain;
    unsigned int port = evtchn->port;

    /*
     * The following bit operations must happen in strict order.
     * NB. On x86, the atomic bit operations also act as memory barriers.
     * There is therefore sufficiently strict ordering for this architecture --
     * others may require explicit memory barriers.
     */

    if ( guest_test_and_set_bit(d, port, &shared_info(d, evtchn_pending)) )
        return;

    if ( !guest_test_bit(d, port, &shared_info(d, evtchn_mask)) &&
         !guest_test_and_set_bit(d, port / BITS_PER_EVTCHN_WORD(d),
                                 &vcpu_info(v, evtchn_pending_sel)) )
    {
        vcpu_mark_events_pending(v);
    }

    evtchn_check_pollers(d, port);
}

static void cf_check evtchn_2l_clear_pending(
    struct domain *d, struct evtchn *evtchn)
{
    guest_clear_bit(d, evtchn->port, &shared_info(d, evtchn_pending));
}

static void cf_check evtchn_2l_unmask(
    struct domain *d, struct evtchn *evtchn)
{
    struct vcpu *v = d->vcpu[evtchn->notify_vcpu_id];
    unsigned int port = evtchn->port;

    /*
     * These operations must happen in strict order. Based on
     * evtchn_2l_set_pending() above.
     */
    if ( guest_test_and_clear_bit(d, port, &shared_info(d, evtchn_mask)) &&
         guest_test_bit(d, port, &shared_info(d, evtchn_pending)) &&
         !guest_test_and_set_bit(d, port / BITS_PER_EVTCHN_WORD(d),
                                 &vcpu_info(v, evtchn_pending_sel)) )
    {
        vcpu_mark_events_pending(v);
    }
}

static bool cf_check evtchn_2l_is_pending(
    const struct domain *d, const struct evtchn *evtchn)
{
    evtchn_port_t port = evtchn->port;
    unsigned int max_ports = BITS_PER_EVTCHN_WORD(d) * BITS_PER_EVTCHN_WORD(d);

    ASSERT(port < max_ports);
    return (port < max_ports &&
            guest_test_bit(d, port, &shared_info(d, evtchn_pending)));
}

static bool cf_check evtchn_2l_is_masked(
    const struct domain *d, const struct evtchn *evtchn)
{
    evtchn_port_t port = evtchn->port;
    unsigned int max_ports = BITS_PER_EVTCHN_WORD(d) * BITS_PER_EVTCHN_WORD(d);

    ASSERT(port < max_ports);
    return (port >= max_ports ||
            guest_test_bit(d, port, &shared_info(d, evtchn_mask)));
}

static void cf_check evtchn_2l_print_state(
    struct domain *d, const struct evtchn *evtchn)
{
    struct vcpu *v = d->vcpu[evtchn->notify_vcpu_id];

    printk("%d", !!test_bit(evtchn->port / BITS_PER_EVTCHN_WORD(d),
                            &vcpu_info(v, evtchn_pending_sel)));
}

static const struct evtchn_port_ops evtchn_port_ops_2l =
{
    .set_pending   = evtchn_2l_set_pending,
    .clear_pending = evtchn_2l_clear_pending,
    .unmask        = evtchn_2l_unmask,
    .is_pending    = evtchn_2l_is_pending,
    .is_masked     = evtchn_2l_is_masked,
    .print_state   = evtchn_2l_print_state,
};

void evtchn_2l_init(struct domain *d)
{
    d->evtchn_port_ops = &evtchn_port_ops_2l;
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

/* === END INLINED: event_2l.c === */
/* === BEGIN INLINED: event_channel.c === */
#include <xen_xen_config.h>
/******************************************************************************
 * event_channel.c
 * 
 * Event notifications from VIRQs, PIRQs, and other domains.
 * 
 * Copyright (c) 2003-2006, K A Fraser.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; If not, see <http://www.gnu.org/licenses/>.
 */

/* event_channel.h inlined below */

#include <xen_init.h>
#include <xen_lib.h>
#include <xen_errno.h>
#include <xen_sched.h>
#include <xen_irq.h>
#include <xen_iocap.h>
#include <xen_compat.h>
#include <xen_guest_access.h>
#include <xen_hypercall.h>
#include <xen_keyhandler.h>
#include <asm_current.h>

#include <public_xen.h>
#include <public_event_channel.h>
#include <xsm_xsm.h>

#ifdef CONFIG_PV_SHIM
#include <asm/guest.h>
#endif

#define consumer_is_xen(e) (!!(e)->xen_consumer)

/*
 * Lock an event channel exclusively. This is allowed only when the channel is
 * free or unbound either when taking or when releasing the lock, as any
 * concurrent operation on the event channel using evtchn_read_trylock() will
 * just assume the event channel is free or unbound at the moment when the
 * evtchn_read_trylock() returns false.
 */
static always_inline void evtchn_write_lock(struct evtchn *evtchn)
{
    write_lock(&evtchn->lock);

#ifndef NDEBUG
    evtchn->old_state = evtchn->state;
#endif
}

static inline unsigned int old_state(const struct evtchn *evtchn)
{
#ifndef NDEBUG
    return evtchn->old_state;
#else
    return ECS_RESERVED; /* Just to allow things to build. */
#endif
}

static inline void evtchn_write_unlock(struct evtchn *evtchn)
{
    /* Enforce lock discipline. */
    ASSERT(old_state(evtchn) == ECS_FREE || old_state(evtchn) == ECS_UNBOUND ||
           evtchn->state == ECS_FREE || evtchn->state == ECS_UNBOUND);

    write_unlock(&evtchn->lock);
}

/*
 * The function alloc_unbound_xen_event_channel() allows an arbitrary
 * notifier function to be specified. However, very few unique functions
 * are specified in practice, so to prevent bloating the evtchn structure
 * with a pointer, we stash them dynamically in a small lookup array which
 * can be indexed by a small integer.
 */
static xen_event_channel_notification_t __read_mostly
    xen_consumers[NR_XEN_CONSUMERS];

/* Default notification action: wake up from wait_on_xen_event_channel(). */
static void cf_check default_xen_notification_fn(
    struct vcpu *v, unsigned int port)
{
    /* Consumer needs notification only if blocked. */
    if ( test_and_clear_bit(_VPF_blocked_in_xen, &v->pause_flags) )
        vcpu_wake(v);
}

/*
 * Given a notification function, return the value to stash in
 * the evtchn->xen_consumer field.
 */
static uint8_t get_xen_consumer(xen_event_channel_notification_t fn)
{
    unsigned int i;

    if ( fn == NULL )
        fn = default_xen_notification_fn;

    for ( i = 0; i < ARRAY_SIZE(xen_consumers); i++ )
    {
        /* Use cmpxchgptr() in lieu of a global lock. */
        if ( xen_consumers[i] == NULL )
            cmpxchgptr(&xen_consumers[i], NULL, fn);
        if ( xen_consumers[i] == fn )
            break;
    }

    BUG_ON(i >= ARRAY_SIZE(xen_consumers));
    return i+1;
}

/* Get the notification function for a given Xen-bound event channel. */
#define xen_notification_fn(e) (xen_consumers[(e)->xen_consumer-1])

static bool virq_is_global(unsigned int virq)
{
    switch ( virq )
    {
    case VIRQ_TIMER:
    case VIRQ_DEBUG:
    case VIRQ_XENOPROF:
    case VIRQ_XENPMU:
        return false;

    case VIRQ_ARCH_0 ... VIRQ_ARCH_7:
        return arch_virq_is_global(virq);
    }

    ASSERT(virq < NR_VIRQS);
    return true;
}

static struct evtchn *_evtchn_from_port(const struct domain *d,
                                        evtchn_port_t port)
{
    return port_is_valid(d, port) ? evtchn_from_port(d, port) : NULL;
}

static void free_evtchn_bucket(struct domain *d, struct evtchn *bucket)
{
    if ( !bucket )
        return;

    xsm_free_security_evtchns(bucket, EVTCHNS_PER_BUCKET);
    xfree(bucket);
}

static struct evtchn *alloc_evtchn_bucket(struct domain *d, unsigned int port)
{
    struct evtchn *chn;
    unsigned int i;

    chn = xzalloc_array(struct evtchn, EVTCHNS_PER_BUCKET);
    if ( !chn )
        goto err;

    if ( xsm_alloc_security_evtchns(chn, EVTCHNS_PER_BUCKET) )
        goto err;

    for ( i = 0; i < EVTCHNS_PER_BUCKET; i++ )
    {
        chn[i].port = port + i;
        rwlock_init(&chn[i].lock);
    }

    return chn;

 err:
    free_evtchn_bucket(d, chn);
    return NULL;
}

/*
 * Allocate a given port and ensure all the buckets up to that ports
 * have been allocated.
 *
 * The last part is important because the rest of the event channel code
 * relies on all the buckets up to d->valid_evtchns to be valid. However,
 * event channels may be sparsed when allocating the static evtchn port
 * numbers that are scattered in nature.
 */
int evtchn_allocate_port(struct domain *d, evtchn_port_t port)
{
    if ( port > d->max_evtchn_port || port >= max_evtchns(d) )
        return -ENOSPC;

    if ( port_is_valid(d, port) )
    {
        const struct evtchn *chn = evtchn_from_port(d, port);

        if ( chn->state != ECS_FREE || evtchn_is_busy(d, chn) )
            return -EBUSY;
    }
    else
    {
        unsigned int alloc_port = read_atomic(&d->valid_evtchns);

        do
        {
            struct evtchn *chn;
            struct evtchn **grp;

            if ( !group_from_port(d, alloc_port) )
            {
                grp = xzalloc_array(struct evtchn *, BUCKETS_PER_GROUP);
                if ( !grp )
                    return -ENOMEM;
                group_from_port(d, alloc_port) = grp;
            }

            chn = alloc_evtchn_bucket(d, alloc_port);
            if ( !chn )
                return -ENOMEM;
            bucket_from_port(d, alloc_port) = chn;

            /*
             * d->valid_evtchns is used to check whether the bucket can be
             * accessed without the per-domain lock. Therefore,
             * d->valid_evtchns should be seen *after* the new bucket has
             * been setup.
             */
            smp_wmb();
            alloc_port += EVTCHNS_PER_BUCKET;
            write_atomic(&d->valid_evtchns, alloc_port);
        } while ( port >= alloc_port );
    }

    write_atomic(&d->active_evtchns, d->active_evtchns + 1);

    return 0;
}

static int get_free_port(struct domain *d)
{
    int            port;

    if ( d->is_dying )
        return -EINVAL;

    for ( port = 0; port <= d->max_evtchn_port; port++ )
    {
        int rc = evtchn_allocate_port(d, port);

        if ( rc == 0 )
            return port;
        else if ( rc != -EBUSY )
            return rc;
    }

    return -ENOSPC;
}

/*
 * Check whether a port is still marked free, and if so update the domain
 * counter accordingly.  To be used on function exit paths.
 */
static void check_free_port(struct domain *d, evtchn_port_t port)
{
    if ( port_is_valid(d, port) &&
         evtchn_from_port(d, port)->state == ECS_FREE )
        write_atomic(&d->active_evtchns, d->active_evtchns - 1);
}

void evtchn_free(struct domain *d, struct evtchn *chn)
{
    /* Clear pending event to avoid unexpected behavior on re-bind. */
    evtchn_port_clear_pending(d, chn);

    if ( consumer_is_xen(chn) )
    {
        write_atomic(&d->xen_evtchns, d->xen_evtchns - 1);
        /* Decrement ->xen_evtchns /before/ ->active_evtchns. */
        smp_wmb();
    }
    write_atomic(&d->active_evtchns, d->active_evtchns - 1);

    /* Reset binding to vcpu0 when the channel is freed. */
    chn->state          = ECS_FREE;
    chn->notify_vcpu_id = 0;
    chn->xen_consumer   = 0;

    xsm_evtchn_close_post(chn);
}

static int evtchn_get_port(struct domain *d, evtchn_port_t port)
{
    int rc;

    if ( port != 0 )
        rc = evtchn_allocate_port(d, port);
    else
        rc = get_free_port(d);

    return rc ?: port;
}

/*
 * If port is zero get the next free port and allocate. If port is non-zero
 * allocate the specified port.
 */
int evtchn_alloc_unbound(evtchn_alloc_unbound_t *alloc, evtchn_port_t port)
{
    struct evtchn *chn;
    struct domain *d;
    int            rc;
    domid_t        dom = alloc->dom;

    d = rcu_lock_domain_by_any_id(dom);
    if ( d == NULL )
        return -ESRCH;

    write_lock(&d->event_lock);

    port = rc = evtchn_get_port(d, port);
    if ( rc < 0 )
    {
        gdprintk(XENLOG_WARNING, "EVTCHNOP failure: error %d\n", rc);
        goto out;
    }

    chn = evtchn_from_port(d, port);

    rc = xsm_evtchn_unbound(XSM_TARGET, d, chn, alloc->remote_dom);
    if ( rc )
        goto out;

    evtchn_write_lock(chn);

    chn->state = ECS_UNBOUND;
    if ( (chn->u.unbound.remote_domid = alloc->remote_dom) == DOMID_SELF )
        chn->u.unbound.remote_domid = current->domain->domain_id;
    evtchn_port_init(d, chn);

    evtchn_write_unlock(chn);

    alloc->port = port;

 out:
    check_free_port(d, port);
    write_unlock(&d->event_lock);
    rcu_unlock_domain(d);

    return rc;
}

static always_inline void double_evtchn_lock(struct evtchn *lchn,
                                             struct evtchn *rchn)
{
    ASSERT(lchn != rchn);

    if ( lchn > rchn )
        SWAP(lchn, rchn);

    evtchn_write_lock(lchn);
    evtchn_write_lock(rchn);
}

static void double_evtchn_unlock(struct evtchn *lchn, struct evtchn *rchn)
{
    evtchn_write_unlock(lchn);
    evtchn_write_unlock(rchn);
}

/*
 * If lport is zero get the next free port and allocate. If port is non-zero
 * allocate the specified lport.
 */
int evtchn_bind_interdomain(evtchn_bind_interdomain_t *bind, struct domain *ld,
                            evtchn_port_t lport)
{
    struct evtchn *lchn, *rchn;
    struct domain *rd;
    int            rc;
    evtchn_port_t  rport = bind->remote_port;
    domid_t        rdom = bind->remote_dom;

    if ( (rd = rcu_lock_domain_by_any_id(rdom)) == NULL )
        return -ESRCH;

    /* Avoid deadlock by first acquiring lock of domain with smaller id. */
    if ( ld < rd )
    {
        write_lock(&ld->event_lock);
        write_lock(&rd->event_lock);
    }
    else
    {
        if ( ld != rd )
            write_lock(&rd->event_lock);
        write_lock(&ld->event_lock);
    }

    lport = rc = evtchn_get_port(ld, lport);
    if ( rc < 0 )
    {
        gdprintk(XENLOG_WARNING, "EVTCHNOP failure: error %d\n", rc);
        goto out;
    }

    rc = 0;

    lchn = evtchn_from_port(ld, lport);

    rchn = _evtchn_from_port(rd, rport);
    if ( !rchn )
    {
        rc = -EINVAL;
        gdprintk(XENLOG_WARNING, "EVTCHNOP failure: %pd, error %d\n", rd, rc);
        goto out;
    }

    if ( (rchn->state != ECS_UNBOUND) ||
         (rchn->u.unbound.remote_domid != ld->domain_id) )
    {
        rc = -EINVAL;
        gdprintk(XENLOG_WARNING, "EVTCHNOP failure: %pd, error %d\n", rd, rc);
        goto out;
    }

    rc = xsm_evtchn_interdomain(XSM_HOOK, ld, lchn, rd, rchn);
    if ( rc )
        goto out;

    double_evtchn_lock(lchn, rchn);

    lchn->u.interdomain.remote_dom  = rd;
    lchn->u.interdomain.remote_port = rport;
    lchn->state                     = ECS_INTERDOMAIN;
    evtchn_port_init(ld, lchn);
    
    rchn->u.interdomain.remote_dom  = ld;
    rchn->u.interdomain.remote_port = lport;
    rchn->state                     = ECS_INTERDOMAIN;

    /*
     * We may have lost notifications on the remote unbound port. Fix that up
     * here by conservatively always setting a notification on the local port.
     */
    evtchn_port_set_pending(ld, lchn->notify_vcpu_id, lchn);

    double_evtchn_unlock(lchn, rchn);

    bind->local_port = lport;

 out:
    check_free_port(ld, lport);
    write_unlock(&ld->event_lock);
    if ( ld != rd )
        write_unlock(&rd->event_lock);
    
    rcu_unlock_domain(rd);

    return rc;
}


int evtchn_bind_virq(evtchn_bind_virq_t *bind, evtchn_port_t port)
{
    struct evtchn *chn;
    struct vcpu   *v;
    struct domain *d = current->domain;
    int            virq = bind->virq, vcpu = bind->vcpu;
    int            rc = 0;

    if ( (virq < 0) || (virq >= ARRAY_SIZE(v->virq_to_evtchn)) )
        return -EINVAL;

   /*
    * Make sure the guest controlled value virq is bounded even during
    * speculative execution.
    */
    virq = array_index_nospec(virq, ARRAY_SIZE(v->virq_to_evtchn));

    if ( virq_is_global(virq) && (vcpu != 0) )
        return -EINVAL;

    if ( (v = domain_vcpu(d, vcpu)) == NULL )
        return -ENOENT;

    write_lock(&d->event_lock);

    if ( read_atomic(&v->virq_to_evtchn[virq]) )
    {
        rc = -EEXIST;
        gdprintk(XENLOG_WARNING, "EVTCHNOP failure: error %d\n", rc);
        goto out;
    }

    port = rc = evtchn_get_port(d, port);
    if ( rc < 0 )
    {
        gdprintk(XENLOG_WARNING, "EVTCHNOP failure: error %d\n", rc);
        goto out;
    }

    rc = 0;

    chn = evtchn_from_port(d, port);

    evtchn_write_lock(chn);

    chn->state          = ECS_VIRQ;
    chn->notify_vcpu_id = vcpu;
    chn->u.virq         = virq;
    evtchn_port_init(d, chn);

    evtchn_write_unlock(chn);

    bind->port = port;
    /*
     * If by any, the update of virq_to_evtchn[] would need guarding by
     * virq_lock, but since this is the last action here, there's no strict
     * need to acquire the lock. Hence holding event_lock isn't helpful
     * anymore at this point, but utilize that its unlocking acts as the
     * otherwise necessary smp_wmb() here.
     */
    write_atomic(&v->virq_to_evtchn[virq], port);

 out:
    write_unlock(&d->event_lock);

    return rc;
}


static int evtchn_bind_ipi(evtchn_bind_ipi_t *bind)
{
    struct evtchn *chn;
    struct domain *d = current->domain;
    int            port, rc = 0;
    unsigned int   vcpu = bind->vcpu;

    if ( domain_vcpu(d, vcpu) == NULL )
        return -ENOENT;

    write_lock(&d->event_lock);

    if ( (port = get_free_port(d)) < 0 )
    {
        rc = port;
        gdprintk(XENLOG_WARNING, "EVTCHNOP failure: error %d\n", rc);
        goto out;
    }

    chn = evtchn_from_port(d, port);

    evtchn_write_lock(chn);

    chn->state          = ECS_IPI;
    chn->notify_vcpu_id = vcpu;
    evtchn_port_init(d, chn);

    evtchn_write_unlock(chn);

    bind->port = port;

 out:
    write_unlock(&d->event_lock);

    return rc;
}

#ifdef CONFIG_HAS_PIRQ

static void link_pirq_port(int port, struct evtchn *chn, struct vcpu *v)
{
    chn->u.pirq.prev_port = 0;
    chn->u.pirq.next_port = v->pirq_evtchn_head;
    if ( v->pirq_evtchn_head )
        evtchn_from_port(v->domain, v->pirq_evtchn_head)
            ->u.pirq.prev_port = port;
    v->pirq_evtchn_head = port;
}

static void unlink_pirq_port(struct evtchn *chn, struct vcpu *v)
{
    struct domain *d = v->domain;

    if ( chn->u.pirq.prev_port )
        evtchn_from_port(d, chn->u.pirq.prev_port)->u.pirq.next_port =
            chn->u.pirq.next_port;
    else
        v->pirq_evtchn_head = chn->u.pirq.next_port;
    if ( chn->u.pirq.next_port )
        evtchn_from_port(d, chn->u.pirq.next_port)->u.pirq.prev_port =
            chn->u.pirq.prev_port;
}

#endif /* CONFIG_HAS_PIRQ */

static int evtchn_bind_pirq(evtchn_bind_pirq_t *bind)
{
#ifdef CONFIG_HAS_PIRQ
    struct evtchn *chn;
    struct domain *d = current->domain;
    struct vcpu   *v = d->vcpu[0];
    struct pirq   *info;
    int            port = 0, rc;
    unsigned int   pirq = bind->pirq;

    if ( pirq >= d->nr_pirqs )
        return -EINVAL;

    if ( !is_hvm_domain(d) && !pirq_access_permitted(d, pirq) )
        return -EPERM;

    write_lock(&d->event_lock);

    if ( pirq_to_evtchn(d, pirq) != 0 )
    {
        rc = -EEXIST;
        gdprintk(XENLOG_WARNING, "EVTCHNOP failure: error %d\n", rc);
        goto out;
    }

    if ( (port = get_free_port(d)) < 0 )
    {
        rc = port;
        gdprintk(XENLOG_WARNING, "EVTCHNOP failure: error %d\n", rc);
        goto out;
    }

    chn = evtchn_from_port(d, port);

    info = pirq_get_info(d, pirq);
    if ( !info )
    {
        rc = -ENOMEM;
        gdprintk(XENLOG_WARNING, "EVTCHNOP failure: error %d\n", rc);
        goto out;
    }

    info->evtchn = port;
    rc = (!is_hvm_domain(d)
          ? pirq_guest_bind(v, info,
                            !!(bind->flags & BIND_PIRQ__WILL_SHARE))
          : 0);
    if ( rc != 0 )
    {
        info->evtchn = 0;
        pirq_cleanup_check(info, d);
        goto out;
    }

    evtchn_write_lock(chn);

    chn->state  = ECS_PIRQ;
    chn->u.pirq.irq = pirq;
    link_pirq_port(port, chn, v);
    evtchn_port_init(d, chn);

    evtchn_write_unlock(chn);

    bind->port = port;

    arch_evtchn_bind_pirq(d, pirq);

 out:
    check_free_port(d, port);
    write_unlock(&d->event_lock);

    return rc;
#else /* !CONFIG_HAS_PIRQ */
    return -EOPNOTSUPP;
#endif
}


int evtchn_close(struct domain *d1, int port1, bool guest)
{
    struct domain *d2 = NULL;
    struct evtchn *chn1 = _evtchn_from_port(d1, port1), *chn2;
    int            rc = 0;

    if ( !chn1 )
        return -EINVAL;

 again:
    write_lock(&d1->event_lock);

    /* Guest cannot close a Xen-attached event channel. */
    if ( unlikely(consumer_is_xen(chn1)) && guest )
    {
        rc = -EINVAL;
        goto out;
    }

    switch ( chn1->state )
    {
    case ECS_FREE:
    case ECS_RESERVED:
        rc = -EINVAL;
        goto out;

    case ECS_UNBOUND:
        break;

#ifdef CONFIG_HAS_PIRQ
    case ECS_PIRQ: {
        struct pirq *pirq = pirq_info(d1, chn1->u.pirq.irq);

        if ( pirq )
        {
            if ( !is_hvm_domain(d1) )
                pirq_guest_unbind(d1, pirq);
            pirq->evtchn = 0;
            if ( !is_hvm_domain(d1) ||
                 domain_pirq_to_irq(d1, pirq->pirq) <= 0 ||
                 unmap_domain_pirq_emuirq(d1, pirq->pirq) < 0 )
                /*
                 * The successful path of unmap_domain_pirq_emuirq() will have
                 * called pirq_cleanup_check() already.
                 */
                pirq_cleanup_check(pirq, d1);
        }
        unlink_pirq_port(chn1, d1->vcpu[chn1->notify_vcpu_id]);
        break;
    }
#endif

    case ECS_VIRQ: {
        struct vcpu *v;
        unsigned long flags;

        v = d1->vcpu[virq_is_global(chn1->u.virq) ? 0 : chn1->notify_vcpu_id];

        write_lock_irqsave(&v->virq_lock, flags);
        ASSERT(read_atomic(&v->virq_to_evtchn[chn1->u.virq]) == port1);
        write_atomic(&v->virq_to_evtchn[chn1->u.virq], 0);
        write_unlock_irqrestore(&v->virq_lock, flags);

        break;
    }

    case ECS_IPI:
        break;

    case ECS_INTERDOMAIN:
        if ( d2 == NULL )
        {
            d2 = chn1->u.interdomain.remote_dom;

            /* If we unlock d1 then we could lose d2. */
            rcu_lock_domain(d2);

            if ( d1 < d2 )
                write_lock(&d2->event_lock);
            else if ( d1 != d2 )
            {
                write_unlock(&d1->event_lock);
                write_lock(&d2->event_lock);
                goto again;
            }
        }
        else if ( d2 != chn1->u.interdomain.remote_dom )
        {
            /*
             * We can only get here if the port was closed and re-bound after
             * unlocking d1 but before locking d2 above. We could retry but
             * it is easier to return the same error as if we had seen the
             * port in ECS_FREE. It must have passed through that state for
             * us to end up here, so it's a valid error to return.
             */
            rc = -EINVAL;
            goto out;
        }

        chn2 = _evtchn_from_port(d2, chn1->u.interdomain.remote_port);
        BUG_ON(!chn2);
        BUG_ON(chn2->state != ECS_INTERDOMAIN);
        BUG_ON(chn2->u.interdomain.remote_dom != d1);

        double_evtchn_lock(chn1, chn2);

        evtchn_free(d1, chn1);

        chn2->state = ECS_UNBOUND;
        chn2->u.unbound.remote_domid = d1->domain_id;

        double_evtchn_unlock(chn1, chn2);

        goto out;

    default:
        BUG();
    }

    evtchn_write_lock(chn1);
    evtchn_free(d1, chn1);
    evtchn_write_unlock(chn1);

 out:
    if ( d2 != NULL )
    {
        if ( d1 != d2 )
            write_unlock(&d2->event_lock);
        rcu_unlock_domain(d2);
    }

    write_unlock(&d1->event_lock);

    return rc;
}

int evtchn_send(struct domain *ld, unsigned int lport)
{
    struct evtchn *lchn = _evtchn_from_port(ld, lport), *rchn;
    struct domain *rd;
    int            rport, ret = 0;

    if ( !lchn )
        return -EINVAL;

    evtchn_read_lock(lchn);

    /* Guest cannot send via a Xen-attached event channel. */
    if ( unlikely(consumer_is_xen(lchn)) )
    {
        ret = -EINVAL;
        goto out;
    }

    ret = xsm_evtchn_send(XSM_HOOK, ld, lchn);
    if ( ret )
        goto out;

    switch ( lchn->state )
    {
    case ECS_INTERDOMAIN:
        rd    = lchn->u.interdomain.remote_dom;
        rport = lchn->u.interdomain.remote_port;
        rchn  = evtchn_from_port(rd, rport);
        if ( consumer_is_xen(rchn) )
        {
            /* Don't keep holding the lock for the call below. */
            xen_event_channel_notification_t fn = xen_notification_fn(rchn);
            struct vcpu *rv = rd->vcpu[rchn->notify_vcpu_id];

            rcu_lock_domain(rd);
            evtchn_read_unlock(lchn);
            fn(rv, rport);
            rcu_unlock_domain(rd);
            return 0;
        }
        evtchn_port_set_pending(rd, rchn->notify_vcpu_id, rchn);
        break;
    case ECS_IPI:
        evtchn_port_set_pending(ld, lchn->notify_vcpu_id, lchn);
        break;
    case ECS_UNBOUND:
        /* silently drop the notification */
        break;
    default:
        ret = -EINVAL;
        break;
    }

out:
    evtchn_read_unlock(lchn);

    return ret;
}


void send_guest_vcpu_virq(struct vcpu *v, uint32_t virq)
{
    unsigned long flags;
    int port;
    struct domain *d;
    struct evtchn *chn;

    ASSERT(!virq_is_global(virq));

    read_lock_irqsave(&v->virq_lock, flags);

    port = read_atomic(&v->virq_to_evtchn[virq]);
    if ( unlikely(port == 0) )
        goto out;

    d = v->domain;
    chn = evtchn_from_port(d, port);
    if ( evtchn_read_trylock(chn) )
    {
        evtchn_port_set_pending(d, v->vcpu_id, chn);
        evtchn_read_unlock(chn);
    }

 out:
    read_unlock_irqrestore(&v->virq_lock, flags);
}

void send_guest_global_virq(struct domain *d, uint32_t virq)
{
    unsigned long flags;
    int port;
    struct vcpu *v;
    struct evtchn *chn;

    ASSERT(virq_is_global(virq));

    if ( unlikely(d == NULL) || unlikely(d->vcpu == NULL) )
        return;

    v = d->vcpu[0];
    if ( unlikely(v == NULL) )
        return;

    read_lock_irqsave(&v->virq_lock, flags);

    port = read_atomic(&v->virq_to_evtchn[virq]);
    if ( unlikely(port == 0) )
        goto out;

    chn = evtchn_from_port(d, port);
    if ( evtchn_read_trylock(chn) )
    {
        evtchn_port_set_pending(d, chn->notify_vcpu_id, chn);
        evtchn_read_unlock(chn);
    }

 out:
    read_unlock_irqrestore(&v->virq_lock, flags);
}


static struct domain *global_virq_handlers[NR_VIRQS] __read_mostly;

static DEFINE_SPINLOCK(global_virq_handlers_lock);

void send_global_virq(uint32_t virq)
{
    ASSERT(virq_is_global(virq));

    send_guest_global_virq(global_virq_handlers[virq] ?: hardware_domain, virq);
}

int set_global_virq_handler(struct domain *d, uint32_t virq)
{
    struct domain *old;

    if (virq >= NR_VIRQS)
        return -EINVAL;
    if (!virq_is_global(virq))
        return -EINVAL;

    if (global_virq_handlers[virq] == d)
        return 0;

    if (unlikely(!get_domain(d)))
        return -EINVAL;

    spin_lock(&global_virq_handlers_lock);
    old = global_virq_handlers[virq];
    global_virq_handlers[virq] = d;
    spin_unlock(&global_virq_handlers_lock);

    if (old != NULL)
        put_domain(old);

    return 0;
}

static void clear_global_virq_handlers(struct domain *d)
{
    uint32_t virq;
    int put_count = 0;

    spin_lock(&global_virq_handlers_lock);

    for (virq = 0; virq < NR_VIRQS; virq++)
    {
        if (global_virq_handlers[virq] == d)
        {
            global_virq_handlers[virq] = NULL;
            put_count++;
        }
    }

    spin_unlock(&global_virq_handlers_lock);

    while (put_count)
    {
        put_domain(d);
        put_count--;
    }
}

int evtchn_status(evtchn_status_t *status)
{
    struct domain   *d;
    domid_t          dom = status->dom;
    int              port = status->port;
    struct evtchn   *chn;
    int              rc = 0;

    d = rcu_lock_domain_by_any_id(dom);
    if ( d == NULL )
        return -ESRCH;

    chn = _evtchn_from_port(d, port);
    if ( !chn )
    {
        rcu_unlock_domain(d);
        return -EINVAL;
    }

    read_lock(&d->event_lock);

    rc = xsm_evtchn_status(XSM_TARGET, d, chn);
    if ( rc )
        goto out;

    switch ( chn->state )
    {
    case ECS_FREE:
    case ECS_RESERVED:
        status->status = EVTCHNSTAT_closed;
        break;
    case ECS_UNBOUND:
        status->status = EVTCHNSTAT_unbound;
        status->u.unbound.dom = chn->u.unbound.remote_domid;
        break;
    case ECS_INTERDOMAIN:
        status->status = EVTCHNSTAT_interdomain;
        status->u.interdomain.dom  =
            chn->u.interdomain.remote_dom->domain_id;
        status->u.interdomain.port = chn->u.interdomain.remote_port;
        break;
    case ECS_PIRQ:
        status->status = EVTCHNSTAT_pirq;
        status->u.pirq = chn->u.pirq.irq;
        break;
    case ECS_VIRQ:
        status->status = EVTCHNSTAT_virq;
        status->u.virq = chn->u.virq;
        break;
    case ECS_IPI:
        status->status = EVTCHNSTAT_ipi;
        break;
    default:
        BUG();
    }

    status->vcpu = chn->notify_vcpu_id;

 out:
    read_unlock(&d->event_lock);
    rcu_unlock_domain(d);

    return rc;
}


int evtchn_bind_vcpu(evtchn_port_t port, unsigned int vcpu_id)
{
    struct domain *d = current->domain;
    struct evtchn *chn;
    int            rc = 0;
    struct vcpu   *v;

    /* Use the vcpu info to prevent speculative out-of-bound accesses */
    if ( (v = domain_vcpu(d, vcpu_id)) == NULL )
        return -ENOENT;

    chn = _evtchn_from_port(d, port);
    if ( !chn )
        return -EINVAL;

    write_lock(&d->event_lock);

    /* Guest cannot re-bind a Xen-attached event channel. */
    if ( unlikely(consumer_is_xen(chn)) )
    {
        rc = -EINVAL;
        goto out;
    }

    switch ( chn->state )
    {
    case ECS_VIRQ:
        if ( virq_is_global(chn->u.virq) )
            chn->notify_vcpu_id = v->vcpu_id;
        else
            rc = -EINVAL;
        break;
    case ECS_UNBOUND:
    case ECS_INTERDOMAIN:
        chn->notify_vcpu_id = v->vcpu_id;
        break;

#ifdef CONFIG_HAS_PIRQ
    case ECS_PIRQ:
        if ( chn->notify_vcpu_id == v->vcpu_id )
            break;
        unlink_pirq_port(chn, d->vcpu[chn->notify_vcpu_id]);
        chn->notify_vcpu_id = v->vcpu_id;
        pirq_set_affinity(d, chn->u.pirq.irq,
                          cpumask_of(v->processor));
        link_pirq_port(port, chn, v);
        break;
#endif

    default:
        rc = -EINVAL;
        break;
    }

 out:
    write_unlock(&d->event_lock);

    return rc;
}


int evtchn_unmask(unsigned int port)
{
    struct domain *d = current->domain;
    struct evtchn *evtchn = _evtchn_from_port(d, port);

    if ( unlikely(!evtchn) )
        return -EINVAL;

    evtchn_read_lock(evtchn);

    evtchn_port_unmask(d, evtchn);

    evtchn_read_unlock(evtchn);

    return 0;
}

static bool has_active_evtchns(const struct domain *d)
{
    unsigned int xen = read_atomic(&d->xen_evtchns);

    /*
     * Read ->xen_evtchns /before/ active_evtchns, to prevent
     * evtchn_reset() exiting its loop early.
     */
    smp_rmb();

    return read_atomic(&d->active_evtchns) > xen;
}

int evtchn_reset(struct domain *d, bool resuming)
{
    unsigned int i;
    int rc = 0;

    if ( d != current->domain && !d->controller_pause_count )
        return -EINVAL;

    write_lock(&d->event_lock);

    /*
     * If we are resuming, then start where we stopped. Otherwise, check
     * that a reset operation is not already in progress, and if none is,
     * record that this is now the case.
     */
    i = resuming ? d->next_evtchn : !d->next_evtchn;
    if ( i > d->next_evtchn )
        d->next_evtchn = i;

    write_unlock(&d->event_lock);

    if ( !i )
        return -EBUSY;

    for ( ; port_is_valid(d, i) && has_active_evtchns(d); i++ )
    {
        evtchn_close(d, i, 1);

        /* NB: Choice of frequency is arbitrary. */
        if ( !(i & 0x3f) && hypercall_preempt_check() )
        {
            write_lock(&d->event_lock);
            d->next_evtchn = i;
            write_unlock(&d->event_lock);
            return -ERESTART;
        }
    }

    write_lock(&d->event_lock);

    d->next_evtchn = 0;

    if ( d->active_evtchns > d->xen_evtchns )
        rc = -EAGAIN;
    else if ( d->evtchn_fifo )
    {
        /* Switching back to 2-level ABI. */
        evtchn_fifo_destroy(d);
        evtchn_2l_init(d);
    }

    write_unlock(&d->event_lock);

    return rc;
}

static int evtchn_set_priority(const struct evtchn_set_priority *set_priority)
{
    struct domain *d = current->domain;
    struct evtchn *chn = _evtchn_from_port(d, set_priority->port);
    int ret;

    if ( !chn )
        return -EINVAL;

    evtchn_read_lock(chn);

    ret = evtchn_port_set_priority(d, chn, set_priority->priority);

    evtchn_read_unlock(chn);

    return ret;
}

long do_event_channel_op(int cmd, XEN_GUEST_HANDLE_PARAM(void) arg)
{
    int rc;

#ifdef CONFIG_PV_SHIM
    if ( unlikely(pv_shim) )
        return pv_shim_event_channel_op(cmd, arg);
#endif

    switch ( cmd )
    {
    case EVTCHNOP_alloc_unbound: {
        struct evtchn_alloc_unbound alloc_unbound;
        if ( copy_from_guest(&alloc_unbound, arg, 1) != 0 )
            return -EFAULT;
        rc = evtchn_alloc_unbound(&alloc_unbound, 0);
        if ( !rc && __copy_to_guest(arg, &alloc_unbound, 1) )
            rc = -EFAULT; /* Cleaning up here would be a mess! */
        break;
    }

    case EVTCHNOP_bind_interdomain: {
        struct evtchn_bind_interdomain bind_interdomain;
        if ( copy_from_guest(&bind_interdomain, arg, 1) != 0 )
            return -EFAULT;
        rc = evtchn_bind_interdomain(&bind_interdomain, current->domain, 0);
        if ( !rc && __copy_to_guest(arg, &bind_interdomain, 1) )
            rc = -EFAULT; /* Cleaning up here would be a mess! */
        break;
    }

    case EVTCHNOP_bind_virq: {
        struct evtchn_bind_virq bind_virq;
        if ( copy_from_guest(&bind_virq, arg, 1) != 0 )
            return -EFAULT;
        rc = evtchn_bind_virq(&bind_virq, 0);
        if ( !rc && __copy_to_guest(arg, &bind_virq, 1) )
            rc = -EFAULT; /* Cleaning up here would be a mess! */
        break;
    }

    case EVTCHNOP_bind_ipi: {
        struct evtchn_bind_ipi bind_ipi;
        if ( copy_from_guest(&bind_ipi, arg, 1) != 0 )
            return -EFAULT;
        rc = evtchn_bind_ipi(&bind_ipi);
        if ( !rc && __copy_to_guest(arg, &bind_ipi, 1) )
            rc = -EFAULT; /* Cleaning up here would be a mess! */
        break;
    }

    case EVTCHNOP_bind_pirq: {
        struct evtchn_bind_pirq bind_pirq;
        if ( copy_from_guest(&bind_pirq, arg, 1) != 0 )
            return -EFAULT;
        rc = evtchn_bind_pirq(&bind_pirq);
        if ( !rc && __copy_to_guest(arg, &bind_pirq, 1) )
            rc = -EFAULT; /* Cleaning up here would be a mess! */
        break;
    }

    case EVTCHNOP_close: {
        struct evtchn_close close;
        if ( copy_from_guest(&close, arg, 1) != 0 )
            return -EFAULT;
        rc = evtchn_close(current->domain, close.port, 1);
        break;
    }

    case EVTCHNOP_send: {
        struct evtchn_send send;
        if ( copy_from_guest(&send, arg, 1) != 0 )
            return -EFAULT;
        rc = evtchn_send(current->domain, send.port);
        break;
    }

    case EVTCHNOP_status: {
        struct evtchn_status status;
        if ( copy_from_guest(&status, arg, 1) != 0 )
            return -EFAULT;
        rc = evtchn_status(&status);
        if ( !rc && __copy_to_guest(arg, &status, 1) )
            rc = -EFAULT;
        break;
    }

    case EVTCHNOP_bind_vcpu: {
        struct evtchn_bind_vcpu bind_vcpu;
        if ( copy_from_guest(&bind_vcpu, arg, 1) != 0 )
            return -EFAULT;
        rc = evtchn_bind_vcpu(bind_vcpu.port, bind_vcpu.vcpu);
        break;
    }

    case EVTCHNOP_unmask: {
        struct evtchn_unmask unmask;
        if ( copy_from_guest(&unmask, arg, 1) != 0 )
            return -EFAULT;
        rc = evtchn_unmask(unmask.port);
        break;
    }

    case EVTCHNOP_reset:
    case EVTCHNOP_reset_cont: {
        struct evtchn_reset reset;
        struct domain *d;

        if ( copy_from_guest(&reset, arg, 1) != 0 )
            return -EFAULT;

        d = rcu_lock_domain_by_any_id(reset.dom);
        if ( d == NULL )
            return -ESRCH;

        rc = xsm_evtchn_reset(XSM_TARGET, current->domain, d);
        if ( !rc )
            rc = evtchn_reset(d, cmd == EVTCHNOP_reset_cont);

        rcu_unlock_domain(d);

        if ( rc == -ERESTART )
            rc = hypercall_create_continuation(__HYPERVISOR_event_channel_op,
                                               "ih", EVTCHNOP_reset_cont, arg);
        break;
    }

    case EVTCHNOP_init_control: {
        struct evtchn_init_control init_control;
        if ( copy_from_guest(&init_control, arg, 1) != 0 )
            return -EFAULT;
        rc = evtchn_fifo_init_control(&init_control);
        if ( !rc && __copy_to_guest(arg, &init_control, 1) )
            rc = -EFAULT;
        break;
    }

    case EVTCHNOP_expand_array: {
        struct evtchn_expand_array expand_array;
        if ( copy_from_guest(&expand_array, arg, 1) != 0 )
            return -EFAULT;
        rc = evtchn_fifo_expand_array(&expand_array);
        break;
    }

    case EVTCHNOP_set_priority: {
        struct evtchn_set_priority set_priority;
        if ( copy_from_guest(&set_priority, arg, 1) != 0 )
            return -EFAULT;
        rc = evtchn_set_priority(&set_priority);
        break;
    }

    default:
        rc = -ENOSYS;
        break;
    }

    return rc;
}


int alloc_unbound_xen_event_channel(
    struct domain *ld, unsigned int lvcpu, domid_t remote_domid,
    xen_event_channel_notification_t notification_fn)
{
    struct evtchn *chn;
    int            port, rc;

    write_lock(&ld->event_lock);

    port = rc = get_free_port(ld);
    if ( rc < 0 )
        goto out;
    chn = evtchn_from_port(ld, port);

    rc = xsm_evtchn_unbound(XSM_TARGET, ld, chn, remote_domid);
    if ( rc )
        goto out;

    evtchn_write_lock(chn);

    chn->state = ECS_UNBOUND;
    chn->xen_consumer = get_xen_consumer(notification_fn);
    chn->notify_vcpu_id = lvcpu;
    chn->u.unbound.remote_domid = remote_domid;

    evtchn_write_unlock(chn);

    /*
     * Increment ->xen_evtchns /after/ ->active_evtchns. No explicit
     * barrier needed due to spin-locked region just above.
     */
    write_atomic(&ld->xen_evtchns, ld->xen_evtchns + 1);

 out:
    check_free_port(ld, port);
    write_unlock(&ld->event_lock);

    return rc < 0 ? rc : port;
}

void free_xen_event_channel(struct domain *d, int port)
{
    if ( !port_is_valid(d, port) )
    {
        /*
         * Make sure ->is_dying is read /after/ ->valid_evtchns, pairing
         * with the kind-of-barrier and BUG_ON() in evtchn_destroy().
         */
        smp_rmb();
        BUG_ON(!d->is_dying);
        return;
    }

    evtchn_close(d, port, 0);
}


void notify_via_xen_event_channel(struct domain *ld, int lport)
{
    struct evtchn *lchn = _evtchn_from_port(ld, lport), *rchn;
    struct domain *rd;

    if ( !lchn )
    {
        /*
         * Make sure ->is_dying is read /after/ ->valid_evtchns, pairing
         * with the kind-of-barrier and BUG_ON() in evtchn_destroy().
         */
        smp_rmb();
        ASSERT(ld->is_dying);
        return;
    }

    if ( !evtchn_read_trylock(lchn) )
        return;

    if ( likely(lchn->state == ECS_INTERDOMAIN) )
    {
        ASSERT(consumer_is_xen(lchn));
        rd    = lchn->u.interdomain.remote_dom;
        rchn  = evtchn_from_port(rd, lchn->u.interdomain.remote_port);
        evtchn_port_set_pending(rd, rchn->notify_vcpu_id, rchn);
    }

    evtchn_read_unlock(lchn);
}

void evtchn_check_pollers(struct domain *d, unsigned int port)
{
    struct vcpu *v;
    unsigned int vcpuid;

    /* Check if some VCPU might be polling for this event. */
    if ( likely(bitmap_empty(d->poll_mask, d->max_vcpus)) )
        return;

    /* Wake any interested (or potentially interested) pollers. */
    for ( vcpuid = find_first_bit(d->poll_mask, d->max_vcpus);
          vcpuid < d->max_vcpus;
          vcpuid = find_next_bit(d->poll_mask, d->max_vcpus, vcpuid+1) )
    {
        v = d->vcpu[vcpuid];
        if ( ((v->poll_evtchn <= 0) || (v->poll_evtchn == port)) &&
             test_and_clear_bit(vcpuid, d->poll_mask) )
        {
            v->poll_evtchn = 0;
            vcpu_unblock(v);
        }
    }
}

int evtchn_init(struct domain *d, unsigned int max_port)
{
    evtchn_2l_init(d);
    d->max_evtchn_port = min_t(unsigned int, max_port, INT_MAX);

    d->evtchn = alloc_evtchn_bucket(d, 0);
    if ( !d->evtchn )
        return -ENOMEM;
    d->valid_evtchns = EVTCHNS_PER_BUCKET;

    rwlock_init(&d->event_lock);

    if ( get_free_port(d) != 0 )
    {
        free_evtchn_bucket(d, d->evtchn);
        return -EINVAL;
    }
    evtchn_from_port(d, 0)->state = ECS_RESERVED;
    write_atomic(&d->active_evtchns, 0);

#if MAX_VIRT_CPUS > BITS_PER_LONG
    d->poll_mask = xzalloc_array(unsigned long, BITS_TO_LONGS(d->max_vcpus));
    if ( !d->poll_mask )
    {
        free_evtchn_bucket(d, d->evtchn);
        return -ENOMEM;
    }
#endif

    return 0;
}

int evtchn_destroy(struct domain *d)
{
    unsigned int i;

    /* After this kind-of-barrier no new event-channel allocations can occur. */
    BUG_ON(!d->is_dying);
    read_lock(&d->event_lock);
    read_unlock(&d->event_lock);

    /* Close all existing event channels. */
    for ( i = d->valid_evtchns; --i; )
    {
        evtchn_close(d, i, 0);

        /*
         * Avoid preempting when called from domain_create()'s error path,
         * and don't check too often (choice of frequency is arbitrary).
         */
        if ( i && !(i & 0x3f) && d->is_dying != DOMDYING_dead &&
             hypercall_preempt_check() )
        {
            write_atomic(&d->valid_evtchns, i);
            return -ERESTART;
        }
    }

    ASSERT(!d->active_evtchns);

    clear_global_virq_handlers(d);

    evtchn_fifo_destroy(d);

    return 0;
}


void evtchn_destroy_final(struct domain *d)
{
    unsigned int i, j;

    /* Free all event-channel buckets. */
    for ( i = 0; i < NR_EVTCHN_GROUPS; i++ )
    {
        if ( !d->evtchn_group[i] )
            continue;
        for ( j = 0; j < BUCKETS_PER_GROUP; j++ )
            free_evtchn_bucket(d, d->evtchn_group[i][j]);
        xfree(d->evtchn_group[i]);
    }
    free_evtchn_bucket(d, d->evtchn);

#if MAX_VIRT_CPUS > BITS_PER_LONG
    xfree(d->poll_mask);
    d->poll_mask = NULL;
#endif
}


void evtchn_move_pirqs(struct vcpu *v)
{
    struct domain *d = v->domain;
    const cpumask_t *mask = cpumask_of(v->processor);
    unsigned int port;
    struct evtchn *chn;

    read_lock(&d->event_lock);
    for ( port = v->pirq_evtchn_head; port; port = chn->u.pirq.next_port )
    {
        chn = evtchn_from_port(d, port);
        pirq_set_affinity(d, chn->u.pirq.irq, mask);
    }
    read_unlock(&d->event_lock);
}


static void domain_dump_evtchn_info(struct domain *d)
{
    unsigned int port;
    int irq;

    printk("Event channel information for domain %d:\n"
           "Polling vCPUs: {%*pbl}\n"
           "    port [p/m/s]\n", d->domain_id, d->max_vcpus, d->poll_mask);

    read_lock(&d->event_lock);

    for ( port = 1; ; ++port )
    {
        const struct evtchn *chn = _evtchn_from_port(d, port);
        char *ssid;

        if ( !chn )
            break;

        if ( chn->state == ECS_FREE )
            continue;

        printk("    %4u [%d/%d/",
               port,
               evtchn_is_pending(d, chn),
               evtchn_is_masked(d, chn));
        evtchn_port_print_state(d, chn);
        printk("]: s=%d n=%d x=%d",
               chn->state, chn->notify_vcpu_id, chn->xen_consumer);

        switch ( chn->state )
        {
        case ECS_UNBOUND:
            printk(" d=%d", chn->u.unbound.remote_domid);
            break;
        case ECS_INTERDOMAIN:
            printk(" d=%d p=%d",
                   chn->u.interdomain.remote_dom->domain_id,
                   chn->u.interdomain.remote_port);
            break;
        case ECS_PIRQ:
            irq = domain_pirq_to_irq(d, chn->u.pirq.irq);
            printk(" p=%d i=%d", chn->u.pirq.irq, irq);
            break;
        case ECS_VIRQ:
            printk(" v=%d", chn->u.virq);
            break;
        }

        ssid = xsm_show_security_evtchn(d, chn);
        if (ssid) {
            printk(" Z=%s\n", ssid);
            xfree(ssid);
        } else {
            printk("\n");
        }
    }

    read_unlock(&d->event_lock);
}

static void cf_check dump_evtchn_info(unsigned char key)
{
    struct domain *d;

    printk("'%c' pressed -> dumping event-channel info\n", key);

    rcu_read_lock(&domlist_read_lock);

    for_each_domain ( d )
        domain_dump_evtchn_info(d);

    rcu_read_unlock(&domlist_read_lock);
}

static int __init cf_check dump_evtchn_info_key_init(void)
{
    register_keyhandler('e', dump_evtchn_info, "dump evtchn info", 1);
    return 0;
}
__initcall(dump_evtchn_info_key_init);

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: event_channel.c === */
/* === BEGIN INLINED: event_fifo.c === */
#include <xen_xen_config.h>
/*
 * FIFO event channel management.
 *
 * Copyright (C) 2013 Citrix Systems R&D Ltd.
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2 or later.  See the file COPYING for more details.
 */

/* event_channel.h inlined below */

#include <xen_init.h>
#include <xen_lib.h>
#include <xen_errno.h>
#include <xen_sched.h>
#include <xen_paging.h>
#include <xen_mm.h>
#include <xen_domain_page.h>

#include <asm_guest_atomics.h>

#include <public_event_channel.h>

struct evtchn_fifo_queue {
    uint32_t *head; /* points into control block */
    uint32_t tail;
    uint8_t priority;
    spinlock_t lock;
};

struct evtchn_fifo_vcpu {
    struct evtchn_fifo_control_block *control_block;
    struct evtchn_fifo_queue queue[EVTCHN_FIFO_MAX_QUEUES];
};

#define EVTCHN_FIFO_EVENT_WORDS_PER_PAGE (PAGE_SIZE / sizeof(event_word_t))
#define EVTCHN_FIFO_MAX_EVENT_ARRAY_PAGES \
    (EVTCHN_FIFO_NR_CHANNELS / EVTCHN_FIFO_EVENT_WORDS_PER_PAGE)

struct evtchn_fifo_domain {
    event_word_t *event_array[EVTCHN_FIFO_MAX_EVENT_ARRAY_PAGES];
    unsigned int num_evtchns;
};

union evtchn_fifo_lastq {
    uint32_t raw;
    struct {
        uint8_t last_priority;
        uint16_t last_vcpu_id;
    };
};

static inline event_word_t *evtchn_fifo_word_from_port(const struct domain *d,
                                                       unsigned int port)
{
    unsigned int p, w;

    /*
     * Callers aren't required to hold d->event_lock, so we need to synchronize
     * with evtchn_fifo_init_control() setting d->evtchn_port_ops /after/
     * d->evtchn_fifo.
     */
    smp_rmb();

    if ( unlikely(port >= d->evtchn_fifo->num_evtchns) )
        return NULL;

    /*
     * Callers aren't required to hold d->event_lock, so we need to synchronize
     * with add_page_to_event_array().
     */
    smp_rmb();

    p = array_index_nospec(port / EVTCHN_FIFO_EVENT_WORDS_PER_PAGE,
                           d->evtchn_fifo->num_evtchns);
    w = port % EVTCHN_FIFO_EVENT_WORDS_PER_PAGE;

    return d->evtchn_fifo->event_array[p] + w;
}

static void cf_check evtchn_fifo_init(struct domain *d, struct evtchn *evtchn)
{
    event_word_t *word;

    evtchn->priority = EVTCHN_FIFO_PRIORITY_DEFAULT;

    /*
     * If this event is still linked, the first event may be delivered
     * on the wrong VCPU or with an unexpected priority.
     */
    word = evtchn_fifo_word_from_port(d, evtchn->port);
    if ( word && guest_test_bit(d, EVTCHN_FIFO_LINKED, word) )
        gdprintk(XENLOG_WARNING, "domain %d, port %d already on a queue\n",
                 d->domain_id, evtchn->port);
}

static int try_set_link(event_word_t *word, event_word_t *w, uint32_t link)
{
    event_word_t new, old;

    if ( !(*w & (1 << EVTCHN_FIFO_LINKED)) )
        return 0;

    old = *w;
    new = (old & ~((1 << EVTCHN_FIFO_BUSY) | EVTCHN_FIFO_LINK_MASK)) | link;
    *w = cmpxchg(word, old, new);
    if ( *w == old )
        return 1;

    return -EAGAIN;
}

/*
 * Atomically set the LINK field iff it is still LINKED.
 *
 * The guest is only permitted to make the following changes to a
 * LINKED event.
 *
 * - set MASKED
 * - clear MASKED
 * - clear PENDING
 * - clear LINKED (and LINK)
 *
 * We block unmasking by the guest by marking the tail word as BUSY,
 * therefore, the cmpxchg() may fail at most 4 times.
 */
static bool evtchn_fifo_set_link(struct domain *d, event_word_t *word,
                                 uint32_t link)
{
    event_word_t w;
    unsigned int try;
    int ret;

    w = read_atomic(word);

    ret = try_set_link(word, &w, link);
    if ( ret >= 0 )
        return ret;

    /* Lock the word to prevent guest unmasking. */
    guest_set_bit(d, EVTCHN_FIFO_BUSY, word);

    w = read_atomic(word);

    for ( try = 0; try < 4; try++ )
    {
        ret = try_set_link(word, &w, link);
        if ( ret >= 0 )
        {
            if ( ret == 0 )
                guest_clear_bit(d, EVTCHN_FIFO_BUSY, word);
            return ret;
        }
    }
    gdprintk(XENLOG_WARNING, "domain %d, port %d not linked\n",
             d->domain_id, link);
    guest_clear_bit(d, EVTCHN_FIFO_BUSY, word);
    return 1;
}

static void cf_check evtchn_fifo_set_pending(
    struct vcpu *v, struct evtchn *evtchn)
{
    struct domain *d = v->domain;
    unsigned int port;
    event_word_t *word;
    unsigned long flags;
    bool check_pollers = false;
    struct evtchn_fifo_queue *q, *old_q;
    unsigned int try;
    bool linked = true;

    port = evtchn->port;
    word = evtchn_fifo_word_from_port(d, port);

    /*
     * Event array page may not exist yet, save the pending state for
     * when the page is added.
     */
    if ( unlikely(!word) )
    {
        evtchn->pending = true;
        return;
    }

    /*
     * Lock all queues related to the event channel (in case of a queue change
     * this might be two).
     * It is mandatory to do that before setting and testing the PENDING bit
     * and to hold the current queue lock until the event has been put into the
     * list of pending events in order to avoid waking up a guest without the
     * event being visibly pending in the guest.
     */
    for ( try = 0; try < 3; try++ )
    {
        union evtchn_fifo_lastq lastq;
        const struct vcpu *old_v;

        lastq.raw = read_atomic(&evtchn->fifo_lastq);
        old_v = d->vcpu[lastq.last_vcpu_id];

        q = &v->evtchn_fifo->queue[evtchn->priority];
        old_q = &old_v->evtchn_fifo->queue[lastq.last_priority];

        if ( q == old_q )
            spin_lock_irqsave(&q->lock, flags);
        else if ( q < old_q )
        {
            spin_lock_irqsave(&q->lock, flags);
            spin_lock(&old_q->lock);
        }
        else
        {
            spin_lock_irqsave(&old_q->lock, flags);
            spin_lock(&q->lock);
        }

        lastq.raw = read_atomic(&evtchn->fifo_lastq);
        old_v = d->vcpu[lastq.last_vcpu_id];
        if ( q == &v->evtchn_fifo->queue[evtchn->priority] &&
             old_q == &old_v->evtchn_fifo->queue[lastq.last_priority] )
            break;

        if ( q != old_q )
            spin_unlock(&old_q->lock);
        spin_unlock_irqrestore(&q->lock, flags);
    }

    /* If we didn't get the lock bail out. */
    if ( try == 3 )
    {
        gprintk(XENLOG_WARNING,
                "%pd port %u lost event (too many queue changes)\n",
                d, evtchn->port);
        goto done;
    }

    /*
     * Control block not mapped.  The guest must not unmask an
     * event until the control block is initialized, so we can
     * just drop the event.
     */
    if ( unlikely(!v->evtchn_fifo->control_block) )
    {
        printk(XENLOG_G_WARNING
               "%pv has no FIFO event channel control block\n", v);
        goto unlock;
    }

    check_pollers = !guest_test_and_set_bit(d, EVTCHN_FIFO_PENDING, word);

    /*
     * Link the event if it unmasked and not already linked.
     */
    if ( !guest_test_bit(d, EVTCHN_FIFO_MASKED, word) &&
         /*
          * This also acts as the read counterpart of the smp_wmb() in
          * map_control_block().
          */
         !guest_test_and_set_bit(d, EVTCHN_FIFO_LINKED, word) )
    {
        /*
         * If this event was a tail, the old queue is now empty and
         * its tail must be invalidated to prevent adding an event to
         * the old queue from corrupting the new queue.
         */
        if ( old_q->tail == port )
            old_q->tail = 0;

        /* Moved to a different queue? */
        if ( old_q != q )
        {
            union evtchn_fifo_lastq lastq = { };

            lastq.last_vcpu_id = v->vcpu_id;
            lastq.last_priority = q->priority;
            write_atomic(&evtchn->fifo_lastq, lastq.raw);

            spin_unlock(&old_q->lock);
            old_q = q;
        }

        /*
         * Atomically link the tail to port iff the tail is linked.
         * If the tail is unlinked the queue is empty.
         *
         * If port is the same as tail, the queue is empty but q->tail
         * will appear linked as we just set LINKED above.
         *
         * If the queue is empty (i.e., we haven't linked to the new
         * event), head must be updated.
         */
        linked = false;
        if ( q->tail )
        {
            event_word_t *tail_word;

            tail_word = evtchn_fifo_word_from_port(d, q->tail);
            linked = evtchn_fifo_set_link(d, tail_word, port);
        }
        if ( !linked )
            write_atomic(q->head, port);
        q->tail = port;
    }

 unlock:
    if ( q != old_q )
        spin_unlock(&old_q->lock);
    spin_unlock_irqrestore(&q->lock, flags);

 done:
    if ( !linked &&
         !guest_test_and_set_bit(d, q->priority,
                                 &v->evtchn_fifo->control_block->ready) )
        vcpu_mark_events_pending(v);

    if ( check_pollers )
        evtchn_check_pollers(d, port);
}

static void cf_check evtchn_fifo_clear_pending(
    struct domain *d, struct evtchn *evtchn)
{
    event_word_t *word;

    word = evtchn_fifo_word_from_port(d, evtchn->port);
    if ( unlikely(!word) )
        return;

    /*
     * Just clear the P bit.
     *
     * No need to unlink as the guest will unlink and ignore
     * non-pending events.
     */
    guest_clear_bit(d, EVTCHN_FIFO_PENDING, word);
}

static void cf_check evtchn_fifo_unmask(struct domain *d, struct evtchn *evtchn)
{
    struct vcpu *v = d->vcpu[evtchn->notify_vcpu_id];
    event_word_t *word;

    word = evtchn_fifo_word_from_port(d, evtchn->port);
    if ( unlikely(!word) )
        return;

    guest_clear_bit(d, EVTCHN_FIFO_MASKED, word);

    /* Relink if pending. */
    if ( guest_test_bit(d, EVTCHN_FIFO_PENDING, word) )
        evtchn_fifo_set_pending(v, evtchn);
}

static bool cf_check evtchn_fifo_is_pending(
    const struct domain *d, const struct evtchn *evtchn)
{
    const event_word_t *word = evtchn_fifo_word_from_port(d, evtchn->port);

    return word && guest_test_bit(d, EVTCHN_FIFO_PENDING, word);
}

static bool cf_check evtchn_fifo_is_masked(
    const struct domain *d, const struct evtchn *evtchn)
{
    const event_word_t *word = evtchn_fifo_word_from_port(d, evtchn->port);

    return !word || guest_test_bit(d, EVTCHN_FIFO_MASKED, word);
}

static bool cf_check evtchn_fifo_is_busy(
    const struct domain *d, const struct evtchn *evtchn)
{
    const event_word_t *word = evtchn_fifo_word_from_port(d, evtchn->port);

    return word && guest_test_bit(d, EVTCHN_FIFO_LINKED, word);
}

static int cf_check evtchn_fifo_set_priority(
    struct domain *d, struct evtchn *evtchn, unsigned int priority)
{
    if ( priority > EVTCHN_FIFO_PRIORITY_MIN )
        return -EINVAL;

    /*
     * Only need to switch to the new queue for future events. If the
     * event is already pending or in the process of being linked it
     * will be on the old queue -- this is fine.
     */
    evtchn->priority = priority;

    return 0;
}

static void cf_check evtchn_fifo_print_state(
    struct domain *d, const struct evtchn *evtchn)
{
    event_word_t *word;

    word = evtchn_fifo_word_from_port(d, evtchn->port);
    if ( !word )
        printk("?     ");
    else if ( guest_test_bit(d, EVTCHN_FIFO_LINKED, word) )
        printk("%c %-4u", guest_test_bit(d, EVTCHN_FIFO_BUSY, word) ? 'B' : ' ',
               *word & EVTCHN_FIFO_LINK_MASK);
    else
        printk("%c -   ", guest_test_bit(d, EVTCHN_FIFO_BUSY, word) ? 'B' : ' ');
}

static const struct evtchn_port_ops evtchn_port_ops_fifo =
{
    .init          = evtchn_fifo_init,
    .set_pending   = evtchn_fifo_set_pending,
    .clear_pending = evtchn_fifo_clear_pending,
    .unmask        = evtchn_fifo_unmask,
    .is_pending    = evtchn_fifo_is_pending,
    .is_masked     = evtchn_fifo_is_masked,
    .is_busy       = evtchn_fifo_is_busy,
    .set_priority  = evtchn_fifo_set_priority,
    .print_state   = evtchn_fifo_print_state,
};

static int map_guest_page(struct domain *d, uint64_t gfn, void **virt)
{
    struct page_info *p;

    p = get_page_from_gfn(d, gfn, NULL, P2M_ALLOC);
    if ( !p )
        return -EINVAL;

    if ( !get_page_type(p, PGT_writable_page) )
    {
        put_page(p);
        return -EINVAL;
    }

    *virt = __map_domain_page_global(p);
    if ( !*virt )
    {
        put_page_and_type(p);
        return -ENOMEM;
    }
    return 0;
}

static void unmap_guest_page(void *virt)
{
    struct page_info *page;

    if ( !virt )
        return;

    virt = (void *)((unsigned long)virt & PAGE_MASK);
    page = mfn_to_page(domain_page_map_to_mfn(virt));

    unmap_domain_page_global(virt);
    put_page_and_type(page);
}

static void init_queue(struct vcpu *v, struct evtchn_fifo_queue *q,
                       unsigned int i)
{
    spin_lock_init(&q->lock);
    q->priority = i;
}

static int setup_control_block(struct vcpu *v)
{
    struct evtchn_fifo_vcpu *efv;
    unsigned int i;

    efv = xzalloc(struct evtchn_fifo_vcpu);
    if ( !efv )
        return -ENOMEM;

    for ( i = 0; i <= EVTCHN_FIFO_PRIORITY_MIN; i++ )
        init_queue(v, &efv->queue[i], i);

    v->evtchn_fifo = efv;

    return 0;
}

static int map_control_block(struct vcpu *v, uint64_t gfn, uint32_t offset)
{
    void *virt;
    struct evtchn_fifo_control_block *control_block;
    unsigned int i;
    int rc;

    if ( v->evtchn_fifo->control_block )
        return -EINVAL;

    rc = map_guest_page(v->domain, gfn, &virt);
    if ( rc < 0 )
        return rc;

    control_block = virt + offset;

    for ( i = 0; i <= EVTCHN_FIFO_PRIORITY_MIN; i++ )
        v->evtchn_fifo->queue[i].head = &control_block->head[i];

    /* All queue heads must have been set before setting the control block. */
    smp_wmb();

    v->evtchn_fifo->control_block = control_block;

    return 0;
}

static void cleanup_control_block(struct vcpu *v)
{
    if ( !v->evtchn_fifo )
        return;

    unmap_guest_page(v->evtchn_fifo->control_block);
    xfree(v->evtchn_fifo);
    v->evtchn_fifo = NULL;
}

/*
 * Setup an event array with no pages.
 */
static int setup_event_array(struct domain *d)
{
    d->evtchn_fifo = xzalloc(struct evtchn_fifo_domain);
    if ( !d->evtchn_fifo )
        return -ENOMEM;

    return 0;
}

static void cleanup_event_array(struct domain *d)
{
    unsigned int i;

    if ( !d->evtchn_fifo )
        return;

    for ( i = 0; i < EVTCHN_FIFO_MAX_EVENT_ARRAY_PAGES; i++ )
        unmap_guest_page(d->evtchn_fifo->event_array[i]);
    xfree(d->evtchn_fifo);
    d->evtchn_fifo = NULL;
}

static void setup_ports(struct domain *d, unsigned int prev_evtchns)
{
    unsigned int port;

    /*
     * For each port that is already bound:
     *
     * - save its pending state.
     * - set default priority.
     */
    for ( port = 1; port < prev_evtchns; port++ )
    {
        struct evtchn *evtchn;

        if ( !port_is_valid(d, port) )
            break;

        evtchn = evtchn_from_port(d, port);

        if ( guest_test_bit(d, port, &shared_info(d, evtchn_pending)) )
            evtchn->pending = true;

        evtchn_fifo_set_priority(d, evtchn, EVTCHN_FIFO_PRIORITY_DEFAULT);
    }
}

int evtchn_fifo_init_control(struct evtchn_init_control *init_control)
{
    struct domain *d = current->domain;
    uint32_t vcpu_id;
    uint64_t gfn;
    uint32_t offset;
    struct vcpu *v;
    int rc;

    init_control->link_bits = EVTCHN_FIFO_LINK_BITS;

    vcpu_id = init_control->vcpu;
    gfn     = init_control->control_gfn;
    offset  = init_control->offset;

    if ( (v = domain_vcpu(d, vcpu_id)) == NULL )
        return -ENOENT;

    /* Must not cross page boundary. */
    if ( offset > (PAGE_SIZE - sizeof(evtchn_fifo_control_block_t)) )
        return -EINVAL;

    /*
     * Make sure the guest controlled value offset is bounded even during
     * speculative execution.
     */
    offset = array_index_nospec(offset,
                           PAGE_SIZE - sizeof(evtchn_fifo_control_block_t) + 1);

    /* Must be 8-bytes aligned. */
    if ( offset & (8 - 1) )
        return -EINVAL;

    write_lock(&d->event_lock);

    /*
     * If this is the first control block, setup an empty event array
     * and switch to the fifo port ops.
     */
    if ( !d->evtchn_fifo )
    {
        struct vcpu *vcb;
        /* Latch the value before it changes during setup_event_array(). */
        unsigned int prev_evtchns = max_evtchns(d);

        for_each_vcpu ( d, vcb ) {
            rc = setup_control_block(vcb);
            if ( rc < 0 )
                goto error;
        }

        rc = setup_event_array(d);
        if ( rc < 0 )
            goto error;

        /*
         * This call, as a side effect, synchronizes with
         * evtchn_fifo_word_from_port().
         */
        rc = map_control_block(v, gfn, offset);
        if ( rc < 0 )
            goto error;

        d->evtchn_port_ops = &evtchn_port_ops_fifo;
        setup_ports(d, prev_evtchns);
    }
    else
        rc = map_control_block(v, gfn, offset);

    write_unlock(&d->event_lock);

    return rc;

 error:
    evtchn_fifo_destroy(d);
    write_unlock(&d->event_lock);
    return rc;
}

static int add_page_to_event_array(struct domain *d, unsigned long gfn)
{
    void *virt;
    unsigned int slot;
    unsigned int port = d->evtchn_fifo->num_evtchns;
    int rc;

    slot = d->evtchn_fifo->num_evtchns / EVTCHN_FIFO_EVENT_WORDS_PER_PAGE;
    if ( slot >= EVTCHN_FIFO_MAX_EVENT_ARRAY_PAGES )
        return -ENOSPC;

    rc = map_guest_page(d, gfn, &virt);
    if ( rc < 0 )
        return rc;

    d->evtchn_fifo->event_array[slot] = virt;

    /* Synchronize with evtchn_fifo_word_from_port(). */
    smp_wmb();

    d->evtchn_fifo->num_evtchns += EVTCHN_FIFO_EVENT_WORDS_PER_PAGE;

    /*
     * Re-raise any events that were pending while this array page was
     * missing.
     */
    for ( ; port < d->evtchn_fifo->num_evtchns; port++ )
    {
        struct evtchn *evtchn;

        if ( !port_is_valid(d, port) )
            break;

        evtchn = evtchn_from_port(d, port);
        if ( evtchn->pending )
            evtchn_fifo_set_pending(d->vcpu[evtchn->notify_vcpu_id], evtchn);
    }

    return 0;
}

int evtchn_fifo_expand_array(const struct evtchn_expand_array *expand_array)
{
    struct domain *d = current->domain;
    int rc;

    if ( !d->evtchn_fifo )
        return -EOPNOTSUPP;

    write_lock(&d->event_lock);
    rc = add_page_to_event_array(d, expand_array->array_gfn);
    write_unlock(&d->event_lock);

    return rc;
}

void evtchn_fifo_destroy(struct domain *d)
{
    struct vcpu *v;

    for_each_vcpu( d, v )
        cleanup_control_block(v);
    cleanup_event_array(d);
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

/* === END INLINED: event_fifo.c === */
/* === BEGIN INLINED: static-evtchn.c === */
#include <xen_xen_config.h>
/* SPDX-License-Identifier: GPL-2.0-only */

#include <xen_event.h>

#include <asm_static-evtchn.h>

#define STATIC_EVTCHN_NODE_SIZE_CELLS 2

static int __init get_evtchn_dt_property(const struct dt_device_node *np,
                                         uint32_t *port, uint32_t *phandle)
{
    const __be32 *prop = NULL;
    uint32_t len;

    prop = dt_get_property(np, "xen,evtchn", &len);
    if ( !prop )
    {
        printk(XENLOG_ERR "xen,evtchn property should not be empty.\n");
        return -EINVAL;
    }

    if ( !len || len < dt_cells_to_size(STATIC_EVTCHN_NODE_SIZE_CELLS) )
    {
        printk(XENLOG_ERR "xen,evtchn property value is not valid.\n");
        return -EINVAL;
    }

    *port = dt_next_cell(1, &prop);
    *phandle = dt_next_cell(1, &prop);

    return 0;
}

static int __init alloc_domain_evtchn(struct dt_device_node *node)
{
    int rc;
    uint32_t domU1_port, domU2_port, remote_phandle;
    struct dt_device_node *remote_node;
    const struct dt_device_node *p1_node, *p2_node;
    struct evtchn_alloc_unbound alloc_unbound;
    struct evtchn_bind_interdomain bind_interdomain;
    struct domain *d1 = NULL, *d2 = NULL;

    if ( !dt_device_is_compatible(node, "xen,evtchn-v1") )
        return 0;

    /*
     * Event channel is already created while parsing the other side of
     * evtchn node.
     */
    if ( dt_device_static_evtchn_created(node) )
        return 0;

    rc = get_evtchn_dt_property(node, &domU1_port, &remote_phandle);
    if ( rc )
        return rc;

    remote_node = dt_find_node_by_phandle(remote_phandle);
    if ( !remote_node )
    {
        printk(XENLOG_ERR
                "evtchn: could not find remote evtchn phandle\n");
        return -EINVAL;
    }

    rc = get_evtchn_dt_property(remote_node, &domU2_port, &remote_phandle);
    if ( rc )
        return rc;

    if ( node->phandle != remote_phandle )
    {
        printk(XENLOG_ERR "xen,evtchn property is not setup correctly.\n");
        return -EINVAL;
    }

    p1_node = dt_get_parent(node);
    if ( !p1_node )
    {
        printk(XENLOG_ERR "evtchn: evtchn parent node is NULL\n" );
        return -EINVAL;
    }

    p2_node = dt_get_parent(remote_node);
    if ( !p2_node )
    {
        printk(XENLOG_ERR "evtchn: remote parent node is NULL\n" );
        return -EINVAL;
    }

    d1 = get_domain_by_id(p1_node->used_by);
    d2 = get_domain_by_id(p2_node->used_by);

    if ( !d1 || !d2 )
    {
        printk(XENLOG_ERR "evtchn: could not find domains\n" );
        return -EINVAL;
    }

    alloc_unbound.dom = d1->domain_id;
    alloc_unbound.remote_dom = d2->domain_id;

    rc = evtchn_alloc_unbound(&alloc_unbound, domU1_port);
    if ( rc < 0 )
    {
        printk(XENLOG_ERR
                "evtchn_alloc_unbound() failure (Error %d) \n", rc);
        return rc;
    }

    bind_interdomain.remote_dom  = d1->domain_id;
    bind_interdomain.remote_port = domU1_port;

    rc = evtchn_bind_interdomain(&bind_interdomain, d2, domU2_port);
    if ( rc < 0 )
    {
        printk(XENLOG_ERR
                "evtchn_bind_interdomain() failure (Error %d) \n", rc);
        return rc;
    }

    dt_device_set_static_evtchn_created(node);
    dt_device_set_static_evtchn_created(remote_node);

    return 0;
}

void __init alloc_static_evtchn(void)
{
    struct dt_device_node *node, *evtchn_node;
    struct dt_device_node *chosen = dt_find_node_by_path("/chosen");

    BUG_ON(chosen == NULL);

    if ( hardware_domain )
        dt_device_set_used_by(chosen, hardware_domain->domain_id);

    dt_for_each_child_node(chosen, node)
    {
        if ( hardware_domain )
        {
            if ( alloc_domain_evtchn(node) != 0 )
                panic("Could not set up domains evtchn\n");
        }

        dt_for_each_child_node(node, evtchn_node)
        {
            if ( alloc_domain_evtchn(evtchn_node) != 0 )
                panic("Could not set up domains evtchn\n");
        }
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

/* === END INLINED: static-evtchn.c === */
/* === BEGIN INLINED: grant_table.c === */
#include <xen_xen_config.h>
/******************************************************************************
 * common/grant_table.c
 *
 * Mechanism for granting foreign access to page frames, and receiving
 * page-ownership transfers.
 *
 * Copyright (c) 2005-2006 Christopher Clark
 * Copyright (c) 2004 K A Fraser
 * Copyright (c) 2005 Andrew Warfield
 * Modifications by Geoffrey Lefebvre are (c) Intel Research Cambridge
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; If not, see <http://www.gnu.org/licenses/>.
 */

#include <xen_err.h>
#include <xen_iocap.h>
#include <xen_lib.h>
#include <xen_sched.h>
#include <xen_mm.h>
#include <xen_param.h>
#include <xen_event.h>
#include <xen_trace.h>
#include <xen_grant_table.h>
#include <xen_guest_access.h>
#include <xen_hypercall.h>
#include <xen_domain_page.h>
#include <xen_iommu.h>
#include <xen_paging.h>
#include <xen_keyhandler.h>
#include <xen_radix-tree.h>
#include <xen_vmap.h>
#include <xen_nospec.h>
#include <xsm_xsm.h>
#include <asm_flushtlb.h>
#include <asm_guest_atomics.h>

#ifdef CONFIG_PV_SHIM
#include <asm/guest.h>
#endif

/* Per-domain grant information. */
struct grant_table {
    /*
     * Lock protecting updates to grant table state (version, active
     * entry list, etc.)
     */
    percpu_rwlock_t       lock;
    /* Lock protecting the maptrack limit */
    spinlock_t            maptrack_lock;
    unsigned int          max_version;
    /*
     * Defaults to v1.  May be changed with GNTTABOP_set_version.  All other
     * values are invalid.
     */
    unsigned int          gt_version;
    /* Resource limits of the domain. */
    unsigned int          max_grant_frames;
    unsigned int          max_maptrack_frames;
    /* Table size. Number of frames shared with guest */
    unsigned int          nr_grant_frames;
    /* Number of grant status frames shared with guest (for version 2) */
    unsigned int          nr_status_frames;
    /*
     * Number of available maptrack entries.  For cleanup purposes it is
     * important to realize that this field and @maptrack further down will
     * only ever be accessed by the local domain.  Thus it is okay to clean
     * up early, and to shrink the limit for the purpose of tracking cleanup
     * progress.
     */
    unsigned int          maptrack_limit;
    /* Shared grant table (see include/public/grant_table.h). */
    union {
        void **shared_raw;
        struct grant_entry_v1 **shared_v1;
        union grant_entry_v2 **shared_v2;
    };
    /* State grant table (see include/public/grant_table.h). */
    grant_status_t       **status;
    /* Active grant table. */
    struct active_grant_entry **active;
    /* Handle-indexed tracking table of mappings. */
    struct grant_mapping **maptrack;
    /*
     * MFN-indexed tracking tree of mappings, if needed.  Note that this is
     * protected by @lock, not @maptrack_lock.
     */
    struct radix_tree_root maptrack_tree;

    /* Domain to which this struct grant_table belongs. */
    struct domain *domain;
};

unsigned int __read_mostly opt_max_grant_frames = 64;
static unsigned int __read_mostly opt_max_maptrack_frames = 1024;

#ifdef CONFIG_HYPFS
#define GRANT_CUSTOM_VAL_SZ  12
static char __read_mostly opt_max_grant_frames_val[GRANT_CUSTOM_VAL_SZ];
static char __read_mostly opt_max_maptrack_frames_val[GRANT_CUSTOM_VAL_SZ];

static void update_gnttab_par(unsigned int val, struct param_hypfs *par,
                              char *parval)
{
    snprintf(parval, GRANT_CUSTOM_VAL_SZ, "%u", val);
    custom_runtime_set_var_sz(par, parval, GRANT_CUSTOM_VAL_SZ);
}

static void __init cf_check gnttab_max_frames_init(struct param_hypfs *par)
{
    update_gnttab_par(opt_max_grant_frames, par, opt_max_grant_frames_val);
}

static void __init cf_check max_maptrack_frames_init(struct param_hypfs *par)
{
    update_gnttab_par(opt_max_maptrack_frames, par,
                      opt_max_maptrack_frames_val);
}
#else
#define update_gnttab_par(v, unused1, unused2)     update_gnttab_par(v)
#define parse_gnttab_limit(a, v, unused1, unused2) parse_gnttab_limit(a, v)

static void update_gnttab_par(unsigned int val, struct param_hypfs *par,
                              char *parval)
{
}
#endif

static int parse_gnttab_limit(const char *arg, unsigned int *valp,
                              struct param_hypfs *par, char *parval)
{
    const char *e;
    unsigned long val;

    val = simple_strtoul(arg, &e, 0);
    if ( *e )
        return -EINVAL;

    if ( val > INT_MAX )
        return -ERANGE;

    *valp = val;
    update_gnttab_par(val, par, parval);

    return 0;
}

static int cf_check parse_gnttab_max_frames(const char *arg);
custom_runtime_param("gnttab_max_frames", parse_gnttab_max_frames,
                     gnttab_max_frames_init);

static int cf_check parse_gnttab_max_frames(const char *arg)
{
    return parse_gnttab_limit(arg, &opt_max_grant_frames,
                              param_2_parfs(parse_gnttab_max_frames),
                              opt_max_grant_frames_val);
}

static int cf_check parse_gnttab_max_maptrack_frames(const char *arg);
custom_runtime_param("gnttab_max_maptrack_frames",
                     parse_gnttab_max_maptrack_frames,
                     max_maptrack_frames_init);

static int cf_check parse_gnttab_max_maptrack_frames(const char *arg)
{
    return parse_gnttab_limit(arg, &opt_max_maptrack_frames,
                              param_2_parfs(parse_gnttab_max_maptrack_frames),
                              opt_max_maptrack_frames_val);
}

#ifndef GNTTAB_MAX_VERSION
#define GNTTAB_MAX_VERSION 2
#endif

unsigned int __read_mostly opt_gnttab_max_version = GNTTAB_MAX_VERSION;
static bool __read_mostly opt_transitive_grants = true;
#ifdef CONFIG_PV
static bool __ro_after_init opt_grant_transfer = true;
#else
#define opt_grant_transfer false
#endif

static int __init cf_check parse_gnttab(const char *s)
{
    const char *ss, *e;
    int val, rc = 0;

    do {
        ss = strchr(s, ',');
        if ( !ss )
            ss = strchr(s, '\0');

        if ( !strncmp(s, "max-ver:", 8) ||
             !strncmp(s, "max_ver:", 8) ) /* Alias for original XSA-226 patch */
        {
            long ver = simple_strtol(s + 8, &e, 10);

            if ( e == ss && ver >= 1 && ver <= 2 )
                opt_gnttab_max_version = ver;
            else
                rc = -EINVAL;
        }
        else if ( (val = parse_boolean("transitive", s, ss)) >= 0 )
            opt_transitive_grants = val;
#ifndef opt_grant_transfer
        else if ( (val = parse_boolean("transfer", s, ss)) >= 0 )
            opt_grant_transfer = val;
#endif
        else
            rc = -EINVAL;

        s = ss + 1;
    } while ( *ss );

    return rc;
}
custom_param("gnttab", parse_gnttab);

/*
 * Note that the three values below are effectively part of the ABI, even if
 * we don't need to make them a formal part of it: A guest suspended for
 * migration in the middle of a continuation would fail to work if resumed on
 * a hypervisor using different values.
 */
#define GNTTABOP_CONTINUATION_ARG_SHIFT 12
#define GNTTABOP_CMD_MASK               ((1<<GNTTABOP_CONTINUATION_ARG_SHIFT)-1)
#define GNTTABOP_ARG_MASK               (~GNTTABOP_CMD_MASK)

/*
 * The first two members of a grant entry are updated as a combined pair.
 * The following union allows that to happen in an endian-neutral fashion.
 */
union grant_combo {
    uint32_t raw;
    struct {
        uint16_t flags;
        domid_t  domid;
    };
};

/* Used to share code between unmap_grant_ref and unmap_and_replace. */
struct gnttab_unmap_common {
    /* Input */
    uint64_t host_addr;
    uint64_t dev_bus_addr;
    uint64_t new_addr;
    grant_handle_t handle;

    /* Return */
    int16_t status;

    /* Shared state beteen *_unmap and *_unmap_complete */
    uint16_t done;
    mfn_t mfn;
    struct domain *rd;
    grant_ref_t ref;
};

/* Number of unmap operations that are done between each tlb flush */
#define GNTTAB_UNMAP_BATCH_SIZE 32


/*
 * Tracks a mapping of another domain's grant reference. Each domain has a
 * table of these, indexes into which are returned as a 'mapping handle'.
 */
struct grant_mapping {
    grant_ref_t ref;        /* grant ref */
    uint16_t flags;         /* 0-4: GNTMAP_* ; 5-15: unused */
    domid_t  domid;         /* granting domain */
    uint32_t vcpu;          /* vcpu which created the grant mapping */
    uint32_t pad;           /* round size to a power of 2 */
};

/* Number of grant table frames. Caller must hold d's grant table lock. */
static inline unsigned int nr_grant_frames(const struct grant_table *gt)
{
    return gt->nr_grant_frames;
}

/* Number of status grant table frames. Caller must hold d's gr. table lock.*/
static inline unsigned int nr_status_frames(const struct grant_table *gt)
{
    return gt->nr_status_frames;
}

#define MAPTRACK_PER_PAGE (PAGE_SIZE / sizeof(struct grant_mapping))
#define maptrack_entry(t, e)                                                   \
    ((t)->maptrack[array_index_nospec(e, (t)->maptrack_limit) /                \
                                    MAPTRACK_PER_PAGE][(e) % MAPTRACK_PER_PAGE])

static inline unsigned int
nr_maptrack_frames(struct grant_table *t)
{
    return t->maptrack_limit / MAPTRACK_PER_PAGE;
}

#define MAPTRACK_TAIL (~0u)

#define SHGNT_PER_PAGE_V1 (PAGE_SIZE / sizeof(grant_entry_v1_t))
#define shared_entry_v1(t, e) \
    ((t)->shared_v1[(e)/SHGNT_PER_PAGE_V1][(e)%SHGNT_PER_PAGE_V1])
#define SHGNT_PER_PAGE_V2 (PAGE_SIZE / sizeof(grant_entry_v2_t))
#define shared_entry_v2(t, e) \
    ((t)->shared_v2[(e)/SHGNT_PER_PAGE_V2][(e)%SHGNT_PER_PAGE_V2])
#define STGNT_PER_PAGE (PAGE_SIZE / sizeof(grant_status_t))
#define status_entry(t, e) \
    ((t)->status[(e)/STGNT_PER_PAGE][(e)%STGNT_PER_PAGE])
static grant_entry_header_t *
shared_entry_header(struct grant_table *t, grant_ref_t ref)
{
    switch ( t->gt_version )
    {
    case 1:
        /* Returned values should be independent of speculative execution */
        block_speculation();
        return (grant_entry_header_t*)&shared_entry_v1(t, ref);

    case 2:
        /* Returned values should be independent of speculative execution */
        block_speculation();
        return &shared_entry_v2(t, ref).hdr;
    }

    ASSERT_UNREACHABLE();
    block_speculation();

    return NULL;
}

/* Active grant entry - used for shadowing GTF_permit_access grants. */
struct active_grant_entry {
/*
 * 4x byte-wide reference counts, for {host,device}{read,write} mappings,
 * implemented as a single 32-bit (presumably to optimise checking for any
 * reference).
 */
    uint32_t      pin;
                          /* Width of the individual counter fields.  */
#define GNTPIN_cntr_width    8
#define GNTPIN_cntr_mask     ((1U << GNTPIN_cntr_width) - 1)
                          /* Count of writable host-CPU mappings.     */
#define GNTPIN_hstw_shift    0
#define GNTPIN_hstw_inc      (1U << GNTPIN_hstw_shift)
#define GNTPIN_hstw_mask     (GNTPIN_cntr_mask << GNTPIN_hstw_shift)
                          /* Count of read-only host-CPU mappings.    */
#define GNTPIN_hstr_shift    (GNTPIN_hstw_shift + GNTPIN_cntr_width)
#define GNTPIN_hstr_inc      (1U << GNTPIN_hstr_shift)
#define GNTPIN_hstr_mask     (GNTPIN_cntr_mask << GNTPIN_hstr_shift)
                          /* Count of writable device-bus mappings.   */
#define GNTPIN_devw_shift    (GNTPIN_hstr_shift + GNTPIN_cntr_width)
#define GNTPIN_devw_inc      (1U << GNTPIN_devw_shift)
#define GNTPIN_devw_mask     (GNTPIN_cntr_mask << GNTPIN_devw_shift)
                          /* Count of read-only device-bus mappings.  */
#define GNTPIN_devr_shift    (GNTPIN_devw_shift + GNTPIN_cntr_width)
#define GNTPIN_devr_inc      (1U << GNTPIN_devr_shift)
#define GNTPIN_devr_mask     (GNTPIN_cntr_mask << GNTPIN_devr_shift)

/* Convert a combination of GNTPIN_*_inc to an overflow checking mask. */
#define GNTPIN_incr2oflow_mask(x) ({                       \
    ASSERT(!((x) & ~(GNTPIN_hstw_inc | GNTPIN_hstr_inc |   \
                     GNTPIN_devw_inc | GNTPIN_devr_inc))); \
    (x) << (GNTPIN_cntr_width - 1);                        \
})

    domid_t       domid;  /* Domain being granted access.             */
    domid_t       src_domid; /* Original domain granting access.      */
    unsigned int  start:15; /* For sub-page grants, the start offset
                               in the page.                           */
    bool          is_sub_page:1; /* True if this is a sub-page grant. */
    unsigned int  length:16; /* For sub-page grants, the length of the
                                grant.                                */
    grant_ref_t   trans_gref;
    mfn_t         mfn;    /* Machine frame being granted.             */
#ifndef NDEBUG
    gfn_t         gfn;    /* Guest's idea of the frame being granted. */
#endif
    spinlock_t    lock;      /* lock to protect access of this entry.
                                see docs/misc/grant-tables.txt for
                                locking protocol                      */
};

#define ACGNT_PER_PAGE (PAGE_SIZE / sizeof(struct active_grant_entry))
#define _active_entry(t, e) \
    ((t)->active[(e)/ACGNT_PER_PAGE][(e)%ACGNT_PER_PAGE])

static inline void act_set_gfn(struct active_grant_entry *act, gfn_t gfn)
{
#ifndef NDEBUG
    act->gfn = gfn;
#endif
}

static DEFINE_PERCPU_RWLOCK_GLOBAL(grant_rwlock);

static always_inline void grant_read_lock(struct grant_table *gt)
{
    percpu_read_lock(grant_rwlock, &gt->lock);
}

static inline void grant_read_unlock(struct grant_table *gt)
{
    percpu_read_unlock(grant_rwlock, &gt->lock);
}

static always_inline void grant_write_lock(struct grant_table *gt)
{
    percpu_write_lock(grant_rwlock, &gt->lock);
}

static inline void grant_write_unlock(struct grant_table *gt)
{
    percpu_write_unlock(grant_rwlock, &gt->lock);
}

static inline void gnttab_flush_tlb(const struct domain *d)
{
    if ( !paging_mode_external(d) )
        arch_flush_tlb_mask(d->dirty_cpumask);
}

static inline unsigned int
num_act_frames_from_sha_frames(const unsigned int num)
{
    /*
     * How many frames are needed for the active grant table,
     * given the size of the shared grant table?
     */
    unsigned int sha_per_page = PAGE_SIZE / sizeof(grant_entry_v1_t);

    return DIV_ROUND_UP(num * sha_per_page, ACGNT_PER_PAGE);
}

#define max_nr_active_grant_frames(gt) \
    num_act_frames_from_sha_frames((gt)->max_grant_frames)

static inline unsigned int
nr_active_grant_frames(struct grant_table *gt)
{
    return num_act_frames_from_sha_frames(nr_grant_frames(gt));
}

static always_inline struct active_grant_entry *
active_entry_acquire(struct grant_table *t, grant_ref_t e)
{
    struct active_grant_entry *act;

    /*
     * The grant table for the active entry should be locked but the
     * percpu rwlock cannot be checked for read lock without race conditions
     * or high overhead so we cannot use an ASSERT
     *
     *   ASSERT(rw_is_locked(&t->lock));
     */

    act = &_active_entry(t, e);
    spin_lock(&act->lock);

    return act;
}

static inline void active_entry_release(struct active_grant_entry *act)
{
    spin_unlock(&act->lock);
}

#define GRANT_STATUS_PER_PAGE (PAGE_SIZE / sizeof(grant_status_t))
#define GRANT_PER_PAGE (PAGE_SIZE / sizeof(grant_entry_v2_t))

static inline unsigned int grant_to_status_frames(unsigned int grant_frames)
{
    return DIV_ROUND_UP(grant_frames * GRANT_PER_PAGE, GRANT_STATUS_PER_PAGE);
}

static inline unsigned int status_to_grant_frames(unsigned int status_frames)
{
    return DIV_ROUND_UP(status_frames * GRANT_STATUS_PER_PAGE, GRANT_PER_PAGE);
}

/* Check if the page has been paged out, or needs unsharing.
   If rc == GNTST_okay, *page contains the page struct with a ref taken.
   Caller must do put_page(*page).
   If any error, *page = NULL, *mfn = INVALID_MFN, no ref taken. */
static int get_paged_frame(unsigned long gfn, mfn_t *mfn,
                           struct page_info **page, bool readonly,
                           struct domain *rd)
{
    p2m_type_t p2mt;
    int rc;

    rc = check_get_page_from_gfn(rd, _gfn(gfn), readonly, &p2mt, page);
    switch ( rc )
    {
    case 0:
        break;

    case -EAGAIN:
        return GNTST_eagain;

    default:
        ASSERT_UNREACHABLE();
        /* Fallthrough */

    case -EINVAL:
        return GNTST_bad_page;
    }

    if ( p2m_is_foreign(p2mt) )
    {
        put_page(*page);
        *page = NULL;

        return GNTST_bad_page;
    }

    *mfn = page_to_mfn(*page);

    return GNTST_okay;
}

#define INVALID_MAPTRACK_HANDLE UINT_MAX

static inline grant_handle_t
_get_maptrack_handle(struct grant_table *t, struct vcpu *v)
{
    unsigned int head, next;

    spin_lock(&v->maptrack_freelist_lock);

    /* No maptrack pages allocated for this VCPU yet? */
    head = v->maptrack_head;
    if ( unlikely(head == MAPTRACK_TAIL) )
    {
        spin_unlock(&v->maptrack_freelist_lock);
        return INVALID_MAPTRACK_HANDLE;
    }

    /*
     * Always keep one entry in the free list to make it easier to
     * add free entries to the tail.
     */
    next = maptrack_entry(t, head).ref;
    if ( unlikely(next == MAPTRACK_TAIL) )
        head = INVALID_MAPTRACK_HANDLE;
    else
        v->maptrack_head = next;

    spin_unlock(&v->maptrack_freelist_lock);

    return head;
}

/*
 * Try to "steal" a free maptrack entry from another VCPU.
 *
 * A stolen entry is transferred to the thief, so the number of
 * entries for each VCPU should tend to the usage pattern.
 *
 * To avoid having to atomically count the number of free entries on
 * each VCPU and to avoid two VCPU repeatedly stealing entries from
 * each other, the initial victim VCPU is selected randomly.
 */
static grant_handle_t steal_maptrack_handle(struct grant_table *t,
                                            const struct vcpu *curr)
{
    const struct domain *currd = curr->domain;
    unsigned int first, i;

    /* Find an initial victim. */
    first = i = get_random() % currd->max_vcpus;

    do {
        if ( currd->vcpu[i] )
        {
            grant_handle_t handle;

            handle = _get_maptrack_handle(t, currd->vcpu[i]);
            if ( handle != INVALID_MAPTRACK_HANDLE )
            {
                maptrack_entry(t, handle).vcpu = curr->vcpu_id;
                return handle;
            }
        }

        i++;
        if ( i == currd->max_vcpus )
            i = 0;
    } while ( i != first );

    /* No free handles on any VCPU. */
    return INVALID_MAPTRACK_HANDLE;
}

static inline void
put_maptrack_handle(
    struct grant_table *t, grant_handle_t handle)
{
    struct domain *currd = current->domain;
    struct vcpu *v;
    unsigned int tail;

    /* 1. Set entry to be a tail. */
    maptrack_entry(t, handle).ref = MAPTRACK_TAIL;

    /* 2. Add entry to the tail of the list on the original VCPU. */
    v = currd->vcpu[maptrack_entry(t, handle).vcpu];

    spin_lock(&v->maptrack_freelist_lock);

    tail = v->maptrack_tail;
    v->maptrack_tail = handle;

    /* 3. Update the old tail entry to point to the new entry. */
    maptrack_entry(t, tail).ref = handle;

    spin_unlock(&v->maptrack_freelist_lock);
}

static inline grant_handle_t
get_maptrack_handle(
    struct grant_table *lgt)
{
    struct vcpu          *curr = current;
    unsigned int          i;
    grant_handle_t        handle;
    struct grant_mapping *new_mt = NULL;

    handle = _get_maptrack_handle(lgt, curr);
    if ( likely(handle != INVALID_MAPTRACK_HANDLE) )
        return handle;

    spin_lock(&lgt->maptrack_lock);

    /*
     * If we've run out of handles and still have frame headroom, try
     * allocating a new maptrack frame.  If there is no headroom, or we're
     * out of memory, try stealing an entry from another VCPU (in case the
     * guest isn't mapping across its VCPUs evenly).
     */
    if ( nr_maptrack_frames(lgt) < lgt->max_maptrack_frames )
        new_mt = alloc_xenheap_page();

    if ( !new_mt )
    {
        spin_unlock(&lgt->maptrack_lock);

        /*
         * Uninitialized free list? Steal an extra entry for the tail
         * sentinel.
         */
        if ( curr->maptrack_tail == MAPTRACK_TAIL )
        {
            handle = steal_maptrack_handle(lgt, curr);
            if ( handle == INVALID_MAPTRACK_HANDLE )
                return handle;
            spin_lock(&curr->maptrack_freelist_lock);
            maptrack_entry(lgt, handle).ref = MAPTRACK_TAIL;
            curr->maptrack_tail = handle;
            if ( curr->maptrack_head == MAPTRACK_TAIL )
                curr->maptrack_head = handle;
            spin_unlock(&curr->maptrack_freelist_lock);
        }
        return steal_maptrack_handle(lgt, curr);
    }

    clear_page(new_mt);

    /*
     * Use the first new entry and add the remaining entries to the
     * head of the free list.
     */
    handle = lgt->maptrack_limit;

    for ( i = 0; i < MAPTRACK_PER_PAGE; i++ )
    {
        BUILD_BUG_ON(sizeof(new_mt->ref) < sizeof(handle));
        new_mt[i].ref = handle + i + 1;
        new_mt[i].vcpu = curr->vcpu_id;
    }

    /* Set tail directly if this is the first page for the local vCPU. */
    if ( curr->maptrack_tail == MAPTRACK_TAIL )
        curr->maptrack_tail = handle + MAPTRACK_PER_PAGE - 1;

    lgt->maptrack[nr_maptrack_frames(lgt)] = new_mt;
    smp_wmb();
    lgt->maptrack_limit += MAPTRACK_PER_PAGE;

    spin_unlock(&lgt->maptrack_lock);

    spin_lock(&curr->maptrack_freelist_lock);
    new_mt[i - 1].ref = curr->maptrack_head;
    curr->maptrack_head = handle + 1;
    spin_unlock(&curr->maptrack_freelist_lock);

    return handle;
}

/* Number of grant table entries. Caller must hold d's grant table lock. */
static unsigned int nr_grant_entries(struct grant_table *gt)
{
    switch ( gt->gt_version )
    {
#define f2e(nr, ver) (((nr) << PAGE_SHIFT) / sizeof(grant_entry_v##ver##_t))
    case 1:
        BUILD_BUG_ON(f2e(INITIAL_NR_GRANT_FRAMES, 1) <
                     GNTTAB_NR_RESERVED_ENTRIES);

        /* Make sure we return a value independently of speculative execution */
        block_speculation();
        return f2e(nr_grant_frames(gt), 1);

    case 2:
        BUILD_BUG_ON(f2e(INITIAL_NR_GRANT_FRAMES, 2) <
                     GNTTAB_NR_RESERVED_ENTRIES);

        /* Make sure we return a value independently of speculative execution */
        block_speculation();
        return f2e(nr_grant_frames(gt), 2);
#undef f2e
    }

    ASSERT_UNREACHABLE();
    block_speculation();

    return 0;
}

static int _set_status_v1(const grant_entry_header_t *shah,
                          struct domain *rd,
                          struct active_grant_entry *act,
                          int readonly,
                          int mapflag,
                          domid_t  ldomid)
{
    int rc = GNTST_okay;
    uint32_t *raw_shah = (uint32_t *)shah;
    union grant_combo scombo;
    uint16_t mask = GTF_type_mask;

    /*
     * We bound the number of times we retry CMPXCHG on memory locations that
     * we share with a guest OS. The reason is that the guest can modify that
     * location at a higher rate than we can read-modify-CMPXCHG, so the guest
     * could cause us to livelock. There are a few cases where it is valid for
     * the guest to race our updates (e.g., to change the GTF_readonly flag),
     * so we allow a few retries before failing.
     */
    int retries = 0;

    /* if this is a grant mapping operation we should ensure GTF_sub_page
       is not set */
    if ( mapflag )
        mask |= GTF_sub_page;

    scombo.raw = ACCESS_ONCE(*raw_shah);

    /*
     * This loop attempts to set the access (reading/writing) flags
     * in the grant table entry.  It tries a cmpxchg on the field
     * up to five times, and then fails under the assumption that
     * the guest is misbehaving.
     */
    for ( ; ; )
    {
        union grant_combo prev, new;

        /* If not already pinned, check the grant domid and type. */
        if ( !act->pin && (((scombo.flags & mask) != GTF_permit_access) ||
                           (scombo.domid != ldomid)) )
        {
            gdprintk(XENLOG_WARNING,
                     "Bad flags (%x) or dom (%d); expected d%d\n",
                     scombo.flags, scombo.domid, ldomid);
            rc = GNTST_general_error;
            goto done;
        }

        new = scombo;
        new.flags |= GTF_reading;

        if ( !readonly )
        {
            new.flags |= GTF_writing;
            if ( unlikely(scombo.flags & GTF_readonly) )
            {
                gdprintk(XENLOG_WARNING,
                         "Attempt to write-pin a r/o grant entry\n");
                rc = GNTST_general_error;
                goto done;
            }
        }

        prev.raw = guest_cmpxchg(rd, raw_shah, scombo.raw, new.raw);
        if ( likely(prev.raw == scombo.raw) )
            break;

        if ( retries++ == 4 )
        {
            gdprintk(XENLOG_WARNING, "Shared grant entry is unstable\n");
            rc = GNTST_general_error;
            goto done;
        }

        scombo = prev;
    }

done:
    return rc;
}

static int _set_status_v2(const grant_entry_header_t *shah,
                          grant_status_t *status,
                          struct domain *rd,
                          struct active_grant_entry *act,
                          int readonly,
                          int mapflag,
                          domid_t  ldomid)
{
    int      rc    = GNTST_okay;
    uint32_t *raw_shah = (uint32_t *)shah;
    union grant_combo scombo;
    uint16_t mask  = GTF_type_mask;

    scombo.raw = ACCESS_ONCE(*raw_shah);

    /* if this is a grant mapping operation we should ensure GTF_sub_page
       is not set */
    if ( mapflag )
        mask |= GTF_sub_page;

    /* If not already pinned, check the grant domid and type. */
    if ( !act->pin &&
         ((((scombo.flags & mask) != GTF_permit_access) &&
           (mapflag || ((scombo.flags & mask) != GTF_transitive))) ||
          (scombo.domid != ldomid)) )
    {
        gdprintk(XENLOG_WARNING,
                 "Bad flags (%x) or dom (%d); expected d%d, flags %x\n",
                 scombo.flags, scombo.domid, ldomid, mask);
        rc = GNTST_general_error;
        goto done;
    }

    if ( readonly )
    {
        *status |= GTF_reading;
    }
    else
    {
        if ( unlikely(scombo.flags & GTF_readonly) )
        {
            gdprintk(XENLOG_WARNING,
                     "Attempt to write-pin a r/o grant entry\n");
            rc = GNTST_general_error;
            goto done;
        }
        *status |= GTF_reading | GTF_writing;
    }

    /* Make sure guest sees status update before checking if flags are
       still valid */
    smp_mb();

    scombo.raw = ACCESS_ONCE(*raw_shah);

    if ( !act->pin )
    {
        if ( (((scombo.flags & mask) != GTF_permit_access) &&
              (mapflag || ((scombo.flags & mask) != GTF_transitive))) ||
             (scombo.domid != ldomid) ||
             (!readonly && (scombo.flags & GTF_readonly)) )
        {
            gnttab_clear_flags(rd, GTF_writing | GTF_reading, status);
            gdprintk(XENLOG_WARNING,
                     "Unstable flags (%x) or dom (%d); expected d%d (r/w: %d)\n",
                     scombo.flags, scombo.domid, ldomid, !readonly);
            rc = GNTST_general_error;
            goto done;
        }
    }
    else
    {
        if ( unlikely(scombo.flags & GTF_readonly) )
        {
            gnttab_clear_flags(rd, GTF_writing, status);
            gdprintk(XENLOG_WARNING, "Unstable grant readonly flag\n");
            rc = GNTST_general_error;
            goto done;
        }
    }

done:
    return rc;
}


static int _set_status(const grant_entry_header_t *shah,
                       grant_status_t *status,
                       struct domain *rd,
                       unsigned int rgt_version,
                       struct active_grant_entry *act,
                       int readonly,
                       int mapflag,
                       domid_t ldomid)
{

    if ( evaluate_nospec(rgt_version == 1) )
        return _set_status_v1(shah, rd, act, readonly, mapflag, ldomid);
    else
        return _set_status_v2(shah, status, rd, act, readonly, mapflag, ldomid);
}

/*
 * The status for a grant may indicate that we're taking more access than
 * the pin requires.  Reduce the status to match the pin.  Called with the
 * domain's grant table lock held at least in read mode and with the active
 * entry lock held (iow act->pin can't change behind our backs).
 */
static void reduce_status_for_pin(struct domain *rd,
                                  const struct active_grant_entry *act,
                                  uint16_t *status, bool readonly)
{
    unsigned int clear_flags = act->pin ? 0 : GTF_reading;

    if ( !readonly && !(act->pin & (GNTPIN_hstw_mask | GNTPIN_devw_mask)) )
        clear_flags |= GTF_writing;

    if ( clear_flags )
        gnttab_clear_flags(rd, clear_flags, status);
}

static struct active_grant_entry *grant_map_exists(const struct domain *ld,
                                                   struct grant_table *rgt,
                                                   mfn_t mfn,
                                                   grant_ref_t *cur_ref)
{
    grant_ref_t ref, max_iter;

    /*
     * The remote grant table should be locked but the percpu rwlock
     * cannot be checked for read lock without race conditions or high
     * overhead so we cannot use an ASSERT
     *
     *   ASSERT(rw_is_locked(&rgt->lock));
     */

    max_iter = min(*cur_ref + (1 << GNTTABOP_CONTINUATION_ARG_SHIFT),
                   nr_grant_entries(rgt));
    for ( ref = *cur_ref; ref < max_iter; ref++ )
    {
        struct active_grant_entry *act = active_entry_acquire(rgt, ref);

        if ( act->pin && act->domid == ld->domain_id &&
             mfn_eq(act->mfn, mfn) )
            return act;
        active_entry_release(act);
    }

    if ( ref < nr_grant_entries(rgt) )
    {
        *cur_ref = ref;
        return NULL;
    }

    return ERR_PTR(-EINVAL);
}

union maptrack_node {
    struct {
        /* Radix tree slot pointers use two of the bits. */
#ifdef __BIG_ENDIAN_BITFIELD
        unsigned long _0 : 2;
#endif
        unsigned long rd : BITS_PER_LONG / 2 - 1;
        unsigned long wr : BITS_PER_LONG / 2 - 1;
#ifndef __BIG_ENDIAN_BITFIELD
        unsigned long _0 : 2;
#endif
    } cnt;
    unsigned long raw;
};

static void
map_grant_ref(
    struct gnttab_map_grant_ref *op)
{
    struct domain *ld, *rd, *owner = NULL;
    struct grant_table *lgt, *rgt;
    grant_ref_t ref;
    grant_handle_t handle;
    mfn_t mfn;
    struct page_info *pg = NULL;
    int            rc = GNTST_okay;
    unsigned int   cache_flags, refcnt = 0, typecnt = 0, pin_incr = 0;
    bool           host_map_created = false;
    struct active_grant_entry *act = NULL;
    struct grant_mapping *mt;
    grant_entry_header_t *shah;
    uint16_t *status;

    ld = current->domain;

    if ( op->flags & GNTMAP_device_map )
        pin_incr += (op->flags & GNTMAP_readonly) ? GNTPIN_devr_inc
                                                  : GNTPIN_devw_inc;
    if ( op->flags & GNTMAP_host_map )
        pin_incr += (op->flags & GNTMAP_readonly) ? GNTPIN_hstr_inc
                                                  : GNTPIN_hstw_inc;

    if ( unlikely(!pin_incr) )
    {
        gdprintk(XENLOG_INFO, "Bad flags in grant map op: %x\n", op->flags);
        op->status = GNTST_bad_gntref;
        return;
    }

    if ( unlikely(paging_mode_external(ld) &&
                  (op->flags & (GNTMAP_device_map|GNTMAP_application_map|
                            GNTMAP_contains_pte))) )
    {
        gdprintk(XENLOG_INFO, "No device mapping in HVM domain\n");
        op->status = GNTST_general_error;
        return;
    }

    if ( unlikely((rd = rcu_lock_domain_by_id(op->dom)) == NULL) )
    {
        gdprintk(XENLOG_INFO, "Could not find domain %d\n", op->dom);
        op->status = GNTST_bad_domain;
        return;
    }

    rc = xsm_grant_mapref(XSM_HOOK, ld, rd, op->flags);
    if ( rc )
    {
        rcu_unlock_domain(rd);
        op->status = GNTST_permission_denied;
        return;
    }

    lgt = ld->grant_table;
    handle = get_maptrack_handle(lgt);
    if ( unlikely(handle == INVALID_MAPTRACK_HANDLE) )
    {
        rcu_unlock_domain(rd);
        gdprintk(XENLOG_INFO, "Failed to obtain maptrack handle\n");
        op->status = GNTST_no_space;
        return;
    }

    rgt = rd->grant_table;
    grant_read_lock(rgt);

    /* Bounds check on the grant ref */
    ref = op->ref;
    if ( unlikely(ref >= nr_grant_entries(rgt)))
    {
        gdprintk(XENLOG_WARNING, "Bad ref %#x for d%d\n",
                 ref, rgt->domain->domain_id);
        rc = GNTST_bad_gntref;
        goto unlock_out;
    }

    /* This call also ensures the above check cannot be passed speculatively */
    shah = shared_entry_header(rgt, ref);
    act = active_entry_acquire(rgt, ref);

    /* If already pinned, check the active domid and avoid refcnt overflow. */
    if ( act->pin &&
         ((act->domid != ld->domain_id) ||
          (act->pin & GNTPIN_incr2oflow_mask(pin_incr)) ||
          (act->is_sub_page)) )
    {
        gdprintk(XENLOG_WARNING,
                 "Bad domain (%d != %d), or risk of counter overflow %08x, or subpage %d\n",
                 act->domid, ld->domain_id, act->pin, act->is_sub_page);
        rc = GNTST_general_error;
        goto act_release_out;
    }

    /* Make sure we do not access memory speculatively */
    status = evaluate_nospec(rgt->gt_version == 1) ? &shah->flags
                                                   : &status_entry(rgt, ref);

    if ( !act->pin ||
         (!(op->flags & GNTMAP_readonly) &&
          !(act->pin & (GNTPIN_hstw_mask|GNTPIN_devw_mask))) )
    {
        if ( (rc = _set_status(shah, status, rd, rgt->gt_version, act,
                               op->flags & GNTMAP_readonly, 1,
                               ld->domain_id)) != GNTST_okay )
            goto act_release_out;

        if ( !act->pin )
        {
            unsigned long gfn = evaluate_nospec(rgt->gt_version == 1) ?
                                shared_entry_v1(rgt, ref).frame :
                                shared_entry_v2(rgt, ref).full_page.frame;

            rc = get_paged_frame(gfn, &mfn, &pg,
                                 op->flags & GNTMAP_readonly, rd);
            if ( rc != GNTST_okay )
                goto unlock_out_clear;
            act_set_gfn(act, _gfn(gfn));
            act->domid = ld->domain_id;
            act->mfn = mfn;
            act->start = 0;
            act->length = PAGE_SIZE;
            act->is_sub_page = false;
            act->src_domid = rd->domain_id;
            act->trans_gref = ref;
        }
    }

    act->pin += pin_incr;

    mfn = act->mfn;

    cache_flags = (shah->flags & (GTF_PAT | GTF_PWT | GTF_PCD) );

    active_entry_release(act);
    grant_read_unlock(rgt);

    /* pg may be set, with a refcount included, from get_paged_frame(). */
    if ( !pg )
    {
        pg = mfn_valid(mfn) ? mfn_to_page(mfn) : NULL;
        if ( pg )
            owner = page_get_owner_and_reference(pg);
    }
    else
        owner = page_get_owner(pg);

    if ( owner )
        refcnt++;

    if ( !pg || (owner == dom_io) )
    {
        /* Only needed the reference to confirm dom_io ownership. */
        if ( pg )
        {
            put_page(pg);
            refcnt--;
        }

        if ( paging_mode_external(ld) )
        {
            gdprintk(XENLOG_WARNING, "HVM guests can't grant map iomem\n");
            rc = GNTST_general_error;
            goto undo_out;
        }

        if ( !iomem_access_permitted(rd, mfn_x(mfn), mfn_x(mfn)) )
        {
            gdprintk(XENLOG_WARNING,
                     "Iomem mapping not permitted %#"PRI_mfn" (domain %d)\n",
                     mfn_x(mfn), rd->domain_id);
            rc = GNTST_general_error;
            goto undo_out;
        }

        if ( op->flags & GNTMAP_host_map )
        {
            rc = create_grant_host_mapping(op->host_addr, mfn, op->flags,
                                           cache_flags);
            if ( rc != GNTST_okay )
                goto undo_out;

            host_map_created = true;
        }
    }
    else if ( owner == rd || (dom_cow && owner == dom_cow) )
    {
        if ( (op->flags & GNTMAP_device_map) && !(op->flags & GNTMAP_readonly) )
        {
            if ( (owner == dom_cow) ||
                 !get_page_type(pg, PGT_writable_page) )
                goto could_not_pin;
            typecnt++;
        }

        if ( op->flags & GNTMAP_host_map )
        {
            /*
             * Only need to grab another reference if device_map claimed
             * the other one.
             */
            if ( op->flags & GNTMAP_device_map )
            {
                if ( !get_page(pg, rd) )
                    goto could_not_pin;
                refcnt++;
            }

            if ( gnttab_host_mapping_get_page_type(op->flags & GNTMAP_readonly,
                                                   ld, rd) )
            {
                if ( (owner == dom_cow) ||
                     !get_page_type(pg, PGT_writable_page) )
                    goto could_not_pin;
                typecnt++;
            }

            rc = create_grant_host_mapping(op->host_addr, mfn, op->flags, 0);
            if ( rc != GNTST_okay )
                goto undo_out;

            host_map_created = true;
        }
    }
    else
    {
    could_not_pin:
        if ( !rd->is_dying )
            gdprintk(XENLOG_WARNING, "Could not pin grant frame %#"PRI_mfn"\n",
                     mfn_x(mfn));
        rc = GNTST_general_error;
        goto undo_out;
    }

    /*
     * This is deliberately not checking the page's owner: get_paged_frame()
     * explicitly rejects foreign pages, and all success paths above yield
     * either owner == rd or owner == dom_io (the dom_cow case is irrelevant
     * as mem-sharing and IOMMU use are incompatible). The dom_io case would
     * need checking separately if we compared against owner here.
     */
    if ( ld != rd && gnttab_need_iommu_mapping(ld) )
    {
        union maptrack_node node = {
            .cnt.rd = !!(op->flags & GNTMAP_readonly),
            .cnt.wr = !(op->flags & GNTMAP_readonly),
        };
        int err;
        void **slot = NULL;
        unsigned int kind;

        grant_write_lock(lgt);

        err = radix_tree_insert(&lgt->maptrack_tree, mfn_x(mfn),
                                radix_tree_ulong_to_ptr(node.raw));
        if ( err == -EEXIST )
        {
            slot = radix_tree_lookup_slot(&lgt->maptrack_tree, mfn_x(mfn));
            if ( likely(slot) )
            {
                node.raw = radix_tree_ptr_to_ulong(*slot);
                err = -EBUSY;

                /* Update node only when refcount doesn't overflow. */
                if ( op->flags & GNTMAP_readonly ? ++node.cnt.rd
                                                 : ++node.cnt.wr )
                {
                    radix_tree_replace_slot(slot,
                                            radix_tree_ulong_to_ptr(node.raw));
                    err = 0;
                }
            }
            else
                ASSERT_UNREACHABLE();
        }

        /*
         * We're not translated, so we know that dfns and mfns are
         * the same things, so the IOMMU entry is always 1-to-1.
         */
        if ( !(op->flags & GNTMAP_readonly) && node.cnt.wr == 1 )
            kind = IOMMUF_readable | IOMMUF_writable;
        else if ( (op->flags & GNTMAP_readonly) &&
                  node.cnt.rd == 1 && !node.cnt.wr )
            kind = IOMMUF_readable;
        else
            kind = 0;
        if ( err ||
             (kind && iommu_legacy_map(ld, _dfn(mfn_x(mfn)), mfn, 1, kind)) )
        {
            if ( !err )
            {
                if ( slot )
                {
                    op->flags & GNTMAP_readonly ? node.cnt.rd--
                                                : node.cnt.wr--;
                    radix_tree_replace_slot(slot,
                                            radix_tree_ulong_to_ptr(node.raw));
                }
                else
                    radix_tree_delete(&lgt->maptrack_tree, mfn_x(mfn));
            }

            rc = GNTST_general_error;
        }

        grant_write_unlock(lgt);

        if ( rc != GNTST_okay )
            goto undo_out;
    }

    TRACE_TIME(TRC_MEM_PAGE_GRANT_MAP, op->dom);

    /*
     * All maptrack entry users check mt->flags first before using the
     * other fields so just ensure the flags field is stored last.
     */
    mt = &maptrack_entry(lgt, handle);
    mt->domid = op->dom;
    mt->ref   = op->ref;
    smp_wmb();
    write_atomic(&mt->flags, op->flags);

    op->dev_bus_addr = mfn_to_maddr(mfn);
    op->handle       = handle;
    op->status       = GNTST_okay;

    rcu_unlock_domain(rd);
    return;

 undo_out:
    if ( host_map_created )
    {
        replace_grant_host_mapping(op->host_addr, mfn, 0, op->flags);
        gnttab_flush_tlb(ld);
    }

    while ( typecnt-- )
        put_page_type(pg);

    while ( refcnt-- )
        put_page(pg);

    grant_read_lock(rgt);

    act = active_entry_acquire(rgt, op->ref);
    act->pin -= pin_incr;

 unlock_out_clear:
    reduce_status_for_pin(rd, act, status, op->flags & GNTMAP_readonly);

 act_release_out:
    active_entry_release(act);

 unlock_out:
    grant_read_unlock(rgt);
    op->status = rc;
    put_maptrack_handle(lgt, handle);
    rcu_unlock_domain(rd);
}

static long
gnttab_map_grant_ref(
    XEN_GUEST_HANDLE_PARAM(gnttab_map_grant_ref_t) uop, unsigned int count)
{
    int i;
    struct gnttab_map_grant_ref op;

    for ( i = 0; i < count; i++ )
    {
        if ( i && hypercall_preempt_check() )
            return i;

        if ( unlikely(__copy_from_guest_offset(&op, uop, i, 1)) )
            return -EFAULT;

        map_grant_ref(&op);

        if ( unlikely(__copy_to_guest_offset(uop, i, &op, 1)) )
            return -EFAULT;
    }

    return 0;
}

static void
unmap_common(
    struct gnttab_unmap_common *op)
{
    domid_t          dom;
    struct domain   *ld, *rd;
    struct grant_table *lgt, *rgt;
    grant_ref_t ref;
    struct active_grant_entry *act;
    s16              rc = 0;
    struct grant_mapping *map;
    unsigned int flags;
    bool put_handle = false;

    ld = current->domain;
    lgt = ld->grant_table;

    if ( unlikely(op->handle >= lgt->maptrack_limit) )
    {
        gdprintk(XENLOG_INFO, "Bad d%d handle %#x\n",
                 lgt->domain->domain_id, op->handle);
        op->status = GNTST_bad_handle;
        return;
    }

    smp_rmb();
    map = &maptrack_entry(lgt, op->handle);

    if ( unlikely(!read_atomic(&map->flags)) )
    {
        gdprintk(XENLOG_INFO, "Zero flags for d%d handle %#x\n",
                 lgt->domain->domain_id, op->handle);
        op->status = GNTST_bad_handle;
        return;
    }

    dom = map->domid;
    if ( unlikely((rd = rcu_lock_domain_by_id(dom)) == NULL) )
    {
        /* This can happen when a grant is implicitly unmapped. */
        gdprintk(XENLOG_INFO, "Could not find domain %d\n", dom);
        domain_crash(ld); /* naughty... */
        return;
    }

    rc = xsm_grant_unmapref(XSM_HOOK, ld, rd);
    if ( rc )
    {
        rcu_unlock_domain(rd);
        op->status = GNTST_permission_denied;
        return;
    }

    TRACE_TIME(TRC_MEM_PAGE_GRANT_UNMAP, dom);

    rgt = rd->grant_table;

    grant_read_lock(rgt);

    op->rd = rd;
    op->ref = map->ref;
    ref = map->ref;

    /*
     * We can't assume there was no racing unmap for this maptrack entry,
     * and hence we can't assume map->ref is valid for rd. While the checks
     * below (with the active entry lock held) will reject any such racing
     * requests, we still need to make sure we don't attempt to acquire an
     * invalid lock.
     */
    smp_rmb();
    if ( unlikely(ref >= nr_grant_entries(rgt)) )
    {
        gdprintk(XENLOG_WARNING, "Unstable d%d handle %#x\n",
                 rgt->domain->domain_id, op->handle);
        rc = GNTST_bad_handle;
        flags = 0;
        goto unlock_out;
    }

    /* Make sure the above bound check cannot be bypassed speculatively */
    block_speculation();

    act = active_entry_acquire(rgt, ref);

    /*
     * Note that we (ab)use the active entry lock here to protect against
     * multiple unmaps of the same mapping here. We don't want to hold lgt's
     * lock, and we only hold rgt's lock for reading (but the latter wouldn't
     * be the right one anyway). Hence the easiest is to rely on a lock we
     * hold anyway; see docs/misc/grant-tables.txt's "Locking" section.
     */

    flags = read_atomic(&map->flags);
    smp_rmb();
    if ( unlikely(!flags) || unlikely(map->domid != dom) ||
         unlikely(map->ref != ref) )
    {
        gdprintk(XENLOG_WARNING, "Unstable handle %#x\n", op->handle);
        rc = GNTST_bad_handle;
        goto act_release_out;
    }

    op->mfn = act->mfn;

    if ( op->dev_bus_addr && (flags & GNTMAP_device_map) &&
         unlikely(op->dev_bus_addr != mfn_to_maddr(act->mfn)) )
    {
        gdprintk(XENLOG_WARNING,
                 "Bus address doesn't match gntref (%"PRIx64" != %"PRIpaddr")\n",
                 op->dev_bus_addr, mfn_to_maddr(act->mfn));
        rc = GNTST_bad_dev_addr;
        goto act_release_out;
    }

    if ( op->host_addr && (flags & GNTMAP_host_map) )
    {
        if ( (rc = replace_grant_host_mapping(op->host_addr,
                                              op->mfn, op->new_addr,
                                              flags)) < 0 )
            goto act_release_out;

        map->flags &= ~GNTMAP_host_map;
        op->done |= GNTMAP_host_map | (flags & GNTMAP_readonly);
    }

    if ( op->dev_bus_addr && (flags & GNTMAP_device_map) )
    {
        map->flags &= ~GNTMAP_device_map;
        op->done |= GNTMAP_device_map | (flags & GNTMAP_readonly);
    }

    if ( !(map->flags & (GNTMAP_device_map|GNTMAP_host_map)) )
    {
        map->flags = 0;
        put_handle = true;
    }

 act_release_out:
    active_entry_release(act);
 unlock_out:
    grant_read_unlock(rgt);

    if ( put_handle )
        put_maptrack_handle(lgt, op->handle);

    /*
     * map_grant_ref() will only increment the refcount (and update the
     * IOMMU) once per mapping. So we only want to decrement it once the
     * maptrack handle has been put, alongside the further IOMMU update.
     *
     * For the second and third check, see the respective comment in
     * map_grant_ref().
     */
    if ( put_handle && ld != rd && gnttab_need_iommu_mapping(ld) )
    {
        void **slot;
        union maptrack_node node;
        int err = 0;

        grant_write_lock(lgt);
        slot = radix_tree_lookup_slot(&lgt->maptrack_tree, mfn_x(op->mfn));
        node.raw = likely(slot) ? radix_tree_ptr_to_ulong(*slot) : 0;

        /* Refcount must not underflow. */
        if ( !(flags & GNTMAP_readonly ? node.cnt.rd--
                                       : node.cnt.wr--) )
            BUG();

        if ( !node.raw )
            err = iommu_legacy_unmap(ld, _dfn(mfn_x(op->mfn)), 1);
        else if ( !(flags & GNTMAP_readonly) && !node.cnt.wr )
            err = iommu_legacy_map(ld, _dfn(mfn_x(op->mfn)), op->mfn, 1,
                                   IOMMUF_readable);

        if ( err )
            ;
        else if ( !node.raw )
            radix_tree_delete(&lgt->maptrack_tree, mfn_x(op->mfn));
        else
            radix_tree_replace_slot(slot,
                                    radix_tree_ulong_to_ptr(node.raw));

        grant_write_unlock(lgt);

        if ( err )
            rc = GNTST_general_error;
    }

    /* If just unmapped a writable mapping, mark as dirtied */
    if ( rc == GNTST_okay && !(flags & GNTMAP_readonly) )
         gnttab_mark_dirty(rd, op->mfn);

    op->status = rc;
    rcu_unlock_domain(rd);
}

static void
unmap_common_complete(struct gnttab_unmap_common *op)
{
    struct domain *ld, *rd = op->rd;
    struct grant_table *rgt;
    struct active_grant_entry *act;
    grant_entry_header_t *sha;
    struct page_info *pg;
    uint16_t *status;

    if ( evaluate_nospec(!op->done) )
    {
        /* unmap_common() didn't do anything - nothing to complete. */
        return;
    }

    ld = current->domain;

    rcu_lock_domain(rd);
    rgt = rd->grant_table;

    grant_read_lock(rgt);

    act = active_entry_acquire(rgt, op->ref);
    sha = shared_entry_header(rgt, op->ref);

    if ( evaluate_nospec(rgt->gt_version == 1) )
        status = &sha->flags;
    else
        status = &status_entry(rgt, op->ref);

    pg = !is_iomem_page(act->mfn) ? mfn_to_page(op->mfn) : NULL;

    if ( op->done & GNTMAP_device_map )
    {
        if ( pg )
        {
            if ( op->done & GNTMAP_readonly )
                put_page(pg);
            else
                put_page_and_type(pg);
        }

        ASSERT(act->pin & (GNTPIN_devw_mask | GNTPIN_devr_mask));
        if ( op->done & GNTMAP_readonly )
            act->pin -= GNTPIN_devr_inc;
        else
            act->pin -= GNTPIN_devw_inc;
    }

    if ( op->done & GNTMAP_host_map )
    {
        if ( pg )
        {
            if ( gnttab_host_mapping_get_page_type(op->done & GNTMAP_readonly,
                                                   ld, rd) )
                put_page_type(pg);
            put_page(pg);
        }

        ASSERT(act->pin & (GNTPIN_hstw_mask | GNTPIN_hstr_mask));
        if ( op->done & GNTMAP_readonly )
            act->pin -= GNTPIN_hstr_inc;
        else
            act->pin -= GNTPIN_hstw_inc;
    }

    reduce_status_for_pin(rd, act, status, op->done & GNTMAP_readonly);

    active_entry_release(act);
    grant_read_unlock(rgt);

    rcu_unlock_domain(rd);
}

static void
unmap_grant_ref(
    struct gnttab_unmap_grant_ref *op,
    struct gnttab_unmap_common *common)
{
    common->host_addr = op->host_addr;
    common->dev_bus_addr = op->dev_bus_addr;
    common->handle = op->handle;

    /* Intialise these in case common contains old state */
    common->done = 0;
    common->new_addr = 0;
    common->rd = NULL;
    common->mfn = INVALID_MFN;

    unmap_common(common);
    op->status = common->status;
}


static long
gnttab_unmap_grant_ref(
    XEN_GUEST_HANDLE_PARAM(gnttab_unmap_grant_ref_t) uop, unsigned int count)
{
    int i, c, partial_done, done = 0;
    struct gnttab_unmap_grant_ref op;
    struct gnttab_unmap_common common[GNTTAB_UNMAP_BATCH_SIZE];

    while ( count != 0 )
    {
        c = min(count, (unsigned int)GNTTAB_UNMAP_BATCH_SIZE);
        partial_done = 0;

        for ( i = 0; i < c; i++ )
        {
            if ( unlikely(__copy_from_guest(&op, uop, 1)) )
                goto fault;
            unmap_grant_ref(&op, &common[i]);
            ++partial_done;
            if ( unlikely(__copy_field_to_guest(uop, &op, status)) )
                goto fault;
            guest_handle_add_offset(uop, 1);
        }

        gnttab_flush_tlb(current->domain);

        for ( i = 0; i < partial_done; i++ )
            unmap_common_complete(&common[i]);

        count -= c;
        done += c;

        if ( count && hypercall_preempt_check() )
            return done;
    }

    return 0;

fault:
    gnttab_flush_tlb(current->domain);

    for ( i = 0; i < partial_done; i++ )
        unmap_common_complete(&common[i]);
    return -EFAULT;
}

static void
unmap_and_replace(
    struct gnttab_unmap_and_replace *op,
    struct gnttab_unmap_common *common)
{
    common->host_addr = op->host_addr;
    common->new_addr = op->new_addr;
    common->handle = op->handle;

    /* Intialise these in case common contains old state */
    common->done = 0;
    common->dev_bus_addr = 0;
    common->rd = NULL;
    common->mfn = INVALID_MFN;

    unmap_common(common);
    op->status = common->status;
}

static long
gnttab_unmap_and_replace(
    XEN_GUEST_HANDLE_PARAM(gnttab_unmap_and_replace_t) uop, unsigned int count)
{
    int i, c, partial_done, done = 0;
    struct gnttab_unmap_and_replace op;
    struct gnttab_unmap_common common[GNTTAB_UNMAP_BATCH_SIZE];

    while ( count != 0 )
    {
        c = min(count, (unsigned int)GNTTAB_UNMAP_BATCH_SIZE);
        partial_done = 0;

        for ( i = 0; i < c; i++ )
        {
            if ( unlikely(__copy_from_guest(&op, uop, 1)) )
                goto fault;
            unmap_and_replace(&op, &common[i]);
            ++partial_done;
            if ( unlikely(__copy_field_to_guest(uop, &op, status)) )
                goto fault;
            guest_handle_add_offset(uop, 1);
        }

        gnttab_flush_tlb(current->domain);

        for ( i = 0; i < partial_done; i++ )
            unmap_common_complete(&common[i]);

        count -= c;
        done += c;

        if ( count && hypercall_preempt_check() )
            return done;
    }

    return 0;

fault:
    gnttab_flush_tlb(current->domain);

    for ( i = 0; i < partial_done; i++ )
        unmap_common_complete(&common[i]);
    return -EFAULT;
}

static int
gnttab_populate_status_frames(struct domain *d, struct grant_table *gt,
                              unsigned int req_nr_frames)
{
    unsigned int i;
    unsigned int req_status_frames;

    req_status_frames = grant_to_status_frames(req_nr_frames);

    /* Make sure, prior version checks are architectural visible */
    block_speculation();

    for ( i = nr_status_frames(gt); i < req_status_frames; i++ )
    {
        if ( (gt->status[i] = alloc_xenheap_page()) == NULL )
            goto status_alloc_failed;
        clear_page(gt->status[i]);
    }
    /* Share the new status frames with the recipient domain */
    for ( i = nr_status_frames(gt); i < req_status_frames; i++ )
        share_xen_page_with_guest(virt_to_page(gt->status[i]), d, SHARE_rw);

    gt->nr_status_frames = req_status_frames;

    return 0;

status_alloc_failed:
    for ( i = nr_status_frames(gt); i < req_status_frames; i++ )
    {
        free_xenheap_page(gt->status[i]);
        gt->status[i] = NULL;
    }
    return -ENOMEM;
}

static int
gnttab_unpopulate_status_frames(struct domain *d, struct grant_table *gt)
{
    unsigned int i;

    /* Make sure, prior version checks are architectural visible */
    block_speculation();

    for ( i = 0; i < nr_status_frames(gt); i++ )
    {
        struct page_info *pg = virt_to_page(gt->status[i]);
        gfn_t gfn = gnttab_get_frame_gfn(gt, true, i);

        /*
         * For translated domains, recovering from failure after partial
         * changes were made is more complicated than it seems worth
         * implementing at this time. Hence respective error paths below
         * crash the domain in such a case.
         */
        if ( paging_mode_translate(d) )
        {
            int rc = gfn_eq(gfn, INVALID_GFN)
                     ? 0
                     : gnttab_set_frame_gfn(gt, true, i, INVALID_GFN,
                                            page_to_mfn(pg));

            if ( rc )
            {
                gprintk(XENLOG_ERR,
                        "Could not remove status frame %u (GFN %#lx) from P2M\n",
                        i, gfn_x(gfn));
                domain_crash(d);
                return rc;
            }
        }

        BUG_ON(page_get_owner(pg) != d);
        put_page_alloc_ref(pg);

        if ( pg->count_info & ~PGC_xen_heap )
        {
            if ( paging_mode_translate(d) )
            {
                gprintk(XENLOG_ERR,
                        "Wrong page state %#lx of status frame %u (GFN %#lx)\n",
                        pg->count_info, i, gfn_x(gfn));
                domain_crash(d);
            }
            else
            {
                if ( get_page(pg, d) )
                    set_bit(_PGC_allocated, &pg->count_info);
                while ( i-- )
                    share_xen_page_with_guest(virt_to_page(gt->status[i]),
                                              d, SHARE_rw);
            }
            return -EBUSY;
        }

        page_set_owner(pg, NULL);
    }

    for ( i = 0; i < nr_status_frames(gt); i++ )
    {
        free_xenheap_page(gt->status[i]);
        gt->status[i] = NULL;
    }
    gt->nr_status_frames = 0;

    return 0;
}

/*
 * Grow the grant table. The caller must hold the grant table's
 * write lock before calling this function.
 */
static int
gnttab_grow_table(struct domain *d, unsigned int req_nr_frames)
{
    struct grant_table *gt = d->grant_table;
    unsigned int i, j;

    if ( req_nr_frames < INITIAL_NR_GRANT_FRAMES )
        req_nr_frames = INITIAL_NR_GRANT_FRAMES;
    ASSERT(req_nr_frames <= gt->max_grant_frames);

    if ( req_nr_frames > INITIAL_NR_GRANT_FRAMES )
        gdprintk(XENLOG_INFO,
                 "Expanding d%d grant table from %u to %u frames\n",
                 d->domain_id, nr_grant_frames(gt), req_nr_frames);

    /* Active */
    for ( i = nr_active_grant_frames(gt);
          i < num_act_frames_from_sha_frames(req_nr_frames); i++ )
    {
        if ( (gt->active[i] = alloc_xenheap_page()) == NULL )
            goto active_alloc_failed;
        clear_page(gt->active[i]);
        for ( j = 0; j < ACGNT_PER_PAGE; j++ )
            spin_lock_init(&gt->active[i][j].lock);
    }

    /* Shared */
    for ( i = nr_grant_frames(gt); i < req_nr_frames; i++ )
    {
        if ( (gt->shared_raw[i] = alloc_xenheap_page()) == NULL )
            goto shared_alloc_failed;
        clear_page(gt->shared_raw[i]);
    }

    /* Status pages - version 2 */
    if ( evaluate_nospec(gt->gt_version > 1) )
    {
        if ( gnttab_populate_status_frames(d, gt, req_nr_frames) )
            goto shared_alloc_failed;
    }

    /* Share the new shared frames with the recipient domain */
    for ( i = nr_grant_frames(gt); i < req_nr_frames; i++ )
        share_xen_page_with_guest(virt_to_page(gt->shared_raw[i]), d, SHARE_rw);
    gt->nr_grant_frames = req_nr_frames;

    return 0;

shared_alloc_failed:
    for ( i = nr_grant_frames(gt); i < req_nr_frames; i++ )
    {
        free_xenheap_page(gt->shared_raw[i]);
        gt->shared_raw[i] = NULL;
    }
active_alloc_failed:
    for ( i = nr_active_grant_frames(gt);
          i < num_act_frames_from_sha_frames(req_nr_frames); i++ )
    {
        free_xenheap_page(gt->active[i]);
        gt->active[i] = NULL;
    }
    gdprintk(XENLOG_INFO, "Allocation failure when expanding d%d grant table\n",
             d->domain_id);

    return -ENOMEM;
}

int grant_table_init(struct domain *d, int max_grant_frames,
                     int max_maptrack_frames, unsigned int options)
{
    struct grant_table *gt;
    unsigned int max_grant_version = options & XEN_DOMCTL_GRANT_version_mask;
    int ret = -ENOMEM;

    if ( !max_grant_version )
    {
        dprintk(XENLOG_INFO, "%pd: invalid grant table version 0 requested\n",
                d);
        return -EINVAL;
    }
    if ( max_grant_version > opt_gnttab_max_version )
    {
        dprintk(XENLOG_INFO,
                "%pd: requested grant version (%u) greater than supported (%u)\n",
                d, max_grant_version, opt_gnttab_max_version);
        return -EINVAL;
    }

    /* Apply defaults if no value was specified */
    if ( max_grant_frames < 0 )
        max_grant_frames = opt_max_grant_frames;
    if ( max_maptrack_frames < 0 )
        max_maptrack_frames = opt_max_maptrack_frames;

    if ( max_grant_frames < INITIAL_NR_GRANT_FRAMES )
    {
        dprintk(XENLOG_INFO, "Bad grant table size: %u frames\n", max_grant_frames);
        return -EINVAL;
    }

    if ( (gt = xzalloc(struct grant_table)) == NULL )
        return -ENOMEM;

    /* Simple stuff. */
    percpu_rwlock_resource_init(&gt->lock, grant_rwlock);
    spin_lock_init(&gt->maptrack_lock);

    gt->gt_version = 1;
    gt->max_grant_frames = max_grant_frames;
    gt->max_maptrack_frames = max_maptrack_frames;
    gt->max_version = max_grant_version;

    /* Install the structure early to simplify the error path. */
    gt->domain = d;
    d->grant_table = gt;

    /* Active grant table. */
    gt->active = xzalloc_array(struct active_grant_entry *,
                               max_nr_active_grant_frames(gt));
    if ( gt->active == NULL )
        goto out;

    /* Tracking of mapped foreign frames table */
    if ( gt->max_maptrack_frames )
    {
        gt->maptrack = vzalloc(gt->max_maptrack_frames * sizeof(*gt->maptrack));
        if ( gt->maptrack == NULL )
            goto out;

        radix_tree_init(&gt->maptrack_tree);
    }

    /* Shared grant table. */
    gt->shared_raw = xzalloc_array(void *, gt->max_grant_frames);
    if ( gt->shared_raw == NULL )
        goto out;

    /* Status pages for grant table - for version 2 */
    gt->status = xzalloc_array(grant_status_t *,
                               grant_to_status_frames(gt->max_grant_frames));
    if ( gt->status == NULL )
        goto out;

    grant_write_lock(gt);

    /* gnttab_grow_table() allocates a min number of frames, so 0 is okay. */
    ret = gnttab_grow_table(d, 0);

    grant_write_unlock(gt);

 out:
    if ( ret )
        grant_table_destroy(d);

    return ret;
}

static long
gnttab_setup_table(
    XEN_GUEST_HANDLE_PARAM(gnttab_setup_table_t) uop, unsigned int count,
    unsigned int limit_max)
{
    struct vcpu *curr = current;
    struct gnttab_setup_table op;
    struct domain *d = NULL;
    struct grant_table *gt;
    unsigned int i;

    if ( count != 1 )
        return -EINVAL;

    if ( unlikely(copy_from_guest(&op, uop, 1)) )
        return -EFAULT;

    if ( !guest_handle_okay(op.frame_list, op.nr_frames) )
        return -EFAULT;

    d = rcu_lock_domain_by_any_id(op.dom);
    if ( d == NULL )
    {
        op.status = GNTST_bad_domain;
        goto out;
    }

    if ( xsm_grant_setup(XSM_TARGET, curr->domain, d) )
    {
        op.status = GNTST_permission_denied;
        goto out;
    }

    gt = d->grant_table;
    grant_write_lock(gt);

    if ( unlikely(op.nr_frames > gt->max_grant_frames) )
    {
        gdprintk(XENLOG_INFO, "d%d is limited to %u grant-table frames\n",
                d->domain_id, gt->max_grant_frames);
        op.status = GNTST_general_error;
        goto unlock;
    }
    if ( unlikely(limit_max < op.nr_frames) )
    {
        gdprintk(XENLOG_WARNING, "nr_frames for d%d is too large (%u,%u)\n",
                 d->domain_id, op.nr_frames, limit_max);
        op.status = GNTST_general_error;
        goto unlock;
    }

    if ( (op.nr_frames > nr_grant_frames(gt) ||
          ((gt->gt_version > 1) &&
           (grant_to_status_frames(op.nr_frames) > nr_status_frames(gt)))) &&
         gnttab_grow_table(d, op.nr_frames) )
    {
        gdprintk(XENLOG_INFO,
                 "Expand grant table of d%d to %u failed. Current: %u Max: %u\n",
                 d->domain_id, op.nr_frames, nr_grant_frames(gt),
                 gt->max_grant_frames);
        op.status = GNTST_general_error;
        goto unlock;
    }

    op.status = GNTST_okay;
    for ( i = 0; i < op.nr_frames; i++ )
    {
        xen_pfn_t gmfn = gfn_x(gnttab_shared_gfn(d, gt, i));

        /* Grant tables cannot be shared */
        BUG_ON(SHARED_M2P(gmfn));

        if ( __copy_to_guest_offset(op.frame_list, i, &gmfn, 1) )
        {
            op.status = GNTST_bad_virt_addr;
            break;
        }
    }

 unlock:
    grant_write_unlock(gt);
 out:
    if ( d )
        rcu_unlock_domain(d);

    if ( unlikely(__copy_field_to_guest(uop, &op, status)) )
        return -EFAULT;

    return 0;
}

static long
gnttab_query_size(
    XEN_GUEST_HANDLE_PARAM(gnttab_query_size_t) uop, unsigned int count)
{
    struct gnttab_query_size op;
    struct domain *d;
    struct grant_table *gt;

    if ( count != 1 )
        return -EINVAL;

    if ( unlikely(copy_from_guest(&op, uop, 1)) )
        return -EFAULT;

    d = rcu_lock_domain_by_any_id(op.dom);
    if ( d == NULL )
    {
        op.status = GNTST_bad_domain;
        goto out;
    }

    if ( xsm_grant_query_size(XSM_TARGET, current->domain, d) )
    {
        op.status = GNTST_permission_denied;
        goto out;
    }

    gt = d->grant_table;

    grant_read_lock(gt);

    op.nr_frames     = nr_grant_frames(gt);
    op.max_nr_frames = gt->max_grant_frames;
    op.status        = GNTST_okay;

    grant_read_unlock(gt);

 out:
    if ( d )
        rcu_unlock_domain(d);

    if ( unlikely(__copy_to_guest(uop, &op, 1)) )
        return -EFAULT;

    return 0;
}

/*
 * Check that the given grant reference (rd,ref) allows 'ld' to transfer
 * ownership of a page frame. If so, lock down the grant entry.
 */
static int
gnttab_prepare_for_transfer(
    struct domain *rd, struct domain *ld, grant_ref_t ref)
{
    struct grant_table *rgt = rd->grant_table;
    uint32_t *raw_shah;
    union grant_combo scombo;
    int                 retries = 0;

    grant_read_lock(rgt);

    if ( unlikely(ref >= nr_grant_entries(rgt)) )
    {
        gdprintk(XENLOG_INFO,
                "Bad grant reference %#x for transfer to d%d\n",
                ref, rd->domain_id);
        goto fail;
    }

    /* This call also ensures the above check cannot be passed speculatively */
    raw_shah = (uint32_t *)shared_entry_header(rgt, ref);
    scombo.raw = ACCESS_ONCE(*raw_shah);

    for ( ; ; )
    {
        union grant_combo prev, new;

        if ( unlikely(scombo.flags != GTF_accept_transfer) ||
             unlikely(scombo.domid != ld->domain_id) )
        {
            gdprintk(XENLOG_INFO,
                     "Bad flags (%x) or dom (%d); expected d%d\n",
                     scombo.flags, scombo.domid, ld->domain_id);
            goto fail;
        }

        new = scombo;
        new.flags |= GTF_transfer_committed;

        prev.raw = guest_cmpxchg(rd, raw_shah, scombo.raw, new.raw);
        if ( likely(prev.raw == scombo.raw) )
            break;

        if ( retries++ == 4 )
        {
            gdprintk(XENLOG_WARNING, "Shared grant entry is unstable\n");
            goto fail;
        }

        scombo = prev;
    }

    grant_read_unlock(rgt);
    return 1;

 fail:
    grant_read_unlock(rgt);
    return 0;
}

static long
gnttab_transfer(
    XEN_GUEST_HANDLE_PARAM(gnttab_transfer_t) uop, unsigned int count)
{
    struct domain *d = current->domain;
    struct domain *e;
    struct page_info *page;
    int i;
    struct gnttab_transfer gop;
    mfn_t mfn;
    unsigned int max_bitsize;
    struct active_grant_entry *act;

    if ( !opt_grant_transfer )
        return -EOPNOTSUPP;

    for ( i = 0; i < count; i++ )
    {
        bool okay;
        int rc;

        if ( i && hypercall_preempt_check() )
            return i;

        /* Read from caller address space. */
        if ( unlikely(__copy_from_guest(&gop, uop, 1)) )
        {
            gdprintk(XENLOG_INFO, "error reading req %d/%u\n",
                    i, count);
            return -EFAULT;
        }

#ifdef CONFIG_X86
        {
            p2m_type_t p2mt;

            mfn = get_gfn_unshare(d, gop.mfn, &p2mt);
            if ( p2m_is_shared(p2mt) || !p2m_is_valid(p2mt) )
                mfn = INVALID_MFN;
        }
#else
        mfn = gfn_to_mfn(d, _gfn(gop.mfn));
#endif

        /* Check the passed page frame for basic validity. */
        if ( unlikely(!mfn_valid(mfn)) )
        {
#ifdef CONFIG_X86
            put_gfn(d, gop.mfn);
#endif
            gdprintk(XENLOG_INFO, "out-of-range %lx\n", (unsigned long)gop.mfn);
            gop.status = GNTST_bad_page;
            goto copyback;
        }

        page = mfn_to_page(mfn);
        if ( (rc = steal_page(d, page, 0)) < 0 )
        {
#ifdef CONFIG_X86
            put_gfn(d, gop.mfn);
#endif
            gop.status = rc == -EINVAL ? GNTST_bad_page : GNTST_general_error;
            goto copyback;
        }

        rc = guest_physmap_remove_page(d, _gfn(gop.mfn), mfn, 0);
        gnttab_flush_tlb(d);
        if ( rc )
        {
            gdprintk(XENLOG_INFO,
                     "can't remove GFN %"PRI_xen_pfn" (MFN %#"PRI_mfn")\n",
                     gop.mfn, mfn_x(mfn));
            gop.status = GNTST_general_error;
            goto put_gfn_and_copyback;
        }

        /* Find the target domain. */
        if ( unlikely((e = rcu_lock_domain_by_id(gop.domid)) == NULL) )
        {
            gdprintk(XENLOG_INFO, "can't find d%d\n", gop.domid);
            gop.status = GNTST_bad_domain;
            goto put_gfn_and_copyback;
        }

        if ( xsm_grant_transfer(XSM_HOOK, d, e) )
        {
            gop.status = GNTST_permission_denied;
        unlock_and_copyback:
            rcu_unlock_domain(e);
        put_gfn_and_copyback:
#ifdef CONFIG_X86
            put_gfn(d, gop.mfn);
#endif
            /* The count_info has already been cleaned */
            free_domheap_page(page);
            goto copyback;
        }

        max_bitsize = domain_clamp_alloc_bitsize(
            e, e->grant_table->gt_version > 1 || paging_mode_translate(e)
               ? BITS_PER_LONG + PAGE_SHIFT : 32 + PAGE_SHIFT);
        if ( max_bitsize < BITS_PER_LONG + PAGE_SHIFT &&
             (mfn_x(mfn) >> (max_bitsize - PAGE_SHIFT)) )
        {
            struct page_info *new_page;

            new_page = alloc_domheap_page(e, MEMF_no_owner |
                                             MEMF_bits(max_bitsize));
            if ( new_page == NULL )
            {
                gop.status = GNTST_address_too_big;
                goto unlock_and_copyback;
            }

            copy_domain_page(page_to_mfn(new_page), mfn);

            /* The count_info has already been cleared */
            free_domheap_page(page);
            page = new_page;
            mfn = page_to_mfn(page);
        }

        nrspin_lock(&e->page_alloc_lock);

        /*
         * Check that 'e' will accept the page and has reservation
         * headroom.  Also, a domain mustn't have PGC_allocated
         * pages when it is dying.
         */
        if ( unlikely(e->is_dying) ||
             unlikely(domain_tot_pages(e) >= e->max_pages) ||
             unlikely(!(e->tot_pages + 1)) )
        {
            nrspin_unlock(&e->page_alloc_lock);

            if ( e->is_dying )
                gdprintk(XENLOG_INFO, "Transferee d%d is dying\n",
                         e->domain_id);
            else
                gdprintk(XENLOG_INFO,
                         "Transferee %pd has no headroom (tot %u, max %u, ex %u)\n",
                         e, domain_tot_pages(e), e->max_pages, e->extra_pages);

            gop.status = GNTST_general_error;
            goto unlock_and_copyback;
        }

        /* Okay, add the page to 'e'. */
        if ( unlikely(domain_adjust_tot_pages(e, 1) == 1) )
            get_knownalive_domain(e);

        /*
         * We must drop the lock to avoid a possible deadlock in
         * gnttab_prepare_for_transfer.  We have reserved a page in e so can
         * safely drop the lock and re-aquire it later to add page to the
         * pagelist.
         */
        nrspin_unlock(&e->page_alloc_lock);
        okay = gnttab_prepare_for_transfer(e, d, gop.ref);

        /*
         * Make sure the reference bound check in gnttab_prepare_for_transfer
         * is respected and speculative execution is blocked accordingly
         */
        if ( unlikely(!evaluate_nospec(okay)) ||
            unlikely(assign_pages(page, 1, e, MEMF_no_refcount)) )
        {
            bool drop_dom_ref;

            /*
             * Need to grab this again to safely free our "reserved"
             * page in the page total
             */
            nrspin_lock(&e->page_alloc_lock);
            drop_dom_ref = !domain_adjust_tot_pages(e, -1);
            nrspin_unlock(&e->page_alloc_lock);

            if ( okay /* i.e. e->is_dying due to the surrounding if() */ )
                gdprintk(XENLOG_INFO, "Transferee d%d is now dying\n",
                         e->domain_id);

            if ( drop_dom_ref )
                put_domain(e);
            gop.status = GNTST_general_error;
            goto unlock_and_copyback;
        }

#ifdef CONFIG_X86
        put_gfn(d, gop.mfn);
#endif

        TRACE_TIME(TRC_MEM_PAGE_GRANT_TRANSFER, e->domain_id);

        /* Tell the guest about its new page frame. */
        grant_read_lock(e->grant_table);
        act = active_entry_acquire(e->grant_table, gop.ref);

        if ( evaluate_nospec(e->grant_table->gt_version == 1) )
        {
            grant_entry_v1_t *sha = &shared_entry_v1(e->grant_table, gop.ref);

            rc = guest_physmap_add_page(e, _gfn(sha->frame), mfn, 0);
            if ( !paging_mode_translate(e) )
                sha->frame = mfn_x(mfn);
        }
        else
        {
            grant_entry_v2_t *sha = &shared_entry_v2(e->grant_table, gop.ref);

            rc = guest_physmap_add_page(e, _gfn(sha->full_page.frame), mfn, 0);
            if ( !paging_mode_translate(e) )
                sha->full_page.frame = mfn_x(mfn);
        }
        smp_wmb();
        shared_entry_header(e->grant_table, gop.ref)->flags |=
            GTF_transfer_completed;

        active_entry_release(act);
        grant_read_unlock(e->grant_table);

        rcu_unlock_domain(e);

        gop.status = rc ? GNTST_general_error : GNTST_okay;

    copyback:
        if ( unlikely(__copy_field_to_guest(uop, &gop, status)) )
        {
            gdprintk(XENLOG_INFO, "error writing resp %d/%u\n", i, count);
            return -EFAULT;
        }
        guest_handle_add_offset(uop, 1);
    }

    return 0;
}

/*
 * Undo acquire_grant_for_copy().  This has no effect on page type and
 * reference counts.
 */
static void
release_grant_for_copy(
    struct domain *rd, grant_ref_t gref, bool readonly)
{
    struct grant_table *rgt = rd->grant_table;
    grant_entry_header_t *sha;
    struct active_grant_entry *act;
    mfn_t mfn;
    uint16_t *status;
    grant_ref_t trans_gref;
    struct domain *td;

    grant_read_lock(rgt);

    act = active_entry_acquire(rgt, gref);
    sha = shared_entry_header(rgt, gref);
    mfn = act->mfn;

    if ( evaluate_nospec(rgt->gt_version == 1) )
    {
        status = &sha->flags;
        td = rd;
        trans_gref = gref;
    }
    else
    {
        status = &status_entry(rgt, gref);
        td = (act->src_domid == rd->domain_id)
             ? rd : knownalive_domain_from_domid(act->src_domid);
        trans_gref = act->trans_gref;
    }

    if ( readonly )
    {
        act->pin -= GNTPIN_hstr_inc;
    }
    else
    {
        gnttab_mark_dirty(rd, mfn);

        act->pin -= GNTPIN_hstw_inc;
    }

    reduce_status_for_pin(rd, act, status, readonly);

    active_entry_release(act);
    grant_read_unlock(rgt);

    if ( td != rd )
    {
        /*
         * Recursive call, but it is bounded (acquire permits only a single
         * level of transitivity), so it's okay.
         */
        release_grant_for_copy(td, trans_gref, readonly);

        rcu_unlock_domain(td);
    }
}

/*
 * Grab a machine frame number from a grant entry and update the flags
 * and pin count as appropriate. If rc == GNTST_okay, note that this *does*
 * take one ref count on the target page, stored in *page.
 * If there is any error, *page = NULL, no ref taken.
 */
static int
acquire_grant_for_copy(
    struct domain *rd, grant_ref_t gref, domid_t ldom, bool readonly,
    mfn_t *mfn, struct page_info **page, uint16_t *page_off,
    uint16_t *length, bool allow_transitive)
{
    struct grant_table *rgt = rd->grant_table;
    grant_entry_v2_t *sha2;
    grant_entry_header_t *shah;
    struct active_grant_entry *act;
    grant_status_t *status;
    uint32_t old_pin;
    domid_t trans_domid;
    grant_ref_t trans_gref;
    struct domain *td;
    mfn_t grant_mfn;
    uint16_t trans_page_off;
    uint16_t trans_length;
    bool is_sub_page;
    s16 rc = GNTST_okay;
    unsigned int pin_incr = readonly ? GNTPIN_hstr_inc : GNTPIN_hstw_inc;

    *page = NULL;

    grant_read_lock(rgt);

    if ( unlikely(gref >= nr_grant_entries(rgt)) )
    {
        gdprintk(XENLOG_WARNING, "Bad grant reference %#x\n", gref);
        rc = GNTST_bad_gntref;
        goto gt_unlock_out;
    }

    /* This call also ensures the above check cannot be passed speculatively */
    shah = shared_entry_header(rgt, gref);
    act = active_entry_acquire(rgt, gref);

    /* If already pinned, check the active domid and avoid refcnt overflow. */
    if ( act->pin &&
         ((act->domid != ldom) ||
          (act->pin & GNTPIN_incr2oflow_mask(pin_incr))) )
    {
        gdprintk(XENLOG_WARNING,
                 "Bad domain (%d != %d), or risk of counter overflow %08x\n",
                 act->domid, ldom, act->pin);
        rc = GNTST_general_error;
        goto unlock_out;
    }

    if ( evaluate_nospec(rgt->gt_version == 1) )
    {
        sha2 = NULL;
        status = &shah->flags;
    }
    else
    {
        sha2 = &shared_entry_v2(rgt, gref);
        status = &status_entry(rgt, gref);
    }

    old_pin = act->pin;
    if ( sha2 && (shah->flags & GTF_type_mask) == GTF_transitive )
    {
        if ( (!old_pin || (!readonly &&
                           !(old_pin & (GNTPIN_devw_mask|GNTPIN_hstw_mask)))) &&
             (rc = _set_status_v2(shah, status, rd, act, readonly, 0,
                                  ldom)) != GNTST_okay )
            goto unlock_out;

        if ( !allow_transitive )
        {
            gdprintk(XENLOG_WARNING,
                     "transitive grant when transitivity not allowed\n");
            rc = GNTST_general_error;
            goto unlock_out_clear;
        }

        trans_domid = sha2->transitive.trans_domid;
        trans_gref = sha2->transitive.gref;
        barrier(); /* Stop the compiler from re-loading
                      trans_domid from shared memory */
        if ( trans_domid == rd->domain_id )
        {
            gdprintk(XENLOG_WARNING,
                     "transitive grants cannot be self-referential\n");
            rc = GNTST_general_error;
            goto unlock_out_clear;
        }

        /*
         * We allow the trans_domid == ldom case, which corresponds to a
         * grant being issued by one domain, sent to another one, and then
         * transitively granted back to the original domain.  Allowing it
         * is easy, and means that you don't need to go out of your way to
         * avoid it in the guest.
         */

        /* We need to leave the rrd locked during the grant copy. */
        td = rcu_lock_domain_by_id(trans_domid);
        if ( td == NULL )
        {
            gdprintk(XENLOG_WARNING,
                     "transitive grant referenced bad domain %d\n",
                     trans_domid);
            rc = GNTST_general_error;
            goto unlock_out_clear;
        }

        /*
         * acquire_grant_for_copy() will take the lock on the remote table,
         * so we have to drop the lock here and reacquire.
         */
        active_entry_release(act);
        grant_read_unlock(rgt);

        rc = acquire_grant_for_copy(td, trans_gref, rd->domain_id,
                                    readonly, &grant_mfn, page,
                                    &trans_page_off, &trans_length,
                                    false);

        grant_read_lock(rgt);
        act = active_entry_acquire(rgt, gref);

        if ( rc != GNTST_okay )
        {
            rcu_unlock_domain(td);
            reduce_status_for_pin(rd, act, status, readonly);
            active_entry_release(act);
            grant_read_unlock(rgt);
            return rc;
        }

        /*
         * We dropped the lock, so we have to check that the grant didn't
         * change, and that nobody else tried to pin/unpin it. If anything
         * changed, just give up and tell the caller to retry.
         */
        if ( rgt->gt_version != 2 ||
             act->pin != old_pin ||
             (old_pin && (act->domid != ldom ||
                          !mfn_eq(act->mfn, grant_mfn) ||
                          act->start != trans_page_off ||
                          act->length != trans_length ||
                          act->src_domid != td->domain_id ||
                          act->trans_gref != trans_gref ||
                          !act->is_sub_page)) )
        {
            /*
             * Like above for acquire_grant_for_copy() we need to drop and then
             * re-acquire the locks here to prevent lock order inversion issues.
             * Unlike for acquire_grant_for_copy() we don't need to re-check
             * anything, as release_grant_for_copy() doesn't depend on the grant
             * table entry: It only updates internal state and the status flags.
             */
            active_entry_release(act);
            grant_read_unlock(rgt);

            release_grant_for_copy(td, trans_gref, readonly);
            rcu_unlock_domain(td);

            grant_read_lock(rgt);
            act = active_entry_acquire(rgt, gref);
            reduce_status_for_pin(rd, act, status, readonly);
            active_entry_release(act);
            grant_read_unlock(rgt);

            put_page(*page);
            *page = NULL;
            return ERESTART;
        }

        if ( !old_pin )
        {
            act->domid = ldom;
            act->start = trans_page_off;
            act->length = trans_length;
            act->src_domid = td->domain_id;
            act->trans_gref = trans_gref;
            act->mfn = grant_mfn;
            act_set_gfn(act, INVALID_GFN);
            /*
             * The actual remote remote grant may or may not be a sub-page,
             * but we always treat it as one because that blocks mappings of
             * transitive grants.
             */
            act->is_sub_page = true;
        }
    }
    else if ( !old_pin ||
              (!readonly && !(old_pin & (GNTPIN_devw_mask|GNTPIN_hstw_mask))) )
    {
        if ( (rc = _set_status(shah, status, rd, rgt->gt_version, act,
                               readonly, 0, ldom)) != GNTST_okay )
             goto unlock_out;

        td = rd;
        trans_gref = gref;
        if ( !sha2 )
        {
            unsigned long gfn = shared_entry_v1(rgt, gref).frame;

            rc = get_paged_frame(gfn, &grant_mfn, page, readonly, rd);
            if ( rc != GNTST_okay )
                goto unlock_out_clear;
            act_set_gfn(act, _gfn(gfn));
            is_sub_page = false;
            trans_page_off = 0;
            trans_length = PAGE_SIZE;
        }
        else if ( !(sha2->hdr.flags & GTF_sub_page) )
        {
            rc = get_paged_frame(sha2->full_page.frame, &grant_mfn, page,
                                 readonly, rd);
            if ( rc != GNTST_okay )
                goto unlock_out_clear;
            act_set_gfn(act, _gfn(sha2->full_page.frame));
            is_sub_page = false;
            trans_page_off = 0;
            trans_length = PAGE_SIZE;
        }
        else
        {
            rc = get_paged_frame(sha2->sub_page.frame, &grant_mfn, page,
                                 readonly, rd);
            if ( rc != GNTST_okay )
                goto unlock_out_clear;
            act_set_gfn(act, _gfn(sha2->sub_page.frame));
            is_sub_page = true;
            trans_page_off = sha2->sub_page.page_off;
            trans_length = sha2->sub_page.length;
        }

        if ( !act->pin )
        {
            act->domid = ldom;
            act->is_sub_page = is_sub_page;
            act->start = trans_page_off;
            act->length = trans_length;
            act->src_domid = td->domain_id;
            act->trans_gref = trans_gref;
            act->mfn = grant_mfn;
        }
    }
    else
    {
        ASSERT(mfn_valid(act->mfn));
        *page = mfn_to_page(act->mfn);
        td = page_get_owner_and_reference(*page);
        /*
         * act->pin being non-zero should guarantee the page to have a
         * non-zero refcount and hence a valid owner (matching the one on
         * record), with one exception: If the owning domain is dying we
         * had better not make implications from pin count (map_grant_ref()
         * updates pin counts before obtaining page references, for
         * example).
         */
        if ( td != rd || rd->is_dying )
        {
            if ( td )
                put_page(*page);
            *page = NULL;
            rc = GNTST_bad_domain;
            goto unlock_out_clear;
        }
    }

    act->pin += pin_incr;

    *page_off = act->start;
    *length = act->length;
    *mfn = act->mfn;

    active_entry_release(act);
    grant_read_unlock(rgt);
    return rc;

 unlock_out_clear:
    reduce_status_for_pin(rd, act, status, readonly);

 unlock_out:
    active_entry_release(act);

 gt_unlock_out:
    grant_read_unlock(rgt);

    return rc;
}

struct gnttab_copy_buf {
    /* Guest provided. */
    struct gnttab_copy_ptr ptr;
    uint16_t len;

    /* Mapped etc. */
    struct domain *domain;
    mfn_t mfn;
    struct page_info *page;
    void *virt;
    bool read_only;
    bool have_grant;
    bool have_type;
};

static int gnttab_copy_lock_domain(domid_t domid, bool is_gref,
                                   struct gnttab_copy_buf *buf)
{
    /* Only DOMID_SELF may reference via frame. */
    if ( domid != DOMID_SELF && !is_gref )
        return GNTST_permission_denied;

    buf->domain = rcu_lock_domain_by_any_id(domid);

    if ( !buf->domain )
        return GNTST_bad_domain;

    buf->ptr.domid = domid;

    return GNTST_okay;
}

static void gnttab_copy_unlock_domains(struct gnttab_copy_buf *src,
                                       struct gnttab_copy_buf *dest)
{
    if ( src->domain )
    {
        rcu_unlock_domain(src->domain);
        src->domain = NULL;
    }
    if ( dest->domain )
    {
        rcu_unlock_domain(dest->domain);
        dest->domain = NULL;
    }
}

static int gnttab_copy_lock_domains(const struct gnttab_copy *op,
                                    struct gnttab_copy_buf *src,
                                    struct gnttab_copy_buf *dest)
{
    int rc;

    rc = gnttab_copy_lock_domain(op->source.domid,
                                 op->flags & GNTCOPY_source_gref, src);
    if ( rc < 0 )
        goto error;
    rc = gnttab_copy_lock_domain(op->dest.domid,
                                 op->flags & GNTCOPY_dest_gref, dest);
    if ( rc < 0 )
        goto error;

    rc = xsm_grant_copy(XSM_HOOK, src->domain, dest->domain);
    if ( rc < 0 )
    {
        rc = GNTST_permission_denied;
        goto error;
    }
    return 0;

 error:
    gnttab_copy_unlock_domains(src, dest);
    return rc;
}

static void gnttab_copy_release_buf(struct gnttab_copy_buf *buf)
{
    if ( buf->virt )
    {
        unmap_domain_page(buf->virt);
        buf->virt = NULL;
    }
    if ( buf->have_grant )
    {
        release_grant_for_copy(buf->domain, buf->ptr.u.ref, buf->read_only);
        buf->have_grant = 0;
    }
    if ( buf->have_type )
    {
        put_page_type(buf->page);
        buf->have_type = 0;
    }
    if ( buf->page )
    {
        put_page(buf->page);
        buf->page = NULL;
    }
}

static int gnttab_copy_claim_buf(const struct gnttab_copy *op,
                                 const struct gnttab_copy_ptr *ptr,
                                 struct gnttab_copy_buf *buf,
                                 unsigned int gref_flag)
{
    int rc;

    buf->read_only = gref_flag == GNTCOPY_source_gref;

    if ( op->flags & gref_flag )
    {
        rc = acquire_grant_for_copy(buf->domain, ptr->u.ref,
                                    current->domain->domain_id,
                                    buf->read_only,
                                    &buf->mfn, &buf->page,
                                    &buf->ptr.offset, &buf->len,
                                    opt_transitive_grants);
        if ( rc != GNTST_okay )
            goto out;
        buf->ptr.u.ref = ptr->u.ref;
        buf->have_grant = 1;
    }
    else
    {
        rc = get_paged_frame(ptr->u.gmfn, &buf->mfn, &buf->page,
                             buf->read_only, buf->domain);
        if ( rc != GNTST_okay )
        {
            gdprintk(XENLOG_WARNING,
                     "source frame %"PRI_xen_pfn" invalid\n", ptr->u.gmfn);
            goto out;
        }

        buf->ptr.u.gmfn = ptr->u.gmfn;
        buf->ptr.offset = 0;
        buf->len = PAGE_SIZE;
    }

    if ( !buf->read_only )
    {
        if ( !get_page_type(buf->page, PGT_writable_page) )
        {
            if ( !buf->domain->is_dying )
                gdprintk(XENLOG_WARNING,
                         "Could not get writable frame %#"PRI_mfn"\n",
                         mfn_x(buf->mfn));
            rc = GNTST_general_error;
            goto out;
        }
        buf->have_type = 1;
    }

    buf->virt = map_domain_page(buf->mfn);
    rc = GNTST_okay;

 out:
    return rc;
}

static bool gnttab_copy_buf_valid(
    const struct gnttab_copy_ptr *p, const struct gnttab_copy_buf *b,
    bool has_gref)
{
    if ( !b->virt )
        return 0;
    if ( has_gref )
        return b->have_grant && p->u.ref == b->ptr.u.ref;
    return p->u.gmfn == b->ptr.u.gmfn;
}

static int gnttab_copy_buf(const struct gnttab_copy *op,
                           struct gnttab_copy_buf *dest,
                           const struct gnttab_copy_buf *src)
{
    if ( ((op->source.offset + op->len) > PAGE_SIZE) ||
         ((op->dest.offset + op->len) > PAGE_SIZE) )
    {
        gdprintk(XENLOG_WARNING, "copy beyond page area\n");
        return GNTST_bad_copy_arg;
    }

    if ( op->source.offset < src->ptr.offset ||
         op->source.offset + op->len > src->ptr.offset + src->len )
    {
        gdprintk(XENLOG_WARNING,
                 "copy source out of bounds: %d < %d || %d > %d\n",
                 op->source.offset, src->ptr.offset, op->len, src->len);
        return GNTST_general_error;
    }

    if ( op->dest.offset < dest->ptr.offset ||
         op->dest.offset + op->len > dest->ptr.offset + dest->len )
    {
        gdprintk(XENLOG_WARNING, "copy dest out of bounds: %d < %d || %d > %d\n",
                 op->dest.offset, dest->ptr.offset, op->len, dest->len);
        return GNTST_general_error;
    }

    /* Make sure the above checks are not bypassed speculatively */
    block_speculation();

    memcpy(dest->virt + op->dest.offset, src->virt + op->source.offset,
           op->len);
    gnttab_mark_dirty(dest->domain, dest->mfn);

    return GNTST_okay;
}

static int gnttab_copy_one(const struct gnttab_copy *op,
                           struct gnttab_copy_buf *dest,
                           struct gnttab_copy_buf *src)
{
    int rc;

    if ( unlikely(!op->len) )
        return GNTST_okay;

    if ( !src->domain || op->source.domid != src->ptr.domid ||
         !dest->domain || op->dest.domid != dest->ptr.domid )
    {
        gnttab_copy_release_buf(src);
        gnttab_copy_release_buf(dest);
        gnttab_copy_unlock_domains(src, dest);

        rc = gnttab_copy_lock_domains(op, src, dest);
        if ( rc < 0 )
            goto out;
    }

    /* Different source? */
    if ( !gnttab_copy_buf_valid(&op->source, src,
                                op->flags & GNTCOPY_source_gref) )
    {
        gnttab_copy_release_buf(src);
        rc = gnttab_copy_claim_buf(op, &op->source, src, GNTCOPY_source_gref);
        if ( rc )
            goto out;
    }

    /* Different dest? */
    if ( !gnttab_copy_buf_valid(&op->dest, dest,
                                op->flags & GNTCOPY_dest_gref) )
    {
        gnttab_copy_release_buf(dest);
        rc = gnttab_copy_claim_buf(op, &op->dest, dest, GNTCOPY_dest_gref);
        if ( rc )
            goto out;
    }

    rc = gnttab_copy_buf(op, dest, src);
 out:
    return rc;
}

/*
 * gnttab_copy(), other than the various other helpers of
 * do_grant_table_op(), returns (besides possible error indicators)
 * "count - i" rather than "i" to ensure that even if no progress
 * was made at all (perhaps due to gnttab_copy_one() returning a
 * positive value) a non-zero value is being handed back (zero needs
 * to be avoided, as that means "success, all done").
 */
static long gnttab_copy(
    XEN_GUEST_HANDLE_PARAM(gnttab_copy_t) uop, unsigned int count)
{
    unsigned int i;
    struct gnttab_copy op;
    struct gnttab_copy_buf src = {};
    struct gnttab_copy_buf dest = {};
    long rc = 0;

    for ( i = 0; i < count; i++ )
    {
        if ( i && hypercall_preempt_check() )
        {
            rc = count - i;
            break;
        }

        if ( unlikely(__copy_from_guest(&op, uop, 1)) )
        {
            rc = -EFAULT;
            break;
        }

        rc = gnttab_copy_one(&op, &dest, &src);
        if ( rc > 0 )
        {
            rc = count - i;
            break;
        }
        if ( rc != GNTST_okay )
        {
            gnttab_copy_release_buf(&src);
            gnttab_copy_release_buf(&dest);
        }

        op.status = rc;
        rc = 0;
        if ( unlikely(__copy_field_to_guest(uop, &op, status)) )
        {
            rc = -EFAULT;
            break;
        }
        guest_handle_add_offset(uop, 1);
    }

    gnttab_copy_release_buf(&src);
    gnttab_copy_release_buf(&dest);
    gnttab_copy_unlock_domains(&src, &dest);

    return rc;
}

static long
gnttab_set_version(XEN_GUEST_HANDLE_PARAM(gnttab_set_version_t) uop)
{
    gnttab_set_version_t op;
    struct domain *currd = current->domain;
    struct grant_table *gt = currd->grant_table;
    grant_entry_v1_t reserved_entries[GNTTAB_NR_RESERVED_ENTRIES];
    int res;
    unsigned int i, nr_ents;

    if ( copy_from_guest(&op, uop, 1) )
        return -EFAULT;

    res = -EINVAL;
    if ( op.version != 1 && op.version != 2 )
        goto out;

    res = -ENOSYS;
    if ( op.version == 2 && gt->max_version == 1 )
        goto out; /* Behave as before set_version was introduced. */

    res = 0;
    if ( gt->gt_version == op.version )
        goto out;

    grant_write_lock(gt);
    /*
     * Make sure that the grant table isn't currently in use when we
     * change the version number, except for the first 8 entries which
     * are allowed to be in use (xenstore/xenconsole keeps them mapped).
     * (You need to change the version number for e.g. kexec.)
     */
    nr_ents = nr_grant_entries(gt);
    for ( i = GNTTAB_NR_RESERVED_ENTRIES; i < nr_ents; i++ )
    {
        if ( read_atomic(&_active_entry(gt, i).pin) != 0 )
        {
            gdprintk(XENLOG_WARNING,
                     "tried to change grant table version from %u to %u, but some grant entries still in use\n",
                     gt->gt_version, op.version);
            res = -EBUSY;
            goto out_unlock;
        }
    }

    switch ( gt->gt_version )
    {
    case 1:
        /* XXX: We could maybe shrink the active grant table here. */
        res = gnttab_populate_status_frames(currd, gt, nr_grant_frames(gt));
        if ( res < 0)
            goto out_unlock;
        break;
    case 2:
        for ( i = 0; i < GNTTAB_NR_RESERVED_ENTRIES; i++ )
        {
            switch ( shared_entry_v2(gt, i).hdr.flags & GTF_type_mask )
            {
            case GTF_permit_access:
                 if ( !(shared_entry_v2(gt, i).full_page.frame >> 32) )
                     break;
                 /* fall through */
            case GTF_transitive:
                gdprintk(XENLOG_WARNING,
                         "tried to change grant table version to 1 with non-representable entries\n");
                res = -ERANGE;
                goto out_unlock;
            }
        }
        break;
    }

    /* Preserve the first 8 entries (toolstack reserved grants). */
    switch ( gt->gt_version )
    {
    case 1:
        memcpy(reserved_entries, &shared_entry_v1(gt, 0),
               sizeof(reserved_entries));
        break;
    case 2:
        for ( i = 0; i < GNTTAB_NR_RESERVED_ENTRIES; i++ )
        {
            unsigned int flags = shared_entry_v2(gt, i).hdr.flags;

            switch ( flags & GTF_type_mask )
            {
            case GTF_permit_access:
                reserved_entries[i].flags = flags | status_entry(gt, i);
                reserved_entries[i].domid = shared_entry_v2(gt, i).hdr.domid;
                reserved_entries[i].frame = shared_entry_v2(gt, i).full_page.frame;
                break;
            default:
                gdprintk(XENLOG_INFO,
                         "bad flags %#x in grant %#x when switching version\n",
                         flags, i);
                /* fall through */
            case GTF_invalid:
                memset(&reserved_entries[i], 0, sizeof(reserved_entries[i]));
                break;
            }
        }
        break;
    }

    if ( op.version < 2 && gt->gt_version == 2 &&
         (res = gnttab_unpopulate_status_frames(currd, gt)) != 0 )
        goto out_unlock;

    /* Make sure there's no crud left over from the old version. */
    for ( i = 0; i < nr_grant_frames(gt); i++ )
        clear_page(gt->shared_raw[i]);

    /* Restore the first 8 entries (toolstack reserved grants). */
    if ( gt->gt_version )
    {
        switch ( op.version )
        {
        case 1:
            memcpy(&shared_entry_v1(gt, 0), reserved_entries, sizeof(reserved_entries));
            break;
        case 2:
            for ( i = 0; i < GNTTAB_NR_RESERVED_ENTRIES; i++ )
            {
                status_entry(gt, i) =
                    reserved_entries[i].flags & (GTF_reading | GTF_writing);
                shared_entry_v2(gt, i).hdr.flags =
                    reserved_entries[i].flags & ~(GTF_reading | GTF_writing);
                shared_entry_v2(gt, i).hdr.domid =
                    reserved_entries[i].domid;
                shared_entry_v2(gt, i).full_page.frame =
                    reserved_entries[i].frame;
            }
            break;
        }
    }

    gt->gt_version = op.version;

 out_unlock:
    grant_write_unlock(gt);

 out:
    op.version = gt->gt_version;

    if ( __copy_to_guest(uop, &op, 1) )
        res = -EFAULT;

    return res;
}

static long
gnttab_get_status_frames(XEN_GUEST_HANDLE_PARAM(gnttab_get_status_frames_t) uop,
                         unsigned int count)
{
    gnttab_get_status_frames_t op;
    struct domain *d;
    struct grant_table *gt;
    uint64_t       gmfn;
    int i;
    int rc;

    if ( count != 1 )
        return -EINVAL;

    if ( unlikely(copy_from_guest(&op, uop, 1) != 0) )
    {
        gdprintk(XENLOG_INFO,
                 "Fault while reading gnttab_get_status_frames_t\n");
        return -EFAULT;
    }

    if ( !guest_handle_okay(op.frame_list, op.nr_frames) )
        return -EFAULT;

    d = rcu_lock_domain_by_any_id(op.dom);
    if ( d == NULL )
    {
        op.status = GNTST_bad_domain;
        goto out1;
    }
    rc = xsm_grant_setup(XSM_TARGET, current->domain, d);
    if ( rc )
    {
        op.status = GNTST_permission_denied;
        goto out2;
    }

    gt = d->grant_table;

    op.status = GNTST_okay;

    grant_read_lock(gt);

    if ( unlikely(op.nr_frames > nr_status_frames(gt)) )
    {
        gdprintk(XENLOG_INFO, "Requested addresses of d%d for %u grant "
                 "status frames, but has only %u\n",
                 d->domain_id, op.nr_frames, nr_status_frames(gt));
        op.status = GNTST_general_error;
        goto unlock;
    }

    for ( i = 0; i < op.nr_frames; i++ )
    {
        gmfn = gfn_x(gnttab_status_gfn(d, gt, i));
        if ( __copy_to_guest_offset(op.frame_list, i, &gmfn, 1) )
        {
            op.status = GNTST_bad_virt_addr;
            break;
        }
    }

 unlock:
    grant_read_unlock(gt);
 out2:
    rcu_unlock_domain(d);
 out1:
    if ( unlikely(__copy_field_to_guest(uop, &op, status)) )
        return -EFAULT;

    return 0;
}

static long
gnttab_get_version(XEN_GUEST_HANDLE_PARAM(gnttab_get_version_t) uop)
{
    gnttab_get_version_t op;
    struct domain *d;
    int rc;

    if ( copy_from_guest(&op, uop, 1) )
        return -EFAULT;

    d = rcu_lock_domain_by_any_id(op.dom);
    if ( d == NULL )
        return -ESRCH;

    rc = xsm_grant_query_size(XSM_TARGET, current->domain, d);
    if ( rc )
    {
        rcu_unlock_domain(d);
        return rc;
    }

    op.version = d->grant_table->gt_version;

    rcu_unlock_domain(d);

    if ( __copy_field_to_guest(uop, &op, version) )
        return -EFAULT;

    return 0;
}

static s16
swap_grant_ref(grant_ref_t ref_a, grant_ref_t ref_b)
{
    struct domain *d = rcu_lock_current_domain();
    struct grant_table *gt = d->grant_table;
    struct active_grant_entry *act_a = NULL;
    struct active_grant_entry *act_b = NULL;
    s16 rc = GNTST_okay;

    grant_write_lock(gt);

    /* Bounds check on the grant refs */
    if ( unlikely(ref_a >= nr_grant_entries(d->grant_table)))
    {
        gdprintk(XENLOG_WARNING, "Bad ref-a %#x\n", ref_a);
        rc = GNTST_bad_gntref;
        goto out;
    }
    if ( unlikely(ref_b >= nr_grant_entries(d->grant_table)))
    {
        gdprintk(XENLOG_WARNING, "Bad ref-b %#x\n", ref_b);
        rc = GNTST_bad_gntref;
        goto out;
    }

    /* Make sure the above checks are not bypassed speculatively */
    block_speculation();

    /* Swapping the same ref is a no-op. */
    if ( ref_a == ref_b )
        goto out;

    act_a = active_entry_acquire(gt, ref_a);
    if ( act_a->pin )
    {
        gdprintk(XENLOG_WARNING, "ref a %#x busy\n", ref_a);
        rc = GNTST_eagain;
        goto out;
    }

    act_b = active_entry_acquire(gt, ref_b);
    if ( act_b->pin )
    {
        gdprintk(XENLOG_WARNING, "ref b %#x busy\n", ref_b);
        rc = GNTST_eagain;
        goto out;
    }

    if ( evaluate_nospec(gt->gt_version == 1) )
    {
        grant_entry_v1_t shared;

        shared = shared_entry_v1(gt, ref_a);
        shared_entry_v1(gt, ref_a) = shared_entry_v1(gt, ref_b);
        shared_entry_v1(gt, ref_b) = shared;
    }
    else
    {
        grant_entry_v2_t shared;
        grant_status_t status;

        shared = shared_entry_v2(gt, ref_a);
        status = status_entry(gt, ref_a);

        shared_entry_v2(gt, ref_a) = shared_entry_v2(gt, ref_b);
        status_entry(gt, ref_a) = status_entry(gt, ref_b);

        shared_entry_v2(gt, ref_b) = shared;
        status_entry(gt, ref_b) = status;
    }

out:
    if ( act_b != NULL )
        active_entry_release(act_b);
    if ( act_a != NULL )
        active_entry_release(act_a);
    grant_write_unlock(gt);

    rcu_unlock_domain(d);

    return rc;
}

static long
gnttab_swap_grant_ref(XEN_GUEST_HANDLE_PARAM(gnttab_swap_grant_ref_t) uop,
                      unsigned int count)
{
    int i;
    gnttab_swap_grant_ref_t op;

    for ( i = 0; i < count; i++ )
    {
        if ( i && hypercall_preempt_check() )
            return i;
        if ( unlikely(__copy_from_guest(&op, uop, 1)) )
            return -EFAULT;
        op.status = swap_grant_ref(op.ref_a, op.ref_b);
        if ( unlikely(__copy_field_to_guest(uop, &op, status)) )
            return -EFAULT;
        guest_handle_add_offset(uop, 1);
    }
    return 0;
}

static int _cache_flush(const gnttab_cache_flush_t *cflush, grant_ref_t *cur_ref)
{
    struct domain *d, *owner;
    struct page_info *page;
    mfn_t mfn;
    struct active_grant_entry *act = NULL;
    void *v;
    int ret;

    if ( (cflush->offset >= PAGE_SIZE) ||
         (cflush->length > PAGE_SIZE) ||
         (cflush->offset + cflush->length > PAGE_SIZE) ||
         (cflush->op & ~(GNTTAB_CACHE_INVAL | GNTTAB_CACHE_CLEAN)) )
        return -EINVAL;

    if ( cflush->length == 0 || cflush->op == 0 )
        return !*cur_ref ? 0 : -EILSEQ;

    /* currently unimplemented */
    if ( cflush->op & GNTTAB_CACHE_SOURCE_GREF )
        return -EOPNOTSUPP;

    d = rcu_lock_current_domain();
    mfn = maddr_to_mfn(cflush->a.dev_bus_addr);

    if ( !mfn_valid(mfn) )
    {
        rcu_unlock_domain(d);
        return -EINVAL;
    }

    page = mfn_to_page(mfn);
    owner = page_get_owner_and_reference(page);
    if ( !owner || !owner->grant_table )
    {
        rcu_unlock_domain(d);
        return -EPERM;
    }

    if ( d != owner )
    {
        grant_read_lock(owner->grant_table);

        act = grant_map_exists(d, owner->grant_table, mfn, cur_ref);
        if ( IS_ERR_OR_NULL(act) )
        {
            grant_read_unlock(owner->grant_table);
            rcu_unlock_domain(d);
            put_page(page);
            return act ? PTR_ERR(act) : 1;
        }
    }

    v = map_domain_page(mfn);
    v += cflush->offset;

    if ( (cflush->op & GNTTAB_CACHE_INVAL) && (cflush->op & GNTTAB_CACHE_CLEAN) )
        ret = clean_and_invalidate_dcache_va_range(v, cflush->length);
    else if ( cflush->op & GNTTAB_CACHE_INVAL )
        ret = invalidate_dcache_va_range(v, cflush->length);
    else if ( cflush->op & GNTTAB_CACHE_CLEAN )
        ret = clean_dcache_va_range(v, cflush->length);
    else
        ret = 0;

    if ( d != owner )
    {
        active_entry_release(act);
        grant_read_unlock(owner->grant_table);
    }

    unmap_domain_page(v);
    put_page(page);
    rcu_unlock_domain(d);

    return ret;
}

static long
gnttab_cache_flush(XEN_GUEST_HANDLE_PARAM(gnttab_cache_flush_t) uop,
                      grant_ref_t *cur_ref,
                      unsigned int count)
{
    unsigned int i;
    gnttab_cache_flush_t op;

    for ( i = 0; i < count; i++ )
    {
        if ( i && hypercall_preempt_check() )
            return i;
        if ( unlikely(__copy_from_guest(&op, uop, 1)) )
            return -EFAULT;
        for ( ; ; )
        {
            int ret = _cache_flush(&op, cur_ref);

            if ( ret < 0 )
                return ret;
            if ( ret == 0 )
                break;
            if ( hypercall_preempt_check() )
                return i;
        }
        *cur_ref = 0;
        guest_handle_add_offset(uop, 1);
    }

    *cur_ref = 0;

    return 0;
}

long do_grant_table_op(
    unsigned int cmd, XEN_GUEST_HANDLE_PARAM(void) uop, unsigned int count)
{
    long rc;
    unsigned int opaque_in = cmd & GNTTABOP_ARG_MASK, opaque_out = 0;

#ifdef CONFIG_PV_SHIM
    if ( unlikely(pv_shim) )
        return pv_shim_grant_table_op(cmd, uop, count);
#endif

    if ( (int)count < 0 )
        return -EINVAL;

    if ( (cmd &= GNTTABOP_CMD_MASK) != GNTTABOP_cache_flush && opaque_in )
        return -EINVAL;

    rc = -EFAULT;
    switch ( cmd )
    {
    case GNTTABOP_map_grant_ref:
    {
        XEN_GUEST_HANDLE_PARAM(gnttab_map_grant_ref_t) map =
            guest_handle_cast(uop, gnttab_map_grant_ref_t);

        if ( unlikely(!guest_handle_okay(map, count)) )
            goto out;
        rc = gnttab_map_grant_ref(map, count);
        if ( rc > 0 )
        {
            guest_handle_add_offset(map, rc);
            uop = guest_handle_cast(map, void);
        }
        break;
    }

    case GNTTABOP_unmap_grant_ref:
    {
        XEN_GUEST_HANDLE_PARAM(gnttab_unmap_grant_ref_t) unmap =
            guest_handle_cast(uop, gnttab_unmap_grant_ref_t);

        if ( unlikely(!guest_handle_okay(unmap, count)) )
            goto out;
        rc = gnttab_unmap_grant_ref(unmap, count);
        if ( rc > 0 )
        {
            guest_handle_add_offset(unmap, rc);
            uop = guest_handle_cast(unmap, void);
        }
        break;
    }

    case GNTTABOP_unmap_and_replace:
    {
        XEN_GUEST_HANDLE_PARAM(gnttab_unmap_and_replace_t) unmap =
            guest_handle_cast(uop, gnttab_unmap_and_replace_t);

        if ( unlikely(!guest_handle_okay(unmap, count)) )
            goto out;
        rc = gnttab_unmap_and_replace(unmap, count);
        if ( rc > 0 )
        {
            guest_handle_add_offset(unmap, rc);
            uop = guest_handle_cast(unmap, void);
        }
        break;
    }

    case GNTTABOP_setup_table:
        rc = gnttab_setup_table(
            guest_handle_cast(uop, gnttab_setup_table_t), count, UINT_MAX);
        ASSERT(rc <= 0);
        break;

    case GNTTABOP_transfer:
    {
        XEN_GUEST_HANDLE_PARAM(gnttab_transfer_t) transfer =
            guest_handle_cast(uop, gnttab_transfer_t);

        if ( unlikely(!guest_handle_okay(transfer, count)) )
            goto out;
        rc = gnttab_transfer(transfer, count);
        if ( rc > 0 )
        {
            guest_handle_add_offset(transfer, rc);
            uop = guest_handle_cast(transfer, void);
        }
        break;
    }

    case GNTTABOP_copy:
    {
        XEN_GUEST_HANDLE_PARAM(gnttab_copy_t) copy =
            guest_handle_cast(uop, gnttab_copy_t);

        if ( unlikely(!guest_handle_okay(copy, count)) )
            goto out;
        rc = gnttab_copy(copy, count);
        if ( rc > 0 )
        {
            guest_handle_add_offset(copy, count - rc);
            uop = guest_handle_cast(copy, void);
        }
        break;
    }

    case GNTTABOP_query_size:
        rc = gnttab_query_size(
            guest_handle_cast(uop, gnttab_query_size_t), count);
        ASSERT(rc <= 0);
        break;

    case GNTTABOP_set_version:
        rc = gnttab_set_version(guest_handle_cast(uop, gnttab_set_version_t));
        break;

    case GNTTABOP_get_status_frames:
        rc = gnttab_get_status_frames(
            guest_handle_cast(uop, gnttab_get_status_frames_t), count);
        break;

    case GNTTABOP_get_version:
        rc = gnttab_get_version(guest_handle_cast(uop, gnttab_get_version_t));
        break;

    case GNTTABOP_swap_grant_ref:
    {
        XEN_GUEST_HANDLE_PARAM(gnttab_swap_grant_ref_t) swap =
            guest_handle_cast(uop, gnttab_swap_grant_ref_t);

        if ( unlikely(!guest_handle_okay(swap, count)) )
            goto out;
        rc = gnttab_swap_grant_ref(swap, count);
        if ( rc > 0 )
        {
            guest_handle_add_offset(swap, rc);
            uop = guest_handle_cast(swap, void);
        }
        break;
    }

    case GNTTABOP_cache_flush:
    {
        XEN_GUEST_HANDLE_PARAM(gnttab_cache_flush_t) cflush =
            guest_handle_cast(uop, gnttab_cache_flush_t);

        if ( unlikely(!guest_handle_okay(cflush, count)) )
            goto out;
        rc = gnttab_cache_flush(cflush, &opaque_in, count);
        if ( rc >= 0 )
        {
            guest_handle_add_offset(cflush, rc);
            uop = guest_handle_cast(cflush, void);
            opaque_out = opaque_in;
        }
        break;
    }

    default:
        rc = -ENOSYS;
        break;
    }

  out:
    if ( rc > 0 || (opaque_out != 0 && rc == 0) )
    {
        /* Adjust rc, see gnttab_copy() for why this is needed. */
        if ( cmd == GNTTABOP_copy )
            rc = count - rc;
        ASSERT(rc < count);
        ASSERT((opaque_out & GNTTABOP_CMD_MASK) == 0);
        rc = hypercall_create_continuation(__HYPERVISOR_grant_table_op, "ihi",
                                           opaque_out | cmd, uop, count - rc);
    }

    return rc;
}

#ifdef CONFIG_COMPAT
#include "compat/grant_table.c"
#endif

int gnttab_release_mappings(struct domain *d)
{
    struct grant_table   *gt = d->grant_table, *rgt;
    struct grant_mapping *map;
    grant_ref_t           ref;
    grant_handle_t        handle;
    struct domain        *rd;
    struct active_grant_entry *act;
    grant_entry_header_t *sha;
    uint16_t             *status;
    struct page_info     *pg;

    BUG_ON(!d->is_dying);

    if ( !gt || !gt->maptrack )
        return 0;

    for ( handle = gt->maptrack_limit; handle; )
    {
        mfn_t mfn;

        /*
         * Deal with full pages such that their freeing (in the body of the
         * if()) remains simple.
         */
        if ( handle < gt->maptrack_limit && !(handle % MAPTRACK_PER_PAGE) )
        {
            /*
             * Changing maptrack_limit alters nr_maptrack_frames()'es return
             * value. Free the then excess trailing page right here, rather
             * than leaving it to grant_table_destroy() (and in turn requiring
             * to leave gt->maptrack_limit unaltered).
             */
            gt->maptrack_limit = handle;
            FREE_XENHEAP_PAGE(gt->maptrack[nr_maptrack_frames(gt)]);

            if ( hypercall_preempt_check() )
                return -ERESTART;
        }

        --handle;

        map = &maptrack_entry(gt, handle);
        if ( !(map->flags & (GNTMAP_device_map|GNTMAP_host_map)) )
            continue;

        ref = map->ref;

        gdprintk(XENLOG_INFO, "Grant release %#x ref %#x flags %#x d%d\n",
                 handle, ref, map->flags, map->domid);

        rd = rcu_lock_domain_by_id(map->domid);
        if ( rd == NULL )
        {
            /* Nothing to clear up... */
            map->flags = 0;
            continue;
        }

        rgt = rd->grant_table;
        grant_read_lock(rgt);

        act = active_entry_acquire(rgt, ref);
        sha = shared_entry_header(rgt, ref);
        if ( rgt->gt_version == 1 )
            status = &sha->flags;
        else
            status = &status_entry(rgt, ref);

        pg = !is_iomem_page(act->mfn) ? mfn_to_page(act->mfn) : NULL;

        if ( map->flags & GNTMAP_readonly )
        {
            if ( map->flags & GNTMAP_device_map )
            {
                BUG_ON(!(act->pin & GNTPIN_devr_mask));
                act->pin -= GNTPIN_devr_inc;
                if ( pg )
                    put_page(pg);
            }

            if ( map->flags & GNTMAP_host_map )
            {
                BUG_ON(!(act->pin & GNTPIN_hstr_mask));
                act->pin -= GNTPIN_hstr_inc;
                if ( pg && gnttab_release_host_mappings(d) )
                    put_page(pg);
            }
        }
        else
        {
            if ( map->flags & GNTMAP_device_map )
            {
                BUG_ON(!(act->pin & GNTPIN_devw_mask));
                act->pin -= GNTPIN_devw_inc;
                if ( pg )
                    put_page_and_type(pg);
            }

            if ( map->flags & GNTMAP_host_map )
            {
                BUG_ON(!(act->pin & GNTPIN_hstw_mask));
                act->pin -= GNTPIN_hstw_inc;
                if ( pg && gnttab_release_host_mappings(d) )
                {
                    if ( gnttab_host_mapping_get_page_type(false, d, rd) )
                        put_page_type(pg);
                    put_page(pg);
                }
            }
        }

        reduce_status_for_pin(rd, act, status, map->flags & GNTMAP_readonly);

        mfn = act->mfn;

        active_entry_release(act);
        grant_read_unlock(rgt);

        rcu_unlock_domain(rd);

        map->flags = 0;

        /*
         * This is excessive in that a single such call would suffice per
         * mapped MFN (or none at all, if no entry was ever inserted). But it
         * should be the common case for an MFN to be mapped just once, and
         * this way we don't need to further maintain the counters. We also
         * don't want to leave cleaning up of the tree as a whole to the end
         * of the function, as this could take quite some time.
         */
        radix_tree_delete(&gt->maptrack_tree, mfn_x(mfn));
    }

    gt->maptrack_limit = 0;
    FREE_XENHEAP_PAGE(gt->maptrack[0]);

    radix_tree_destroy(&gt->maptrack_tree, NULL);

    return 0;
}

void grant_table_warn_active_grants(struct domain *d)
{
    struct grant_table *gt = d->grant_table;
    struct active_grant_entry *act;
    grant_ref_t ref;
    unsigned int nr_active = 0, nr_ents;

#define WARN_GRANT_MAX 10

    grant_read_lock(gt);

    nr_ents = nr_grant_entries(gt);
    for ( ref = 0; ref != nr_ents; ref++ )
    {
        act = active_entry_acquire(gt, ref);
        if ( !act->pin )
        {
            active_entry_release(act);
            continue;
        }

        nr_active++;
        if ( nr_active <= WARN_GRANT_MAX )
            printk(XENLOG_G_DEBUG "d%d has active grant %x ("
#ifndef NDEBUG
                   "GFN %lx, "
#endif
                   "MFN: %#"PRI_mfn")\n",
                   d->domain_id, ref,
#ifndef NDEBUG
                   gfn_x(act->gfn),
#endif
                   mfn_x(act->mfn));
        active_entry_release(act);
    }

    if ( nr_active > WARN_GRANT_MAX )
        printk(XENLOG_G_DEBUG "d%d has too many (%d) active grants to report\n",
               d->domain_id, nr_active);

    grant_read_unlock(gt);

#undef WARN_GRANT_MAX
}

void
grant_table_destroy(
    struct domain *d)
{
    struct grant_table *t = d->grant_table;
    int i;

    if ( t == NULL )
        return;

    for ( i = 0; i < nr_grant_frames(t); i++ )
        free_xenheap_page(t->shared_raw[i]);
    xfree(t->shared_raw);

    ASSERT(!t->maptrack_limit);
    vfree(t->maptrack);

    for ( i = 0; i < nr_active_grant_frames(t); i++ )
        free_xenheap_page(t->active[i]);
    xfree(t->active);

    for ( i = 0; i < nr_status_frames(t); i++ )
        free_xenheap_page(t->status[i]);
    xfree(t->status);

    xfree(t);
    d->grant_table = NULL;
}

void grant_table_init_vcpu(struct vcpu *v)
{
    spin_lock_init(&v->maptrack_freelist_lock);
    v->maptrack_head = MAPTRACK_TAIL;
    v->maptrack_tail = MAPTRACK_TAIL;
}

#ifdef CONFIG_MEM_SHARING
int mem_sharing_gref_to_gfn(struct grant_table *gt, grant_ref_t ref,
                            gfn_t *gfn, uint16_t *status)
{
    int rc = 0;
    uint16_t flags = 0;

    grant_read_lock(gt);

    if ( gt->gt_version < 1 )
        rc = -EINVAL;
    else if ( ref >= nr_grant_entries(gt) )
        rc = -ENOENT;
    else if ( evaluate_nospec(gt->gt_version == 1) )
    {
        const grant_entry_v1_t *sha1 = &shared_entry_v1(gt, ref);

        flags = sha1->flags;
        *gfn = _gfn(sha1->frame);
    }
    else
    {
        const grant_entry_v2_t *sha2 = &shared_entry_v2(gt, ref);

        flags = sha2->hdr.flags;
        if ( flags & GTF_sub_page )
           *gfn = _gfn(sha2->sub_page.frame);
        else
           *gfn = _gfn(sha2->full_page.frame);
    }

    if ( !rc && (flags & GTF_type_mask) != GTF_permit_access )
        rc = -ENXIO;
    else if ( !rc && status )
    {
        if ( evaluate_nospec(gt->gt_version == 1) )
            *status = flags;
        else
            *status = status_entry(gt, ref);
    }

    grant_read_unlock(gt);

    return rc;
}
#endif

/* caller must hold write lock */
static int gnttab_get_status_frame_mfn(struct domain *d,
                                       unsigned int idx, mfn_t *mfn)
{
    const struct grant_table *gt = d->grant_table;

    ASSERT(gt->gt_version == 2);

    /* Make sure we have version equal to 2 even under speculation */
    block_speculation();

    if ( idx >= nr_status_frames(gt) )
    {
        unsigned int nr_status;
        unsigned int nr_grant;

        nr_status = idx + 1; /* sufficient frames to make idx valid */

        if ( nr_status == 0 ) /* overflow? */
            return -EINVAL;

        nr_grant = status_to_grant_frames(nr_status);

        if ( grant_to_status_frames(nr_grant) != nr_status ) /* overflow? */
            return -EINVAL;

        if ( nr_grant <= gt->max_grant_frames )
            gnttab_grow_table(d, nr_grant);

        /* check whether gnttab_grow_table() succeeded */
        if ( idx >= nr_status_frames(gt) )
            return -EINVAL;
    }

    /* Make sure idx is bounded wrt nr_status_frames */
    *mfn = _mfn(virt_to_mfn(
                gt->status[array_index_nospec(idx, nr_status_frames(gt))]));
    return 0;
}

/* caller must hold write lock */
static int gnttab_get_shared_frame_mfn(struct domain *d,
                                       unsigned int idx, mfn_t *mfn)
{
    const struct grant_table *gt = d->grant_table;

    ASSERT(gt->gt_version != 0);

    if ( idx >= nr_grant_frames(gt) )
    {
        unsigned int nr_grant;

        nr_grant = idx + 1; /* sufficient frames to make idx valid */

        if ( nr_grant == 0 ) /* overflow? */
            return -EINVAL;

        if ( nr_grant <= gt->max_grant_frames )
            gnttab_grow_table(d, nr_grant);

        /* check whether gnttab_grow_table() succeeded */
        if ( idx >= nr_grant_frames(gt) )
            return -EINVAL;
    }

    /* Make sure idx is bounded wrt nr_status_frames */
    *mfn = _mfn(virt_to_mfn(
                gt->shared_raw[array_index_nospec(idx, nr_grant_frames(gt))]));
    return 0;
}

unsigned int gnttab_resource_max_frames(const struct domain *d, unsigned int id)
{
    const struct grant_table *gt = d->grant_table;
    unsigned int nr = 0;

    /* Don't need the grant lock.  This limit is fixed at domain create time. */
    switch ( id )
    {
    case XENMEM_resource_grant_table_id_shared:
        nr = gt->max_grant_frames;
        break;

    case XENMEM_resource_grant_table_id_status:
        if ( GNTTAB_MAX_VERSION < 2 )
            break;

        nr = grant_to_status_frames(gt->max_grant_frames);
        break;
    }

    return nr;
}

int gnttab_acquire_resource(
    struct domain *d, unsigned int id, unsigned int frame,
    unsigned int nr_frames, xen_pfn_t mfn_list[])
{
    struct grant_table *gt = d->grant_table;
    unsigned int i, final_frame;
    mfn_t tmp;
    void **vaddrs = NULL;
    int rc = -EINVAL;

    if ( !nr_frames )
        return rc;

    final_frame = frame + nr_frames - 1;

    /* Grow table if necessary. */
    grant_write_lock(gt);
    switch ( id )
    {
    case XENMEM_resource_grant_table_id_shared:
        vaddrs = gt->shared_raw;
        rc = gnttab_get_shared_frame_mfn(d, final_frame, &tmp);
        break;

    case XENMEM_resource_grant_table_id_status:
        if ( gt->gt_version != 2 )
            break;

        /* Check that void ** is a suitable representation for gt->status. */
        BUILD_BUG_ON(!__builtin_types_compatible_p(
                         typeof(gt->status), grant_status_t **));
        vaddrs = (void **)gt->status;
        rc = gnttab_get_status_frame_mfn(d, final_frame, &tmp);
        break;
    }

    /*
     * Some older toolchains can't spot that vaddrs won't remain uninitialized
     * on non-error paths, and hence it needs setting to NULL at the top of the
     * function.  Leave some runtime safety.
     */
    if ( !rc && !vaddrs )
    {
        ASSERT_UNREACHABLE();
        rc = -ENODATA;
    }

    /* Any errors?  Bad id, or from growing the table? */
    if ( rc )
        goto out;

    for ( i = 0; i < nr_frames; ++i )
        mfn_list[i] = virt_to_mfn(vaddrs[frame + i]);

    /* Success.  Passed nr_frames back to the caller. */
    rc = nr_frames;

 out:
    grant_write_unlock(gt);

    return rc;
}

int gnttab_map_frame(struct domain *d, unsigned long idx, gfn_t gfn, mfn_t *mfn)
{
    int rc = 0;
    struct grant_table *gt = d->grant_table;
    bool status = false;

    if ( gfn_eq(gfn, INVALID_GFN) )
    {
        ASSERT_UNREACHABLE();
        return -EINVAL;
    }

    grant_write_lock(gt);

    if ( evaluate_nospec(gt->gt_version == 2) && (idx & XENMAPIDX_grant_table_status) )
    {
        idx &= ~XENMAPIDX_grant_table_status;
        status = true;

        rc = gnttab_get_status_frame_mfn(d, idx, mfn);
    }
    else
        rc = gnttab_get_shared_frame_mfn(d, idx, mfn);

    if ( !rc )
    {
        struct page_info *pg = mfn_to_page(*mfn);

        /*
         * Make sure gnttab_unpopulate_status_frames() won't (successfully)
         * free the page until our caller has completed its operation.
         */
        if ( !get_page(pg, d) )
            rc = -EBUSY;
        else if ( (rc = gnttab_set_frame_gfn(gt, status, idx, gfn, *mfn)) )
            put_page(pg);
    }

    grant_write_unlock(gt);

    return rc;
}

static void gnttab_usage_print(struct domain *rd)
{
    int first = 1;
    grant_ref_t ref;
    struct grant_table *gt = rd->grant_table;
    unsigned int nr_ents;

    printk("      -------- active --------       -------- shared --------\n");
    printk("[ref] localdom mfn      pin          localdom gmfn     flags\n");

    grant_read_lock(gt);

    printk("grant-table for remote d%d (v%u)\n"
           "  %u frames (%u max), %u maptrack frames (%u max)\n",
           rd->domain_id, gt->gt_version,
           nr_grant_frames(gt), gt->max_grant_frames,
           nr_maptrack_frames(gt), gt->max_maptrack_frames);

    nr_ents = nr_grant_entries(gt);
    for ( ref = 0; ref != nr_ents; ref++ )
    {
        struct active_grant_entry *act;
        struct grant_entry_header *sha;
        uint16_t status;
        uint64_t frame;

        act = active_entry_acquire(gt, ref);
        if ( !act->pin )
        {
            active_entry_release(act);
            continue;
        }

        sha = shared_entry_header(gt, ref);

        if ( gt->gt_version == 1 )
        {
            status = sha->flags;
            frame = shared_entry_v1(gt, ref).frame;
        }
        else
        {
            frame = shared_entry_v2(gt, ref).full_page.frame;
            status = status_entry(gt, ref);
        }

        first = 0;

        /*      [0xXXX]  ddddd 0xXXXXX 0xXXXXXXXX      ddddd 0xXXXXXX 0xXX */
        printk("[0x%03x]  %5d 0x%"PRI_mfn" 0x%08x      %5d 0x%06"PRIx64" 0x%02x\n",
               ref, act->domid, mfn_x(act->mfn), act->pin,
               sha->domid, frame, status);
        active_entry_release(act);
    }

    grant_read_unlock(gt);

    if ( first )
        printk("no active grant table entries\n");
}

static void cf_check gnttab_usage_print_all(unsigned char key)
{
    struct domain *d;

    printk("%s [ key '%c' pressed\n", __func__, key);

    rcu_read_lock(&domlist_read_lock);

    for_each_domain ( d )
        gnttab_usage_print(d);

    rcu_read_unlock(&domlist_read_lock);

    printk("%s ] done\n", __func__);
}

static int __init cf_check gnttab_usage_init(void)
{
    register_keyhandler('g', gnttab_usage_print_all,
                        "print grant table usage", 1);
    return 0;
}
__initcall(gnttab_usage_init);

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: grant_table.c === */
/* === BEGIN INLINED: hvm.c === */
#include <xen_xen_config.h>
/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/arm/hvm.c
 *
 * Arch-specific hardware virtual machine abstractions.
 */

#include <xen_init.h>
#include <xen_lib.h>
#include <xen_errno.h>
#include <xen_guest_access.h>
#include <xen_hypercall.h>
#include <xen_sched.h>
#include <xen_monitor.h>

#include <xsm_xsm.h>

#include <public_xen.h>
#include <public_hvm_params.h>
#include <public_hvm_hvm_op.h>

static int hvm_allow_set_param(const struct domain *d, unsigned int param)
{
    switch ( param )
    {
        /*
         * The following parameters are intended for toolstack usage only.
         * They may not be set by the domain.
         *
         * The {STORE,CONSOLE}_EVTCHN values will need to become read/write to
         * the guest (not just the toolstack) if a new ABI hasn't appeared by
         * the time migration support is added.
         */
    case HVM_PARAM_CALLBACK_IRQ:
    case HVM_PARAM_STORE_PFN:
    case HVM_PARAM_STORE_EVTCHN:
    case HVM_PARAM_CONSOLE_PFN:
    case HVM_PARAM_CONSOLE_EVTCHN:
    case HVM_PARAM_MONITOR_RING_PFN:
        return d == current->domain ? -EPERM : 0;

        /* Writeable only by Xen, hole, deprecated, or out-of-range. */
    default:
        return -EINVAL;
    }
}

static int hvm_allow_get_param(const struct domain *d, unsigned int param)
{
    switch ( param )
    {
        /* The following parameters can be read by the guest and toolstack. */
    case HVM_PARAM_CALLBACK_IRQ:
    case HVM_PARAM_STORE_PFN:
    case HVM_PARAM_STORE_EVTCHN:
    case HVM_PARAM_CONSOLE_PFN:
    case HVM_PARAM_CONSOLE_EVTCHN:
        return 0;

        /*
         * The following parameters are intended for toolstack usage only.
         * They may not be read by the domain.
         */
    case HVM_PARAM_MONITOR_RING_PFN:
        return d == current->domain ? -EPERM : 0;

        /* Hole, deprecated, or out-of-range. */
    default:
        return -EINVAL;
    }
}

long do_hvm_op(unsigned long op, XEN_GUEST_HANDLE_PARAM(void) arg)
{
    long rc = 0;

    switch ( op )
    {
    case HVMOP_set_param:
    case HVMOP_get_param:
    {
        struct xen_hvm_param a;
        struct domain *d;

        if ( copy_from_guest(&a, arg, 1) )
            return -EFAULT;

        d = rcu_lock_domain_by_any_id(a.domid);
        if ( d == NULL )
            return -ESRCH;

        rc = xsm_hvm_param(XSM_TARGET, d, op);
        if ( rc )
            goto param_fail;

        if ( op == HVMOP_set_param )
        {
            rc = hvm_allow_set_param(d, a.index);
            if ( rc )
                goto param_fail;

            d->arch.hvm.params[a.index] = a.value;
        }
        else
        {
            rc = hvm_allow_get_param(d, a.index);
            if ( rc )
                goto param_fail;

            a.value = d->arch.hvm.params[a.index];
            rc = copy_to_guest(arg, &a, 1) ? -EFAULT : 0;
        }

    param_fail:
        rcu_unlock_domain(d);
        break;
    }

    case HVMOP_guest_request_vm_event:
        if ( guest_handle_is_null(arg) )
            monitor_guest_request();
        else
            rc = -EINVAL;
        break;

    default:
    {
        gdprintk(XENLOG_DEBUG, "HVMOP op=%lu: not implemented\n", op);
        rc = -ENOSYS;
        break;
    }
    }

    return rc;
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

/* === END INLINED: hvm.c === */
/* === BEGIN INLINED: common_domctl.c === */
#include <xen_xen_config.h>
/******************************************************************************
 * domctl.c
 *
 * Domain management operations. For use by node control stack.
 *
 * Copyright (c) 2002-2006, K A Fraser
 */

#include <xen_types.h>
#include <xen_lib.h>
#include <xen_err.h>
#include <xen_mm.h>
#include <xen_sched.h>
#include <xen_domain.h>
#include <xen_event.h>
#include <xen_grant_table.h>
#include <xen_domain_page.h>
#include <xen_trace.h>
#include <xen_console.h>
#include <xen_iocap.h>
#include <xen_rcupdate.h>
#include <xen_guest_access.h>
#include <xen_bitmap.h>
#include <xen_paging.h>
#include <xen_hypercall.h>
#include <xen_vm_event.h>
#include <xen_monitor.h>
#include <asm_current.h>
#include <asm_irq.h>
#include <asm_page.h>
#include <asm_p2m.h>
#include <public_domctl.h>
#include <xsm_xsm.h>

static DEFINE_SPINLOCK(domctl_lock);

static int nodemask_to_xenctl_bitmap(struct xenctl_bitmap *xenctl_nodemap,
                                     const nodemask_t *nodemask)
{
    return bitmap_to_xenctl_bitmap(xenctl_nodemap, nodemask_bits(nodemask),
                                   MAX_NUMNODES);
}

static int xenctl_bitmap_to_nodemask(nodemask_t *nodemask,
                                     const struct xenctl_bitmap *xenctl_nodemap)
{
    return xenctl_bitmap_to_bitmap(nodemask_bits(nodemask), xenctl_nodemap,
                                   MAX_NUMNODES);
}

static inline int is_free_domid(domid_t dom)
{
    struct domain *d;

    if ( dom >= DOMID_FIRST_RESERVED )
        return 0;

    if ( (d = rcu_lock_domain_by_id(dom)) == NULL )
        return 1;

    rcu_unlock_domain(d);
    return 0;
}

void getdomaininfo(struct domain *d, struct xen_domctl_getdomaininfo *info)
{
    struct vcpu *v;
    u64 cpu_time = 0;
    int flags = XEN_DOMINF_blocked;
    struct vcpu_runstate_info runstate;

    memset(info, 0, sizeof(*info));

    info->domain = d->domain_id;
    info->max_vcpu_id = XEN_INVALID_MAX_VCPU_ID;

    /*
     * - domain is marked as blocked only if all its vcpus are blocked
     * - domain is marked as running if any of its vcpus is running
     */
    for_each_vcpu ( d, v )
    {
        vcpu_runstate_get(v, &runstate);
        cpu_time += runstate.time[RUNSTATE_running];
        info->max_vcpu_id = v->vcpu_id;
        if ( !(v->pause_flags & VPF_down) )
        {
            if ( !(v->pause_flags & VPF_blocked) )
                flags &= ~XEN_DOMINF_blocked;
            if ( v->is_running )
                flags |= XEN_DOMINF_running;
            info->nr_online_vcpus++;
        }
    }

    info->cpu_time = cpu_time;

    info->flags = (info->nr_online_vcpus ? flags : 0) |
        ((d->is_dying == DOMDYING_dead) ? XEN_DOMINF_dying     : 0) |
        (d->is_shut_down                ? XEN_DOMINF_shutdown  : 0) |
        (d->controller_pause_count > 0  ? XEN_DOMINF_paused    : 0) |
        (d->debugger_attached           ? XEN_DOMINF_debugged  : 0) |
        (is_xenstore_domain(d)          ? XEN_DOMINF_xs_domain : 0) |
        (is_hvm_domain(d)               ? XEN_DOMINF_hvm_guest : 0) |
        d->shutdown_code << XEN_DOMINF_shutdownshift;

    xsm_security_domaininfo(d, info);

    info->tot_pages         = domain_tot_pages(d);
    info->max_pages         = d->max_pages;
    info->outstanding_pages = d->outstanding_pages;
#ifdef CONFIG_MEM_SHARING
    info->shr_pages         = atomic_read(&d->shr_pages);
#endif
#ifdef CONFIG_MEM_PAGING
    info->paged_pages       = atomic_read(&d->paged_pages);
#endif
    info->shared_info_frame =
        gfn_x(mfn_to_gfn(d, _mfn(virt_to_mfn(d->shared_info))));
    BUG_ON(SHARED_M2P(info->shared_info_frame));

    info->cpupool = cpupool_get_id(d);

    memcpy(info->handle, d->handle, sizeof(xen_domain_handle_t));

    arch_get_domain_info(d, info);
}

bool domctl_lock_acquire(void)
{
    /*
     * Caller may try to pause its own VCPUs. We must prevent deadlock
     * against other non-domctl routines which try to do the same.
     */
    if ( !spin_trylock(&current->domain->hypercall_deadlock_mutex) )
        return 0;

    /*
     * Trylock here is paranoia if we have multiple privileged domains. Then
     * we could have one domain trying to pause another which is spinning
     * on domctl_lock -- results in deadlock.
     */
    if ( spin_trylock(&domctl_lock) )
        return 1;

    spin_unlock(&current->domain->hypercall_deadlock_mutex);
    return 0;
}

void domctl_lock_release(void)
{
    spin_unlock(&domctl_lock);
    spin_unlock(&current->domain->hypercall_deadlock_mutex);
}

void vnuma_destroy(struct vnuma_info *vnuma)
{
    if ( vnuma )
    {
        xfree(vnuma->vmemrange);
        xfree(vnuma->vcpu_to_vnode);
        xfree(vnuma->vdistance);
        xfree(vnuma->vnode_to_pnode);
        xfree(vnuma);
    }
}

/*
 * Allocates memory for vNUMA, **vnuma should be NULL.
 * Caller has to make sure that domain has max_pages
 * and number of vcpus set for domain.
 * Verifies that single allocation does not exceed
 * PAGE_SIZE.
 */
static struct vnuma_info *vnuma_alloc(unsigned int nr_vnodes,
                                      unsigned int nr_ranges,
                                      unsigned int nr_vcpus)
{

    struct vnuma_info *vnuma;

    /*
     * Check if any of the allocations are bigger than PAGE_SIZE.
     * See XSA-77.
     */
    if ( nr_vnodes == 0 ||
         nr_vnodes > (PAGE_SIZE / sizeof(*vnuma->vdistance) / nr_vnodes) ||
         nr_ranges > (PAGE_SIZE / sizeof(*vnuma->vmemrange)) )
        return ERR_PTR(-EINVAL);

    /*
     * If allocations become larger then PAGE_SIZE, these allocations
     * should be split into PAGE_SIZE allocations due to XSA-77.
     */
    vnuma = xmalloc(struct vnuma_info);
    if ( !vnuma )
        return ERR_PTR(-ENOMEM);

    vnuma->vdistance = xmalloc_array(unsigned int, nr_vnodes * nr_vnodes);
    vnuma->vcpu_to_vnode = xmalloc_array(unsigned int, nr_vcpus);
    vnuma->vnode_to_pnode = xmalloc_array(nodeid_t, nr_vnodes);
    vnuma->vmemrange = xmalloc_array(xen_vmemrange_t, nr_ranges);

    if ( vnuma->vdistance == NULL || vnuma->vmemrange == NULL ||
         vnuma->vcpu_to_vnode == NULL || vnuma->vnode_to_pnode == NULL )
    {
        vnuma_destroy(vnuma);
        return ERR_PTR(-ENOMEM);
    }

    return vnuma;
}

/*
 * Construct vNUMA topology form uinfo.
 */
static struct vnuma_info *vnuma_init(const struct xen_domctl_vnuma *uinfo,
                                     const struct domain *d)
{
    unsigned int i, nr_vnodes;
    int ret = -EINVAL;
    struct vnuma_info *info;

    nr_vnodes = uinfo->nr_vnodes;

    if ( uinfo->nr_vcpus != d->max_vcpus || uinfo->pad != 0 )
        return ERR_PTR(ret);

    info = vnuma_alloc(nr_vnodes, uinfo->nr_vmemranges, d->max_vcpus);
    if ( IS_ERR(info) )
        return info;

    ret = -EFAULT;

    if ( copy_from_guest(info->vdistance, uinfo->vdistance,
                         nr_vnodes * nr_vnodes) )
        goto vnuma_fail;

    if ( copy_from_guest(info->vmemrange, uinfo->vmemrange,
                         uinfo->nr_vmemranges) )
        goto vnuma_fail;

    if ( copy_from_guest(info->vcpu_to_vnode, uinfo->vcpu_to_vnode,
                         d->max_vcpus) )
        goto vnuma_fail;

    ret = -E2BIG;
    for ( i = 0; i < d->max_vcpus; ++i )
        if ( info->vcpu_to_vnode[i] >= nr_vnodes )
            goto vnuma_fail;

    for ( i = 0; i < nr_vnodes; ++i )
    {
        unsigned int pnode;

        ret = -EFAULT;
        if ( copy_from_guest_offset(&pnode, uinfo->vnode_to_pnode, i, 1) )
            goto vnuma_fail;
        ret = -E2BIG;
        if ( pnode >= MAX_NUMNODES )
            goto vnuma_fail;
        info->vnode_to_pnode[i] = pnode;
    }

    info->nr_vnodes = nr_vnodes;
    info->nr_vmemranges = uinfo->nr_vmemranges;

    /* Check that vmemranges flags are zero. */
    ret = -EINVAL;
    for ( i = 0; i < info->nr_vmemranges; i++ )
        if ( info->vmemrange[i].flags != 0 )
            goto vnuma_fail;

    return info;

 vnuma_fail:
    vnuma_destroy(info);
    return ERR_PTR(ret);
}

long do_domctl(XEN_GUEST_HANDLE_PARAM(xen_domctl_t) u_domctl)
{
    long ret = 0;
    bool copyback = false;
    struct xen_domctl curop, *op = &curop;
    struct domain *d;

    if ( copy_from_guest(op, u_domctl, 1) )
        return -EFAULT;

    if ( op->interface_version != XEN_DOMCTL_INTERFACE_VERSION )
        return -EACCES;

    switch ( op->cmd )
    {
    case XEN_DOMCTL_createdomain:
        d = NULL;
        break;

    case XEN_DOMCTL_assign_device:
    case XEN_DOMCTL_deassign_device:
        if ( op->domain == DOMID_IO )
        {
            d = dom_io;
            break;
        }
        else if ( op->domain == DOMID_INVALID )
            return -ESRCH;
        fallthrough;
    case XEN_DOMCTL_test_assign_device:
    case XEN_DOMCTL_vm_event_op:
        if ( op->domain == DOMID_INVALID )
        {
            d = NULL;
            break;
        }
        fallthrough;
    default:
        d = rcu_lock_domain_by_id(op->domain);
        if ( !d )
            return -ESRCH;
        break;
    }

    ret = xsm_domctl(XSM_OTHER, d, op->cmd);
    if ( ret )
        goto domctl_out_unlock_domonly;

    if ( !domctl_lock_acquire() )
    {
        if ( d && d != dom_io )
            rcu_unlock_domain(d);
        return hypercall_create_continuation(
            __HYPERVISOR_domctl, "h", u_domctl);
    }

    switch ( op->cmd )
    {

    case XEN_DOMCTL_setvcpucontext:
    {
        vcpu_guest_context_u c = { .nat = NULL };
        unsigned int vcpu = op->u.vcpucontext.vcpu;
        struct vcpu *v;

        ret = -EINVAL;
        if ( (d == current->domain) || /* no domain_pause() */
             (vcpu >= d->max_vcpus) || ((v = d->vcpu[vcpu]) == NULL) )
            break;

        if ( guest_handle_is_null(op->u.vcpucontext.ctxt) )
        {
            ret = vcpu_reset(v);
            if ( ret == -ERESTART )
                ret = hypercall_create_continuation(
                          __HYPERVISOR_domctl, "h", u_domctl);
            break;
        }

#ifdef CONFIG_COMPAT
        BUILD_BUG_ON(sizeof(struct vcpu_guest_context)
                     < sizeof(struct compat_vcpu_guest_context));
#endif
        ret = -ENOMEM;
        if ( (c.nat = alloc_vcpu_guest_context()) == NULL )
            break;

#ifdef CONFIG_COMPAT
        if ( !is_pv_32bit_domain(d) )
            ret = copy_from_guest(c.nat, op->u.vcpucontext.ctxt, 1);
        else
            ret = copy_from_guest(c.cmp,
                                  guest_handle_cast(op->u.vcpucontext.ctxt,
                                                    void), 1);
#else
        ret = copy_from_guest(c.nat, op->u.vcpucontext.ctxt, 1);
#endif
        ret = ret ? -EFAULT : 0;

        if ( ret == 0 )
        {
            domain_pause(d);
            ret = arch_set_info_guest(v, c);
            domain_unpause(d);

            if ( ret == -ERESTART )
                ret = hypercall_create_continuation(
                          __HYPERVISOR_domctl, "h", u_domctl);
        }

        free_vcpu_guest_context(c.nat);
        break;
    }

    case XEN_DOMCTL_pausedomain:
        ret = -EINVAL;
        if ( d != current->domain )
            ret = domain_pause_by_systemcontroller(d);
        break;

    case XEN_DOMCTL_unpausedomain:
        ret = domain_unpause_by_systemcontroller(d);
        break;

    case XEN_DOMCTL_resumedomain:
        if ( d == current->domain ) /* no domain_pause() */
            ret = -EINVAL;
        else
            domain_resume(d);
        break;

    case XEN_DOMCTL_createdomain:
    {
        domid_t        dom;
        static domid_t rover = 0;

        dom = op->domain;
        if ( (dom > 0) && (dom < DOMID_FIRST_RESERVED) )
        {
            ret = -EEXIST;
            if ( !is_free_domid(dom) )
                break;
        }
        else
        {
            for ( dom = rover + 1; dom != rover; dom++ )
            {
                if ( dom == DOMID_FIRST_RESERVED )
                    dom = 1;
                if ( is_free_domid(dom) )
                    break;
            }

            ret = -ENOMEM;
            if ( dom == rover )
                break;

            rover = dom;
        }

        d = domain_create(dom, &op->u.createdomain, false);
        if ( IS_ERR(d) )
        {
            ret = PTR_ERR(d);
            d = NULL;
            break;
        }

        ret = 0;
        op->domain = d->domain_id;
        copyback = 1;
        d = NULL;
        break;
    }

    case XEN_DOMCTL_max_vcpus:
    {
        unsigned int i, max = op->u.max_vcpus.max;

        ret = -EINVAL;
        if ( (d == current->domain) || /* no domain_pause() */
             (max != d->max_vcpus) )   /* max_vcpus set up in createdomain */
            break;

        /* Needed, for example, to ensure writable p.t. state is synced. */
        domain_pause(d);

        ret = -ENOMEM;

        for ( i = 0; i < max; i++ )
        {
            if ( d->vcpu[i] != NULL )
                continue;

            if ( vcpu_create(d, i) == NULL )
                goto maxvcpu_out;
        }

        domain_update_node_affinity(d);
        ret = 0;

    maxvcpu_out:
        domain_unpause(d);
        break;
    }

    case XEN_DOMCTL_soft_reset:
    case XEN_DOMCTL_soft_reset_cont:
        if ( d == current->domain ) /* no domain_pause() */
        {
            ret = -EINVAL;
            break;
        }
        ret = domain_soft_reset(d, op->cmd == XEN_DOMCTL_soft_reset_cont);
        if ( ret == -ERESTART )
        {
            op->cmd = XEN_DOMCTL_soft_reset_cont;
            if ( !__copy_field_to_guest(u_domctl, op, cmd) )
                ret = hypercall_create_continuation(__HYPERVISOR_domctl,
                                                    "h", u_domctl);
            else
                ret = -EFAULT;
        }
        break;

    case XEN_DOMCTL_destroydomain:
        ret = domain_kill(d);
        if ( ret == -ERESTART )
            ret = hypercall_create_continuation(
                __HYPERVISOR_domctl, "h", u_domctl);
        break;

    case XEN_DOMCTL_setnodeaffinity:
    {
        nodemask_t new_affinity;

        ret = xenctl_bitmap_to_nodemask(&new_affinity,
                                        &op->u.nodeaffinity.nodemap);
        if ( !ret )
            ret = domain_set_node_affinity(d, &new_affinity);
        break;
    }

    case XEN_DOMCTL_getnodeaffinity:
        ret = nodemask_to_xenctl_bitmap(&op->u.nodeaffinity.nodemap,
                                        &d->node_affinity);
        break;

    case XEN_DOMCTL_setvcpuaffinity:
    case XEN_DOMCTL_getvcpuaffinity:
        ret = vcpu_affinity_domctl(d, op->cmd, &op->u.vcpuaffinity);
        break;

    case XEN_DOMCTL_scheduler_op:
        ret = sched_adjust(d, &op->u.scheduler_op);
        copyback = 1;
        break;

    case XEN_DOMCTL_getdomaininfo:
        ret = xsm_getdomaininfo(XSM_HOOK, d);
        if ( ret )
            break;

        getdomaininfo(d, &op->u.getdomaininfo);

        op->domain = op->u.getdomaininfo.domain;
        copyback = 1;
        break;

    case XEN_DOMCTL_getvcpucontext:
    {
        vcpu_guest_context_u c = { .nat = NULL };
        struct vcpu         *v;

        ret = -EINVAL;
        if ( op->u.vcpucontext.vcpu >= d->max_vcpus ||
             (v = d->vcpu[op->u.vcpucontext.vcpu]) == NULL ||
             v == current ) /* no vcpu_pause() */
            goto getvcpucontext_out;

        ret = -ENODATA;
        if ( !v->is_initialised )
            goto getvcpucontext_out;

#ifdef CONFIG_COMPAT
        BUILD_BUG_ON(sizeof(struct vcpu_guest_context)
                     < sizeof(struct compat_vcpu_guest_context));
#endif
        ret = -ENOMEM;
        if ( (c.nat = xzalloc(struct vcpu_guest_context)) == NULL )
            goto getvcpucontext_out;

        vcpu_pause(v);

        arch_get_info_guest(v, c);
        ret = 0;

        vcpu_unpause(v);

#ifdef CONFIG_COMPAT
        if ( !is_pv_32bit_domain(d) )
            ret = copy_to_guest(op->u.vcpucontext.ctxt, c.nat, 1);
        else
            ret = copy_to_guest(guest_handle_cast(op->u.vcpucontext.ctxt,
                                                  void), c.cmp, 1);
#else
        ret = copy_to_guest(op->u.vcpucontext.ctxt, c.nat, 1);
#endif

        if ( ret )
            ret = -EFAULT;
        copyback = 1;

    getvcpucontext_out:
        xfree(c.nat);
        break;
    }

    case XEN_DOMCTL_getvcpuinfo:
    {
        struct vcpu   *v;
        struct vcpu_runstate_info runstate;

        ret = -EINVAL;
        if ( op->u.getvcpuinfo.vcpu >= d->max_vcpus )
            break;

        ret = -ESRCH;
        if ( (v = d->vcpu[op->u.getvcpuinfo.vcpu]) == NULL )
            break;

        vcpu_runstate_get(v, &runstate);

        op->u.getvcpuinfo.online   = !(v->pause_flags & VPF_down);
        op->u.getvcpuinfo.blocked  = !!(v->pause_flags & VPF_blocked);
        op->u.getvcpuinfo.running  = v->is_running;
        op->u.getvcpuinfo.cpu_time = runstate.time[RUNSTATE_running];
        op->u.getvcpuinfo.cpu      = v->processor;
        ret = 0;
        copyback = 1;
        break;
    }

    case XEN_DOMCTL_max_mem:
    {
        uint64_t new_max = op->u.max_mem.max_memkb >> (PAGE_SHIFT - 10);

        nrspin_lock(&d->page_alloc_lock);
        /*
         * NB. We removed a check that new_max >= current tot_pages; this means
         * that the domain will now be allowed to "ratchet" down to new_max. In
         * the meantime, while tot > max, all new allocations are disallowed.
         */
        d->max_pages = min(new_max, (uint64_t)(typeof(d->max_pages))-1);
        nrspin_unlock(&d->page_alloc_lock);
        break;
    }

    case XEN_DOMCTL_setdomainhandle:
        memcpy(d->handle, op->u.setdomainhandle.handle,
               sizeof(xen_domain_handle_t));
        break;

    case XEN_DOMCTL_setdebugging:
        if ( unlikely(d == current->domain) ) /* no domain_pause() */
            ret = -EINVAL;
        else
        {
            domain_pause(d);
            d->debugger_attached = !!op->u.setdebugging.enable;
            domain_unpause(d); /* causes guest to latch new status */
        }
        break;

#ifdef CONFIG_HAS_PIRQ
    case XEN_DOMCTL_irq_permission:
    {
        unsigned int pirq = op->u.irq_permission.pirq, irq;
        int allow = op->u.irq_permission.allow_access;

        if ( pirq >= current->domain->nr_pirqs )
        {
            ret = -EINVAL;
            break;
        }
        irq = pirq_access_permitted(current->domain, pirq);
        if ( !irq || xsm_irq_permission(XSM_HOOK, d, irq, allow) )
            ret = -EPERM;
        else if ( allow )
            ret = irq_permit_access(d, irq);
        else
            ret = irq_deny_access(d, irq);
        break;
    }
#endif

    case XEN_DOMCTL_iomem_permission:
    {
        unsigned long mfn = op->u.iomem_permission.first_mfn;
        unsigned long nr_mfns = op->u.iomem_permission.nr_mfns;
        int allow = op->u.iomem_permission.allow_access;

        ret = -EINVAL;
        if ( (mfn + nr_mfns - 1) < mfn ) /* wrap? */
            break;

        if ( !iomem_access_permitted(current->domain,
                                     mfn, mfn + nr_mfns - 1) ||
             xsm_iomem_permission(XSM_HOOK, d, mfn, mfn + nr_mfns - 1, allow) )
            ret = -EPERM;
        else if ( allow )
            ret = iomem_permit_access(d, mfn, mfn + nr_mfns - 1);
        else
            ret = iomem_deny_access(d, mfn, mfn + nr_mfns - 1);
        break;
    }

    case XEN_DOMCTL_memory_mapping:
    {
        unsigned long gfn = op->u.memory_mapping.first_gfn;
        unsigned long mfn = op->u.memory_mapping.first_mfn;
        unsigned long nr_mfns = op->u.memory_mapping.nr_mfns;
        unsigned long mfn_end = mfn + nr_mfns - 1;
        int add = op->u.memory_mapping.add_mapping;

        ret = -EINVAL;
        if ( mfn_end < mfn || /* wrap? */
             ((mfn | mfn_end) >> (paddr_bits - PAGE_SHIFT)) ||
             (gfn + nr_mfns - 1) < gfn ) /* wrap? */
            break;

#ifndef CONFIG_X86 /* XXX ARM!? */
        ret = -E2BIG;
        /* Must break hypercall up as this could take a while. */
        if ( nr_mfns > 64 )
            break;
#endif

        ret = -EPERM;
        if ( !iomem_access_permitted(current->domain, mfn, mfn_end) ||
             !iomem_access_permitted(d, mfn, mfn_end) )
            break;

        ret = xsm_iomem_mapping(XSM_HOOK, d, mfn, mfn_end, add);
        if ( ret )
            break;

        if ( !paging_mode_translate(d) )
            break;

        if ( add )
        {
            printk(XENLOG_G_DEBUG
                   "memory_map:add: dom%d gfn=%lx mfn=%lx nr=%lx\n",
                   d->domain_id, gfn, mfn, nr_mfns);

            ret = map_mmio_regions(d, _gfn(gfn), nr_mfns, _mfn(mfn));
            if ( ret < 0 )
                printk(XENLOG_G_WARNING
                       "memory_map:fail: dom%d gfn=%lx mfn=%lx nr=%lx ret:%ld\n",
                       d->domain_id, gfn, mfn, nr_mfns, ret);
        }
        else
        {
            printk(XENLOG_G_DEBUG
                   "memory_map:remove: dom%d gfn=%lx mfn=%lx nr=%lx\n",
                   d->domain_id, gfn, mfn, nr_mfns);

            ret = unmap_mmio_regions(d, _gfn(gfn), nr_mfns, _mfn(mfn));
            if ( ret < 0 && is_hardware_domain(current->domain) )
                printk(XENLOG_ERR
                       "memory_map: error %ld removing dom%d access to [%lx,%lx]\n",
                       ret, d->domain_id, mfn, mfn_end);
        }
        break;
    }

    case XEN_DOMCTL_settimeoffset:
        domain_set_time_offset(d, op->u.settimeoffset.time_offset_seconds);
        break;

    case XEN_DOMCTL_set_target:
    {
        struct domain *e;

        ret = -ESRCH;
        e = get_domain_by_id(op->u.set_target.target);
        if ( e == NULL )
            break;

        ret = -EINVAL;
        if ( (d == e) || (d->target != NULL) )
        {
            put_domain(e);
            break;
        }

        ret = -EOPNOTSUPP;
        if ( is_hvm_domain(e) )
            ret = xsm_set_target(XSM_HOOK, d, e);
        if ( ret )
        {
            put_domain(e);
            break;
        }

        /* Hold reference on @e until we destroy @d. */
        d->target = e;
        break;
    }

    case XEN_DOMCTL_subscribe:
        d->suspend_evtchn = op->u.subscribe.port;
        break;

    case XEN_DOMCTL_vm_event_op:
        ret = vm_event_domctl(d, &op->u.vm_event_op);
        if ( ret == 0 )
            copyback = true;
        break;

#ifdef CONFIG_MEM_ACCESS
    case XEN_DOMCTL_set_access_required:
        if ( unlikely(current->domain == d) ) /* no domain_pause() */
            ret = -EPERM;
        else
        {
            domain_pause(d);
            arch_p2m_set_access_required(d,
                op->u.access_required.access_required);
            domain_unpause(d);
        }
        break;
#endif

    case XEN_DOMCTL_set_virq_handler:
        ret = set_global_virq_handler(d, op->u.set_virq_handler.virq);
        break;

    case XEN_DOMCTL_setvnumainfo:
    {
        struct vnuma_info *vnuma;

        vnuma = vnuma_init(&op->u.vnuma, d);
        if ( IS_ERR(vnuma) )
        {
            ret = PTR_ERR(vnuma);
            break;
        }

        /* overwrite vnuma topology for domain. */
        write_lock(&d->vnuma_rwlock);
        vnuma_destroy(d->vnuma);
        d->vnuma = vnuma;
        write_unlock(&d->vnuma_rwlock);

        break;
    }

    case XEN_DOMCTL_monitor_op:
        ret = monitor_domctl(d, &op->u.monitor_op);
        if ( !ret )
            copyback = 1;
        break;

    case XEN_DOMCTL_assign_device:
    case XEN_DOMCTL_test_assign_device:
    case XEN_DOMCTL_deassign_device:
    case XEN_DOMCTL_get_device_group:
        ret = iommu_do_domctl(op, d, u_domctl);
        break;

    case XEN_DOMCTL_get_paging_mempool_size:
        ret = arch_get_paging_mempool_size(d, &op->u.paging_mempool.size);
        if ( !ret )
            copyback = 1;
        break;

    case XEN_DOMCTL_set_paging_mempool_size:
        ret = arch_set_paging_mempool_size(d, op->u.paging_mempool.size);

        if ( ret == -ERESTART )
            ret = hypercall_create_continuation(
                __HYPERVISOR_domctl, "h", u_domctl);
        break;

    default:
        ret = arch_do_domctl(op, d, u_domctl);
        break;
    }

    domctl_lock_release();

 domctl_out_unlock_domonly:
    if ( d && d != dom_io )
        rcu_unlock_domain(d);

    if ( copyback && __copy_to_guest(u_domctl, op, 1) )
        ret = -EFAULT;

    return ret;
}

static void __init __maybe_unused build_assertions(void)
{
    struct xen_domctl d;

    BUILD_BUG_ON(sizeof(d) != 16 /* header */ + 128 /* union */);
    BUILD_BUG_ON(offsetof(typeof(d), u) != 16);
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

/* === END INLINED: common_domctl.c === */
/* === BEGIN INLINED: domctl.c === */
#include <xen_xen_config.h>
/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 * Arch-specific domctl.c
 *
 * Copyright (c) 2012, Citrix Systems
 */

#include <xen_dt-overlay.h>
#include <xen_errno.h>
#include <xen_guest_access.h>
#include <xen_hypercall.h>
#include <xen_iocap.h>
#include <xen_lib.h>
#include <xen_mm.h>
#include <xen_sched.h>
#include <xen_types.h>
#include <xsm_xsm.h>
#include <public_domctl.h>

void arch_get_domain_info(const struct domain *d,
                          struct xen_domctl_getdomaininfo *info)
{
    /* All ARM domains use hardware assisted paging. */
    info->flags |= XEN_DOMINF_hap;

    info->gpaddr_bits = p2m_ipa_bits;
}

static int handle_vuart_init(struct domain *d, 
                             struct xen_domctl_vuart_op *vuart_op)
{
    int rc;
    struct vpl011_init_info info;

    info.console_domid = vuart_op->console_domid;
    info.gfn = _gfn(vuart_op->gfn);

    if ( d->creation_finished )
        return -EPERM;

    if ( vuart_op->type != XEN_DOMCTL_VUART_TYPE_VPL011 )
        return -EOPNOTSUPP;

    rc = domain_vpl011_init(d, &info);

    if ( !rc )
        vuart_op->evtchn = info.evtchn;

    return rc;
}

long arch_do_domctl(struct xen_domctl *domctl, struct domain *d,
                    XEN_GUEST_HANDLE_PARAM(xen_domctl_t) u_domctl)
{
    switch ( domctl->cmd )
    {
    case XEN_DOMCTL_cacheflush:
    {
        gfn_t s = _gfn(domctl->u.cacheflush.start_pfn);
        gfn_t e = gfn_add(s, domctl->u.cacheflush.nr_pfns);
        int rc;

        if ( domctl->u.cacheflush.nr_pfns > (1U<<MAX_ORDER) )
            return -EINVAL;

        if ( gfn_x(e) < gfn_x(s) )
            return -EINVAL;

        /* XXX: Handle preemption */
        do
            rc = p2m_cache_flush_range(d, &s, e);
        while ( rc == -ERESTART );

        return rc;
    }
    case XEN_DOMCTL_bind_pt_irq:
    {
        int rc;
        struct xen_domctl_bind_pt_irq *bind = &domctl->u.bind_pt_irq;
        uint32_t irq = bind->u.spi.spi;
        uint32_t virq = bind->machine_irq;

        /* We only support PT_IRQ_TYPE_SPI */
        if ( bind->irq_type != PT_IRQ_TYPE_SPI )
            return -EOPNOTSUPP;

        /*
         * XXX: For now map the interrupt 1:1. Other support will require to
         * modify domain_pirq_to_irq macro.
         */
        if ( irq != virq )
            return -EINVAL;

        /*
         * ARM doesn't require separating IRQ assignation into 2
         * hypercalls (PHYSDEVOP_map_pirq and DOMCTL_bind_pt_irq).
         *
         * Call xsm_map_domain_irq in order to keep the same XSM checks
         * done by the 2 hypercalls for consistency with other
         * architectures.
         */
        rc = xsm_map_domain_irq(XSM_HOOK, d, irq, NULL);
        if ( rc )
            return rc;

        rc = xsm_bind_pt_irq(XSM_HOOK, d, bind);
        if ( rc )
            return rc;

        if ( !irq_access_permitted(current->domain, irq) )
            return -EPERM;

        if ( !vgic_reserve_virq(d, virq) )
            return -EBUSY;

        rc = route_irq_to_guest(d, virq, irq, "routed IRQ");
        if ( rc )
            vgic_free_virq(d, virq);

        return rc;
    }
    case XEN_DOMCTL_unbind_pt_irq:
    {
        int rc;
        struct xen_domctl_bind_pt_irq *bind = &domctl->u.bind_pt_irq;
        uint32_t irq = bind->u.spi.spi;
        uint32_t virq = bind->machine_irq;

        /* We only support PT_IRQ_TYPE_SPI */
        if ( bind->irq_type != PT_IRQ_TYPE_SPI )
            return -EOPNOTSUPP;

        /* For now map the interrupt 1:1 */
        if ( irq != virq )
            return -EINVAL;

        rc = xsm_unbind_pt_irq(XSM_HOOK, d, bind);
        if ( rc )
            return rc;

        if ( !irq_access_permitted(current->domain, irq) )
            return -EPERM;

        rc = release_guest_irq(d, virq);
        if ( rc )
            return rc;

        vgic_free_virq(d, virq);

        return 0;
    }

    case XEN_DOMCTL_vuart_op:
    {
        int rc;
        unsigned int i;
        struct xen_domctl_vuart_op *vuart_op = &domctl->u.vuart_op;

        /* check that structure padding must be 0. */
        for ( i = 0; i < sizeof(vuart_op->pad); i++ )
            if ( vuart_op->pad[i] )
                return -EINVAL;

        switch( vuart_op->cmd )
        {
        case XEN_DOMCTL_VUART_OP_INIT:
            rc = handle_vuart_init(d, vuart_op);
            break;

        default:
            rc = -EINVAL;
            break;
        }

        if ( !rc )
            rc = copy_to_guest(u_domctl, domctl, 1);

        return rc;
    }
    case XEN_DOMCTL_dt_overlay:
        return dt_overlay_domctl(d, &domctl->u.dt_overlay);
    default:
        return subarch_do_domctl(domctl, d, u_domctl);
    }
}

void arch_get_info_guest(struct vcpu *v, vcpu_guest_context_u c)
{
    struct vcpu_guest_context *ctxt = c.nat;
    struct vcpu_guest_core_regs *regs = &c.nat->user_regs;

    vcpu_regs_hyp_to_user(v, regs);

    ctxt->sctlr = v->arch.sctlr;
    ctxt->ttbr0 = v->arch.ttbr0;
    ctxt->ttbr1 = v->arch.ttbr1;
    ctxt->ttbcr = v->arch.ttbcr;

    if ( !test_bit(_VPF_down, &v->pause_flags) )
        ctxt->flags |= VGCF_online;
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

/* === END INLINED: domctl.c === */
/* === BEGIN INLINED: common_sysctl.c === */
#include <xen_xen_config.h>
/******************************************************************************
 * sysctl.c
 * 
 * System management operations. For use by node control stack.
 * 
 * Copyright (c) 2002-2006, K Fraser
 */

#include <xen_types.h>
#include <xen_lib.h>
#include <xen_mm.h>
#include <xen_sched.h>
#include <xen_domain.h>
#include <xen_event.h>
#include <xen_grant_table.h>
#include <xen_domain_page.h>
#include <xen_trace.h>
#include <xen_console.h>
#include <xen_iocap.h>
#include <xen_guest_access.h>
#include <xen_keyhandler.h>
#include <asm_current.h>
#include <xen_hypercall.h>
#include <public_sysctl.h>
#include <xen_nodemask.h>
#include <xen_numa.h>
#include <xsm_xsm.h>
#include <xen_pmstat.h>
#include <xen_livepatch.h>
#include <xen_coverage.h>

long do_sysctl(XEN_GUEST_HANDLE_PARAM(xen_sysctl_t) u_sysctl)
{
    long ret = 0;
    int copyback = -1;
    struct xen_sysctl curop, *op = &curop;
    static DEFINE_SPINLOCK(sysctl_lock);

    if ( copy_from_guest(op, u_sysctl, 1) )
        return -EFAULT;

    if ( op->interface_version != XEN_SYSCTL_INTERFACE_VERSION )
        return -EACCES;

    ret = xsm_sysctl(XSM_PRIV, op->cmd);
    if ( ret )
        return ret;

    /*
     * Trylock here avoids deadlock with an existing sysctl critical section
     * which might (for some current or future reason) want to synchronise
     * with this vcpu.
     */
    while ( !spin_trylock(&sysctl_lock) )
        if ( hypercall_preempt_check() )
            return hypercall_create_continuation(
                __HYPERVISOR_sysctl, "h", u_sysctl);

    switch ( op->cmd )
    {
    case XEN_SYSCTL_readconsole:
        ret = xsm_readconsole(XSM_HOOK, op->u.readconsole.clear);
        if ( ret )
            break;

        ret = read_console_ring(&op->u.readconsole);
        break;

    case XEN_SYSCTL_tbuf_op:
        ret = tb_control(&op->u.tbuf_op);
        break;

    case XEN_SYSCTL_sched_id:
        op->u.sched_id.sched_id = scheduler_id();
        break;

    case XEN_SYSCTL_getdomaininfolist:
    { 
        struct domain *d;
        struct xen_domctl_getdomaininfo info;
        u32 num_domains = 0;

        rcu_read_lock(&domlist_read_lock);

        for_each_domain ( d )
        {
            if ( d->domain_id < op->u.getdomaininfolist.first_domain )
                continue;
            if ( num_domains == op->u.getdomaininfolist.max_domains )
                break;

            if ( xsm_getdomaininfo(XSM_HOOK, d) )
                continue;

            getdomaininfo(d, &info);

            if ( copy_to_guest_offset(op->u.getdomaininfolist.buffer,
                                      num_domains, &info, 1) )
            {
                ret = -EFAULT;
                break;
            }
            
            num_domains++;
        }
        
        rcu_read_unlock(&domlist_read_lock);
        
        if ( ret != 0 )
            break;
        
        op->u.getdomaininfolist.num_domains = num_domains;
    }
    break;

#ifdef CONFIG_PERF_COUNTERS
    case XEN_SYSCTL_perfc_op:
        ret = perfc_control(&op->u.perfc_op);
        break;
#endif

#ifdef CONFIG_DEBUG_LOCK_PROFILE
    case XEN_SYSCTL_lockprof_op:
        ret = spinlock_profile_control(&op->u.lockprof_op);
        break;
#endif
    case XEN_SYSCTL_debug_keys:
    {
        char c;
        uint32_t i;

        ret = -EFAULT;
        for ( i = 0; i < op->u.debug_keys.nr_keys; i++ )
        {
            if ( copy_from_guest_offset(&c, op->u.debug_keys.keys, i, 1) )
                goto out;
            handle_keypress(c, false);
        }
        ret = 0;
        copyback = 0;
    }
    break;

    case XEN_SYSCTL_getcpuinfo:
    {
        uint32_t i, nr_cpus;
        struct xen_sysctl_cpuinfo cpuinfo = { 0 };

        nr_cpus = min(op->u.getcpuinfo.max_cpus, nr_cpu_ids);

        ret = -EFAULT;
        for ( i = 0; i < nr_cpus; i++ )
        {
            cpuinfo.idletime = get_cpu_idle_time(i);

            if ( copy_to_guest_offset(op->u.getcpuinfo.info, i, &cpuinfo, 1) )
                goto out;
        }

        op->u.getcpuinfo.nr_cpus = i;
        ret = 0;
    }
    break;

    case XEN_SYSCTL_availheap:
        op->u.availheap.avail_bytes = avail_domheap_pages_region(
            op->u.availheap.node,
            op->u.availheap.min_bitwidth,
            op->u.availheap.max_bitwidth);
        op->u.availheap.avail_bytes <<= PAGE_SHIFT;
        break;

#if defined (CONFIG_ACPI) && defined (CONFIG_HAS_CPUFREQ)
    case XEN_SYSCTL_get_pmstat:
        ret = do_get_pm_info(&op->u.get_pmstat);
        break;

    case XEN_SYSCTL_pm_op:
        ret = do_pm_op(&op->u.pm_op);
        if ( ret == -EAGAIN )
            copyback = 1;
        break;
#endif

    case XEN_SYSCTL_page_offline_op:
    {
        uint32_t *status, *ptr;
        mfn_t mfn;

        ret = -EINVAL;
        if ( op->u.page_offline.end < op->u.page_offline.start )
            break;

        ret = xsm_page_offline(XSM_HOOK, op->u.page_offline.cmd);
        if ( ret )
            break;

        ptr = status = xmalloc_array(uint32_t,
                                     (op->u.page_offline.end -
                                      op->u.page_offline.start + 1));
        if ( !status )
        {
            dprintk(XENLOG_WARNING, "Out of memory for page offline op\n");
            ret = -ENOMEM;
            break;
        }

        memset(status, PG_OFFLINE_INVALID, sizeof(uint32_t) *
                      (op->u.page_offline.end - op->u.page_offline.start + 1));

        for ( mfn = _mfn(op->u.page_offline.start);
              mfn_x(mfn) <= op->u.page_offline.end;
              mfn = mfn_add(mfn, 1) )
        {
            switch ( op->u.page_offline.cmd )
            {
                /* Shall revert her if failed, or leave caller do it? */
                case sysctl_page_offline:
                    ret = offline_page(mfn, 0, ptr++);
                    break;
                case sysctl_page_online:
                    ret = online_page(mfn, ptr++);
                    break;
                case sysctl_query_page_offline:
                    ret = query_page_offline(mfn, ptr++);
                    break;
                default:
                    ret = -EINVAL;
                    break;
            }

            if (ret)
                break;
        }

        if ( copy_to_guest(
                 op->u.page_offline.status, status,
                 op->u.page_offline.end - op->u.page_offline.start + 1) )
            ret = -EFAULT;

        xfree(status);
        copyback = 0;
    }
    break;

    case XEN_SYSCTL_cpupool_op:
        ret = cpupool_do_sysctl(&op->u.cpupool_op);
        break;

    case XEN_SYSCTL_scheduler_op:
        ret = sched_adjust_global(&op->u.scheduler_op);
        break;

    case XEN_SYSCTL_physinfo:
    {
        struct xen_sysctl_physinfo *pi = &op->u.physinfo;

        memset(pi, 0, sizeof(*pi));
        pi->threads_per_core =
            cpumask_weight(per_cpu(cpu_sibling_mask, 0));
        pi->cores_per_socket =
            cpumask_weight(per_cpu(cpu_core_mask, 0)) / pi->threads_per_core;
        pi->nr_cpus = num_online_cpus();
        pi->nr_nodes = num_online_nodes();
        pi->max_node_id = MAX_NUMNODES-1;
        pi->max_cpu_id = nr_cpu_ids - 1;
        pi->total_pages = total_pages;
        /* Protected by lock */
        get_outstanding_claims(&pi->free_pages, &pi->outstanding_pages);
        pi->scrub_pages = 0;
        pi->xen_cpu_khz = xen_cpu_khz;
        pi->max_mfn = get_upper_mfn_bound();
        arch_do_physinfo(pi);
        if ( iommu_enabled )
        {
            pi->capabilities |= XEN_SYSCTL_PHYSCAP_directio;
            if ( iommu_hap_pt_share )
                pi->capabilities |= XEN_SYSCTL_PHYSCAP_iommu_hap_pt_share;
        }
        if ( vmtrace_available )
            pi->capabilities |= XEN_SYSCTL_PHYSCAP_vmtrace;

        if ( vpmu_is_available )
            pi->capabilities |= XEN_SYSCTL_PHYSCAP_vpmu;

        if ( opt_gnttab_max_version >= 1 )
            pi->capabilities |= XEN_SYSCTL_PHYSCAP_gnttab_v1;
        if ( opt_gnttab_max_version >= 2 )
            pi->capabilities |= XEN_SYSCTL_PHYSCAP_gnttab_v2;

        if ( copy_to_guest(u_sysctl, op, 1) )
            ret = -EFAULT;
    }
    break;

    case XEN_SYSCTL_numainfo:
    {
        unsigned int i, j, num_nodes;
        struct xen_sysctl_numainfo *ni = &op->u.numainfo;
        bool do_meminfo = !guest_handle_is_null(ni->meminfo);
        bool do_distance = !guest_handle_is_null(ni->distance);

        num_nodes = last_node(node_online_map) + 1;

        if ( do_meminfo || do_distance )
        {
            struct xen_sysctl_meminfo meminfo = { };

            if ( num_nodes > ni->num_nodes )
                num_nodes = ni->num_nodes;
            for ( i = 0; i < num_nodes; ++i )
            {
                static uint32_t distance[MAX_NUMNODES];

                if ( do_meminfo )
                {
                    if ( node_online(i) )
                    {
                        meminfo.memsize = node_spanned_pages(i) << PAGE_SHIFT;
                        meminfo.memfree = avail_node_heap_pages(i) << PAGE_SHIFT;
                    }
                    else
                        meminfo.memsize = meminfo.memfree = XEN_INVALID_MEM_SZ;

                    if ( copy_to_guest_offset(ni->meminfo, i, &meminfo, 1) )
                    {
                        ret = -EFAULT;
                        break;
                    }
                }

                if ( do_distance )
                {
                    for ( j = 0; j < num_nodes; j++ )
                    {
                        distance[j] = __node_distance(i, j);
                        if ( distance[j] == NUMA_NO_DISTANCE )
                            distance[j] = XEN_INVALID_NODE_DIST;
                    }

                    if ( copy_to_guest_offset(ni->distance, i * num_nodes,
                                              distance, num_nodes) )
                    {
                        ret = -EFAULT;
                        break;
                    }
                }
            }
        }
        else
            i = num_nodes;

        if ( !ret && (ni->num_nodes != i) )
        {
            ni->num_nodes = i;
            if ( __copy_field_to_guest(u_sysctl, op,
                                       u.numainfo.num_nodes) )
            {
                ret = -EFAULT;
                break;
            }
        }
    }
    break;

    case XEN_SYSCTL_cputopoinfo:
    {
        unsigned int i, num_cpus;
        struct xen_sysctl_cputopoinfo *ti = &op->u.cputopoinfo;

        num_cpus = cpumask_last(&cpu_present_map) + 1;
        if ( !guest_handle_is_null(ti->cputopo) )
        {
            struct xen_sysctl_cputopo cputopo = { };

            if ( num_cpus > ti->num_cpus )
                num_cpus = ti->num_cpus;
            for ( i = 0; i < num_cpus; ++i )
            {
                if ( cpu_present(i) )
                {
                    cputopo.core = cpu_to_core(i);
                    cputopo.socket = cpu_to_socket(i);
                    cputopo.node = cpu_to_node(i);
                    if ( cputopo.node == NUMA_NO_NODE )
                        cputopo.node = XEN_INVALID_NODE_ID;
                }
                else
                {
                    cputopo.core = XEN_INVALID_CORE_ID;
                    cputopo.socket = XEN_INVALID_SOCKET_ID;
                    cputopo.node = XEN_INVALID_NODE_ID;
                }

                if ( copy_to_guest_offset(ti->cputopo, i, &cputopo, 1) )
                {
                    ret = -EFAULT;
                    break;
                }
            }
        }
        else
            i = num_cpus;

        if ( !ret && (ti->num_cpus != i) )
        {
            ti->num_cpus = i;
            if ( __copy_field_to_guest(u_sysctl, op,
                                       u.cputopoinfo.num_cpus) )
            {
                ret = -EFAULT;
                break;
            }
        }
    }
    break;

    case XEN_SYSCTL_coverage_op:
        ret = sysctl_cov_op(&op->u.coverage_op);
        copyback = 1;
        break;

#ifdef CONFIG_HAS_PCI
    case XEN_SYSCTL_pcitopoinfo:
    {
        struct xen_sysctl_pcitopoinfo *ti = &op->u.pcitopoinfo;
        unsigned int i = 0;

        if ( guest_handle_is_null(ti->devs) ||
             guest_handle_is_null(ti->nodes) )
        {
            ret = -EINVAL;
            break;
        }

        while ( i < ti->num_devs )
        {
            physdev_pci_device_t dev;
            uint32_t node;
            const struct pci_dev *pdev;

            if ( copy_from_guest_offset(&dev, ti->devs, i, 1) )
            {
                ret = -EFAULT;
                break;
            }

            pcidevs_lock();
            pdev = pci_get_pdev(NULL, PCI_SBDF(dev.seg, dev.bus, dev.devfn));
            if ( !pdev )
                node = XEN_INVALID_DEV;
            else if ( pdev->node == NUMA_NO_NODE )
                node = XEN_INVALID_NODE_ID;
            else
                node = pdev->node;
            pcidevs_unlock();

            if ( copy_to_guest_offset(ti->nodes, i, &node, 1) )
            {
                ret = -EFAULT;
                break;
            }

            if ( (++i > 0x3f) && hypercall_preempt_check() )
                break;
        }

        if ( !ret && (ti->num_devs != i) )
        {
            ti->num_devs = i;
            if ( __copy_field_to_guest(u_sysctl, op, u.pcitopoinfo.num_devs) )
                ret = -EFAULT;
        }
        break;
    }
#endif

    case XEN_SYSCTL_livepatch_op:
        ret = livepatch_op(&op->u.livepatch);
        if ( ret != -ENOSYS && ret != -EOPNOTSUPP )
            copyback = 1;
        break;

    default:
        ret = arch_do_sysctl(op, u_sysctl);
        copyback = 0;
        break;
    }

 out:
    spin_unlock(&sysctl_lock);

    if ( copyback && (!ret || copyback > 0) &&
         __copy_to_guest(u_sysctl, op, 1) )
        ret = -EFAULT;

    return ret;
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

/* === END INLINED: common_sysctl.c === */
/* === BEGIN INLINED: sysctl.c === */
#include <xen_xen_config.h>
/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 * Arch-specific sysctl.c
 *
 * System management operations. For use by node control stack.
 *
 * Copyright (c) 2012, Citrix Systems
 */

#include <xen_types.h>
#include <xen_lib.h>
#include <xen_dt-overlay.h>
#include <xen_errno.h>
#include <xen_hypercall.h>
#include <asm_arm64_sve.h>
#include <public_sysctl.h>

void arch_do_physinfo(struct xen_sysctl_physinfo *pi)
{
    pi->capabilities |= XEN_SYSCTL_PHYSCAP_hvm | XEN_SYSCTL_PHYSCAP_hap;

    pi->arch_capabilities |= MASK_INSR(sve_encode_vl(get_sys_vl_len()),
                                       XEN_SYSCTL_PHYSCAP_ARM_SVE_MASK);
}

long arch_do_sysctl(struct xen_sysctl *sysctl,
                    XEN_GUEST_HANDLE_PARAM(xen_sysctl_t) u_sysctl)
{
    long ret;

    switch ( sysctl->cmd )
    {
    case XEN_SYSCTL_dt_overlay:
        ret = dt_overlay_sysctl(&sysctl->u.dt_overlay);
        break;

    default:
        ret = -ENOSYS;
        break;
    }

    return ret;
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

/* === END INLINED: sysctl.c === */
/* === BEGIN INLINED: common_irq.c === */
#include <xen_xen_config.h>
#include <xen_irq.h>
#include <xen_errno.h>

DEFINE_PER_CPU(const struct cpu_user_regs *, irq_regs);

const hw_irq_controller no_irq_type = {
    .typename  = "none",
    .startup   = irq_startup_none,
    .shutdown  = irq_shutdown_none,
    .enable    = irq_enable_none,
    .disable   = irq_disable_none,
    .ack       = irq_ack_none,

#ifdef irq_end_none /* Hook is optional per arch */
    .end       = irq_end_none,
#endif
};

int init_one_irq_desc(struct irq_desc *desc)
{
    int err;

    if (irq_desc_initialized(desc))
        return 0;

    if ( !alloc_cpumask_var(&desc->affinity) )
        return -ENOMEM;

    desc->status = IRQ_DISABLED;
    desc->handler = &no_irq_type;
    spin_lock_init(&desc->lock);
    cpumask_setall(desc->affinity);
    INIT_LIST_HEAD(&desc->rl_link);

    err = arch_init_one_irq_desc(desc);
    if ( err )
    {
        free_cpumask_var(desc->affinity);
        desc->handler = NULL;
    }

    return err;
}


void cf_check irq_actor_none(struct irq_desc *desc)
{
}

unsigned int cf_check irq_startup_none(struct irq_desc *desc)
{
    return 0;
}

/* === END INLINED: common_irq.c === */
/* === BEGIN INLINED: common_monitor.c === */
#include <xen_xen_config.h>
/*
 * xen/common/monitor.c
 *
 * Common monitor_op domctl handler.
 *
 * Copyright (c) 2015 Tamas K Lengyel (tamas@tklengyel.com)
 * Copyright (c) 2016, Bitdefender S.R.L.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; If not, see <http://www.gnu.org/licenses/>.
 */

#include <xen_event.h>
#include <xen_monitor.h>
#include <xen_sched.h>
#include <xen_vm_event.h>
#include <xsm_xsm.h>
#include <asm_generic_altp2m.h>
#include <asm_monitor.h>
#include <asm_generic_vm_event.h>

int monitor_domctl(struct domain *d, struct xen_domctl_monitor_op *mop)
{
    int rc;
    bool requested_status = false;

    if ( unlikely(current->domain == d) ) /* no domain_pause() */
        return -EPERM;

    rc = xsm_vm_event_control(XSM_PRIV, d, mop->op, mop->event);
    if ( unlikely(rc) )
        return rc;

    switch ( mop->op )
    {
    case XEN_DOMCTL_MONITOR_OP_ENABLE:
        requested_status = true;
        /* fallthrough */
    case XEN_DOMCTL_MONITOR_OP_DISABLE:
        /* sanity check: avoid left-shift undefined behavior */
        if ( unlikely(mop->event > 31) )
            return -EINVAL;
        /* Check if event type is available. */
        if ( unlikely(!(arch_monitor_get_capabilities(d) & (1U << mop->event))) )
            return -EOPNOTSUPP;
        break;

    case XEN_DOMCTL_MONITOR_OP_GET_CAPABILITIES:
        mop->event = arch_monitor_get_capabilities(d);
        return 0;

    default:
        /* The monitor op is probably handled on the arch-side. */
        return arch_monitor_domctl_op(d, mop);
    }

    switch ( mop->event )
    {
    case XEN_DOMCTL_MONITOR_EVENT_GUEST_REQUEST:
    {
        bool old_status = d->monitor.guest_request_enabled;

        if ( unlikely(old_status == requested_status) )
            return -EEXIST;

        domain_pause(d);
        d->monitor.guest_request_sync = mop->u.guest_request.sync;
        d->monitor.guest_request_enabled = requested_status;
        arch_monitor_allow_userspace(d, mop->u.guest_request.allow_userspace);
        domain_unpause(d);
        break;
    }

    default:
        /* Give arch-side the chance to handle this event */
        return arch_monitor_domctl_event(d, mop);
    }

    return 0;
}

int monitor_traps(struct vcpu *v, bool sync, vm_event_request_t *req)
{
    int rc;
    struct domain *d = v->domain;

    rc = vm_event_claim_slot(d, d->vm_event_monitor);
    switch ( rc )
    {
    case 0:
        break;
    case -EOPNOTSUPP:
        /*
         * If there was no ring to handle the event, then
         * simply continue executing normally.
         */
        return 0;
    default:
        return rc;
    };

    req->vcpu_id = v->vcpu_id;

    if ( sync )
    {
        req->flags |= VM_EVENT_FLAG_VCPU_PAUSED;
        vm_event_sync_event(v, true);
        vm_event_vcpu_pause(v);
        rc = 1;
    }

    if ( altp2m_active(d) )
    {
        req->flags |= VM_EVENT_FLAG_ALTERNATE_P2M;
        req->altp2m_idx = altp2m_vcpu_idx(v);
    }

    vm_event_fill_regs(req);
    vm_event_put_request(d, d->vm_event_monitor, req);

    return rc;
}

void monitor_guest_request(void)
{
    struct vcpu *curr = current;
    struct domain *d = curr->domain;

    if ( d->monitor.guest_request_enabled )
    {
        vm_event_request_t req = {
            .reason = VM_EVENT_REASON_GUEST_REQUEST,
            .vcpu_id = curr->vcpu_id,
        };

        monitor_traps(curr, d->monitor.guest_request_sync, &req);
    }
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: common_monitor.c === */
/* === BEGIN INLINED: common_shutdown.c === */
#include <xen_xen_config.h>
#include <xen_init.h>
#include <xen_lib.h>
#include <xen_param.h>
#include <xen_sched.h>
#include <xen_domain.h>
#include <xen_delay.h>
#include <xen_watchdog.h>
#include <xen_shutdown.h>
#include <xen_console.h>
#include <xen_kexec.h>
#include <public_sched.h>

/* opt_noreboot: If true, machine will need manual reset on error. */
bool __read_mostly opt_noreboot;
boolean_param("noreboot", opt_noreboot);

static void noreturn reboot_or_halt(void)
{
    if ( opt_noreboot )
    {
        printk("'noreboot' set - not rebooting.\n");
        machine_halt();
    }
    else
    {
        printk("rebooting machine in 5 seconds.\n");
        watchdog_disable();
        machine_restart(5000);
    }
}

void hwdom_shutdown(u8 reason)
{
    switch ( reason )
    {
    case SHUTDOWN_poweroff:
        printk("Hardware Dom%u halted: halting machine\n",
               hardware_domain->domain_id);
        machine_halt();

    case SHUTDOWN_crash:
        printk("Hardware Dom%u crashed: ", hardware_domain->domain_id);
        kexec_crash(CRASHREASON_HWDOM);
        reboot_or_halt();

    case SHUTDOWN_reboot:
        printk("Hardware Dom%u shutdown: rebooting machine\n",
               hardware_domain->domain_id);
        machine_restart(0);

    case SHUTDOWN_watchdog:
        printk("Hardware Dom%u shutdown: watchdog rebooting machine\n",
               hardware_domain->domain_id);
        kexec_crash(CRASHREASON_WATCHDOG);
        machine_restart(0);

    case SHUTDOWN_soft_reset:
        printk("Hardware domain %d did unsupported soft reset, rebooting.\n",
               hardware_domain->domain_id);
        machine_restart(0);

    default:
        printk("Hardware Dom%u shutdown (unknown reason %u): ",
               hardware_domain->domain_id, reason);
        reboot_or_halt();
    }
}

/* === END INLINED: common_shutdown.c === */
/* === BEGIN INLINED: common_vm_event.c === */
#include <xen_xen_config.h>
/******************************************************************************
 * vm_event.c
 *
 * VM event support.
 *
 * Copyright (c) 2009 Citrix Systems, Inc. (Patrick Colp)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; If not, see <http://www.gnu.org/licenses/>.
 */


#include <xen_sched.h>
#include <xen_event.h>
#include <xen_wait.h>
#include <xen_vm_event.h>
#include <xen_mem_access.h>
#include <asm_p2m.h>
#include <asm_monitor.h>
#include <asm_generic_vm_event.h>

#ifdef CONFIG_MEM_SHARING
#include <asm/mem_sharing.h>
#endif

#include <xsm_xsm.h>
#include <public_hvm_params.h>

/* for public/io/ring.h macros */
#define xen_mb()   smp_mb()
#define xen_rmb()  smp_rmb()
#define xen_wmb()  smp_wmb()

static int vm_event_enable(
    struct domain *d,
    struct xen_domctl_vm_event_op *vec,
    struct vm_event_domain **p_ved,
    int pause_flag,
    int param,
    xen_event_channel_notification_t notification_fn)
{
    int rc;
    unsigned long ring_gfn = d->arch.hvm.params[param];
    struct vm_event_domain *ved;

    /*
     * Only one connected agent at a time.  If the helper crashed, the ring is
     * in an undefined state, and the guest is most likely unrecoverable.
     */
    if ( *p_ved != NULL )
        return -EBUSY;

    /* No chosen ring GFN?  Nothing we can do. */
    if ( ring_gfn == 0 )
        return -EOPNOTSUPP;

    ved = xzalloc(struct vm_event_domain);
    if ( !ved )
        return -ENOMEM;

    /* Trivial setup. */
    spin_lock_init(&ved->lock);
    init_waitqueue_head(&ved->wq);
    ved->pause_flag = pause_flag;

    rc = vm_event_init_domain(d);
    if ( rc < 0 )
        goto err;

    rc = prepare_ring_for_helper(d, ring_gfn, &ved->ring_pg_struct,
                                 &ved->ring_page);
    if ( rc < 0 )
        goto err;

    FRONT_RING_INIT(&ved->front_ring,
                    (vm_event_sring_t *)ved->ring_page,
                    PAGE_SIZE);

    rc = alloc_unbound_xen_event_channel(d, 0, current->domain->domain_id,
                                         notification_fn);
    if ( rc < 0 )
        goto err;

    ved->xen_port = vec->u.enable.port = rc;

    /* Success.  Fill in the domain's appropriate ved. */
    *p_ved = ved;

    return 0;

 err:
    destroy_ring_for_helper(&ved->ring_page, ved->ring_pg_struct);
    xfree(ved);

    return rc;
}

static unsigned int vm_event_ring_available(struct vm_event_domain *ved)
{
    int avail_req = RING_FREE_REQUESTS(&ved->front_ring);

    avail_req -= ved->target_producers;
    avail_req -= ved->foreign_producers;

    BUG_ON(avail_req < 0);

    return avail_req;
}

/*
 * vm_event_wake_blocked() will wakeup vcpus waiting for room in the
 * ring. These vCPUs were paused on their way out after placing an event,
 * but need to be resumed where the ring is capable of processing at least
 * one event from them.
 */
static void vm_event_wake_blocked(struct domain *d, struct vm_event_domain *ved)
{
    struct vcpu *v;
    unsigned int i, j, k, avail_req = vm_event_ring_available(ved);

    if ( avail_req == 0 || ved->blocked == 0 )
        return;

    /* We remember which vcpu last woke up to avoid scanning always linearly
     * from zero and starving higher-numbered vcpus under high load */
    for ( i = ved->last_vcpu_wake_up + 1, j = 0; j < d->max_vcpus; i++, j++ )
    {
        k = i % d->max_vcpus;
        v = d->vcpu[k];
        if ( !v )
            continue;

        if ( !ved->blocked || avail_req == 0 )
            break;

        if ( test_and_clear_bit(ved->pause_flag, &v->pause_flags) )
        {
            vcpu_unpause(v);
            avail_req--;
            ved->blocked--;
            ved->last_vcpu_wake_up = k;
        }
    }
}

/*
 * In the event that a vCPU attempted to place an event in the ring and
 * was unable to do so, it is queued on a wait queue.  These are woken as
 * needed, and take precedence over the blocked vCPUs.
 */
static void vm_event_wake_queued(struct domain *d, struct vm_event_domain *ved)
{
    unsigned int avail_req = vm_event_ring_available(ved);

    if ( avail_req > 0 )
        wake_up_nr(&ved->wq, avail_req);
}

/*
 * vm_event_wake() will wakeup all vcpus waiting for the ring to
 * become available.  If we have queued vCPUs, they get top priority. We
 * are guaranteed that they will go through code paths that will eventually
 * call vm_event_wake() again, ensuring that any blocked vCPUs will get
 * unpaused once all the queued vCPUs have made it through.
 */
static void vm_event_wake(struct domain *d, struct vm_event_domain *ved)
{
    if ( !list_empty(&ved->wq.list) )
        vm_event_wake_queued(d, ved);
    else
        vm_event_wake_blocked(d, ved);
}

static int vm_event_disable(struct domain *d, struct vm_event_domain **p_ved)
{
    struct vm_event_domain *ved = *p_ved;

    if ( vm_event_check_ring(ved) )
    {
        struct vcpu *v;

        spin_lock(&ved->lock);

        if ( !list_empty(&ved->wq.list) )
        {
            spin_unlock(&ved->lock);
            return -EBUSY;
        }

        /* Free domU's event channel and leave the other one unbound */
        free_xen_event_channel(d, ved->xen_port);

        /* Unblock all vCPUs */
        for_each_vcpu ( d, v )
        {
            if ( test_and_clear_bit(ved->pause_flag, &v->pause_flags) )
            {
                vcpu_unpause(v);
                ved->blocked--;
            }
        }

        destroy_ring_for_helper(&ved->ring_page, ved->ring_pg_struct);

        vm_event_cleanup_domain(d);

        spin_unlock(&ved->lock);
    }

    xfree(ved);
    *p_ved = NULL;

    return 0;
}

static void vm_event_release_slot(struct domain *d,
                                  struct vm_event_domain *ved)
{
    /* Update the accounting */
    if ( current->domain == d )
        ved->target_producers--;
    else
        ved->foreign_producers--;

    /* Kick any waiters */
    vm_event_wake(d, ved);
}

/*
 * vm_event_mark_and_pause() tags vcpu and put it to sleep.
 * The vcpu will resume execution in vm_event_wake_blocked().
 */
static void vm_event_mark_and_pause(struct vcpu *v, struct vm_event_domain *ved)
{
    if ( !test_and_set_bit(ved->pause_flag, &v->pause_flags) )
    {
        vcpu_pause_nosync(v);
        ved->blocked++;
    }
}

/*
 * This must be preceded by a call to claim_slot(), and is guaranteed to
 * succeed.  As a side-effect however, the vCPU may be paused if the ring is
 * overly full and its continued execution would cause stalling and excessive
 * waiting.  The vCPU will be automatically unpaused when the ring clears.
 */
void vm_event_put_request(struct domain *d,
                          struct vm_event_domain *ved,
                          vm_event_request_t *req)
{
    vm_event_front_ring_t *front_ring;
    int free_req;
    unsigned int avail_req;
    RING_IDX req_prod;
    struct vcpu *curr = current;

    if( !vm_event_check_ring(ved) )
        return;

    if ( curr->domain != d )
    {
        req->flags |= VM_EVENT_FLAG_FOREIGN;

        if ( !(req->flags & VM_EVENT_FLAG_VCPU_PAUSED) )
            gdprintk(XENLOG_WARNING, "d%dv%d was not paused.\n",
                     d->domain_id, req->vcpu_id);
    }

    req->version = VM_EVENT_INTERFACE_VERSION;

    spin_lock(&ved->lock);

    /* Due to the reservations, this step must succeed. */
    front_ring = &ved->front_ring;
    free_req = RING_FREE_REQUESTS(front_ring);
    ASSERT(free_req > 0);

    /* Copy request */
    req_prod = front_ring->req_prod_pvt;
    memcpy(RING_GET_REQUEST(front_ring, req_prod), req, sizeof(*req));
    req_prod++;

    /* Update ring */
    front_ring->req_prod_pvt = req_prod;
    RING_PUSH_REQUESTS(front_ring);

    /* We've actually *used* our reservation, so release the slot. */
    vm_event_release_slot(d, ved);

    /* Give this vCPU a black eye if necessary, on the way out.
     * See the comments above wake_blocked() for more information
     * on how this mechanism works to avoid waiting. */
    avail_req = vm_event_ring_available(ved);
    if( curr->domain == d && avail_req < d->max_vcpus &&
        !atomic_read(&curr->vm_event_pause_count) )
        vm_event_mark_and_pause(curr, ved);

    spin_unlock(&ved->lock);

    notify_via_xen_event_channel(d, ved->xen_port);
}

static int vm_event_get_response(struct domain *d, struct vm_event_domain *ved,
                                 vm_event_response_t *rsp)
{
    vm_event_front_ring_t *front_ring;
    RING_IDX rsp_cons;
    int rc = 0;

    spin_lock(&ved->lock);

    front_ring = &ved->front_ring;
    rsp_cons = front_ring->rsp_cons;

    if ( !RING_HAS_UNCONSUMED_RESPONSES(front_ring) )
        goto out;

    /* Copy response */
    memcpy(rsp, RING_GET_RESPONSE(front_ring, rsp_cons), sizeof(*rsp));
    rsp_cons++;

    /* Update ring */
    front_ring->rsp_cons = rsp_cons;
    front_ring->sring->rsp_event = rsp_cons + 1;

    /* Kick any waiters -- since we've just consumed an event,
     * there may be additional space available in the ring. */
    vm_event_wake(d, ved);

    rc = 1;

 out:
    spin_unlock(&ved->lock);

    return rc;
}

/*
 * Pull all responses from the given ring and unpause the corresponding vCPU
 * if required. Based on the response type, here we can also call custom
 * handlers.
 *
 * Note: responses are handled the same way regardless of which ring they
 * arrive on.
 */
static int vm_event_resume(struct domain *d, struct vm_event_domain *ved)
{
    vm_event_response_t rsp;

    /*
     * vm_event_resume() runs in either XEN_DOMCTL_VM_EVENT_OP_*, or
     * EVTCHN_send context from the introspection consumer. Both contexts
     * are guaranteed not to be the subject of vm_event responses.
     * While we could ASSERT(v != current) for each VCPU in d in the loop
     * below, this covers the case where we would need to iterate over all
     * of them more succintly.
     */
    ASSERT(d != current->domain);

    if ( unlikely(!vm_event_check_ring(ved)) )
         return -ENODEV;

    /* Pull all responses off the ring. */
    while ( vm_event_get_response(d, ved, &rsp) )
    {
        struct vcpu *v;

        if ( rsp.version != VM_EVENT_INTERFACE_VERSION )
        {
            printk(XENLOG_G_WARNING "vm_event interface version mismatch\n");
            continue;
        }

        /* Validate the vcpu_id in the response. */
        v = domain_vcpu(d, rsp.vcpu_id);
        if ( !v )
            continue;

        /*
         * In some cases the response type needs extra handling, so here
         * we call the appropriate handlers.
         */

        /* Check flags which apply only when the vCPU is paused */
        if ( atomic_read(&v->vm_event_pause_count) )
        {
#ifdef CONFIG_MEM_PAGING
            if ( rsp.reason == VM_EVENT_REASON_MEM_PAGING )
                p2m_mem_paging_resume(d, &rsp);
#endif
#ifdef CONFIG_MEM_SHARING
            if ( mem_sharing_is_fork(d) )
            {
                bool reset_state = rsp.flags & VM_EVENT_FLAG_RESET_FORK_STATE;
                bool reset_mem = rsp.flags & VM_EVENT_FLAG_RESET_FORK_MEMORY;

                if ( (reset_state || reset_mem) &&
                     mem_sharing_fork_reset(d, reset_state, reset_mem) )
                    ASSERT_UNREACHABLE();
            }
#endif

            /*
             * Check emulation flags in the arch-specific handler only, as it
             * has to set arch-specific flags when supported, and to avoid
             * bitmask overhead when it isn't supported.
             */
            vm_event_emulate_check(v, &rsp);

            /*
             * Check in arch-specific handler to avoid bitmask overhead when
             * not supported.
             */
            vm_event_register_write_resume(v, &rsp);

            /*
             * Check in arch-specific handler to avoid bitmask overhead when
             * not supported.
             */
            vm_event_toggle_singlestep(d, v, &rsp);

            /* Check for altp2m switch */
            if ( rsp.flags & VM_EVENT_FLAG_ALTERNATE_P2M )
                p2m_altp2m_check(v, rsp.altp2m_idx);

            if ( rsp.flags & VM_EVENT_FLAG_SET_REGISTERS )
                vm_event_set_registers(v, &rsp);

            if ( rsp.flags & VM_EVENT_FLAG_GET_NEXT_INTERRUPT )
                vm_event_monitor_next_interrupt(v);

            if ( rsp.flags & VM_EVENT_FLAG_RESET_VMTRACE )
                vm_event_reset_vmtrace(v);

            if ( rsp.flags & VM_EVENT_FLAG_VCPU_PAUSED )
                vm_event_vcpu_unpause(v);
        }
    }

    return 0;
}


static int vm_event_grab_slot(struct vm_event_domain *ved, int foreign)
{
    unsigned int avail_req;
    int rc;

    if ( !ved->ring_page )
        return -EOPNOTSUPP;

    spin_lock(&ved->lock);

    avail_req = vm_event_ring_available(ved);

    rc = -EBUSY;
    if ( avail_req == 0 )
        goto out;

    if ( !foreign )
        ved->target_producers++;
    else
        ved->foreign_producers++;

    rc = 0;

 out:
    spin_unlock(&ved->lock);

    return rc;
}

/* Simple try_grab wrapper for use in the wait_event() macro. */
static int vm_event_wait_try_grab(struct vm_event_domain *ved, int *rc)
{
    *rc = vm_event_grab_slot(ved, 0);

    return *rc;
}

/* Call vm_event_grab_slot() until the ring doesn't exist, or is available. */
static int vm_event_wait_slot(struct vm_event_domain *ved)
{
    int rc = -EBUSY;

    wait_event(ved->wq, vm_event_wait_try_grab(ved, &rc) != -EBUSY);

    return rc;
}

bool vm_event_check_ring(struct vm_event_domain *ved)
{
    return ved && ved->ring_page;
}

/*
 * Determines whether or not the current vCPU belongs to the target domain,
 * and calls the appropriate wait function.  If it is a guest vCPU, then we
 * use vm_event_wait_slot() to reserve a slot.  As long as there is a ring,
 * this function will always return 0 for a guest.  For a non-guest, we check
 * for space and return -EBUSY if the ring is not available.
 *
 * Return codes: -EOPNOTSUPP: the ring is not yet configured
 *               -EBUSY: the ring is busy
 *               0: a spot has been reserved
 *
 */
int __vm_event_claim_slot(struct domain *d, struct vm_event_domain *ved,
                          bool allow_sleep)
{
    if ( !vm_event_check_ring(ved) )
        return -EOPNOTSUPP;

    if ( (current->domain == d) && allow_sleep )
        return vm_event_wait_slot(ved);
    else
        return vm_event_grab_slot(ved, current->domain != d);
}

#ifdef CONFIG_MEM_PAGING
/* Registered with Xen-bound event channel for incoming notifications. */
static void cf_check mem_paging_notification(struct vcpu *v, unsigned int port)
{
    vm_event_resume(v->domain, v->domain->vm_event_paging);
}
#endif

/* Registered with Xen-bound event channel for incoming notifications. */
static void cf_check monitor_notification(struct vcpu *v, unsigned int port)
{
    vm_event_resume(v->domain, v->domain->vm_event_monitor);
}

#ifdef CONFIG_MEM_SHARING
/* Registered with Xen-bound event channel for incoming notifications. */
static void cf_check mem_sharing_notification(struct vcpu *v, unsigned int port)
{
    vm_event_resume(v->domain, v->domain->vm_event_share);
}
#endif

/* Clean up on domain destruction */
void vm_event_cleanup(struct domain *d)
{
#ifdef CONFIG_MEM_PAGING
    if ( vm_event_check_ring(d->vm_event_paging) )
    {
        /* Destroying the wait queue head means waking up all
         * queued vcpus. This will drain the list, allowing
         * the disable routine to complete. It will also drop
         * all domain refs the wait-queued vcpus are holding.
         * Finally, because this code path involves previously
         * pausing the domain (domain_kill), unpausing the
         * vcpus causes no harm. */
        destroy_waitqueue_head(&d->vm_event_paging->wq);
        (void)vm_event_disable(d, &d->vm_event_paging);
    }
#endif
    if ( vm_event_check_ring(d->vm_event_monitor) )
    {
        destroy_waitqueue_head(&d->vm_event_monitor->wq);
        (void)vm_event_disable(d, &d->vm_event_monitor);
    }
#ifdef CONFIG_MEM_SHARING
    if ( vm_event_check_ring(d->vm_event_share) )
    {
        destroy_waitqueue_head(&d->vm_event_share->wq);
        (void)vm_event_disable(d, &d->vm_event_share);
    }
#endif
}

int vm_event_domctl(struct domain *d, struct xen_domctl_vm_event_op *vec)
{
    int rc;

    if ( vec->op == XEN_VM_EVENT_GET_VERSION )
    {
        vec->u.version = VM_EVENT_INTERFACE_VERSION;
        return 0;
    }

    rc = xsm_vm_event_control(XSM_PRIV, d, vec->mode, vec->op);
    if ( rc )
        return rc;

    if ( unlikely(d == current->domain) ) /* no domain_pause() */
    {
        gdprintk(XENLOG_INFO, "Tried to do a memory event op on itself.\n");
        return -EINVAL;
    }

    if ( unlikely(d->is_dying) )
    {
        gdprintk(XENLOG_INFO, "Ignoring memory event op on dying domain %u\n",
                 d->domain_id);
        return 0;
    }

    if ( unlikely(d->vcpu == NULL) || unlikely(d->vcpu[0] == NULL) )
    {
        gdprintk(XENLOG_INFO,
                 "Memory event op on a domain (%u) with no vcpus\n",
                 d->domain_id);
        return -EINVAL;
    }

    rc = -ENOSYS;

    switch ( vec->mode )
    {
#ifdef CONFIG_MEM_PAGING
    case XEN_DOMCTL_VM_EVENT_OP_PAGING:
    {
        rc = -EINVAL;

        switch( vec->op )
        {
        case XEN_VM_EVENT_ENABLE:
        {
            rc = -EOPNOTSUPP;
            /* hvm fixme: p2m_is_foreign types need addressing */
            if ( is_hvm_domain(hardware_domain) )
                break;

            rc = -ENODEV;
            /* Only HAP is supported */
            if ( !hap_enabled(d) )
                break;

            /* No paging if iommu is used */
            rc = -EMLINK;
            if ( unlikely(is_iommu_enabled(d)) )
                break;

            rc = -EXDEV;
            /* Disallow paging in a PoD guest */
            if ( p2m_pod_active(d) )
                break;

            /* domain_pause() not required here, see XSA-99 */
            rc = vm_event_enable(d, vec, &d->vm_event_paging, _VPF_mem_paging,
                                 HVM_PARAM_PAGING_RING_PFN,
                                 mem_paging_notification);
        }
        break;

        case XEN_VM_EVENT_DISABLE:
            if ( vm_event_check_ring(d->vm_event_paging) )
            {
                domain_pause(d);
                rc = vm_event_disable(d, &d->vm_event_paging);
                domain_unpause(d);
            }
            break;

        case XEN_VM_EVENT_RESUME:
            rc = vm_event_resume(d, d->vm_event_paging);
            break;

        default:
            rc = -ENOSYS;
            break;
        }
    }
    break;
#endif

    case XEN_DOMCTL_VM_EVENT_OP_MONITOR:
    {
        rc = -EINVAL;

        switch( vec->op )
        {
        case XEN_VM_EVENT_ENABLE:
            /* domain_pause() not required here, see XSA-99 */
            rc = arch_monitor_init_domain(d);
            if ( rc )
                break;
            rc = vm_event_enable(d, vec, &d->vm_event_monitor, _VPF_mem_access,
                                 HVM_PARAM_MONITOR_RING_PFN,
                                 monitor_notification);
            break;

        case XEN_VM_EVENT_DISABLE:
            if ( vm_event_check_ring(d->vm_event_monitor) )
            {
                domain_pause(d);
                rc = vm_event_disable(d, &d->vm_event_monitor);
                arch_monitor_cleanup_domain(d);
                domain_unpause(d);
            }
            break;

        case XEN_VM_EVENT_RESUME:
            rc = vm_event_resume(d, d->vm_event_monitor);
            break;

        default:
            rc = -ENOSYS;
            break;
        }
    }
    break;

#ifdef CONFIG_MEM_SHARING
    case XEN_DOMCTL_VM_EVENT_OP_SHARING:
    {
        rc = -EINVAL;

        switch( vec->op )
        {
        case XEN_VM_EVENT_ENABLE:
            rc = -EOPNOTSUPP;
            /* hvm fixme: p2m_is_foreign types need addressing */
            if ( is_hvm_domain(hardware_domain) )
                break;

            rc = -ENODEV;
            /* Only HAP is supported */
            if ( !hap_enabled(d) )
                break;

            /* domain_pause() not required here, see XSA-99 */
            rc = vm_event_enable(d, vec, &d->vm_event_share, _VPF_mem_sharing,
                                 HVM_PARAM_SHARING_RING_PFN,
                                 mem_sharing_notification);
            break;

        case XEN_VM_EVENT_DISABLE:
            if ( vm_event_check_ring(d->vm_event_share) )
            {
                domain_pause(d);
                rc = vm_event_disable(d, &d->vm_event_share);
                domain_unpause(d);
            }
            break;

        case XEN_VM_EVENT_RESUME:
            rc = vm_event_resume(d, d->vm_event_share);
            break;

        default:
            rc = -ENOSYS;
            break;
        }
    }
    break;
#endif

    default:
        rc = -ENOSYS;
        break;
    }

    return rc;
}

void vm_event_vcpu_pause(struct vcpu *v)
{
    ASSERT(v == current);

    atomic_inc(&v->vm_event_pause_count);
    vcpu_pause_nosync(v);
}

void vm_event_vcpu_unpause(struct vcpu *v)
{
    int old, new, prev = v->vm_event_pause_count.counter;

    /*
     * All unpause requests as a result of toolstack responses.
     * Prevent underflow of the vcpu pause count.
     */
    do
    {
        old = prev;
        new = old - 1;

        if ( new < 0 )
        {
            printk(XENLOG_G_WARNING
                   "%pv vm_event: Too many unpause attempts\n", v);
            return;
        }

        prev = cmpxchg(&v->vm_event_pause_count.counter, old, new);
    } while ( prev != old );

    vcpu_unpause(v);
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: common_vm_event.c === */
/* === BEGIN INLINED: vm_event.c === */
#include <xen_xen_config.h>
/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/arm/vm_event.c
 *
 * Architecture-specific vm_event handling routines
 *
 * Copyright (c) 2016 Tamas K Lengyel (tamas.lengyel@zentific.com)
 */

#include <xen_sched.h>
#include <xen_vm_event.h>

void vm_event_fill_regs(vm_event_request_t *req)
{
    const struct cpu_user_regs *regs = guest_cpu_user_regs();

    req->data.regs.arm.cpsr = regs->cpsr;
    req->data.regs.arm.pc = regs->pc;
    req->data.regs.arm.ttbcr = READ_SYSREG(TCR_EL1);
    req->data.regs.arm.ttbr0 = READ_SYSREG64(TTBR0_EL1);
    req->data.regs.arm.ttbr1 = READ_SYSREG64(TTBR1_EL1);
}

void vm_event_set_registers(struct vcpu *v, vm_event_response_t *rsp)
{
    struct cpu_user_regs *regs = &v->arch.cpu_info->guest_cpu_user_regs;

    /* vCPU should be paused */
    ASSERT(atomic_read(&v->vm_event_pause_count));

    regs->pc = rsp->data.regs.arm.pc;
}

void vm_event_monitor_next_interrupt(struct vcpu *v)
{
    /* Not supported on ARM. */
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: vm_event.c === */
/* === BEGIN INLINED: monitor.c === */
#include <xen_xen_config.h>
/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/arm/monitor.c
 *
 * Arch-specific monitor_op domctl handler.
 *
 * Copyright (c) 2016 Tamas K Lengyel (tamas.lengyel@zentific.com)
 */

#include <xen_vm_event.h>
#include <xen_monitor.h>
#include <asm_monitor.h>
#include <asm_generic_vm_event.h>
#include <public_vm_event.h>

int arch_monitor_domctl_event(struct domain *d,
                              struct xen_domctl_monitor_op *mop)
{
    struct arch_domain *ad = &d->arch;
    bool requested_status = (XEN_DOMCTL_MONITOR_OP_ENABLE == mop->op);

    switch ( mop->event )
    {
    case XEN_DOMCTL_MONITOR_EVENT_PRIVILEGED_CALL:
    {
        bool old_status = ad->monitor.privileged_call_enabled;

        if ( unlikely(old_status == requested_status) )
            return -EEXIST;

        domain_pause(d);
        ad->monitor.privileged_call_enabled = requested_status;
        domain_unpause(d);
        break;
    }

    default:
        /*
         * Should not be reached unless arch_monitor_get_capabilities() is
         * not properly implemented.
         */
        ASSERT_UNREACHABLE();
        return -EOPNOTSUPP;
    }

    return 0;
}

int monitor_smc(void)
{
    vm_event_request_t req = {
        .reason = VM_EVENT_REASON_PRIVILEGED_CALL
    };

    return monitor_traps(current, 1, &req);
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: monitor.c === */
/* === BEGIN INLINED: multicall.c === */
#include <xen_xen_config.h>
/******************************************************************************
 * multicall.c
 */

#include <xen_types.h>
#include <xen_lib.h>
#include <xen_mm.h>
#include <xen_sched.h>
#include <xen_event.h>
#include <xen_multicall.h>
#include <xen_guest_access.h>
#include <xen_hypercall.h>
#include <xen_perfc.h>
#include <xen_trace.h>
#include <asm_current.h>
#include <asm_generic_hardirq.h>

#ifndef COMPAT
typedef long ret_t;
#define xlat_multicall_entry(mcs)

static void __trace_multicall_call(multicall_entry_t *call)
{
    __trace_hypercall(TRC_PV_HYPERCALL_SUBCALL, call->op, call->args);
}
#endif

static void trace_multicall_call(multicall_entry_t *call)
{
    if ( !tb_init_done )
        return;

    __trace_multicall_call(call);
}

ret_t do_multicall(
    XEN_GUEST_HANDLE_PARAM(multicall_entry_t) call_list, uint32_t nr_calls)
{
    struct vcpu *curr = current;
    struct mc_state *mcs = &curr->mc_state;
    uint32_t         i;
    int              rc = 0;
    enum mc_disposition disp = mc_continue;

    if ( unlikely(__test_and_set_bit(_MCSF_in_multicall, &mcs->flags)) )
    {
        gdprintk(XENLOG_INFO, "Multicall reentry is disallowed.\n");
        return -EINVAL;
    }

    if ( unlikely(!guest_handle_okay(call_list, nr_calls)) )
        rc = -EFAULT;

    for ( i = 0; !rc && disp == mc_continue && i < nr_calls; i++ )
    {
        if ( i && hypercall_preempt_check() )
            goto preempted;

        if ( unlikely(__copy_from_guest(&mcs->call, call_list, 1)) )
        {
            rc = -EFAULT;
            break;
        }

        trace_multicall_call(&mcs->call);

        disp = arch_do_multicall_call(mcs);

        /*
         * In the unlikely event that a hypercall has left interrupts,
         * spinlocks, or other things in a bad way, continuing the multicall
         * will typically lead to far more subtle issues to debug.
         */
        ASSERT_NOT_IN_ATOMIC();

#ifndef NDEBUG
        {
            /*
             * Deliberately corrupt the contents of the multicall structure.
             * The caller must depend only on the 'result' field on return.
             */
            struct multicall_entry corrupt;
            memset(&corrupt, 0xAA, sizeof(corrupt));
            (void)__copy_to_guest(call_list, &corrupt, 1);
        }
#endif

        if ( unlikely(disp == mc_exit) )
        {
            if ( __copy_field_to_guest(call_list, &mcs->call, result) )
                /* nothing, best effort only */;
            rc = mcs->call.result;
        }
        else if ( unlikely(__copy_field_to_guest(call_list, &mcs->call,
                                                 result)) )
            rc = -EFAULT;
        else if ( curr->hcall_preempted )
        {
            /* Translate sub-call continuation to guest layout */
            xlat_multicall_entry(mcs);

            /* Copy the sub-call continuation. */
            if ( likely(!__copy_to_guest(call_list, &mcs->call, 1)) )
                goto preempted;
            else
                hypercall_cancel_continuation(curr);
            rc = -EFAULT;
        }
        else
            guest_handle_add_offset(call_list, 1);
    }

    if ( unlikely(disp == mc_preempt) && i < nr_calls )
        goto preempted;

    perfc_add(calls_from_multicall, i);
    mcs->flags = 0;
    return rc;

 preempted:
    perfc_add(calls_from_multicall, i);
    mcs->flags = 0;
    return hypercall_create_continuation(
        __HYPERVISOR_multicall, "hi", call_list, nr_calls-i);
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

/* === END INLINED: multicall.c === */
/* === BEGIN INLINED: platform_hypercall.c === */
#include <xen_xen_config.h>
/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 * platform_hypercall.c
 *
 * Hardware platform operations. Intended for use by domain-0 kernel.
 *
 * Copyright (c) 2015, Citrix
 */

#include <xen_types.h>
#include <xen_sched.h>
#include <xen_guest_access.h>
#include <xen_hypercall.h>
#include <xen_spinlock.h>
#include <public_platform.h>
#include <xsm_xsm.h>
#include <asm_current.h>
#include <asm_event.h>

static DEFINE_SPINLOCK(xenpf_lock);

long do_platform_op(XEN_GUEST_HANDLE_PARAM(xen_platform_op_t) u_xenpf_op)
{
    long ret;
    struct xen_platform_op curop, *op = &curop;
    struct domain *d;

    if ( copy_from_guest(op, u_xenpf_op, 1) )
        return -EFAULT;

    if ( op->interface_version != XENPF_INTERFACE_VERSION )
        return -EACCES;

    d = rcu_lock_current_domain();
    if ( d == NULL )
        return -ESRCH;

    ret = xsm_platform_op(XSM_PRIV, op->cmd);
    if ( ret )
        return ret;

    /*
     * Trylock here avoids deadlock with an existing platform critical section
     * which might (for some current or future reason) want to synchronise
     * with this vcpu.
     */
    while ( !spin_trylock(&xenpf_lock) )
        if ( hypercall_preempt_check() )
            return hypercall_create_continuation(
                __HYPERVISOR_platform_op, "h", u_xenpf_op);

    switch ( op->cmd )
    {
    case XENPF_settime64:
        if ( likely(!op->u.settime64.mbz) )
            do_settime(op->u.settime64.secs,
                       op->u.settime64.nsecs,
                       op->u.settime64.system_time + SECONDS(d->time_offset.seconds));
        else
            ret = -EINVAL;
        break;

    default:
        ret = -ENOSYS;
        break;
    }

    spin_unlock(&xenpf_lock);
    rcu_unlock_domain(d);
    return ret;
}

/* === END INLINED: platform_hypercall.c === */
/* === BEGIN INLINED: physdev.c === */
#include <xen_xen_config.h>
/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 * Arch-specific physdev.c
 *
 * Copyright (c) 2012, Citrix Systems
 */

#include <xen_types.h>
#include <xen_lib.h>
#include <xen_errno.h>
#include <xen_sched.h>
#include <xen_hypercall.h>


int do_arm_physdev_op(int cmd, XEN_GUEST_HANDLE_PARAM(void) arg)
{
#ifdef CONFIG_HAS_PCI
    return pci_physdev_op(cmd, arg);
#else
    gdprintk(XENLOG_DEBUG, "PHYSDEVOP cmd=%d: not implemented\n", cmd);
    return -ENOSYS;
#endif
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: physdev.c === */
/* === BEGIN INLINED: shutdown.c === */
#include <xen_xen_config.h>
/* SPDX-License-Identifier: GPL-2.0 */
#include <xen_console.h>
#include <xen_cpu.h>
#include <xen_delay.h>
#include <xen_lib.h>
#include <xen_shutdown.h>
#include <xen_smp.h>
#include <asm_platform.h>
#include <asm_psci.h>

static void noreturn halt_this_cpu(void *arg)
{
    local_irq_disable();
    /* Make sure the write happens before we sleep forever */
    dsb(sy);
    isb();
    while ( 1 )
        wfi();
}

void machine_halt(void)
{
    int timeout = 10;

    watchdog_disable();
    console_start_sync();
    local_irq_enable();
    smp_call_function(halt_this_cpu, NULL, 0);
    local_irq_disable();

    /* Wait at most another 10ms for all other CPUs to go offline. */
    while ( (num_online_cpus() > 1) && (timeout-- > 0) )
        mdelay(1);

    /* This is mainly for PSCI-0.2, which does not return if success. */
    call_psci_system_off();

    /* Alternative halt procedure */
    platform_poweroff();
    halt_this_cpu(NULL);
}

void machine_restart(unsigned int delay_millisecs)
{
    int timeout = 10;
    unsigned long count = 0;

    watchdog_disable();
    console_start_sync();
    spin_debug_disable();

    local_irq_enable();
    smp_call_function(halt_this_cpu, NULL, 0);
    local_irq_disable();

    mdelay(delay_millisecs);

    /* Wait at most another 10ms for all other CPUs to go offline. */
    while ( (num_online_cpus() > 1) && (timeout-- > 0) )
        mdelay(1);

    /* This is mainly for PSCI-0.2, which does not return if success. */
    call_psci_system_reset();

    /* Alternative reset procedure */
    while ( 1 )
    {
        platform_reset();
        mdelay(100);
        if ( (count % 50) == 0 )
            printk(XENLOG_ERR "Xen: Platform reset did not work properly!\n");
        count++;
    }
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: shutdown.c === */
/* === BEGIN INLINED: vpsci.c === */
#include <xen_xen_config.h>
/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <xen_errno.h>
#include <xen_sched.h>
#include <xen_types.h>

#include <asm_current.h>
#include <asm_vgic.h>
#include <asm_vpsci.h>
#include <asm_event.h>

#include <public_sched.h>

static int do_common_cpu_on(register_t target_cpu, register_t entry_point,
                            register_t context_id)
{
    struct vcpu *v;
    struct domain *d = current->domain;
    struct vcpu_guest_context *ctxt;
    int rc;
    bool is_thumb = entry_point & 1;
    register_t vcpuid;

    vcpuid = vaffinity_to_vcpuid(target_cpu);

    if ( (v = domain_vcpu(d, vcpuid)) == NULL )
        return PSCI_INVALID_PARAMETERS;

    /* THUMB set is not allowed with 64-bit domain */
    if ( is_64bit_domain(d) && is_thumb )
        return PSCI_INVALID_ADDRESS;

    if ( !test_bit(_VPF_down, &v->pause_flags) )
        return PSCI_ALREADY_ON;

    if ( (ctxt = alloc_vcpu_guest_context()) == NULL )
        return PSCI_DENIED;

    vgic_clear_pending_irqs(v);

    memset(ctxt, 0, sizeof(*ctxt));
    ctxt->user_regs.pc64 = (u64) entry_point;
    ctxt->sctlr = SCTLR_GUEST_INIT;
    ctxt->ttbr0 = 0;
    ctxt->ttbr1 = 0;
    ctxt->ttbcr = 0; /* Defined Reset Value */

    /*
     * x0/r0_usr are always updated because for PSCI 0.1 the general
     * purpose registers are undefined upon CPU_on.
     */
    if ( is_32bit_domain(d) )
    {
        ctxt->user_regs.cpsr = PSR_GUEST32_INIT;
        /* Start the VCPU with THUMB set if it's requested by the kernel */
        if ( is_thumb )
        {
            ctxt->user_regs.cpsr |= PSR_THUMB;
            ctxt->user_regs.pc64 &= ~(u64)1;
        }

        ctxt->user_regs.r0_usr = context_id;
    }
#ifdef CONFIG_ARM_64
    else
    {
        ctxt->user_regs.cpsr = PSR_GUEST64_INIT;
        ctxt->user_regs.x0 = context_id;
    }
#endif
    ctxt->flags = VGCF_online;

    domain_lock(d);
    rc = arch_set_info_guest(v, ctxt);
    domain_unlock(d);

    free_vcpu_guest_context(ctxt);

    if ( rc < 0 )
        return PSCI_DENIED;

    vcpu_wake(v);

    return PSCI_SUCCESS;
}

static int32_t do_psci_cpu_on(uint32_t vcpuid, register_t entry_point)
{
    int32_t ret;

    ret = do_common_cpu_on(vcpuid, entry_point, 0);
    /*
     * PSCI 0.1 does not define the return codes PSCI_ALREADY_ON and
     * PSCI_INVALID_ADDRESS.
     * Instead, return PSCI_INVALID_PARAMETERS.
     */
    if ( ret == PSCI_ALREADY_ON || ret == PSCI_INVALID_ADDRESS )
        ret = PSCI_INVALID_PARAMETERS;

    return ret;
}

static int32_t do_psci_cpu_off(uint32_t power_state)
{
    struct vcpu *v = current;
    if ( !test_and_set_bit(_VPF_down, &v->pause_flags) )
        vcpu_sleep_nosync(v);
    return PSCI_SUCCESS;
}

static uint32_t do_psci_0_2_version(void)
{
    /*
     * PSCI is backward compatible from 0.2. So we can bump the version
     * without any issue.
     */
    return PSCI_VERSION(1, 1);
}

static register_t do_psci_0_2_cpu_suspend(uint32_t power_state,
                                          register_t entry_point,
                                          register_t context_id)
{
    struct vcpu *v = current;

    /*
     * Power off requests are treated as performing standby
     * as this simplifies Xen implementation.
     */

    vcpu_block_unless_event_pending(v);
    return PSCI_SUCCESS;
}

static int32_t do_psci_0_2_cpu_off(void)
{
    return do_psci_cpu_off(0);
}

static int32_t do_psci_0_2_cpu_on(register_t target_cpu,
                                  register_t entry_point,
                                  register_t context_id)
{
    return do_common_cpu_on(target_cpu, entry_point, context_id);
}

static const unsigned long target_affinity_mask[] = {
    ( MPIDR_HWID_MASK & AFFINITY_MASK( 0 ) ),
    ( MPIDR_HWID_MASK & AFFINITY_MASK( 1 ) ),
    ( MPIDR_HWID_MASK & AFFINITY_MASK( 2 ) )
#ifdef CONFIG_ARM_64
    ,( MPIDR_HWID_MASK & AFFINITY_MASK( 3 ) )
#endif
};

static int32_t do_psci_0_2_affinity_info(register_t target_affinity,
                                         uint32_t lowest_affinity_level)
{
    struct domain *d = current->domain;
    struct vcpu *v;
    uint32_t vcpuid;
    unsigned long tmask;

    if ( lowest_affinity_level < ARRAY_SIZE(target_affinity_mask) )
    {
        tmask = target_affinity_mask[lowest_affinity_level];
        target_affinity &= tmask;
    }
    else
        return PSCI_INVALID_PARAMETERS;

    for ( vcpuid = 0; vcpuid < d->max_vcpus; vcpuid++ )
    {
        v = d->vcpu[vcpuid];

        if ( ( ( v->arch.vmpidr & tmask ) == target_affinity )
                && ( !test_bit(_VPF_down, &v->pause_flags) ) )
            return PSCI_0_2_AFFINITY_LEVEL_ON;
    }

    return PSCI_0_2_AFFINITY_LEVEL_OFF;
}

static int32_t do_psci_0_2_migrate_info_type(void)
{
    return PSCI_0_2_TOS_MP_OR_NOT_PRESENT;
}

static void do_psci_0_2_system_off( void )
{
    struct domain *d = current->domain;
    domain_shutdown(d,SHUTDOWN_poweroff);
}

static void do_psci_0_2_system_reset(void)
{
    struct domain *d = current->domain;
    domain_shutdown(d,SHUTDOWN_reboot);
}

static int32_t do_psci_1_0_features(uint32_t psci_func_id)
{
    /* /!\ Ordered by function ID and not name */
    switch ( psci_func_id )
    {
    case PSCI_0_2_FN32_PSCI_VERSION:
    case PSCI_0_2_FN32_CPU_SUSPEND:
    case PSCI_0_2_FN64_CPU_SUSPEND:
    case PSCI_0_2_FN32_CPU_OFF:
    case PSCI_0_2_FN32_CPU_ON:
    case PSCI_0_2_FN64_CPU_ON:
    case PSCI_0_2_FN32_AFFINITY_INFO:
    case PSCI_0_2_FN64_AFFINITY_INFO:
    case PSCI_0_2_FN32_MIGRATE_INFO_TYPE:
    case PSCI_0_2_FN32_SYSTEM_OFF:
    case PSCI_0_2_FN32_SYSTEM_RESET:
    case PSCI_1_0_FN32_PSCI_FEATURES:
    case ARM_SMCCC_VERSION_FID:
        return 0;
    default:
        return PSCI_NOT_SUPPORTED;
    }
}

#define PSCI_SET_RESULT(reg, val) set_user_reg(reg, 0, val)
#define PSCI_ARG(reg, n) get_user_reg(reg, n)

#ifdef CONFIG_ARM_64
#define PSCI_ARG32(reg, n) (uint32_t)(get_user_reg(reg, n))
#else
#define PSCI_ARG32(reg, n) PSCI_ARG(reg, n)
#endif

/*
 * PSCI 0.1 calls. It will return false if the function ID is not
 * handled.
 */
bool do_vpsci_0_1_call(struct cpu_user_regs *regs, uint32_t fid)
{
    switch ( (uint32_t)get_user_reg(regs, 0) )
    {
    case PSCI_cpu_off:
    {
        uint32_t pstate = PSCI_ARG32(regs, 1);

        perfc_incr(vpsci_cpu_off);
        PSCI_SET_RESULT(regs, do_psci_cpu_off(pstate));
        return true;
    }
    case PSCI_cpu_on:
    {
        uint32_t vcpuid = PSCI_ARG32(regs, 1);
        register_t epoint = PSCI_ARG(regs, 2);

        perfc_incr(vpsci_cpu_on);
        PSCI_SET_RESULT(regs, do_psci_cpu_on(vcpuid, epoint));
        return true;
    }
    default:
        return false;
    }
}

/*
 * PSCI 0.2 or later calls. It will return false if the function ID is
 * not handled.
 */
bool do_vpsci_0_2_call(struct cpu_user_regs *regs, uint32_t fid)
{
    /*
     * /!\ VPSCI_NR_FUNCS (in asm/vpsci.h) should be updated when
     * adding/removing a function. SCCC_SMCCC_*_REVISION should be
     * updated once per release.
     */
    switch ( fid )
    {
    case PSCI_0_2_FN32_PSCI_VERSION:
        perfc_incr(vpsci_version);
        PSCI_SET_RESULT(regs, do_psci_0_2_version());
        return true;

    case PSCI_0_2_FN32_CPU_OFF:
        perfc_incr(vpsci_cpu_off);
        PSCI_SET_RESULT(regs, do_psci_0_2_cpu_off());
        return true;

    case PSCI_0_2_FN32_MIGRATE_INFO_TYPE:
        perfc_incr(vpsci_migrate_info_type);
        PSCI_SET_RESULT(regs, do_psci_0_2_migrate_info_type());
        return true;

    case PSCI_0_2_FN32_SYSTEM_OFF:
        perfc_incr(vpsci_system_off);
        do_psci_0_2_system_off();
        PSCI_SET_RESULT(regs, PSCI_INTERNAL_FAILURE);
        return true;

    case PSCI_0_2_FN32_SYSTEM_RESET:
        perfc_incr(vpsci_system_reset);
        do_psci_0_2_system_reset();
        PSCI_SET_RESULT(regs, PSCI_INTERNAL_FAILURE);
        return true;

    case PSCI_0_2_FN32_CPU_ON:
    case PSCI_0_2_FN64_CPU_ON:
    {
        register_t vcpuid = PSCI_ARG(regs, 1);
        register_t epoint = PSCI_ARG(regs, 2);
        register_t cid = PSCI_ARG(regs, 3);

        perfc_incr(vpsci_cpu_on);
        PSCI_SET_RESULT(regs, do_psci_0_2_cpu_on(vcpuid, epoint, cid));
        return true;
    }

    case PSCI_0_2_FN32_CPU_SUSPEND:
    case PSCI_0_2_FN64_CPU_SUSPEND:
    {
        uint32_t pstate = PSCI_ARG32(regs, 1);
        register_t epoint = PSCI_ARG(regs, 2);
        register_t cid = PSCI_ARG(regs, 3);

        perfc_incr(vpsci_cpu_suspend);
        PSCI_SET_RESULT(regs, do_psci_0_2_cpu_suspend(pstate, epoint, cid));
        return true;
    }

    case PSCI_0_2_FN32_AFFINITY_INFO:
    case PSCI_0_2_FN64_AFFINITY_INFO:
    {
        register_t taff = PSCI_ARG(regs, 1);
        uint32_t laff = PSCI_ARG32(regs, 2);

        perfc_incr(vpsci_cpu_affinity_info);
        PSCI_SET_RESULT(regs, do_psci_0_2_affinity_info(taff, laff));
        return true;
    }

    case PSCI_1_0_FN32_PSCI_FEATURES:
    {
        uint32_t psci_func_id = PSCI_ARG32(regs, 1);

        perfc_incr(vpsci_features);
        PSCI_SET_RESULT(regs, do_psci_1_0_features(psci_func_id));
        return true;
    }

    default:
        return false;
    }
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: vpsci.c === */
/* === BEGIN INLINED: vsmc.c === */
#include <xen_xen_config.h>
#include <xen_xen_config.h>
/* SPDX-License-Identifier: GPL-2.0 */
/*
 * xen/arch/arm/vsmc.c
 *
 * Generic handler for SMC and HVC calls according to
 * ARM SMC calling convention
 */

#include <xen_xen_config.h>

#include <xen_lib.h>
#include <xen_types.h>
#include <public_arch_arm_smccc.h>
#include <asm_cpuerrata.h>
#include <asm_cpufeature.h>
#include <asm_monitor.h>
#include <asm_regs.h>
#include <asm_smccc.h>
#include <asm_tee_ffa.h>
#include <asm_tee_tee.h>
#include <asm_traps.h>
#include <asm_vpsci.h>
#include <asm_platform.h>

/* Number of functions currently supported by Hypervisor Service. */
#define XEN_SMCCC_FUNCTION_COUNT 3

/* Number of functions currently supported by Standard Service Service Calls. */
#define SSSC_SMCCC_FUNCTION_COUNT (3 + VPSCI_NR_FUNCS + FFA_NR_FUNCS)

static bool fill_uid(struct cpu_user_regs *regs, xen_uuid_t uuid)
{
    int n;

    /*
     * UID is returned in registers r0..r3, four bytes per register,
     * first byte is stored in low-order bits of a register.
     * (ARM DEN 0028B page 14)
     */
    for (n = 0; n < 4; n++)
    {
        const uint8_t *bytes = uuid.a + n * 4;
        uint32_t r;

        r = bytes[0];
        r |= bytes[1] << 8;
        r |= bytes[2] << 16;
        r |= bytes[3] << 24;

        set_user_reg(regs, n, r);
    }

    return true;
}

static bool fill_revision(struct cpu_user_regs *regs, uint32_t major,
                         uint32_t minor)
{
    /*
     * Revision is returned in registers r0 and r1.
     * r0 stores major part of the version
     * r1 stores minor part of the version
     * (ARM DEN 0028B page 15)
     */
    set_user_reg(regs, 0, major);
    set_user_reg(regs, 1, minor);

    return true;
}

static bool fill_function_call_count(struct cpu_user_regs *regs, uint32_t cnt)
{
    /*
     * Function call count is retuned as any other return value in register r0
     * (ARM DEN 0028B page 17)
     */
    set_user_reg(regs, 0, cnt);

    return true;
}

/* SMCCC interface for ARM Architecture */
static bool handle_arch(struct cpu_user_regs *regs)
{
    uint32_t fid = (uint32_t)get_user_reg(regs, 0);

    switch ( fid )
    {
    case ARM_SMCCC_VERSION_FID:
        set_user_reg(regs, 0, ARM_SMCCC_VERSION_1_2);
        return true;

    case ARM_SMCCC_ARCH_FEATURES_FID:
    {
        uint32_t arch_func_id = get_user_reg(regs, 1);
        int ret = ARM_SMCCC_NOT_SUPPORTED;

        switch ( arch_func_id )
        {
        case ARM_SMCCC_ARCH_WORKAROUND_1_FID:
            /*
             * Workaround 3 is also mitigating spectre v2 so advertise that we
             * support Workaround 1 if we do Workaround 3 on exception entry.
             */
            if ( cpus_have_cap(ARM_HARDEN_BRANCH_PREDICTOR) ||
                 cpus_have_cap(ARM_WORKAROUND_BHB_SMCC_3) )
                ret = ARM_SMCCC_SUCCESS;
            break;
        case ARM_SMCCC_ARCH_WORKAROUND_2_FID:
            switch ( get_ssbd_state() )
            {
            case ARM_SSBD_UNKNOWN:
            case ARM_SSBD_FORCE_DISABLE:
                break;

            case ARM_SSBD_RUNTIME:
                ret = ARM_SMCCC_SUCCESS;
                break;

            case ARM_SSBD_FORCE_ENABLE:
            case ARM_SSBD_MITIGATED:
                ret = ARM_SMCCC_NOT_REQUIRED;
                break;
            }
            break;
        case ARM_SMCCC_ARCH_WORKAROUND_3_FID:
            if ( cpus_have_cap(ARM_WORKAROUND_BHB_SMCC_3) )
                ret = ARM_SMCCC_SUCCESS;
            break;
        }

        set_user_reg(regs, 0, ret);

        return true;
    }

    case ARM_SMCCC_ARCH_WORKAROUND_1_FID:
    case ARM_SMCCC_ARCH_WORKAROUND_3_FID:
        /* No return value */
        return true;

    case ARM_SMCCC_ARCH_WORKAROUND_2_FID:
    {
        bool enable = (uint32_t)get_user_reg(regs, 1);

        /*
         * ARM_WORKAROUND_2_FID should only be called when mitigation
         * state can be changed at runtime.
         */
        if ( unlikely(get_ssbd_state() != ARM_SSBD_RUNTIME) )
            return true;

        if ( enable )
            get_cpu_info()->flags |= CPUINFO_WORKAROUND_2_FLAG;
        else
            get_cpu_info()->flags &= ~CPUINFO_WORKAROUND_2_FLAG;

        return true;
    }
    }

    return false;
}

/* SMCCC interface for hypervisor. Tell about itself. */
static bool handle_hypervisor(struct cpu_user_regs *regs)
{
    uint32_t fid = (uint32_t)get_user_reg(regs, 0);

    switch ( fid )
    {
    case ARM_SMCCC_CALL_COUNT_FID(HYPERVISOR):
        return fill_function_call_count(regs, XEN_SMCCC_FUNCTION_COUNT);
    case ARM_SMCCC_CALL_UID_FID(HYPERVISOR):
        return fill_uid(regs, XEN_SMCCC_UID);
    case ARM_SMCCC_REVISION_FID(HYPERVISOR):
        return fill_revision(regs, XEN_SMCCC_MAJOR_REVISION,
                             XEN_SMCCC_MINOR_REVISION);
    default:
        return false;
    }
}

/* Existing (pre SMCCC) APIs. This includes PSCI 0.1 interface */
static bool handle_existing_apis(struct cpu_user_regs *regs)
{
    /* Only least 32 bits are significant (ARM DEN 0028B, page 12) */
    uint32_t fid = (uint32_t)get_user_reg(regs, 0);

    return do_vpsci_0_1_call(regs, fid);
}

static bool is_psci_fid(uint32_t fid)
{
    uint32_t fn = fid & ARM_SMCCC_FUNC_MASK;

    return fn >= PSCI_FNUM_MIN_VALUE && fn <= PSCI_FNUM_MAX_VALUE;
}

/* PSCI 0.2 interface and other Standard Secure Calls */
static bool handle_sssc(struct cpu_user_regs *regs)
{
    uint32_t fid = (uint32_t)get_user_reg(regs, 0);

    if ( is_psci_fid(fid) )
        return do_vpsci_0_2_call(regs, fid);

    if ( is_ffa_fid(fid) )
        return tee_handle_call(regs);

    switch ( fid )
    {
    case ARM_SMCCC_CALL_COUNT_FID(STANDARD):
        return fill_function_call_count(regs, SSSC_SMCCC_FUNCTION_COUNT);

    case ARM_SMCCC_CALL_UID_FID(STANDARD):
        return fill_uid(regs, SSSC_SMCCC_UID);

    case ARM_SMCCC_REVISION_FID(STANDARD):
        return fill_revision(regs, SSSC_SMCCC_MAJOR_REVISION,
                             SSSC_SMCCC_MINOR_REVISION);

    default:
        return false;
    }
}

/*
 * vsmccc_handle_call() - handle SMC/HVC call according to ARM SMCCC.
 * returns true if that was valid SMCCC call (even if function number
 * was unknown).
 */
static bool vsmccc_handle_call(struct cpu_user_regs *regs)
{
    bool handled = false;
    const union hsr hsr = { .bits = regs->hsr };
    uint32_t funcid = get_user_reg(regs, 0);

    /*
     * Check immediate value for HVC32, HVC64 and SMC64.
     * It is not so easy to check immediate value for SMC32,
     * because it is not stored in HSR.ISS field. To get immediate
     * value we need to disassemble instruction at current pc, which
     * is expensive. So we will assume that it is 0x0.
     */
    switch ( hsr.ec )
    {
    case HSR_EC_HVC32:
#ifdef CONFIG_ARM_64
    case HSR_EC_HVC64:
    case HSR_EC_SMC64:
#endif
        if ( (hsr.iss & HSR_XXC_IMM_MASK) != 0)
            return false;
        break;
    case HSR_EC_SMC32:
        break;
    default:
        return false;
    }

    /* 64 bit calls are allowed only from 64 bit domains. */
    if ( smccc_is_conv_64(funcid) && is_32bit_domain(current->domain) )
    {
        set_user_reg(regs, 0, ARM_SMCCC_ERR_UNKNOWN_FUNCTION);
        return true;
    }

    /*
     * Special case: identifier range for existing APIs.
     * This range is described in SMCCC (ARM DEN 0028B, page 16),
     * but it does not conforms to standard function identifier
     * encoding.
     */
    if ( funcid >= ARM_SMCCC_RESERVED_RANGE_START &&
         funcid <= ARM_SMCCC_RESERVED_RANGE_END )
        handled = handle_existing_apis(regs);
    else
    {
        switch ( smccc_get_owner(funcid) )
        {
        case ARM_SMCCC_OWNER_ARCH:
            handled = handle_arch(regs);
            break;
        case ARM_SMCCC_OWNER_HYPERVISOR:
            handled = handle_hypervisor(regs);
            break;
        case ARM_SMCCC_OWNER_STANDARD:
            handled = handle_sssc(regs);
            break;
        case ARM_SMCCC_OWNER_SIP:
            handled = platform_smc(regs);
            break;
        case ARM_SMCCC_OWNER_TRUSTED_APP ... ARM_SMCCC_OWNER_TRUSTED_APP_END:
        case ARM_SMCCC_OWNER_TRUSTED_OS ... ARM_SMCCC_OWNER_TRUSTED_OS_END:
            handled = tee_handle_call(regs);
            break;
        }
    }

    if ( !handled )
    {
        gprintk(XENLOG_INFO, "Unhandled SMC/HVC: %#x\n", funcid);

        /* Inform caller that function is not supported. */
        set_user_reg(regs, 0, ARM_SMCCC_ERR_UNKNOWN_FUNCTION);
    }

    return true;
}

void do_trap_smc(struct cpu_user_regs *regs, const union hsr hsr)
{
    int rc = 0;

    if ( !check_conditional_instr(regs, hsr) )
    {
        advance_pc(regs, hsr);
        return;
    }

    /* If monitor is enabled, let it handle the call. */
    if ( current->domain->arch.monitor.privileged_call_enabled )
        rc = monitor_smc();

    if ( rc == 1 )
        return;

    /*
     * Use standard routines to handle the call.
     * vsmccc_handle_call() will return false if this call is not
     * SMCCC compatible (e.g. immediate value != 0). As it is not
     * compatible, we can't be sure that guest will understand
     * ARM_SMCCC_ERR_UNKNOWN_FUNCTION.
     */
    if ( vsmccc_handle_call(regs) )
        advance_pc(regs, hsr);
    else
        inject_undef_exception(regs, hsr);
}

void do_trap_hvc_smccc(struct cpu_user_regs *regs)
{
    const union hsr hsr = { .bits = regs->hsr };

    /*
     * vsmccc_handle_call() will return false if this call is not
     * SMCCC compatible (e.g. immediate value != 0). As it is not
     * compatible, we can't be sure that guest will understand
     * ARM_SMCCC_ERR_UNKNOWN_FUNCTION.
     */
    if ( !vsmccc_handle_call(regs) )
        inject_undef_exception(regs, hsr);
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: vsmc.c === */
/* === BEGIN INLINED: psci.c === */
#include <xen_xen_config.h>
/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * xen/arch/arm/psci.c
 *
 * PSCI host support
 *
 * Andre Przywara <andre.przywara@linaro.org>
 * Copyright (c) 2013 Linaro Limited.
 */


#include <xen_acpi.h>
#include <xen_types.h>
#include <xen_init.h>
#include <xen_mm.h>
#include <xen_smp.h>
#include <asm_cpufeature.h>
#include <asm_psci.h>
#include <asm_acpi.h>

/*
 * While a 64-bit OS can make calls with SMC32 calling conventions, for
 * some calls it is necessary to use SMC64 to pass or return 64-bit values.
 * For such calls PSCI_0_2_FN_NATIVE(x) will choose the appropriate
 * (native-width) function ID.
 */
#ifdef CONFIG_ARM_64
#define PSCI_0_2_FN_NATIVE(name)    PSCI_0_2_FN64_##name
#else
#define PSCI_0_2_FN_NATIVE(name)    PSCI_0_2_FN32_##name
#endif

uint32_t psci_ver;
uint32_t smccc_ver;

static uint32_t psci_cpu_on_nr;

#define PSCI_RET(res)   ((int32_t)(res).a0)

int call_psci_cpu_on(int cpu)
{
    struct arm_smccc_res res;

    arm_smccc_smc(psci_cpu_on_nr, cpu_logical_map(cpu), __pa(init_secondary),
                  &res);

    return PSCI_RET(res);
}

void call_psci_cpu_off(void)
{
    if ( psci_ver > PSCI_VERSION(0, 1) )
    {
        struct arm_smccc_res res;

        /* If successfull the PSCI cpu_off call doesn't return */
        arm_smccc_smc(PSCI_0_2_FN32_CPU_OFF, &res);
        panic("PSCI cpu off failed for CPU%d err=%d\n", smp_processor_id(),
              PSCI_RET(res));
    }
}

void call_psci_system_off(void)
{
    if ( psci_ver > PSCI_VERSION(0, 1) )
        arm_smccc_smc(PSCI_0_2_FN32_SYSTEM_OFF, NULL);
}

void call_psci_system_reset(void)
{
    if ( psci_ver > PSCI_VERSION(0, 1) )
        arm_smccc_smc(PSCI_0_2_FN32_SYSTEM_RESET, NULL);
}

static int __init psci_features(uint32_t psci_func_id)
{
    struct arm_smccc_res res;

    if ( psci_ver < PSCI_VERSION(1, 0) )
        return PSCI_NOT_SUPPORTED;

    arm_smccc_smc(PSCI_1_0_FN32_PSCI_FEATURES, psci_func_id, &res);

    return PSCI_RET(res);
}

static int __init psci_is_smc_method(const struct dt_device_node *psci)
{
    int ret;
    const char *prop_str;

    ret = dt_property_read_string(psci, "method", &prop_str);
    if ( ret )
    {
        printk("/psci node does not provide a method (%d)\n", ret);
        return -EINVAL;
    }

    /* Since Xen runs in HYP all of the time, it does not make sense to
     * let it call into HYP for PSCI handling, since the handler just
     * won't be there. So bail out with an error if "smc" is not used.
     */
    if ( strcmp(prop_str, "smc") )
    {
        printk("/psci method must be smc, but is: \"%s\"\n", prop_str);
        return -EINVAL;
    }

    return 0;
}

static void __init psci_init_smccc(void)
{
    /* PSCI is using at least SMCCC 1.0 calling convention. */
    smccc_ver = ARM_SMCCC_VERSION_1_0;

    if ( psci_features(ARM_SMCCC_VERSION_FID) != PSCI_NOT_SUPPORTED )
    {
        struct arm_smccc_res res;

        arm_smccc_smc(ARM_SMCCC_VERSION_FID, &res);
        if ( PSCI_RET(res) != ARM_SMCCC_NOT_SUPPORTED )
            smccc_ver = PSCI_RET(res);
    }

    if ( smccc_ver >= SMCCC_VERSION(1, 1) )
        cpus_set_cap(ARM_SMCCC_1_1);

    printk(XENLOG_INFO "Using SMC Calling Convention v%u.%u\n",
           SMCCC_VERSION_MAJOR(smccc_ver), SMCCC_VERSION_MINOR(smccc_ver));
}

static int __init psci_init_0_1(void)
{
    int ret;
    const struct dt_device_node *psci;

    if ( !acpi_disabled )
    {
        printk("PSCI 0.1 is not supported when using ACPI\n");
        return -EINVAL;
    }

    psci = dt_find_compatible_node(NULL, NULL, "arm,psci");
    if ( !psci )
        return -EOPNOTSUPP;

    ret = psci_is_smc_method(psci);
    if ( ret )
        return -EINVAL;

    if ( !dt_property_read_u32(psci, "cpu_on", &psci_cpu_on_nr) )
    {
        printk("/psci node is missing the \"cpu_on\" property\n");
        return -ENOENT;
    }

    psci_ver = PSCI_VERSION(0, 1);

    return 0;
}

static int __init psci_init_0_2(void)
{
    static const struct dt_device_match psci_ids[] __initconst =
    {
        DT_MATCH_COMPATIBLE("arm,psci-0.2"),
        DT_MATCH_COMPATIBLE("arm,psci-1.0"),
        { /* sentinel */ },
    };
    int ret;
    struct arm_smccc_res res;

    if ( acpi_disabled )
    {
        const struct dt_device_node *psci;

        psci = dt_find_matching_node(NULL, psci_ids);
        if ( !psci )
            return -EOPNOTSUPP;

        ret = psci_is_smc_method(psci);
        if ( ret )
            return -EINVAL;
    }
    else
    {
        if ( acpi_psci_hvc_present() ) {
            printk("PSCI conduit must be SMC, but is HVC\n");
            return -EINVAL;
        }
    }

    arm_smccc_smc(PSCI_0_2_FN32_PSCI_VERSION, &res);
    psci_ver = PSCI_RET(res);

    /* For the moment, we only support PSCI 0.2 and PSCI 1.x */
    if ( psci_ver != PSCI_VERSION(0, 2) && PSCI_VERSION_MAJOR(psci_ver) != 1 )
    {
        printk("Error: Unrecognized PSCI version %u.%u\n",
               PSCI_VERSION_MAJOR(psci_ver), PSCI_VERSION_MINOR(psci_ver));
        return -EOPNOTSUPP;
    }

    psci_cpu_on_nr = PSCI_0_2_FN_NATIVE(CPU_ON);

    return 0;
}


static int __init psci_init_0_1_prtos(void)
{
    int ret;
    const struct dt_device_node *psci;

    if ( !acpi_disabled )
    {
        printk("PSCI 0.1 is not supported when using ACPI\n");
        return -EINVAL;
    }

    psci = dt_find_compatible_node(NULL, NULL, "arm,psci");
    if ( !psci )
        return -EOPNOTSUPP;

    ret = psci_is_smc_method(psci);
    if ( ret )
        return -EINVAL;

    if ( !dt_property_read_u32(psci, "cpu_on", &psci_cpu_on_nr) )
    {
        printk("/psci node is missing the \"cpu_on\" property\n");
        return -ENOENT;
    }

    psci_ver = PSCI_VERSION(0, 1);

    return 0;
}

static int __init psci_init_0_2_prtos(void)
{
    static const struct dt_device_match psci_ids[] __initconst =
    {
        DT_MATCH_COMPATIBLE("arm,psci-0.2"),
        DT_MATCH_COMPATIBLE("arm,psci-1.0"),
        { /* sentinel */ },
    };
    int ret = 0;
    struct arm_smccc_res res;

    // if ( acpi_disabled )
    // {
    //     const struct dt_device_node *psci;

    //     psci = dt_find_matching_node(NULL, psci_ids);
    //     if ( !psci )
    //         return -EOPNOTSUPP;

    //     ret = psci_is_smc_method(psci);
    //     if ( ret )
    //         return -EINVAL;
    // }
    // else
    // {
    //     if ( acpi_psci_hvc_present() ) {
    //         printk("PSCI conduit must be SMC, but is HVC\n");
    //         return -EINVAL;
    //     }
    // }

    arm_smccc_smc(PSCI_0_2_FN32_PSCI_VERSION, &res);
    psci_ver = PSCI_RET(res);

    /* For the moment, we only support PSCI 0.2 and PSCI 1.x */
    if ( psci_ver != PSCI_VERSION(0, 2) && PSCI_VERSION_MAJOR(psci_ver) != 1 )
    {
        printk("Error: Unrecognized PSCI version %u.%u\n",
               PSCI_VERSION_MAJOR(psci_ver), PSCI_VERSION_MINOR(psci_ver));
        return -EOPNOTSUPP;
    }

    psci_cpu_on_nr = PSCI_0_2_FN_NATIVE(CPU_ON);

    return 0;
}

int __init psci_init(void)
{
    int ret;

    if ( !acpi_disabled && !acpi_psci_present() )
        return -EOPNOTSUPP;

    ret = psci_init_0_2();
    if ( ret )
        ret = psci_init_0_1();

    if ( ret )
        return ret;

    psci_init_smccc();

    printk(XENLOG_INFO "Using PSCI v%u.%u\n",
           PSCI_VERSION_MAJOR(psci_ver), PSCI_VERSION_MINOR(psci_ver));

    return 0;
}


int __init psci_init_prtos(void)
{
    int ret;

    // if ( !acpi_disabled && !acpi_psci_present() )
    //     return -EOPNOTSUPP;

    ret = psci_init_0_2_prtos();
    // if ( ret )
    //     ret = psci_init_0_1_prtos();

    if ( ret )
        return ret;

    psci_init_smccc();

    printk(XENLOG_INFO "Using PSCI v%u.%u\n",
           PSCI_VERSION_MAJOR(psci_ver), PSCI_VERSION_MINOR(psci_ver));

    return 0;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: psci.c === */
