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
 *   0x80200000  Linux kernel (vmlinux_system.bin, 2MB aligned)
 *
 * The buildroot rootfs (with prtos_manager / virtio_backend /
 * S99virtio_backend installed under /usr/bin and /etc/init.d) is embedded
 * directly in vmlinux_system.bin via CONFIG_INITRAMFS_SOURCE -- no
 * separate initrd is needed.
 *
 * SMP: 2 vCPUs (pCPU 0-1).
 * Console: NS16550 UART at 0x1FE001E0 (ttyS0)
 */

	.section .text
	.globl _boot
_boot:
	/* PRTOS hands control here only for vCPU 0 of this partition.
	 * Secondary vCPUs are bootstrapped by the host's IOCSR MBUF
	 * poller in core/kernel/loongarch64 start_up_guest() and never
	 * reach _boot.  Do NOT branch on CSR_CPUID (0x20) -- the LVZ
	 * shim does NOT virtualize it, so on a partition whose vCPU 0 is
	 * pinned to pCPU != 0 this would mis-detect us as a secondary
	 * and spin forever in _secondary_wait. */

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

	/* Jump to Linux kernel_entry. Offset is supplied by the Makefile via
	 * -DLINUX_KERNEL_ENTRY_OFFSET so it auto-tracks every kernel build. */
#ifndef LINUX_KERNEL_ENTRY_OFFSET
#define LINUX_KERNEL_ENTRY_OFFSET 0x1628000
#endif
	la.local $t0, _linux_image_sys_start
	li.d	$t1, LINUX_KERNEL_ENTRY_OFFSET
	add.d	$t0, $t0, $t1
	jr	$t0

	.section .linux_image_sys, "ax", @progbits
	.balign 0x200000
	.globl _linux_image_sys_start
_linux_image_sys_start:
	.incbin "vmlinux_system.bin"
	.globl _linux_image_sys_end
_linux_image_sys_end:
