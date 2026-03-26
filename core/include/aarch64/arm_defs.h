/*
 * FILE: arm_defs.h
 *
 * ARMv8/AArch64 architecture definitions for PRTOS hypervisor.
 * Provides system register, page table, SMCCC, and other hardware-level
 * constants needed by assembly boot/entry code.
 *
 * These definitions were extracted from the minimal set required by
 * start.S, head.S, and entry.S for the PRTOS aarch64 port.
 *
 * www.prtos.org
 */

#ifndef _PRTOS_AARCH64_ARM_DEFS_H_
#define _PRTOS_AARCH64_ARM_DEFS_H_

/* ---- Assembler constant helpers ---- */
#ifdef __ASSEMBLY__
#define _AC(X,Y)     X
#define _AT(T,X)     X
#else
#define __AC(X,Y)    (X##Y)
#define _AC(X,Y)     __AC(X,Y)
#define _AT(T,X)     ((T)(X))
#endif

#define BIT(pos, sfx)   (_AC(1, sfx) << (pos))

/* ---- Basic types ---- */
#define LONG_BYTEORDER  3
#define BYTES_PER_LONG  (1 << LONG_BYTEORDER)
#define BITS_PER_LONG   (BYTES_PER_LONG << 3)

/* ---- Page definitions ---- */
#ifndef PAGE_SHIFT
#define PAGE_SHIFT      12
#endif
#ifndef PAGE_SIZE
#define PAGE_SIZE       (_AC(1,L) << PAGE_SHIFT)
#endif
#ifndef PAGE_MASK
#define PAGE_MASK       (~(PAGE_SIZE-1))
#endif

/* ---- ARM configuration ---- */
#define CONFIG_PAGING_LEVELS    3
#define CONFIG_ARM              1
#define CONFIG_ARM_64           1
#define CONFIG_SMP              1
#define CONFIG_MMU              1

/* Early UART config */
#ifndef CONFIG_EARLY_PRINTK
#define CONFIG_EARLY_PRINTK     1
#endif
#ifndef CONFIG_EARLY_UART_BASE_ADDRESS
#define CONFIG_EARLY_UART_BASE_ADDRESS 0x9000000
#endif
#define CONFIG_EARLY_UART_INIT  1
#define CONFIG_EARLY_UART_PL011_BAUD_RATE 115200
#define CONFIG_DTB_FILE         ""
#define CONFIG_ARM_EFI          1
#define CONFIG_ARM_SSBD         0

/* Stack configuration */
#define STACK_ORDER     3
#define STACK_SIZE      (PAGE_SIZE << STACK_ORDER)

#ifndef CONFIG_KSTACK_SIZE
#define CONFIG_KSTACK_SIZE STACK_SIZE
#endif

/* ---- MPIDR mask ---- */
#define MPIDR_HWID_MASK     _AC(0xff00ffffff,UL)

/* ---- PSR mode definitions ---- */
#define PSR_MODE_MASK   0x1f
#define PSR_MODE_BIT    0x10U   /* Set iff AArch32 */
#define PSR_MODE_EL3h   0x0dU
#define PSR_MODE_EL3t   0x0cU
#define PSR_MODE_EL2h   0x09U
#define PSR_MODE_EL2t   0x08U
#define PSR_MODE_EL1h   0x05U
#define PSR_MODE_EL1t   0x04U
#define PSR_MODE_EL0t   0x00U

#define PSR_DBG_MASK    (1U << 9)
#define PSR_ABT_MASK    (1U << 8)
#ifndef PSR_IRQ_MASK
#define PSR_IRQ_MASK    (1U << 7)
#endif
#ifndef PSR_FIQ_MASK
#define PSR_FIQ_MASK    (1U << 6)
#endif

/* ---- MAIR register values ---- */
#define MT_DEVICE_nGnRnE 0x0
#define MT_NORMAL_NC     0x1
#define MT_NORMAL_WT     0x2
#define MT_NORMAL_WB     0x3
#define MT_DEVICE_nGnRE  0x4
#define MT_NORMAL        0x7

#define _MAIR0(attr, mt) (_AC(attr, ULL) << ((mt) * 8))
#define _MAIR1(attr, mt) (_AC(attr, ULL) << (((mt) * 8) - 32))

