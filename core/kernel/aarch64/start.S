/*
 * FILE: start.S
 *
 * The execution of prtos starts from this file (start symbol)
 *
 * www.prtos.org
 */


#include <linkage.h>
#include <prtosef.h>
#include <hypercalls.h>
#include <arch/asm_offsets.h>
#include <arch/irqs.h>
#include <arch/paging.h>
//#include <arch/processor.h>
#include <arch/segments.h>
#include <arch/prtos_def.h>
//#include <arch/layout.h>

#include <xen/xen_config.h>

#include <asm/page.h>
#include <asm/early_printk.h>

#ifdef CONFIG_ARM_EFI
#include <efi/efierr.h>
#include <asm/arm64/efibind.h>
#endif

#define KB(_kb)     (_AC(_kb, ULL) << 10)
#define MB(_mb)     (_AC(_mb, ULL) << 20)
#define GB(_gb)     (_AC(_gb, ULL) << 30)


#define __HEAD_FLAG_PAGE_SIZE   ((PAGE_SHIFT - 10) / 2)

#define __HEAD_FLAG_PHYS_BASE   1

#define __HEAD_FLAGS            ((__HEAD_FLAG_PAGE_SIZE << 1) | \
                                 (__HEAD_FLAG_PHYS_BASE << 3))

#include "debug-pl011.inc"


.section .boot.text, "ax"

FUNC(start)
        /*
         * DO NOT MODIFY. Image header expected by Linux boot-loaders.
         */
efi_head:
        /*
         * This add instruction has no meaningful effect except that
         * its opcode forms the magic "MZ" signature of a PE/COFF file
         * that is required for UEFI applications.
         */
        add     x13, x18, #0x16
        b       real_start           /* branch to kernel start */
END(start)



FUNC_LOCAL(real_start)
        /* BSS should be zeroed when booting without EFI */
        mov   x26, #0                /* x26 := skip_zero_bss */

real_start_efi:
        msr   DAIFSet, 0xf           /* Disable all interrupts */

        /* Save the bootloader arguments in less-clobberable registers */
        mov   x21, x0                /* x21 := DTB, physical address  */

        /* Find out where we are */
        ldr   x0, =start
        adr   x19, start             /* x19 := paddr (start) */
        sub   x20, x19, x0           /* x20 := phys-offset */

        /* Using the DTB in the .dtb section? */
.ifnes CONFIG_DTB_FILE,""
        adr_l x21, _sdtb
.endif

        /* Initialize the UART if earlyprintk has been enabled. */
#ifdef CONFIG_EARLY_PRINTK
        bl    init_uart
#endif
        PRINT("- Boot CPU booting -\r\n")

        bl    check_cpu_mode
        bl    cpu_init

        ldr   lr, =primary_switched
        b     enable_boot_cpu_mm

primary_switched:
        bl    zero_bss
        PRINT("- Ready -\r\n")
        /* Setup the arguments for start_xen and jump to C world */
        mov   x0, x20                /* x0 := cpu_id */
        mov   x1, x21                /* x1 := idle */
        ldr   x2, =setup_kernel //FIXME: just comment it for build pass
        b     launch
END(real_start)

FUNC(init_secondary)
        msr   DAIFSet, 0xf           /* Disable all interrupts */

        /* Find out where we are */
        ldr   x0, =start
        adr   x19, start             /* x19 := paddr (start) */
        sub   x20, x19, x0           /* x20 := phys-offset */

        mrs   x0, mpidr_el1
        ldr   x13, =(~MPIDR_HWID_MASK)
        bic   x24, x0, x13           /* Mask out flags to get CPU ID */

        /* Wait here until __cpu_up is ready to handle the CPU */
        adr_l x0, smp_up_cpu  // FIXME: here just comment it to build pass
        dsb   sy
2:      ldr   x1, [x0]
        cmp   x1, x24
        beq   1f
        wfe
        b     2b
1:

#ifdef CONFIG_EARLY_PRINTK
        ldr   x23, =CONFIG_EARLY_UART_BASE_ADDRESS /* x23 := UART base address */   // 0x9000000
        PRINT_ID("- CPU ")
        print_reg x24
        PRINT_ID(" booting -\r\n")
#endif
        bl    check_cpu_mode
        bl    cpu_init

        ldr   lr, =secondary_switched
        b     enable_secondary_cpu_mm

secondary_switched:
        PRINT("- Ready -\r\n")
        /* Jump to C world */
        ldr   x2, =start_secondary  // FIXME: here just comment it for build pass
        b     launch_ap
END(init_secondary)

/*
 * Check if the CPU has been booted in Hypervisor mode.
 * This function will never return when the CPU is booted in another mode
 * than Hypervisor mode.
 *
 * Clobbers x0 - x5
 */
