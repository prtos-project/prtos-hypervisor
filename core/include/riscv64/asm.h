/*
 * FILE: asm.h
 *
 * RISC-V 64-bit assembly macros and functions
 *
 * www.prtos.org
 */

#ifndef _PRTOS_ARCH_ASM_H_
#define _PRTOS_ARCH_ASM_H_

#ifdef _PRTOS_KERNEL_

#ifndef _GENERATE_OFFSETS_
#include <arch/asm_offsets.h>
#endif

#include <arch/processor.h>
#include <arch/paging.h>

#ifndef __ASSEMBLY__

#define ASM_EXPTABLE(_a, _b)        \
    ".section .exptable, \"a\"\n\t" \
    ".align 4\n\t"                  \
    ".long " #_a "\n\t"             \
    ".long " #_b "\n\t"             \
    ".previous\n\t"

/* RISC-V interrupt enable/disable via sstatus.SIE */
#define SSTATUS_SIE  (1UL << 1)
#define SSTATUS_SPIE (1UL << 5)

#define local_irq_disable() asm volatile("csrc sstatus, %0\n" : : "r"(SSTATUS_SIE) : "memory")
#define local_irq_enable()  asm volatile("csrs sstatus, %0\n" : : "r"(SSTATUS_SIE) : "memory")

#define local_save_flags(x) ({ asm volatile("csrr %0, sstatus" : "=r"(x) : : "memory"); })

#define local_irq_save(x)    \
    ({                       \
        local_save_flags(x); \
        local_irq_disable(); \
    })

#define local_irq_restore(x) ({ asm volatile("csrw sstatus, %0" : : "r"(x) : "memory"); })

static inline int local_irq_is_enabled(void) {
    prtos_word_t flags;
    local_save_flags(flags);
    return (flags & SSTATUS_SIE) ? 1 : 0;
}

#define hw_cli() local_irq_disable()
#define hw_sti() local_irq_enable()
#define hw_restore_flags(flags) local_irq_restore(flags)
#define hw_save_flags(flags) local_save_flags(flags)

#define PUSH_REGISTERS
#define FP_OFFSET 108
#define PUSH_FP
#define POP_FP
#define POP_REGISTERS

/* context-switch: save callee-saved registers, swap stacks, restore.
 * RISC-V callee-saved: s0-s11 (x8-x9, x18-x27), ra (x1), sp (x2) */
