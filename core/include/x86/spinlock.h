/*
 * FILE: spinlock.h
 *
 * Spin locks related stuffs
 *
 * www.prtos.org
 *
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

#define __ARCH_SPINLOCK_UNLOCKED \
    { 1 }
#define __ARCH_BARRIER_MASK_INIT \
    { 0 }

static inline void __arch_barrier_write_mask(arch_barrier_mask_t *bm, prtos_u8_t bitMask) {
#ifdef CONFIG_SMP
    __asm__ __volatile__("lock; or %0,%1\n\t" : : "r"(bitMask), "m"(*(&bm->mask)) : "memory");
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
    __asm__ __volatile__("\n1:\t"
                         "cmpl $1, %0\n\t"
                         "je 2f\n\t"
                         "pause\n\t"
                         "jmp 1b\n\t"
                         "2:\t"
                         "mov $0, %%eax\n\t"
                         "xchg %%eax, %0\n\t"
                         "cmpl $1, %%eax\n\t"
                         "jne 1b\n\t"
                         "3:\n\t"
                         : "+m"(lock->lock)
                         :
                         : "eax", "memory");
#else
    lock->lock = 0;
#endif
}

static inline void __arch_spin_unlock(arch_spin_lock_t *lock) {
#ifdef CONFIG_SMP
    prtos_s8_t oldval = 1;
    __asm__ __volatile__("xchgb %b0, %1" : "=q"(oldval), "+m"(lock->lock) : "0"(oldval) : "memory");
#else
    lock->lock = 1;
#endif
}

static inline prtos_s32_t __arch_spin_try_lock(arch_spin_lock_t *lock) {
    prtos_s8_t oldval;
#ifdef CONFIG_SMP
    __asm__ __volatile__("xchgb %b0,%1" : "=q"(oldval), "+m"(lock->lock) : "0"(0) : "memory");
#else
    oldval = lock->lock;
    lock->lock = 1;
#endif
    return oldval > 0;
}

#define __arch_spin_is_locked(x) (*(volatile prtos_s8_t *)(&(x)->lock) <= 0)

#define hw_save_flags_cli(flags) \
    {                            \
        hw_save_flags(flags);    \
        hw_cli();                \
    }

static inline prtos_s32_t hw_is_sti(void) {
    prtos_word_t flags;
    hw_save_flags(flags);
    return (flags & 0x200);
}

#endif