FUNC_LOCAL(check_cpu_mode)
        PRINT_ID("- Current EL ")
        mrs   x5, CurrentEL
        print_reg x5
        PRINT_ID(" -\r\n")

        /* Are we in EL2 */
        cmp   x5, #PSR_MODE_EL2t
        ccmp  x5, #PSR_MODE_EL2h, #0x4, ne
        b.ne  1f /* No */
        ret
1:
        /* OK, we're boned. */
        PRINT_ID("- PRTOS must be entered in NS EL2 mode -\r\n")
        PRINT_ID("- Please update the bootloader -\r\n")
        b fail
END(check_cpu_mode)

/*
 * Zero BSS
 *
 * Inputs:
 *   x26: Do we need to zero BSS?
 *
 * Clobbers x0 - x3
 */
FUNC_LOCAL(zero_bss)
        /* Zero BSS only when requested */
        cbnz  x26, skip_bss

        PRINT("- Zero BSS -\r\n")
        ldr   x0, =_sbss       /* x0 := vaddr(__bss_start) */  
        ldr   x1, =_ebss       /* x1 := vaddr(__bss_end)   */

1:      str   xzr, [x0], #8
        cmp   x0, x1
        b.lo  1b

skip_bss:
        ret
END(zero_bss)

/*
 * Initialize the processor for turning the MMU on.
 *
 * Clobbers x0 - x3
 */
FUNC_LOCAL(cpu_init)
        PRINT_ID("- Initialize CPU -\r\n")

        /* Set up memory attribute type tables */
        ldr   x0, =MAIRVAL
        msr   mair_el2, x0

        /*
         * Set up TCR_EL2:
         * PS -- Based on ID_AA64MMFR0_EL1.PARange
         * Top byte is used
         * PT walks use Inner-Shareable accesses,
         * PT walks are write-back, write-allocate in both cache levels,
         * 48-bit virtual address space goes through this table.
         */
        ldr   x0, =(TCR_RES1|TCR_SH0_IS|TCR_ORGN0_WBWA|TCR_IRGN0_WBWA|TCR_T0SZ(64-48))
        /* ID_AA64MMFR0_EL1[3:0] (PARange) corresponds to TCR_EL2[18:16] (PS) */
        mrs   x1, ID_AA64MMFR0_EL1
        /* Limit to 48 bits, 256TB PA range (#5) */
        ubfm  x1, x1, #0, #3
        mov   x2, #5
        cmp   x1, x2
        csel  x1, x1, x2, lt

        bfi   x0, x1, #16, #3

        msr   tcr_el2, x0

        ldr   x0, =SCTLR_EL2_SET
        msr   SCTLR_EL2, x0
        isb

        /*
         * Ensure that any exceptions encountered at EL2
         * are handled using the EL2 stack pointer, rather
         * than SP_EL0.
         */
        msr spsel, #1
        ret
END(cpu_init)

/*
 * Setup the initial stack and jump to the C world
 *
 * Inputs:
 *   x0 : Argument 0 of the C function to call
 *   x1 : Argument 1 of the C function to call
 *   x2 : C entry point
 *
 * Clobbers x3
 */
FUNC_LOCAL(launch)
        ldr   x3, =init_data_prtos  // need adjust for PRTOS hyperviosr
        add   x3, x3, #0 /* Find the boot-time stack */  // need adjust for PRTOS hyperviosr
        ldr   x3, [x3]

        mov   x0, #0                /* x0 := cpu_id */
        mov   x1, x3                /* x1 := idle */

        add   x3, x3, #CONFIG_KSTACK_SIZE     /* (which grows down from the top). */
        sub   x3, x3, #CPUINFO_sizeof /* Make room for CPU save record */  // need adjust for PRTOS hyperviosr
        mov   sp, x3

        /* Jump to C world */
        br    x2
END(launch)


FUNC_LOCAL(launch_ap)
        ldr   x3, =init_data
        add   x3, x3, #INITINFO_stack /* Find the boot-time stack */
        ldr   x3, [x3]
        add   x3, x3, #STACK_SIZE     /* (which grows down from the top). */
        sub   x3, x3, #CPUINFO_sizeof /* Make room for CPU save record */
        mov   sp, x3

        /* Jump to C world */
        br    x2
END(launch_ap)

/* Fail-stop */
FUNC_LOCAL(fail)
        PRINT_ID("- Boot failed -\r\n")
1:      wfe
        b     1b
END(fail)

#ifdef CONFIG_EARLY_PRINTK
/*
 * Initialize the UART. Should only be called on the boot CPU.
 *
 * Output:
 *  x23: Early UART base physical address
 *
 * Clobbers x0 - x1
 */
FUNC_LOCAL(init_uart)
        ldr   x23, =CONFIG_EARLY_UART_BASE_ADDRESS   // comes from include/generated/xen_autoconf.h
