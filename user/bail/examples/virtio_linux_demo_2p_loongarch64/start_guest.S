/*
 * Boot stub for Guest Partition (Partition 1) - LoongArch64 Virtio Frontend.
 *
 * Memory layout (GPA space, base = 0xA0000000):
 *   0xA0000000  PRTOS image header + boot stub
 *   0xA0200000  Linux kernel (vmlinux.bin, 2MB aligned)
 *   0xA4000000  guest rootfs overlay CPIO (4K aligned)
 *
 * SMP: 2 vCPUs (pCPU 2-3).
 * Console: none (uses virtio-console via shared memory).
 */

	.section .text
	.globl _boot
_boot:
	/* Set up Linux boot protocol arguments */
	li.d	$a0, 0
	li.d	$a1, 0
	li.d	$a2, 0

	/* Jump to Linux kernel_entry */
	la.local $t0, _linux_image_guest_start
	li.d	$t1, 0x1622000
	add.d	$t0, $t0, $t1
	jr	$t0

	.section .linux_image_guest, "ax", @progbits
	.balign 0x200000
	.globl _linux_image_guest_start
_linux_image_guest_start:
	.incbin "vmlinux.bin"
	.globl _linux_image_guest_end
_linux_image_guest_end:

	.section .initrd_guest, "a", @progbits
	.balign 4096
	.globl _initrd_guest_start
_initrd_guest_start:
	.incbin "guest_rootfs_overlay.cpio"
	.globl _initrd_guest_end
_initrd_guest_end:
