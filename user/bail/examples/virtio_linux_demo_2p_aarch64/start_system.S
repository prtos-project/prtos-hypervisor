/*
 * Boot stub for System Partition (Partition 0) - Virtio Backend host.
 * AArch64 hw-virt: Linux boots via ARM64 boot protocol (x0=DTB).
 *
 * Memory layout (IPA space, base = 0x10000000):
 *   0x10000000  PRTOS image header + boot stub
 *   0x10001000  DTB (embedded, System partition)
 *   0x10200000  Linux Image (embedded, 2MB aligned)
 *   (after Image) rootfs overlay CPIO (embedded, 4K aligned)
 *
 * SMP: 2 vCPUs (pCPU 0-1), Linux handles AP startup via PRTOS PSCI.
 * Console: PL011 UART (ttyAMA0) -> serial stdio
 */

	.section .text
	.globl _boot
_boot:
	/* x0 from PRTOS JMP_PARTITION = PCT physical address (ignored) */

	/* Load DTB address for Linux boot protocol */
	adrp	x0, _dtb_sys_start
	add	x0, x0, :lo12:_dtb_sys_start

	/* x1, x2, x3 must be zero per ARM64 boot protocol */
	mov	x1, xzr
	mov	x2, xzr
	mov	x3, xzr

	/* Set up initrd location in DTB is done at build time - the Makefile
	 * injects linux,initrd-start/end into the DTB chosen node with
	 * the correct addresses (0x14000000 + CPIO size). */

	/* Jump to Linux Image entry point */
	adrp	x4, _linux_image_sys_start
	add	x4, x4, :lo12:_linux_image_sys_start
	br	x4

/* ------------------------------------------------------------------ */
/* Embedded binary data sections                                       */
/* ------------------------------------------------------------------ */

	.section .dtb_sys, "a", %progbits
	.balign 4096
	.globl _dtb_sys_start
_dtb_sys_start:
	.incbin "linux_system.dtb"
	.globl _dtb_sys_end
_dtb_sys_end:

	.section .linux_image_sys, "ax", %progbits
	.balign 0x200000	/* 2MB aligned for Linux Image requirement */
	.globl _linux_image_sys_start
_linux_image_sys_start:
	.incbin "Image"
	.globl _linux_image_sys_end
_linux_image_sys_end:

	.section .initrd_sys, "a", %progbits
	.balign 4096
	.globl _initrd_sys_start
_initrd_sys_start:
	.incbin "rootfs_overlay.cpio"
	.globl _initrd_sys_end
_initrd_sys_end:
