/*
 * Boot stub for Guest Partition (Partition 1) - Virtio Frontend client.
 * AArch64 hw-virt: Linux boots via ARM64 boot protocol (x0=DTB).
 *
 * Memory layout (IPA space, base = 0x18000000):
 *   0x18000000  PRTOS image header + boot stub
 *   0x18001000  DTB (embedded, Guest partition)
 *   0x18200000  Linux Image (embedded, 2MB aligned)
 *   (after Image) guest rootfs overlay CPIO (4K aligned)
 *
 * SMP: 2 vCPUs (pCPU 2-3), Linux handles AP startup via PRTOS PSCI.
 * Console: no direct UART; uses virtio-console via shared memory.
 */

	.section .text
	.globl _boot
_boot:
	/* x0 from PRTOS JMP_PARTITION = PCT physical address (ignored) */

	/* Load DTB address for Linux boot protocol */
	adrp	x0, _dtb_guest_start
	add	x0, x0, :lo12:_dtb_guest_start

	/* x1, x2, x3 must be zero per ARM64 boot protocol */
	mov	x1, xzr
	mov	x2, xzr
	mov	x3, xzr

	/* Jump to Linux Image entry point */
	adrp	x4, _linux_image_guest_start
	add	x4, x4, :lo12:_linux_image_guest_start
	br	x4

/* ------------------------------------------------------------------ */
/* Embedded binary data sections                                       */
/* ------------------------------------------------------------------ */

	.section .dtb_guest, "a", %progbits
	.balign 4096
	.globl _dtb_guest_start
_dtb_guest_start:
	.incbin "linux_guest.dtb"
	.globl _dtb_guest_end
_dtb_guest_end:

	.section .linux_image_guest, "ax", %progbits
	.balign 0x200000	/* 2MB aligned for Linux Image requirement */
	.globl _linux_image_guest_start
_linux_image_guest_start:
	.incbin "Image"
	.globl _linux_image_guest_end
_linux_image_guest_end:

	.section .initrd_guest, "a", %progbits
	.balign 4096
	.globl _initrd_guest_start
_initrd_guest_start:
	.incbin "guest_rootfs_overlay.cpio"
	.globl _initrd_guest_end
_initrd_guest_end:
