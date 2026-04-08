/*
 * Boot stub for System Partition (Partition 0) - RISC-V 64 Virtio Backend.
 *
 * PRTOS JMP_PARTITION enters at _boot with:
 *   a0 = 0 (hartid), a1 = PCT physical address
 * RISC-V Linux boot protocol requires:
 *   a0 = hartid, a1 = DTB physical address
 *
 * Memory layout (IPA space, base = 0x88000000):
 *   0x88000000  PRTOS image header + boot stub
 *   0x88001000  DTB (embedded, System partition)
 *   0x88200000  Linux Image (embedded, 2MB aligned)
 *   (after Image) rootfs overlay CPIO (4K aligned)
 *
 * SMP: 2 vCPUs (pCPU 0-1), Linux handles AP startup via SBI HSM.
 * Console: NS16550 UART at 0x10000000 (ttyS0)
 */

	.section .text
	.globl _boot
_boot:
	/* Load DTB address for Linux boot protocol */
	lla	a1, _dtb_sys_start

	/* a0 = 0 (hartid) */
	li	a0, 0

	/* Jump to Linux Image entry point */
	lla	t0, _linux_image_sys_start
	jr	t0

	.section .dtb_sys, "a", @progbits
	.balign 4096
	.globl _dtb_sys_start
_dtb_sys_start:
	.incbin "linux_system.dtb"
	.globl _dtb_sys_end
_dtb_sys_end:

	.section .linux_image_sys, "ax", @progbits
	.balign 0x200000
	.globl _linux_image_sys_start
_linux_image_sys_start:
	.incbin "Image"
	.globl _linux_image_sys_end
_linux_image_sys_end:

	.section .initrd_sys, "a", @progbits
	.balign 4096
	.globl _initrd_sys_start
_initrd_sys_start:
	.incbin "rootfs_overlay.cpio"
	.globl _initrd_sys_end
_initrd_sys_end:
