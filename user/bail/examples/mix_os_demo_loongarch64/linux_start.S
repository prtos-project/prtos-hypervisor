/*
 * Boot stub for Linux guest running in PRTOS partition 0 (mix_os_demo_loongarch64).
 *
 * PRTOS JMP_PARTITION enters at _boot with:
 *   a0 = 0 (vCPU id), a1 = PCT physical address
 *
 * LoongArch64 Linux boot protocol requires:
 *   a0 = fw_arg0 (0 = non-EFI boot)
 *   a1 = fw_arg1 (command line physical address, 0 if CONFIG_CMDLINE_FORCE)
 *   a2 = fw_arg2 (EFI system table pointer, 0 if non-EFI)
 *
 * Memory layout (GPA space, base = 0x80000000):
 *   0x80000000  PRTOS image header + boot stub
 *   0x80200000  Linux kernel (vmlinux.bin, flat binary, 2MB aligned)
 *   Entry point: kernel_entry = base + 0x200000 + 0x1622000
 */

	.section .text
	.globl _boot
_boot:
	/* Write 'M' to UART (NS16550 at 0x1FE001E0) for verification */
	li.d	$t0, 0x1FE001E0
1:	ld.bu	$t1, $t0, 5	/* Read LSR (offset 5) */
	andi	$t1, $t1, 0x20	/* Check THRE (bit 5) */
	beqz	$t1, 1b		/* Wait until ready */
	li.w	$t1, 'M'
	st.b	$t1, $t0, 0	/* Write to THR */

	/* Set up Linux boot protocol arguments */
	li.d	$a0, 0		/* fw_arg0 = 0 (non-EFI boot) */
	li.d	$a1, 0		/* fw_arg1 = 0 (cmdline forced in config) */
	li.d	$a2, 0		/* fw_arg2 = 0 (no EFI system table) */

	/* Jump to Linux kernel_entry (embedded via .incbin at 0x200000 offset) */
	la.local $t0, _linux_image_start
	/* Entry offset = kernel_entry - _text = 0x1622000 */
	li.d	$t1, 0x1622000
	add.d	$t0, $t0, $t1
	jr	$t0

	.section .linux_image, "ax", @progbits
	.balign 0x200000	/* 2MB aligned: Linux kernel at GPA 0x80200000 */
	.globl _linux_image_start
_linux_image_start:
	.incbin "vmlinux.bin"
	.globl _linux_image_end
_linux_image_end:
