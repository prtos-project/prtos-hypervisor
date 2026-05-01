/*
 * Boot stub for Linux guest running in a PRTOS hw-virt partition (LoongArch64).
 *
 * PRTOS JMP_PARTITION enters at _boot with:
 *   a0 = 0 (vCPU id), a1 = PCT physical address
 *
 * LoongArch64 Linux boot protocol requires:
 *   a0 = fw_arg0 (0 = non-EFI boot)
 *   a1 = fw_arg1 (command line physical address, 0 if CONFIG_CMDLINE_FORCE)
 *   a2 = fw_arg2 (EFI system table pointer, 0 if non-EFI)
 *
 * Memory layout (GPA space, base = 0x80000000, in QEMU high memory):
 *   0x80000000  PRTOS image header + boot stub
 *   0x80200000  Linux kernel (vmlinux.bin, flat binary, 2MB aligned)
 *   Entry point: 0x81822000 (kernel_entry = base + 0x200000 + 0x1622000)
 */

	.section .text
	.globl _boot
_boot:
	/* Determine which vCPU we are by reading CSR_CPUID (0x20).
	 * Boot CPU (id 0) jumps to kernel_entry; secondaries spin on
	 * the IOCSR mailbox MBUF0 (0x1020), which Linux's
	 * loongson_boot_secondary() writes with the address of
	 * smpboot_entry (PA, requires DMW0 prefix to enter). */
	csrrd	$t2, 0x20
	bnez	$t2, _secondary_wait

	/* Write 'V' to UART (NS16550 at 0x1FE001E0) for verification */
	li.d	$t0, 0x1FE001E0
1:	ld.bu	$t1, $t0, 5	/* Read LSR (offset 5) */
	andi	$t1, $t1, 0x20	/* Check THRE (bit 5) */
	beqz	$t1, 1b		/* Wait until ready */
	li.w	$t1, 'V'
	st.b	$t1, $t0, 0	/* Write to THR */

	/* Set up Linux boot protocol arguments */
	li.d	$a0, 0		/* fw_arg0 = 0 (non-EFI boot) */
	li.d	$a1, 0		/* fw_arg1 = 0 (cmdline forced in config) */
	li.d	$a2, 0		/* fw_arg2 = 0 (no EFI system table) */

	/* Jump to Linux kernel_entry (embedded via .incbin).
	 * Offset (kernel_entry - _text) is supplied by the Makefile via
	 * -DLINUX_KERNEL_ENTRY_OFFSET=0x... so it auto-tracks the kernel build. */
#ifndef LINUX_KERNEL_ENTRY_OFFSET
#define LINUX_KERNEL_ENTRY_OFFSET 0x1628000
#endif
	la.local $t0, _linux_image_start
	li.d	$t1, LINUX_KERNEL_ENTRY_OFFSET
	add.d	$t0, $t0, $t1
	jr	$t0

	/* Secondary vCPU bootstrap: poll IOCSR MBUF0 until Linux writes
	 * the smpboot_entry physical address, then convert to a DMW0
	 * cached virtual address (PA | 0x9000_0000_0000_0000) and jump. */
_secondary_wait:
	li.d	$t0, 0x1020
	iocsrwr.d $zero, $t0
1:	iocsrrd.d $t1, $t0
	beqz	$t1, 1b
	/* Set bits 63:60 = 0x9 (DMW1 cached prefix) so the PA becomes a
	 * VA that the secondary can fetch from while DMW1 is still set
	 * to PRTOS's host value (0x9000_0000_0000_0019). */
	li.d	$t2, 0x9
	bstrins.d $t1, $t2, 63, 60
	jr	$t1

	.section .linux_image, "ax", @progbits
	.balign 0x200000	/* 2MB aligned: Linux kernel at PA 0x200000 */
	.globl _linux_image_start
_linux_image_start:
	.incbin "vmlinux.bin"
	.globl _linux_image_end
_linux_image_end:
