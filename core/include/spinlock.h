/*
 * FILE: spinlock.h
 *
 * Spin locks related stuffs
 *
 * www.prtos.org
 *
 */

#ifndef _PRTOS_SPINLOCK_H_
#define _PRTOS_SPINLOCK_H_

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

#include <arch/spinlock.h>
#include <arch/smp.h>

extern prtos_u16_t __nr_cpus;

typedef struct {
    arch_spin_lock_t archLock;
} spin_lock_t;

typedef struct {
    volatile prtos_s32_t v;
} barrier_t;

typedef struct {
    arch_barrier_mask_t bm;
} barrier_mask_t;

#define SPINLOCK_INIT                         \
    (spin_lock_t) {                           \
        .archLock = __ARCH_SPINLOCK_UNLOCKED, \
    }
#define BARRIER_INIT \
    (barrier_t) {    \
        .v = 0,      \
    }
#define BARRIER_MASK_INIT               \
    (barrier_mask_t) {                  \
        .bm = __ARCH_BARRIER_MASK_INIT, \
    }

static inline void barrier_write_mask(barrier_mask_t *m) {
    __arch_barrier_write_mask(&m->bm, 1 << GET_CPU_ID());
}

static inline int barrier_check_mask(barrier_mask_t *m) {
    return __arch_barrier_check_mask(&m->bm, (1 << __nr_cpus) - 1);
}

static inline void barrier_wait_mask(barrier_mask_t *m) {
    while (barrier_check_mask(m))
        ;
}

static inline void barrier_wait(barrier_t *b) {
    while (b->v)
        ;
}

static inline void barrier_lock(barrier_t *b) {
    b->v = 1;
}

static inline void barrier_unlock(barrier_t *b) {
    b->v = 0;
}

static inline void spin_lock(spin_lock_t *s) {
    __arch_spin_lock(&s->archLock);
}

static inline void spin_unlock(spin_lock_t *s) {
    __arch_spin_unlock(&s->archLock);
}

static inline prtos_s32_t spin_is_locked(spin_lock_t *s) {
    return (prtos_s32_t)__arch_spin_is_locked(&s->archLock);
}

#define spin_lock_irq_save(s, flags) \
    do {                             \
        hw_save_flags_cli(flags);    \
        spin_lock(s);                \
    } while (0)

#define spin_unlockIrqRestore(s, flags) \
    do {                                \
        spin_unlock(s);                 \
        hw_restore_flags(flags);        \
    } while (0)

static inline prtos_s32_t spin_try_lock(spin_lock_t *s) {
    return __arch_spin_try_lock(&s->archLock);
}

#endif