#ifdef CONFIG_EARLY_UART_INIT
        early_uart_init x23, 0  // comes from arch/arm/arm64/debug-pl011.inc
#endif
        PRINT("- UART enabled -\r\n")
        ret
END(init_uart)

/*
 * Print early debug messages.
 * Note: This function must be called from assembly.
 * x0: Nul-terminated string to print.
 * x23: Early UART base address
 * Clobbers x0-x1
 */
FUNC(asm_puts)
        early_uart_ready x23, 1
        ldrb  w1, [x0], #1           /* Load next char */
        cbz   w1, 1f                 /* Exit on nul */
        early_uart_transmit x23, w1
        b     asm_puts
1:
        ret
END(asm_puts)

/*
 * Print a 64-bit number in hex.
 * Note: This function must be called from assembly.
 * x0: Number to print.
 * x23: Early UART base address
 * Clobbers x0-x3
 */
FUNC(asm_putn)
        adr_l x1, hex
        mov   x3, #16
1:
        early_uart_ready x23, 2
        and   x2, x0, #(0xf<<60)     /* Mask off the top nybble */
        lsr   x2, x2, #60
        ldrb  w2, [x1, x2]           /* Convert to a char */
        early_uart_transmit x23, w2
        lsl   x0, x0, #4             /* Roll it through one nybble at a time */
        subs  x3, x3, #1
        b.ne  1b
        ret
END(asm_putn)

RODATA_SECT(.boot.rodata.idmap, hex, "0123456789abcdef")

#endif /* CONFIG_EARLY_PRINTK */

/* This provides a C-API version of __lookup_processor_type
 * TODO: For now, the implementation return NULL every time
 */
FUNC(lookup_processor_type)
        mov  x0, #0
        ret
END(lookup_processor_type)

#ifdef CONFIG_ARM_EFI
/*
 *  Function to transition from EFI loader in C, to PRTOS entry point.
 *  void noreturn efi_xen_start(void *fdt_ptr, uint32_t fdt_size);
 */
FUNC(efi_xen_start)
        /*
         * Preserve x0 (fdt pointer) across call to __flush_dcache_area,
         * restore for entry into PRTOS.
         */
        mov   x20, x0

        /* flush dcache covering the FDT updated by EFI boot code */
        // bl    __flush_dcache_area // FIXME: here just comment it to build pass

        /*
         * Flush dcache covering current runtime addresses
         * of PRTOS text/data. Then flush all of icache.
         */
        // adr_l x1, _start //FIXME: just  comment it to build pass
        mov   x0, x1
        // adr_l x2, _end  //FIXME: just  comment it to build pass
        sub   x1, x2, x1

        // bl    __flush_dcache_area //FIXME: just  comment it to build pass
        ic    ialluis
        tlbi  alle2

        /*
         * Turn off cache and MMU as PRTOS expects. EFI enables them, but also
         * mandates a 1:1 (unity) VA->PA mapping, so we can turn off the
         * MMU while executing EFI code before entering PRTOS.
         * The EFI loader calls this to start PRTOS.
         */

        /* Turn off Dcache and MMU */
        mrs   x0, sctlr_el2
        bic   x0, x0, #1 << 0        /* clear SCTLR.M */
        bic   x0, x0, #1 << 2        /* clear SCTLR.C */
        msr   sctlr_el2, x0
        isb

        /* Jump to PRTOS entry point */
        mov   x0, x20
        mov   x1, xzr
        mov   x2, xzr
        mov   x3, xzr
        /*
         * The EFI stub and PRTOS may share some information living in
         * BSS. Don't zero BSS to avoid loosing them.
         *
         * Note that the EFI firmware has already zeroed BSS for us
         * before jump into the stub.
         */
        mov   x26, #1               /* x26 := skip_zero_bss */

        b     real_start_efi
END(efi_xen_start)


#endif /* CONFIG_ARM_EFI */










        
__boot_halt_system:

#ifdef CONFIG_SMP
	
ENTRY(smp_start16)

smp_gdt_descr:
        .word EARLY_PRTOS_GDT_ENTRIES*8-1
        .quad _VIRT2PHYS(early_gdt_table)

smp_idt_descr:
ENTRY(smp_start16_end)
#endif        /*CONFIG_SMP*/
        
.section .boot.data, "ax"
        .word 0
_reset_addr:
        .quad 0
_gdt_desc:
        .word EARLY_PRTOS_GDT_ENTRIES*8-1
        .quad _VIRT2PHYS(early_gdt_table)
_smp_stack:
        .zero 64

.bss
.align 1024
.globl _stack
_stack:
	.zero (1024)
.globl _estack
_estack:


.section .kbuild_info, "a"
.ascii "BUILD_TIME="TO_STR(BUILD_TIME)"\n"
.ascii "BUILD_IDR="TO_STR(BUILD_IDR)"\n"
.previous
