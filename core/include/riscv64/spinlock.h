/*
 * FILE: spinlock.h
 *
 * RISC-V 64-bit spin locks
 *
 * www.prtos.org
 */

#ifndef _PRTOS_ARCH_SPINLOCK_H_
#define _PRTOS_ARCH_SPINLOCK_H_

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

#include <arch/asm.h>

typedef struct {
    volatile prtos_u32_t lock;
} arch_spin_lock_t;

typedef struct {
    volatile prtos_u8_t mask;
} arch_barrier_mask_t;

#define __ARCH_SPINLOCK_UNLOCKED {0}
#define __ARCH_SPINLOCK_LOCKED {1}

#define __ARCH_BARRIER_MASK_INIT {0}

static inline void __arch_barrier_write_mask(arch_barrier_mask_t *bm, prtos_u8_t bitMask) {
#ifdef CONFIG_SMP
    prtos_u8_t tmp;
    __asm__ __volatile__(
        "1:\n"
        "  lr.w %0, (%1)\n"
        "  or   %0, %0, %2\n"
        "  sc.w %0, %0, (%1)\n"
        "  bnez %0, 1b\n"
        : "=&r"(tmp)
        : "r"(&bm->mask), "r"((prtos_u32_t)bitMask)
        : "memory");
#endif
}

static inline int __arch_barrier_check_mask(arch_barrier_mask_t *bm, prtos_u8_t mask) {
#ifdef CONFIG_SMP
    if (bm->mask == mask) return 0;
    return -1;
#else
    return 0;
#endif
}

static inline void __arch_spin_lock(arch_spin_lock_t *lock) {
#ifdef CONFIG_SMP
    prtos_u32_t tmp;
    __asm__ __volatile__(
        "1:\n"
        "  lr.w.aq %0, (%1)\n"
        "  bnez    %0, 1b\n"
        "  li      %0, 1\n"
        "  sc.w.rl %0, %0, (%1)\n"
        "  bnez    %0, 1b\n"
        : "=&r"(tmp)
        : "r"(&lock->lock)
        : "memory");
#else
    lock->lock = 1;
#endif
}

static inline void __arch_spin_unlock(arch_spin_lock_t *lock) {
#ifdef CONFIG_SMP
    __asm__ __volatile__("fence rw, w\n"
                         "sw zero, (%0)\n"
                         : : "r"(&lock->lock) : "memory");
#else
    lock->lock = 0;
#endif
}

static inline prtos_s32_t __arch_spin_try_lock(arch_spin_lock_t *lock) {
#ifdef CONFIG_SMP
    prtos_u32_t tmp;
    __asm__ __volatile__(
        "lr.w.aq %0, (%1)\n"
        "bnez    %0, 1f\n"
        "li      %0, 1\n"
        "sc.w.rl %0, %0, (%1)\n"
        "1:\n"
        : "=&r"(tmp)
        : "r"(&lock->lock)
        : "memory");
    return tmp == 0;
#else
    if (lock->lock) return 0;
    lock->lock = 1;
    return 1;
#endif
}

static inline prtos_s32_t __arch_spin_is_locked(arch_spin_lock_t *lock) {
    return lock->lock != 0;
}

#define hw_save_flags_cli(flags) \
    {                            \
        hw_save_flags(flags);    \
        hw_cli();                \
    }

#endif
