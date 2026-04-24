/*
 * Boot stub for System Partition (Partition 0) - LoongArch64 Virtio Backend.
 *
 * LoongArch64 Linux boot protocol:
 *   a0 = fw_arg0 (0 = non-EFI boot)
 *   a1 = fw_arg1 (0 = cmdline forced in config)
 *   a2 = fw_arg2 (0 = no EFI system table)
 *
 * Memory layout (GPA space, base = 0x80000000):
 *   0x80000000  PRTOS image header + boot stub
 *   0x80200000  Linux kernel (vmlinux.bin, 2MB aligned)
 *   0x8C000000  rootfs overlay CPIO (4K aligned)
 *
 * SMP: 2 vCPUs (pCPU 0-1).
 * Console: NS16550 UART at 0x1FE001E0 (ttyS0)
 */

	.section .text
	.globl _boot
_boot:
	/* Write 'S' to UART (NS16550 at 0x1FE001E0) for verification */
	li.d	$t0, 0x1FE001E0
1:	ld.bu	$t1, $t0, 5
	andi	$t1, $t1, 0x20
	beqz	$t1, 1b
	li.w	$t1, 'S'
	st.b	$t1, $t0, 0

	/* Set up Linux boot protocol arguments */
	li.d	$a0, 0
	li.d	$a1, 0
	li.d	$a2, 0

	/* Jump to Linux kernel_entry */
	la.local $t0, _linux_image_sys_start
	li.d	$t1, 0x1622000
	add.d	$t0, $t0, $t1
	jr	$t0

	.section .linux_image_sys, "ax", @progbits
	.balign 0x200000
	.globl _linux_image_sys_start
_linux_image_sys_start:
	.incbin "vmlinux.bin"
	.globl _linux_image_sys_end
_linux_image_sys_end:

	.section .initrd_sys, "a", @progbits
	.balign 4096
	.globl _initrd_sys_start
_initrd_sys_start:
	.incbin "rootfs_overlay.cpio"
	.globl _initrd_sys_end
_initrd_sys_end:
