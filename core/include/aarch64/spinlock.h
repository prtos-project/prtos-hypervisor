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

#define __ARCH_SPINLOCK_UNLOCKED {0}
#define __ARCH_SPINLOCK_LOCKED {1}

#define __ARCH_BARRIER_MASK_INIT {0}

static inline void __arch_barrier_write_mask(arch_barrier_mask_t *bm, prtos_u8_t bitMask) {
#ifdef CONFIG_SMP
    prtos_u8_t tmp;
    __asm__ __volatile__("1:\n"
                         "ldxr   %w0, [%1]\n"      // 原子加载 bm->mask 到 tmp
                         "orr    %w0, %w0, %w2\n"  // tmp |= bitMask
                         "stxr   w3, %w0, [%1]\n"  // 原子存储回 bm->mask，失败则重试
                         "cbnz   w3, 1b\n"
                         : "=&r"(tmp)                    // %0 输出：tmp，early clobber
                         : "r"(&bm->mask), "r"(bitMask)  // %1 输入：bm->mask 的地址，%2 输入：bitMask
                         : "w3", "memory");
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
    arch_spin_lock_t const ONE = __ARCH_SPINLOCK_LOCKED;
    arch_spin_lock_t tmp;

    asm volatile("1:\n\t"
                 "ldaxr %w0, %1 \n\t"
                 "cbnz %w0, 1b \n\t"
                 "stxr %w0, %w2, %1 \n\t"
                 "cbnz %w0, 1b \n\t"
                 : "=&r"(tmp), "+Q"(*lock)
                 : "r"(ONE)
                 : "memory");
#else
    lock->lock = 1;
#endif
}

static inline void __arch_spin_unlock(arch_spin_lock_t *lock) {
#ifdef CONFIG_SMP
    asm volatile("stlr wzr, %0\n\t" ::"Q"(*lock) : "memory");
#else
    lock->lock = 0;
#endif
}

static inline prtos_s32_t __arch_spin_try_lock(arch_spin_lock_t *lock) {
#ifdef CONFIG_SMP
    prtos_s32_t tmp = 0;
    prtos_s32_t res;

    asm volatile("ldaxr   %w0, [%2]\n"       // Load the current lock value with acquire semantics
                 "cbnz    %w0, 1f\n"         // If lock is already held (value != 0), branch to fail
                 "mov     %w0, #1\n"         // Prepare to set lock value to 1
                 "stlxr   %w1, %w0, [%2]\n"  // Attempt to store the new value atomically
                 "cbnz    %w1, 1f\n"         // If store failed, branch to fail
                 "mov     %w0, #0\n"         // Set return value to 0 (success)
                 "b       2f\n"              // Branch to exit
                 "1:\n"                      // Set return value of the lock
                 "2:\n"
                 : "=&r"(tmp), "=&r"(res)  // Outputs
                 : "r"(&lock->lock)        // Inputs
                 : "memory");

    return tmp == 0;
#else
    prtos_s8_t oldval;
    oldval = lock->lock;
    lock->lock = 1;
    return oldval == 0;
#endif
}

#define __arch_spin_is_locked(x) (*(volatile prtos_s8_t *)(&(x)->lock) <= 0)

#define hw_save_flags_cli(flags) \
    {                            \
        hw_save_flags(flags);    \
        hw_cli();                \
    }

static inline prtos_s32_t hw_is_sti(void) {
    return local_fiq_is_enabled();
}

#endif
