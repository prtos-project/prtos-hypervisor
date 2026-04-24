/*
 * Boot stub for Linux guest running in PRTOS partition 0 (mix_os_demo_riscv64).
 *
 * Memory layout (IPA space, base = 0x90000000):
 *   0x90000000  PRTOS image header + boot stub
 *   0x90001000  DTB (embedded)
 *   0x90200000  Linux Image (embedded via .incbin, 2MB aligned)
 */

	.section .text
	.globl _boot
_boot:
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