#define MAIR0VAL (_MAIR0(0x00, MT_DEVICE_nGnRnE)| \
                  _MAIR0(0x44, MT_NORMAL_NC)    | \
                  _MAIR0(0xaa, MT_NORMAL_WT)    | \
                  _MAIR0(0xee, MT_NORMAL_WB))

#define MAIR1VAL (_MAIR1(0x04, MT_DEVICE_nGnRE) | \
                  _MAIR1(0xff, MT_NORMAL))

#define MAIRVAL (MAIR1VAL << 32 | MAIR0VAL)

/* ---- SCTLR_EL2 register definitions ---- */
#define SCTLR_A64_ELx_SA    BIT(3, UL)
#define SCTLR_Axx_ELx_I     BIT(12, UL)
#define SCTLR_Axx_ELx_C     BIT(2, UL)
#define SCTLR_Axx_ELx_A     BIT(1, UL)
#define SCTLR_Axx_ELx_M     BIT(0, UL)

#define SCTLR_EL2_RES1  (BIT( 4, UL) | BIT( 5, UL) | BIT(11, UL) | \
                          BIT(16, UL) | BIT(18, UL) | BIT(22, UL) | \
                          BIT(23, UL) | BIT(28, UL) | BIT(29, UL))

#define SCTLR_EL2_SET   (SCTLR_EL2_RES1     | SCTLR_A64_ELx_SA  | \
                          SCTLR_Axx_ELx_I)

/* ---- TCR_EL2 register definitions ---- */
#define TCR_T0SZ_SHIFT  (0)
#define TCR_T0SZ(x)     ((x)<<TCR_T0SZ_SHIFT)
#define TCR_IRGN0_WBWA  (_AC(0x1,UL)<<8)
#define TCR_ORGN0_WBWA  (_AC(0x1,UL)<<10)
#define TCR_SH0_IS      (_AC(0x3,UL)<<12)

/* AArch64 EL2 */
#define TCR_RES1        (_AC(1,UL)<<31|_AC(1,UL)<<23)

/* ---- HSR (ESR_EL2) definitions ---- */
#define HSR_EC_SHIFT                26
#define HSR_EC_HVC32                0x12
#define HSR_EC_HVC64                0x16

/* ---- SMCCC definitions ---- */
#define ARM_SMCCC_FAST_CALL             _AC(1,U)
#define ARM_SMCCC_CONV_32               _AC(0,U)
#define ARM_SMCCC_CONV_64               _AC(1,U)
#define ARM_SMCCC_TYPE_SHIFT            31
#define ARM_SMCCC_CONV_SHIFT            30
#define ARM_SMCCC_OWNER_SHIFT           24
#define ARM_SMCCC_OWNER_MASK            _AC(0x3F,U)
#define ARM_SMCCC_FUNC_MASK             _AC(0xFFFF,U)

#define ARM_SMCCC_CALL_VAL(type, calling_convention, owner, func_num)           \
        (((type) << ARM_SMCCC_TYPE_SHIFT) |                                     \
         ((calling_convention) << ARM_SMCCC_CONV_SHIFT) |                       \
         (((owner) & ARM_SMCCC_OWNER_MASK) << ARM_SMCCC_OWNER_SHIFT) |          \
         (func_num))

#define ARM_SMCCC_OWNER_ARCH            0

#define ARM_SMCCC_ARCH_WORKAROUND_1_FID             \
    ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,         \
                       ARM_SMCCC_CONV_32,           \
                       ARM_SMCCC_OWNER_ARCH,        \
                       0x8000)

#define ARM_SMCCC_ARCH_WORKAROUND_2_FID             \
    ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,         \
                       ARM_SMCCC_CONV_32,           \
                       ARM_SMCCC_OWNER_ARCH,        \
                       0x7FFF)

#define ARM_SMCCC_ARCH_WORKAROUND_3_FID             \
    ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,         \
                       ARM_SMCCC_CONV_32,           \
                       ARM_SMCCC_OWNER_ARCH,        \
                       0x3FFF)

/* ---- LPAE page table definitions ---- */
#define LPAE_SHIFT              (PAGE_SHIFT - 3)        /* 9 for 4K pages */
#define LPAE_ENTRIES            (_AC(1,U) << LPAE_SHIFT) /* 512 */
#define LPAE_ENTRY_MASK         (LPAE_ENTRIES - 1)      /* 0x1FF */

#define PRTOS_PT_LPAE_SHIFT       LPAE_SHIFT
#define PRTOS_PT_LPAE_ENTRIES     LPAE_ENTRIES
#define PRTOS_PT_LPAE_ENTRY_MASK  LPAE_ENTRY_MASK

/*
 * Page table level shift: for 4K pages with 3-level paging:
 *   Level 3 (page):    shift = 12
 *   Level 2 (block):   shift = 21
 *   Level 1 (block):   shift = 30
 *   Level 0 (table):   shift = 39
 */
#define PRTOS_PT_LEVEL_SHIFT(lvl) ((3 - (lvl)) * LPAE_SHIFT + PAGE_SHIFT)
#define PRTOS_PT_LEVEL_SIZE(lvl)  (_AT(unsigned long, 1) << PRTOS_PT_LEVEL_SHIFT(lvl))
#define PRTOS_PT_LEVEL_MASK(lvl)  (~(PRTOS_PT_LEVEL_SIZE(lvl) - 1))

#define THIRD_SHIFT         PRTOS_PT_LEVEL_SHIFT(3)   /* 12 */
#define THIRD_SIZE          PRTOS_PT_LEVEL_SIZE(3)     /* 4096 */
#define THIRD_MASK          PRTOS_PT_LEVEL_MASK(3)     /* ~0xFFF */

#define SECOND_SHIFT        PRTOS_PT_LEVEL_SHIFT(2)    /* 21 */
#define SECOND_SIZE         PRTOS_PT_LEVEL_SIZE(2)     /* 2MB */
#define SECOND_MASK         PRTOS_PT_LEVEL_MASK(2)

#define FIRST_SHIFT         PRTOS_PT_LEVEL_SHIFT(1)    /* 30 */
#define FIRST_SIZE          PRTOS_PT_LEVEL_SIZE(1)     /* 1GB */
#define FIRST_MASK          PRTOS_PT_LEVEL_MASK(1)

#define ZEROETH_SHIFT       PRTOS_PT_LEVEL_SHIFT(0)    /* 39 */
#define ZEROETH_SIZE        PRTOS_PT_LEVEL_SIZE(0)     /* 512GB */
#define ZEROETH_MASK        PRTOS_PT_LEVEL_MASK(0)

/* Table offset extraction macros */
#define zeroeth_linear_offset(va)  ((va) >> ZEROETH_SHIFT)
#define first_linear_offset(va)    ((va) >> FIRST_SHIFT)
#define second_linear_offset(va)   ((va) >> SECOND_SHIFT)
#define third_linear_offset(va)    ((va) >> THIRD_SHIFT)

#define TABLE_OFFSET(offs)          (_AT(unsigned int, offs) & PRTOS_PT_LPAE_ENTRY_MASK)
#define zeroeth_table_offset(va)    TABLE_OFFSET(zeroeth_linear_offset(va))
#define first_table_offset(va)      TABLE_OFFSET(first_linear_offset(va))
#define second_table_offset(va)     TABLE_OFFSET(second_linear_offset(va))
#define third_table_offset(va)      TABLE_OFFSET(third_linear_offset(va))

/* ---- Virtual memory layout ---- */
#define SLOT0_ENTRY_BITS    39
#define SLOT0(slot)         (_AT(unsigned long, slot) << SLOT0_ENTRY_BITS)

#define IDENTITY_MAPPING_AREA_NR_L0  20
#define PRTOS_VM_MAPPING      SLOT0(IDENTITY_MAPPING_AREA_NR_L0)

#ifndef __ASSEMBLY__
typedef unsigned long vaddr_t;
typedef unsigned long paddr_t;
#endif

#define MB(_mb)     (_AC(_mb, ULL) << 20)
#define GB(_gb)     (_AC(_gb, ULL) << 30)
#define KB(_kb)     (_AC(_kb, ULL) << 10)

#define PRTOS_VIRT_START      (PRTOS_VM_MAPPING + MB(2))
#define PRTOS_VIRT_SIZE       MB(32)
#define PRTOS_NR_ENTRIES(lvl) (PRTOS_VIRT_SIZE / PRTOS_PT_LEVEL_SIZE(lvl))

#define FIXMAP_VIRT_START   (PRTOS_VIRT_START + PRTOS_VIRT_SIZE)
#define FIXMAP_VIRT_SIZE    MB(2)
#define FIXMAP_ADDR(n)      (FIXMAP_VIRT_START + (n) * PAGE_SIZE)

#define FIX_CONSOLE         0   /* The primary UART fixmap slot */

/* need to add the uart address offset in page to the fixmap address */
#define EARLY_UART_VIRTUAL_ADDRESS \
    (FIXMAP_ADDR(FIX_CONSOLE) + (CONFIG_EARLY_UART_BASE_ADDRESS & ~PAGE_MASK))

/* ---- CPU feature flags for alternative patching ---- */
#define SKIP_SYNCHRONIZE_SERROR_ENTRY_EXIT  5
#define ARM_HAS_SB                          16
#define ARM64_WORKAROUND_1508412            17
#define ARM_NCAPS                           18
#define ARM_CB_PATCH                        ARM_NCAPS

/* Instruction patching size */
#define ARCH_PATCH_INSN_SIZE    4

/* Instruction alignment for functions */
#define CONFIG_FUNCTION_ALIGNMENT 4

/* ---- Register frame offsets (from asm-offsets) ---- */
#define UREGS_X0            0
#define UREGS_X1            8
#define UREGS_LR            240
#define UREGS_SP            248
#define UREGS_PC            256
#define UREGS_CPSR          264
#define UREGS_ESR_el2       272
#define UREGS_SPSR_el1      288
#define UREGS_SPSR_fiq      296
#define UREGS_SPSR_irq      300
#define UREGS_SPSR_und      304
#define UREGS_SPSR_abt      308
#define UREGS_SP_el0        312
#define UREGS_SP_el1        320
#define UREGS_ELR_el1       328
#define UREGS_kernel_sizeof 288

/* CPU info structure offsets */
#define CPUINFO_sizeof      352
#define CPUINFO_flags       344

/* Workaround 2 flag */
#define CPUINFO_WORKAROUND_2_FLAG_SHIFT   0
#define CPUINFO_WORKAROUND_2_FLAG (_AC(1, U) << CPUINFO_WORKAROUND_2_FLAG_SHIFT)

/* VCPU context offset */
#define VCPU_arch_saved_context 640

/* Init info */
#define INITINFO_stack      0

/* SMCCC result offsets */
#define SMCCC_RES_a0        0
#define SMCCC_RES_a2        16

/* ---- Assembly helper macros ---- */
#ifdef __ASSEMBLY__

/* Align for function entry */
#define ALIGN .balign CONFIG_FUNCTION_ALIGNMENT

/* Read-only data in a named section */
#define RODATA_SECT(section, label, msg)         \
.pushsection section, "aMS", %progbits, 1 ;     \
label:  .asciz msg;                             \
.popsection

/* ---- Alternative instruction patching (assembly) ---- */

.macro altinstruction_entry orig_offset repl_offset feature orig_len repl_len
	.word \orig_offset - .
	.word \repl_offset - .
	.hword \feature
	.byte \orig_len
	.byte \repl_len
.endm

.macro alternative_insn insn1, insn2, cap, enable = 1
	.if \enable
661:	\insn1
662:	.pushsection .altinstructions, "a"
	altinstruction_entry 661b, 663f, \cap, 662b-661b, 664f-663f
	.popsection
	.pushsection .altinstr_replacement, "ax"
663:	\insn2
664:	.popsection
	.org	. - (664b-663b) + (662b-661b)
	.org	. - (662b-661b) + (664b-663b)
	.endif
.endm

.macro alternative_if_not cap
	.set .Lasm_alt_mode, 0
	.pushsection .altinstructions, "a"
	altinstruction_entry 661f, 663f, \cap, 662f-661f, 664f-663f
	.popsection
661:
.endm

.macro alternative_if cap
	.set .Lasm_alt_mode, 1
	.pushsection .altinstructions, "a"
	altinstruction_entry 663f, 661f, \cap, 664f-663f, 662f-661f
	.popsection
	.pushsection .altinstr_replacement, "ax"
	.align 2
