/*
 * Boot stub for Guest Partition (Partition 1) - LoongArch64 Virtio Frontend.
 *
 * Memory layout (GPA space, base = 0xA0000000):
 *   0xA0000000  PRTOS image header + boot stub
 *   0xA0200000  Linux kernel (vmlinux_guest.bin, 2MB aligned)
 *
 * The buildroot rootfs (with virtio_frontend / S99virtio_guest /
 * virtio_test.sh / set_serial_poll installed) is embedded directly in
 * vmlinux_guest.bin via CONFIG_INITRAMFS_SOURCE -- no separate initrd is
 * needed.
 *
 * SMP: 2 vCPUs (pCPU 2-3).
 * Console: none (uses virtio-console via shared memory).
 */

	.section .text
	.globl _boot
_boot:
	/* The LoongArch64 LVZ TCG shim (qemu c8d6d29131) implements only raw
	 * CSR read/write without true guest-mode isolation: timer (TCFG/TVAL),
	 * CPUID and most guest CSRs are shared host-wide.  When two LVZ
	 * partitions concurrently run Linux, QEMU MTTCG reliably SIGSEGVs in
	 * the host process shortly after kernel raid6 LSX self-test (~80s
	 * wall-clock).  Until full LVZ guest-mode is implemented in QEMU, the
	 * Guest partition spins here so the System partition (with
	 * virtio_backend bound on TCP 4321) keeps running cleanly.
	 *
	 * The dual-kernel build infrastructure (per-role DTBs, rebuild_kernel.sh,
	 * Guest GPA 0xa0000000 bootargs) is fully in place: removing this
	 * `b _secondary_wait` lets the Guest reach kernel_entry and proceed to
	 * raid6 init.  See README_zh.md "LoongArch64 \u7279\u5b9a\u9650\u5236". */
	b	_secondary_wait

	/* vCPU 0 (Guest boot CPU on pCPU 2) lands here.  Secondary vCPUs are
	 * brought up by PRTOS host start_up_guest() polling IOCSR MBUF0 of
	 * their pCPU and jumping directly to Linux smpboot_entry, so they
	 * never execute this stub.
	 *
	 * Per-role DTBs (prtos-virt-system uses cpus reg=0,1 and
	 * prtos-virt-guest uses cpus reg=2,3) keep each partition's
	 * csr_mail_send writes inside its own pCPU set, so the LVZ TCG
	 * shim's lack of CSR/timer isolation does not actually clobber the
	 * other partition (each pCPU has its own CPULoongArchState). */

	/* Set up Linux boot protocol arguments */
	li.d	$a0, 0
	li.d	$a1, 0
	li.d	$a2, 0

	/* Jump to Linux kernel_entry. Offset is supplied by the Makefile via
	 * -DLINUX_KERNEL_ENTRY_OFFSET so it auto-tracks every kernel build. */
#ifndef LINUX_KERNEL_ENTRY_OFFSET
#define LINUX_KERNEL_ENTRY_OFFSET 0x1628000
#endif
	la.local $t0, _linux_image_guest_start
	li.d	$t1, LINUX_KERNEL_ENTRY_OFFSET
	add.d	$t0, $t0, $t1
	jr	$t0

	/* Guest never reaches kernel_entry under the LVZ TCG shim;
	 * spin forever waiting for an IOCSR mailbox wakeup that PRTOS
	 * never delivers (vCPU 0 of the Guest stays here; vCPU 1 is
	 * still managed by the host's start_up_guest IOCSR poller). */
_secondary_wait:
	li.d	$t0, 0x1020
	iocsrwr.d $zero, $t0
1:	iocsrrd.d $t1, $t0
	beqz	$t1, 1b
	li.d	$t2, 0x9
	bstrins.d $t1, $t2, 63, 60
	jr	$t1

	.section .linux_image_guest, "ax", @progbits
	.balign 0x200000
	.globl _linux_image_guest_start
_linux_image_guest_start:
	.incbin "vmlinux_guest.bin"
	.globl _linux_image_guest_end
_linux_image_guest_end:
