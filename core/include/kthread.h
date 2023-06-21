/*
 * FILE: kthread.h
 *
 * Kernel and Guest kthreads
 *
 * www.prtos.org
 */

#ifndef _PRTOS_KTHREAD_H_
#define _PRTOS_KTHREAD_H_

#include <assert.h>
#include <guest.h>
#include <ktimer.h>
#include <prtosconf.h>
#include <prtosef.h>
#include <objdir.h>
#include <spinlock.h>
#include <arch/kthread.h>
#include <arch/atomic.h>
#include <arch/irqs.h>
#include <arch/prtos_def.h>
#ifdef CONFIG_OBJ_STATUS_ACC
#include <objects/status.h>
#endif

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

struct guest {
#define PART_VCPU_ID2KID(part_id, vcpu_id) ((vcpu_id) << 8) | ((part_id)&0xff)
#define KID2PARTID(id) ((id)&0xff)
#define KID2VCPUID(id) ((id) >> 8)
    prtos_id_t id;  // 15..8: vcpu_id, 7..0: partitionId
    struct kthread_arch karch;
    vtimer_t vtimer;
    ktimer_t ktimer;
    ktimer_t watchdogTimer;
    vclock_t vclock;
    prtos_u32_t op_mode; /*Only for debug vcpus*/
    part_ctl_table_t *part_ctrl_table;
    prtos_u32_t sw_trap;
    struct trap_handler override_trap_table[NO_TRAPS];
};

static inline prtos_s32_t is_ptr_in_ctrl_table_page(prtos_address_t addr, struct guest *g) {
    prtos_address_t a, b;
    a = _VIRT2PHYS(g->part_ctrl_table);
    b = a + g->part_ctrl_table->part_ctrl_table_size;
    return ((addr >= a) && (addr < b)) ? 1 : 0;
}

#define CHECK_KTHR_SANITY(k) ASSERT((k->ctrl.magic1 == KTHREAD_MAGIC) && (k->ctrl.magic2 == KTHREAD_MAGIC))

#ifdef CONFIG_MMU
#define GET_GUEST_GCTRL_ADDR(k) (k)->ctrl.g->RAM + ROUNDUP(sizeof(part_ctl_table_t))
#else
#define GET_GUEST_GCTRL_ADDR(k) ((prtos_address_t)((k)->ctrl.g->part_ctrl_table))
#endif

typedef union kthread {
    struct __kthread {
        // Hard-coded, don't change it
        prtos_u32_t magic1;
        // Hard-coded, don't change it
        prtos_address_t *kstack;
        spin_lock_t lock;
        volatile prtos_u32_t flags;
        //  [3...0] -> scheduling bits
#define KTHREAD_FP_F (1 << 1)         // Floating point enabled
#define KTHREAD_HALTED_F (1 << 2)     // 1:HALTED
#define KTHREAD_SUSPENDED_F (1 << 3)  // 1:SUSPENDED
#define KTHREAD_READY_F (1 << 4)      // 1:READY
#define KTHREAD_FLUSH_CACHE_B 5
#define KTHREAD_FLUSH_CACHE_W 3
#define KTHREAD_FLUSH_DCACHE_F (1 << 5)
#define KTHREAD_FLUSH_ICACHE_F (1 << 6)
#define KTHREAD_CACHE_ENABLED_B 7
#define KTHREAD_CACHE_ENABLED_W 3
#define KTHREAD_DCACHE_ENABLED_F (1 << 7)
#define KTHREAD_ICACHE_ENABLED_F (1 << 8)

#define KTHREAD_NO_PARTITIONS_FIELD (0xff << 16)  // No. partitions
#define KTHREAD_TRAP_PENDING_F (1 << 31)          // 31: PENDING

        struct dyn_list local_active_ktimers;
        struct guest *g;
        void *sched_data;
        cpu_ctxt_t *irq_cpu_ctxt;
        prtos_u32_t irqMask;
        prtos_u32_t magic2;
    } ctrl;
    prtos_u8_t kstack[CONFIG_KSTACK_SIZE];
} kthread_t;

static inline void set_kthread_flags(kthread_t *k, prtos_u32_t f) {
    spin_lock(&k->ctrl.lock);
    k->ctrl.flags |= f;
    if (k->ctrl.g && k->ctrl.g->part_ctrl_table) k->ctrl.g->part_ctrl_table->flags |= f;
    spin_unlock(&k->ctrl.lock);
}

static inline void clear_kthread_flags(kthread_t *k, prtos_u32_t f) {
    spin_lock(&k->ctrl.lock);
    k->ctrl.flags &= ~f;
    if (k->ctrl.g && k->ctrl.g->part_ctrl_table) k->ctrl.g->part_ctrl_table->flags &= ~f;
    spin_unlock(&k->ctrl.lock);
}

static inline prtos_u32_t are_kthread_flags_set(kthread_t *k, prtos_u32_t f) {
    prtos_u32_t __r;

    spin_lock(&k->ctrl.lock);
    __r = k->ctrl.flags & f;
    spin_unlock(&k->ctrl.lock);
    return __r;
}

typedef struct partition {
    kthread_t **kthread;
    prtos_address_t pctArray;
    prtos_u_size_t pctArraySize;
    prtos_u32_t op_mode;
    prtos_address_t image_start; /*Partition Memory address in the container*/
    prtos_address_t vLdrStack;   /*Stack address allocated by PRTOS*/
    struct prtos_conf_part *cfg;
} partition_t;

extern partition_t *part_table;

static inline partition_t *get_partition(kthread_t *k) {
    if (k->ctrl.g) return &part_table[KID2PARTID(k->ctrl.g->id)];

    return 0;
}

extern void init_idle(kthread_t *idle, prtos_s32_t cpu);

extern partition_t *create_partition(struct prtos_conf_part *conf);
extern void setup_kthread_arch(kthread_t *k);
extern prtos_s32_t reset_partition(partition_t *p, prtos_u32_t cold, prtos_u32_t status) __WARN_UNUSED_RESULT;
extern void reset_kthread(kthread_t *k, prtos_address_t ptd_level_1, prtos_address_t entry_point, prtos_u32_t status);
extern void setup_pct_mm(part_ctl_table_t *part_ctrl_table, kthread_t *k);
extern void setup_pct_arch(part_ctl_table_t *part_ctrl_table, kthread_t *k);
extern void switch_kthread_arch_pre(kthread_t *new, kthread_t *current);
extern void switch_kthread_arch_post(kthread_t *current);

static inline void set_hw_irq_pending(kthread_t *k, prtos_s32_t irq) {
    ASSERT(k->ctrl.g);
    ASSERT((irq >= PRTOS_VT_HW_FIRST) && (irq <= PRTOS_VT_HW_LAST));
    if (are_kthread_flags_set(k, KTHREAD_HALTED_F)) return;
    k->ctrl.g->part_ctrl_table->hw_irqs_pend |= (1 << irq);
    set_kthread_flags(k, KTHREAD_READY_F);
}

static inline void set_part_hw_irq_pending(partition_t *p, prtos_s32_t irq) {
    kthread_t *k;
    prtos_s32_t e;

    ASSERT((irq >= PRTOS_VT_HW_FIRST) && (irq <= PRTOS_VT_HW_LAST));

    for (e = 0; e < p->cfg->num_of_vcpus; e++) {
        k = p->kthread[e];

        if (are_kthread_flags_set(k, KTHREAD_HALTED_F)) continue;
        spin_lock(&k->ctrl.lock);
        k->ctrl.g->part_ctrl_table->hw_irqs_pend |= (1 << irq);
        spin_unlock(&k->ctrl.lock);
        set_kthread_flags(k, KTHREAD_READY_F);
    }
}

static inline void set_ext_irq_pending(kthread_t *k, prtos_s32_t irq) {
    ASSERT(k->ctrl.g);
    ASSERT((irq >= PRTOS_VT_EXT_FIRST) && (irq <= PRTOS_VT_EXT_LAST));
    irq -= PRTOS_VT_EXT_FIRST;
    if (are_kthread_flags_set(k, KTHREAD_HALTED_F)) return;
#ifdef CONFIG_OBJ_STATUS_ACC
    if (k->ctrl.g) partition_status[KID2PARTID(k->ctrl.g->id)].num_of_virqs++;
#endif
    spin_lock(&k->ctrl.lock);
    k->ctrl.g->part_ctrl_table->ext_irqs_pend |= (1 << irq);
    spin_unlock(&k->ctrl.lock);
    set_kthread_flags(k, KTHREAD_READY_F);
}

static inline int are_part_ext_irq_pending_set(partition_t *p, prtos_s32_t irq) {
    kthread_t *k;
    prtos_s32_t e;

    ASSERT((irq >= PRTOS_VT_EXT_FIRST) && (irq <= PRTOS_VT_EXT_LAST));
    irq -= PRTOS_VT_EXT_FIRST;

    for (e = 0; e < p->cfg->num_of_vcpus; e++) {
        k = p->kthread[e];
        spin_lock(&k->ctrl.lock);
        if (!(k->ctrl.g->part_ctrl_table->ext_irqs_pend & (1 << irq))) {
            spin_unlock(&k->ctrl.lock);
            return 0;
        }
        spin_unlock(&k->ctrl.lock);
    }
    return 1;
}

static inline int are_ext_irq_pending_set(kthread_t *k, prtos_s32_t irq) {
    ASSERT((irq >= PRTOS_VT_EXT_FIRST) && (irq <= PRTOS_VT_EXT_LAST));
    irq -= PRTOS_VT_EXT_FIRST;

    spin_lock(&k->ctrl.lock);
    if (!(k->ctrl.g->part_ctrl_table->ext_irqs_pend & (1 << irq))) {
        spin_unlock(&k->ctrl.lock);
        return 0;
    }
    spin_unlock(&k->ctrl.lock);
    return 1;
}

static inline void set_part_ext_irq_pending(partition_t *p, prtos_s32_t irq) {
    kthread_t *k;
    prtos_s32_t e;

    ASSERT((irq >= PRTOS_VT_EXT_FIRST) && (irq <= PRTOS_VT_EXT_LAST));
    irq -= PRTOS_VT_EXT_FIRST;

    for (e = 0; e < p->cfg->num_of_vcpus; e++) {
        k = p->kthread[e];

        if (are_kthread_flags_set(k, KTHREAD_HALTED_F)) continue;
        spin_lock(&k->ctrl.lock);
        k->ctrl.g->part_ctrl_table->ext_irqs_pend |= (1 << irq);
        spin_unlock(&k->ctrl.lock);
        set_kthread_flags(k, KTHREAD_READY_F);
        /*#ifdef CONFIG_OBJ_STATUS_ACC
                if (k->ctrl.g)
                    partition_status[k->ctrl.g->cfg->id].num_of_virqs++;
        #endif*/
    }
}

extern void setup_kstack(kthread_t *k, void *StartUp, prtos_address_t entry_point);
extern void kthread_arch_init(kthread_t *k);
extern void start_up_guest(prtos_address_t entry);
extern void rsv_part_frames(void);

#endif