661:
.endm

.macro alternative_else
662:
	.if .Lasm_alt_mode==0
	.pushsection .altinstr_replacement, "ax"
	.else
	.popsection
	.endif
663:
.endm

.macro alternative_cb cb
	.set .Lasm_alt_mode, 0
	.pushsection .altinstructions, "a"
	altinstruction_entry 661f, \cb, ARM_CB_PATCH, 662f-661f, 0
	.popsection
661:
.endm

.macro alternative_endif
664:
	.if .Lasm_alt_mode==0
	.popsection
	.endif
	.org	. - (664b-663b) + (662b-661b)
	.org	. - (662b-661b) + (664b-663b)
.endm

.macro alternative_else_nop_endif
alternative_else
	nops	(662b-661b) / ARCH_PATCH_INSN_SIZE
alternative_endif
.endm

.macro alternative_cb_end
662:
.endm

    /* NOP sequence  */
    .macro nops, num
    .rept   \num
    nop
    .endr
    .endm

    /* Speculative barrier */
    .macro sb
alternative_if_not ARM_HAS_SB
    dsb nsh
    isb
alternative_else
    .inst 0xd50330ff
    nop
alternative_endif
    .endm

/* ---- CPU info and per-cpu macros ---- */

    /*
     * @dst: Result of get_cpu_info()
     */
    .macro  adr_cpu_info, dst
    add     \dst, sp, #STACK_SIZE
    and     \dst, \dst, #~(STACK_SIZE - 1)
    sub     \dst, \dst, #CPUINFO_sizeof
    .endm

    /*
     * @dst: Result of READ_ONCE(per_cpu(sym, smp_processor_id()))
     * @sym: The name of the per-cpu variable
     * @tmp: scratch register
     */
    .macro  ldr_this_cpu, dst, sym, tmp
    ldr     \dst, =per_cpu__\sym
    mrs     \tmp, tpidr_el2
    ldr     \dst, [\dst, \tmp]
    .endm

    .macro  ret
        /* ret opcode */
        .inst 0xd65f03c0
        sb
    .endm

    /* clearbhb instruction clearing the branch history */
    .macro clearbhb
        hint    #22
    .endm

/* ---- Early printk macros ---- */
#ifdef CONFIG_EARLY_PRINTK

#define PRINT_SECT(section, string)         \
        mov   x3, lr                       ;\
        adr_l x0, 98f                      ;\
        bl    asm_puts                     ;\
        mov   lr, x3                       ;\
        RODATA_SECT(section, 98, string)

#define PRINT(string) PRINT_SECT(.boot.rodata.str, string)
#define PRINT_ID(string) PRINT_SECT(.boot.rodata.idmap, string)

.macro print_reg xb
        mov   x0, \xb
        mov   x4, lr
        bl    asm_putn
        mov   lr, x4
.endm

#else /* !CONFIG_EARLY_PRINTK */
#define PRINT(s)
#define PRINT_ID(s)

.macro print_reg xb
.endm

#endif /* CONFIG_EARLY_PRINTK */

/*
 * PC relative adr <reg>, <symbol> where <symbol> is
 * within the range +/- 4GB of the PC.
 */
.macro  adr_l, dst, sym
        adrp \dst, \sym
        add  \dst, \dst, :lo12:\sym
.endm

/* Register aliases */
lr      .req    x30

/* Label macro (C preprocessor style) */
#define LABEL(name) \
    .globl name; \
    name:

/* Function entry/exit macros */
#ifndef FUNC
#define FUNC(name) \
    .type name, STT_FUNC; \
    .globl name; .hidden name; \
    .balign CONFIG_FUNCTION_ALIGNMENT; \
    name:
#endif

#ifndef FUNC_LOCAL
#define FUNC_LOCAL(name) \
    .type name, STT_FUNC; \
    .balign CONFIG_FUNCTION_ALIGNMENT; \
    name:
#endif

#ifndef END
#define END(name) .size name, . - name
#endif

#endif /* __ASSEMBLY__ */

#endif /* _PRTOS_AARCH64_ARM_DEFS_H_ */