#define CONTEXT_SWITCH(_cs_next, _cs_old)                                                                                     \
    do {                                                                                                                      \
        kthread_t **_cs_cur_ptr = &GET_LOCAL_PROCESSOR()->sched.current_kthread;                                              \
        __asm__ __volatile__(                                                                                                 \
            "la t0, 1f\n\t"          /* Save resume PC in ra */                                                                \
            /* Push callee-saved: s0-s11, ra */                                                                                \
            "addi sp, sp, -112\n\t"                                                                                            \
            "sd s0,   0(sp)\n\t"                                                                                               \
            "sd s1,   8(sp)\n\t"                                                                                               \
            "sd s2,  16(sp)\n\t"                                                                                               \
            "sd s3,  24(sp)\n\t"                                                                                               \
            "sd s4,  32(sp)\n\t"                                                                                               \
            "sd s5,  40(sp)\n\t"                                                                                               \
            "sd s6,  48(sp)\n\t"                                                                                               \
            "sd s7,  56(sp)\n\t"                                                                                               \
            "sd s8,  64(sp)\n\t"                                                                                               \
            "sd s9,  72(sp)\n\t"                                                                                               \
            "sd s10, 80(sp)\n\t"                                                                                               \
            "sd s11, 88(sp)\n\t"                                                                                               \
            "sd t0,  96(sp)\n\t"     /* ra = resume PC */                                                                      \
            "sd sp, 104(sp)\n\t"     /* save sp itself for debug */                                                            \
            /* Save old kthread sp: old->ctrl.kstack = sp */                                                                   \
            "ld t1, 0(%[cur])\n\t"   /* t1 = *_cs_cur_ptr (old kthread) */                                                    \
            "sd sp, %[kstack_off](t1)\n\t"                                                                                     \
            /* Switch to new kthread */                                                                                        \
            "sd %[next], 0(%[cur])\n\t"  /* *_cs_cur_ptr = _cs_next */                                                        \
            "ld sp, %[kstack_off](%[next])\n\t"  /* sp = new->ctrl.kstack */                                                   \
            /* Pop callee-saved of new kthread */                                                                              \
            "ld s0,   0(sp)\n\t"                                                                                               \
            "ld s1,   8(sp)\n\t"                                                                                               \
            "ld s2,  16(sp)\n\t"                                                                                               \
            "ld s3,  24(sp)\n\t"                                                                                               \
            "ld s4,  32(sp)\n\t"                                                                                               \
            "ld s5,  40(sp)\n\t"                                                                                               \
            "ld s6,  48(sp)\n\t"                                                                                               \
            "ld s7,  56(sp)\n\t"                                                                                               \
            "ld s8,  64(sp)\n\t"                                                                                               \
            "ld s9,  72(sp)\n\t"                                                                                               \
            "ld s10, 80(sp)\n\t"                                                                                               \
            "ld s11, 88(sp)\n\t"                                                                                               \
            "ld ra,  96(sp)\n\t"                                                                                               \
            "addi sp, sp, 112\n\t"                                                                                             \
            "jr ra\n\t"              /* Jump to new kthread's resume point */                                                  \
            "1:\n\t"                                                                                                           \
            :                                                                                                                  \
            : [next] "r"(_cs_next),                                                                                            \
              [cur] "r"(_cs_cur_ptr),                                                                                          \
              [kstack_off] "i"(__builtin_offsetof(kthread_t, ctrl) + __builtin_offsetof(struct __kthread, kstack))               \
            : "memory", "t0", "t1", "ra",                                                                                     \
              "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",                                                                 \
              "s8", "s9", "s10", "s11"                                                                                         \
        );                                                                                                                     \
    } while (0)

#define hw_is_sti() local_irq_is_enabled()

#define do_nop() __asm__ __volatile__("nop\n\t" ::)

static inline prtos_s32_t asm_rw_check(prtos_address_t param, prtos_u_size_t size, prtos_u32_t alignment) {
    /* Identity mapped: address is always valid */
    return 0;
}

static inline prtos_s32_t asm_ronly_check(prtos_address_t param, prtos_u_size_t size, prtos_u32_t alignment) {
    return 0;
}

/* Jump to partition entry point via sret into VS-mode.
 * a0 = PCT physical address, entry = partition entry point.
 * Uses H-extension: set hstatus.SPV=1 so sret enters VS-mode. */
#define JMP_PARTITION(entry, k)                                                                          \
    do {                                                                                                 \
        prtos_u64_t _pct_paddr = (prtos_u64_t)_VIRT2PHYS((prtos_address_t)(k)->ctrl.g->part_ctrl_table); \
        prtos_u64_t _entry = (prtos_u64_t)(entry);                                                      \
        __asm__ __volatile__(                                                                            \
            /* Set sepc = partition entry point */                                                        \
            "csrw sepc, %1\n\t"                                                                          \
            /* Set hstatus.SPV = 1 (bit 7) to return to VS-mode via sret */                              \
            "li t0, (1 << 7)\n\t"                                                                        \
            "csrs hstatus, t0\n\t"                                                                       \
            /* Set sstatus: SPP=1 (bit 8), SPIE=1 (bit 5) for VS-mode */                                \
            "li t0, (1 << 8) | (1 << 5)\n\t"                                                            \
            "csrs sstatus, t0\n\t"                                                                       \
            /* Clear SIE (bit 1) so interrupts are re-enabled by sret via SPIE */                        \
            "li t0, (1 << 1)\n\t"                                                                        \
            "csrc sstatus, t0\n\t"                                                                       \
            /* Allow guest to read time CSR: set hcounteren.TM (bit 1) */                                  \
            "li t0, (1 << 1)\n\t"                                                                        \
            "csrs hcounteren, t0\n\t"                                                                    \
            /* Delegate VS-mode exceptions to guest via hedeleg */                                      \
            "li t0, (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 8) | (1 << 12) | (1 << 13) | (1 << 15)\n\t" \
            "csrs hedeleg, t0\n\t"                                                                      \
            /* Delegate VS-mode interrupts to guest via hideleg:
             * bit 2 = VSSIP, bit 6 = VSTIP, bit 10 = VSEIP */                                         \
            "li t0, (1 << 2) | (1 << 6) | (1 << 10)\n\t"                                               \
            "csrs hideleg, t0\n\t"                                                                      \
            /* Set sscratch = current kernel sp for trap reentry */                                      \
            "addi sp, sp, -8\n\t"                                                                        \
            "sd tp, 0(sp)\n\t"         /* Save host tp in stack slot */                                  \
            "csrw sscratch, sp\n\t"                                                                      \
            /* SBI convention: a0 = hartid (0), a1 = PCT physical address.
             * This is compatible with both BAIL partitions (which read a1)
             * and native guests (which check a0 == 0 for hart ID). */                                  \
            "mv a1, %0\n\t"                                                                              \
            "li a0, 0\n\t"                                                                               \
            /* Enter VS-mode */                                                                          \
            "sret\n\t"                                                                                   \
            :                                                                                            \
            : "r"(_pct_paddr), "r"(_entry)                                                               \
            : "a0", "a1", "t0", "memory");                                                               \
    } while (0)

/* Jump to partition for SBI HSM secondary vCPU boot.
 * a0 = hartid (vCPU ID), a1 = opaque (set by SBI HART_START).
 * Enters VS-mode at hsm_entry address. */
#define JMP_PARTITION_HSM(k)                                                                             \
    do {                                                                                                 \
        prtos_u64_t _entry = (k)->ctrl.g->karch.hsm_entry;                                              \
        prtos_u64_t _opaque = (k)->ctrl.g->karch.hsm_opaque;                                            \
        prtos_u64_t _hartid = (prtos_u64_t)KID2VCPUID((k)->ctrl.g->id);                                 \
        __asm__ __volatile__(                                                                            \
            "csrw sepc, %0\n\t"                                                                          \
            "li t0, (1 << 7)\n\t"                                                                        \
            "csrs hstatus, t0\n\t"                                                                       \
            "li t0, (1 << 8) | (1 << 5)\n\t"                                                            \
            "csrs sstatus, t0\n\t"                                                                       \
            "li t0, (1 << 1)\n\t"                                                                        \
            "csrc sstatus, t0\n\t"                                                                       \
            "li t0, (1 << 1)\n\t"                                                                        \
            "csrs hcounteren, t0\n\t"                                                                    \
            /* Delegate VS-mode exceptions to guest via hedeleg */                                      \
            "li t0, (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 8) | (1 << 12) | (1 << 13) | (1 << 15)\n\t" \
            "csrs hedeleg, t0\n\t"                                                                      \
            "li t0, (1 << 2) | (1 << 6) | (1 << 10)\n\t"                                               \
            "csrs hideleg, t0\n\t"                                                                      \
            "addi sp, sp, -8\n\t"                                                                        \
            "sd tp, 0(sp)\n\t"         /* Save host tp in stack slot */                                  \
            "csrw sscratch, sp\n\t"                                                                      \
            "mv a1, %1\n\t"                                                                              \
            "mv a0, %2\n\t"                                                                              \
            "sret\n\t"                                                                                   \
            :                                                                                            \
            : "r"(_entry), "r"(_opaque), "r"(_hartid)                                                    \
            : "a0", "a1", "t0", "memory");                                                               \
    } while (0)

/* No MMU page table switching needed for para-virt */
#define load_part_page_table(k) do {} while (0)
#define load_hyp_page_table()   do {} while (0)

#endif /* __ASSEMBLY__ */
#endif /* _PRTOS_KERNEL_ */
#endif
