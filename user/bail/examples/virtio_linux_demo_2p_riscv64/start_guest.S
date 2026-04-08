/*
 * Boot stub for Guest Partition (Partition 1) - RISC-V 64 Virtio Frontend.
 *
 * Memory layout (IPA space, base = 0x90000000):
 *   0x90000000  PRTOS image header + boot stub
 *   0x90001000  DTB (embedded, Guest partition)
 *   0x90200000  Linux Image (embedded, 2MB aligned)
 *   (after Image) guest rootfs overlay CPIO (4K aligned)
 *
 * SMP: 2 vCPUs (pCPU 2-3), Linux handles AP startup via SBI HSM.
 * Console: none (uses virtio-console via shared memory).
 */

	.section .text
	.globl _boot
_boot:
	/* Load DTB address for Linux boot protocol */
	lla	a1, _dtb_guest_start

	/* a0 = 0 (hartid) */
	li	a0, 0

	/* Jump to Linux Image entry point */
	lla	t0, _linux_image_guest_start
	jr	t0

	.section .dtb_guest, "a", @progbits
	.balign 4096
	.globl _dtb_guest_start
_dtb_guest_start:
	.incbin "linux_guest.dtb"
	.globl _dtb_guest_end
_dtb_guest_end:

	.section .linux_image_guest, "ax", @progbits
	.balign 0x200000
	.globl _linux_image_guest_start
_linux_image_guest_start:
	.incbin "Image"
	.globl _linux_image_guest_end
_linux_image_guest_end:

	.section .initrd_guest, "a", @progbits
	.balign 4096
	.globl _initrd_guest_start
_initrd_guest_start:
	.incbin "guest_rootfs_overlay.cpio"
	.globl _initrd_guest_end
_initrd_guest_end:
