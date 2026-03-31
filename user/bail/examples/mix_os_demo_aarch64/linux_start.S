/*
 * Boot stub for Linux guest running in PRTOS partition 1 (mix_os_demo_aarch64).
 *
 * Memory layout (IPA space, base = 0x10000000):
 *   0x10000000  PRTOS image header + boot stub
 *   0x10001000  DTB (embedded)
 *   0x10200000  Linux Image (embedded, 2MB aligned)
 */

	.section .text
	.globl _boot
_boot:
	/* Load DTB address for Linux boot protocol */
	adrp	x0, _dtb_start
	add	x0, x0, :lo12:_dtb_start

	/* x1, x2, x3 must be zero per ARM64 boot protocol */
	mov	x1, xzr
	mov	x2, xzr
	mov	x3, xzr

	/* Jump to Linux Image entry point */
	adrp	x4, _linux_image_start
	add	x4, x4, :lo12:_linux_image_start
	br	x4

	.section .dtb, "a", %progbits
	.balign 4096
	.globl _dtb_start
_dtb_start:
	.incbin "linux_guest.dtb"
	.globl _dtb_end
_dtb_end:

	.section .linux_image, "ax", %progbits
	.balign 0x200000
	.globl _linux_image_start
_linux_image_start:
	.incbin "Image"
	.globl _linux_image_end
_linux_image_end:
