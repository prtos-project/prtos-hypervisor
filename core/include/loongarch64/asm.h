/*
 * FILE: asm.h
 *
 * LoongArch 64-bit assembly macros and functions
 *
 * http://www.prtos.org/
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

/* LoongArch interrupt enable/disable via CRMD.IE (bit 2) */
#define CRMD_IE  (1UL << 2)

#define local_irq_disable() asm volatile( \
    "li.w $t0, %0\n\t" \
    "csrxchg $zero, $t0, %1\n\t" \
    : : "i"(CRMD_IE), "i"(0x0) : "$t0", "memory")

#define local_irq_enable()  asm volatile( \
    "li.w $t0, %0\n\t" \
    "csrxchg $t0, $t0, %1\n\t" \
    : : "i"(CRMD_IE), "i"(0x0) : "$t0", "memory")

#define local_save_flags(x) ({ asm volatile("csrrd %0, 0x0" : "=r"(x) : : "memory"); })

#define local_irq_save(x)    \
    ({                       \
        local_save_flags(x); \
        local_irq_disable(); \
    })

#define local_irq_restore(x) ({ asm volatile("csrwr %0, 0x0" : "+r"(x) : : "memory"); })

static inline int local_irq_is_enabled(void) {
    prtos_word_t flags;
    local_save_flags(flags);
    return (flags & CRMD_IE) ? 1 : 0;
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

/* Context-switch: save callee-saved registers, swap stacks, restore.
 *
 * Inline-asm approach (like RISC-V): the resume point is a local label
 * after the asm block, so GCC never sees a "second return" — it treats
 * all clobbered registers as needing reload.
 *
 * Frame layout (96 bytes) matches setup_kstack() in kthread.c:
 *   [sp+ 0] fp    [sp+ 8] s0   [sp+16] s1   [sp+24] s2
 *   [sp+32] s3    [sp+40] s4   [sp+48] s5   [sp+56] s6
 *   [sp+64] s7    [sp+72] s8   [sp+80] ra
 *
 * kstack offset in kthread_t = 8 */

#define CONTEXT_SWITCH(_cs_next, _cs_old)                                                                                     \
    do {                                                                                                                      \
        kthread_t **_cs_cur_ptr = &GET_LOCAL_PROCESSOR()->sched.current_kthread;                                              \
        __asm__ __volatile__(                                                                                                  \
            /* Compute resume PC and store in t2 */                                                                            \
            "la.local $t2, 1f\n\t"                                                                                             \
            /* Push callee-saved frame (96 bytes) */                                                                           \
            "addi.d $sp, $sp, -96\n\t"                                                                                         \
            "st.d $fp,  $sp, 0\n\t"                                                                                            \
            "st.d $s0,  $sp, 8\n\t"                                                                                            \
            "st.d $s1,  $sp, 16\n\t"                                                                                           \
            "st.d $s2,  $sp, 24\n\t"                                                                                           \
            "st.d $s3,  $sp, 32\n\t"                                                                                           \
            "st.d $s4,  $sp, 40\n\t"                                                                                           \
            "st.d $s5,  $sp, 48\n\t"                                                                                           \
            "st.d $s6,  $sp, 56\n\t"                                                                                           \
            "st.d $s7,  $sp, 64\n\t"                                                                                           \
            "st.d $s8,  $sp, 72\n\t"                                                                                           \
            "st.d $t2,  $sp, 80\n\t"  /* ra = resume PC */                                                                    \
            "dbar 0\n\t"                                                                                                       \
            /* Save old kthread sp: old->ctrl.kstack = sp */                                                                   \
            "ld.d $t0, %[cur], 0\n\t"                                                                                          \
            "st.d $sp, $t0, 8\n\t"                                                                                             \
            /* Switch to new kthread */                                                                                        \
            "st.d %[next], %[cur], 0\n\t"                                                                                      \
            "ld.d $sp, %[next], 8\n\t"                                                                                         \
            "dbar 0\n\t"                                                                                                       \
            /* Pop callee-saved of new kthread */                                                                              \
            "ld.d $fp,  $sp, 0\n\t"                                                                                            \
            "ld.d $s0,  $sp, 8\n\t"                                                                                            \
            "ld.d $s1,  $sp, 16\n\t"                                                                                           \
            "ld.d $s2,  $sp, 24\n\t"                                                                                           \
            "ld.d $s3,  $sp, 32\n\t"                                                                                           \
            "ld.d $s4,  $sp, 40\n\t"                                                                                           \
            "ld.d $s5,  $sp, 48\n\t"                                                                                           \
            "ld.d $s6,  $sp, 56\n\t"                                                                                           \
            "ld.d $s7,  $sp, 64\n\t"                                                                                           \
            "ld.d $s8,  $sp, 72\n\t"                                                                                           \
            "ld.d $ra,  $sp, 80\n\t"                                                                                           \
            "addi.d $sp, $sp, 96\n\t"                                                                                          \
            "jirl $zero, $ra, 0\n\t"  /* Jump to new kthread's resume point */                                                 \
            "1:\n\t"                                                                                                           \
            :                                                                                                                  \
            : [next] "r"(_cs_next),                                                                                            \
              [cur] "r"(_cs_cur_ptr)                                                                                           \
            : "memory", "$t0", "$t1", "$t2", "$ra",                                                                            \
              "$fp", "$s0", "$s1", "$s2", "$s3", "$s4", "$s5", "$s6", "$s7", "$s8"                                             \
        );                                                                                                                     \
    } while (0)

#define hw_is_sti() local_irq_is_enabled()

#define do_nop() __asm__ __volatile__("nop\n\t" ::)

static inline prtos_s32_t asm_rw_check(prtos_address_t param, prtos_u_size_t size, prtos_u32_t alignment) {
    return 0;
}

static inline prtos_s32_t asm_ronly_check(prtos_address_t param, prtos_u_size_t size, prtos_u32_t alignment) {
    return 0;
}

/* Jump to partition entry point via ertn into guest mode.
 * a0 = PCT physical address, entry = partition entry point.
 * For para-virt: enters PLV3 (user mode).
 * For hw-virt (LVZ): enters guest mode at guest PLV0. */
extern prtos_u32_t prtos_lvz_available;

#define JMP_PARTITION(entry, k)                                                                          \
    do {                                                                                                 \
        prtos_u64_t _pct_paddr = (prtos_u64_t)_VIRT2PHYS((prtos_address_t)(k)->ctrl.g->part_ctrl_table); \
        prtos_u64_t _entry = (prtos_u64_t)(entry);                                                      \
        if ((k)->ctrl.g->karch.lvz_enabled) {                          \
            /* LVZ hw-virt path: enter guest mode at PLV0 */                                             \
            JMP_PARTITION_LVZ(_entry, _pct_paddr, (k));                                                  \
        } else {                                                                                         \
            /* Para-virt / PLV3 hw-virt path:                                                            \
             * DMW PLV3 is permanently enabled at boot so guest can access                               \
             * DMW addresses (0x8000.../0x9000...) at PLV3. */                                           \
            __asm__ __volatile__(                                                                        \
                "csrwr %1, 0x6\n\t"       /* ERA = entry */                                              \
                "li.w $t0, 0x7\n\t"       /* PRMD: PPLV=3, PIE=1 */                                     \
                "csrwr $t0, 0x1\n\t"                                                                     \
                "move $a0, %0\n\t"        /* a0 = PCT */                                                 \
                "li.d $a1, 0\n\t"                                                                        \
                "csrwr $sp, 0x30\n\t"     /* Save host sp to SAVE0 */                                    \
                "ertn\n\t"                                                                               \
                :                                                                                        \
                : "r"(_pct_paddr), "r"(_entry)                                                           \
                : "$a0", "$a1", "$t0", "memory");                                                        \
        }                                                                                                \
    } while (0)

/* LVZ guest entry: set GSTAT.PVM=1 and GTLBC.TGID, then ertn into guest mode */
#define JMP_PARTITION_LVZ(entry, pct_paddr, k)                                                           \
    do {                                                                                                 \
        prtos_u32_t _gid = (k)->ctrl.g->karch.guest_gid;                                                \
        __asm__ __volatile__(                                                                            \
            /* Set ERA = entry point */                                                                  \
            "csrwr %1, 0x6\n\t"                                                                          \
            /* Set PRMD = PIE (PPLV=0, enable host interrupts after ertn) */                             \
            "li.w $t0, 0x4\n\t"                                                                          \
            "csrwr $t0, 0x1\n\t"                                                                         \
            /* Set GSTAT.GID and PVM */                                                                  \
            "csrrd $t0, 0x50\n\t"       /* Read GSTAT */                                                 \
            "bstrins.w $t0, %3, 23, 16\n\t" /* Set GID field */                                          \
            "ori $t0, $t0, 0x2\n\t"     /* Set PVM bit */                                                \
            "csrwr $t0, 0x50\n\t"                                                                        \
            /* Set GTLBC.TGID = GID */                                                                   \
            "csrrd $t1, 0x15\n\t"       /* Read GTLBC */                                                 \
            "bstrins.w $t1, %3, 23, 16\n\t" /* Set TGID */                                               \
            "csrwr $t1, 0x15\n\t"                                                                        \
            /* a0 = PCT address, a1 = 0 */                                                               \
            "move $a0, %0\n\t"                                                                           \
            "li.d $a1, 0\n\t"                                                                            \
            /* Save host sp to SAVE0 */                                                                  \
            "csrwr $sp, 0x30\n\t"                                                                        \
            /* Set LVZ flag in SAVE5 */                                                                  \
            "li.w $t0, 1\n\t"                                                                            \
            "csrwr $t0, 0x35\n\t"                                                                        \
            /* Enter guest */                                                                            \
            "ertn\n\t"                                                                                   \
            :                                                                                            \
            : "r"(pct_paddr), "r"(entry), "r"(0UL), "r"(_gid)                                           \
            : "$a0", "$a1", "$t0", "$t1", "memory");                                                     \
    } while (0)

/* Jump to partition for secondary vCPU boot (hw-virt). */
#define JMP_PARTITION_HSM(k)                                                                             \
    do {                                                                                                 \
        prtos_u64_t _entry = (k)->ctrl.g->karch.hsm_entry;                                             \
        prtos_u64_t _opaque = (k)->ctrl.g->karch.hsm_opaque;                                           \
        prtos_u64_t _cpuid = (prtos_u64_t)KID2VCPUID((k)->ctrl.g->id);                                  \
        if (prtos_lvz_available && ((k)->ctrl.g->karch.lvz_enabled)) {                                   \
            prtos_u32_t _gid = (k)->ctrl.g->karch.guest_gid;                                            \
            __asm__ __volatile__(                                                                        \
                "csrwr %0, 0x6\n\t"        /* ERA = entry */                                             \
                "li.w $t0, 0x4\n\t"        /* PRMD: PIE=1, PPLV=0 */                                    \
                "csrwr $t0, 0x1\n\t"                                                                     \
                /* Set GSTAT.GID and PVM */                                                              \
                "csrrd $t0, 0x50\n\t"                                                                    \
                "bstrins.w $t0, %3, 23, 16\n\t"                                                          \
                "ori $t0, $t0, 0x2\n\t"                                                                  \
                "csrwr $t0, 0x50\n\t"                                                                    \
                /* Set GTLBC.TGID */                                                                     \
                "csrrd $t1, 0x15\n\t"                                                                    \
                "bstrins.w $t1, %3, 23, 16\n\t"                                                          \
                "csrwr $t1, 0x15\n\t"                                                                    \
                "csrwr $sp, 0x30\n\t"                                                                    \
                "li.w $t0, 1\n\t"                                                                        \
                "csrwr $t0, 0x35\n\t"      /* LVZ flag */                                                \
                "move $a1, %1\n\t"                                                                       \
                "move $a0, %2\n\t"                                                                       \
                "ertn\n\t"                                                                               \
                :                                                                                        \
                : "r"(_entry), "r"(_opaque), "r"(_cpuid), "r"(_gid)                                      \
                : "$a0", "$a1", "$t0", "$t1", "memory");                                                 \
        } else {                                                                                         \
            __asm__ __volatile__(                                                                        \
                "csrwr %0, 0x6\n\t"        /* ERA = entry */                                             \
                "li.w $t0, 0x7\n\t"                                                                      \
                "csrwr $t0, 0x1\n\t"       /* PRMD: PPLV=3, PIE=1 */                                    \
                "csrwr $sp, 0x30\n\t"      /* Save host sp */                                            \
                "move $a1, %1\n\t"         /* a1 = opaque */                                             \
                "move $a0, %2\n\t"         /* a0 = cpuid */                                              \
                "ertn\n\t"                                                                               \
                :                                                                                        \
                : "r"(_entry), "r"(_opaque), "r"(_cpuid)                                                 \
                : "$a0", "$a1", "$t0", "memory");                                                        \
        }                                                                                                \
    } while (0)

/* No MMU page table switching needed for para-virt */
#define load_part_page_table(k) do {} while (0)
#define load_hyp_page_table()   do {} while (0)

#endif /* __ASSEMBLY__ */
#endif /* _PRTOS_KERNEL_ */
#endif
