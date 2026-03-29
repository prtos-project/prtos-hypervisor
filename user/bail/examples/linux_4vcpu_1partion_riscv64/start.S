/*
 * Boot stub for Linux guest running in a PRTOS hw-virt partition (RISC-V 64).
 *
 * PRTOS JMP_PARTITION enters at _boot with:
 *   a0 = 0 (hartid), a1 = PCT physical address
 *
 * RISC-V Linux boot protocol requires:
 *   a0 = hartid, a1 = DTB physical address
 *
 * Memory layout (IPA space, base = 0x88000000):
 *   0x88000000  PRTOS image header + boot stub
 *   0x88001000  DTB (embedded)
 *   0x88200000  Linux Image (embedded via .incbin, 2MB aligned)
 */

	.section .text
	.globl _boot
_boot:
	/* Test: direct VS-mode MMIO write to UART through G-stage */
	li	t0, 0x10000000
1:	lbu	t1, 5(t0)	/* Read LSR (offset 5) */
	andi	t1, t1, 0x20	/* Check THRE (bit 5) */
	beqz	t1, 1b		/* Wait until ready */
	li	t1, 'V'		/* 'V' for VS-mode direct write */
	sb	t1, 0(t0)	/* Write to THR */

	/* Load DTB address for Linux boot protocol */
	lla	a1, _dtb_start

	/* a0 = 0 (hartid) */
	li	a0, 0

	/* Jump to Linux Image entry point (embedded via .incbin) */
	lla	t0, _linux_image_start
	jr	t0

	.section .dtb, "a", @progbits
	.balign 4096
	.globl _dtb_start
_dtb_start:
	.incbin "linux_guest.dtb"
	.globl _dtb_end
_dtb_end:

	.section .linux_image, "ax", @progbits
	.balign 0x200000	/* 2MB aligned for Linux Image requirement */
	.globl _linux_image_start
_linux_image_start:
	.incbin "Image"
	.globl _linux_image_end
_linux_image_end:
