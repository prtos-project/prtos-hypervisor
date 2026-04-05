/*
 * Boot stub for System Partition (Partition 0) - Virtio Backend host.
 *
 * PRTOS VMX launches the guest in 32-bit protected mode (PE=1, PG=0).
 * This stub bridges to the Linux boot protocol (startup_32).
 *
 * Memory layout (GPA space, base = 0x6000000):
 *   +0x000000  PRTOS image header
 *   +0x001000  Boot stub code (this file)
 *   +0x010000  boot_params (zero page, 4KB)
 *   +0x011000  Command line string
 *   +0x100000  bzImage (embedded via .incbin)
 *   (after bzImage, 4K aligned)  rootfs_overlay.cpio (embedded via .incbin)
 *     Contains: /usr/bin/prtos_manager, /usr/bin/virtio_backend
 *     Linux unpacks this CPIO on top of the built-in initramfs.
 *
 * SMP: 2 vCPUs (pCPU 0-1), Linux handles AP startup via PRTOS VMX.
 * Console: UART (ttyS0,115200) -> SSH terminal
 * Shared memory at GPA 0x16000000+ for Virtio device regions.
 */

#define PARTITION_BASE      0x6000000
#define PARTITION_SIZE      0x8000000   /* 128MB */
#define BOOT_PARAMS_ADDR    0x6010000   /* Absolute GPA for boot_params */
#define CMDLINE_ADDR        0x6011000   /* Absolute GPA for command line */

/* Offsets into bzImage/setup header */
#define HDR_SETUP_SECTS     0x1f1
#define HDR_START           0x1f1
#define HDR_SIZE            (0x0290 - 0x01f1)
#define BP_HDR_OFFSET       0x1f1

/* boot_params field offsets */
#define BP_E820_ENTRIES     0x1e8
#define BP_E820_TABLE       0x2d0
#define BP_CMDLINE_PTR      0x228
#define BP_TYPE_OF_LOADER   0x210
#define BP_LOADFLAGS        0x211
#define BP_HEAP_END_PTR     0x224
#define BP_RAMDISK_IMAGE    0x218
#define BP_RAMDISK_SIZE     0x21c

	.code32
	.section .text
	.globl _boot
_boot:
	/* --- UART debug: 'S' for System partition --- */
	movl	$0x3f8, %edx
1:	movl	$0x3fd, %edx
	inb	%dx, %al
	testb	$0x20, %al
	jz	1b
	movl	$0x3f8, %edx
	movb	$'S', %al
	outb	%al, %dx

	/* --- Step 1: Clear boot_params (zero page) --- */
	movl	$BOOT_PARAMS_ADDR, %edi
	xorl	%eax, %eax
	movl	$(4096 / 4), %ecx
	cld
	rep	stosl

	/* --- Step 2: Copy setup header from bzImage into boot_params --- */
	movl	$(_bzimage_sys_start + HDR_START), %esi
	movl	$(BOOT_PARAMS_ADDR + BP_HDR_OFFSET), %edi
	movl	$HDR_SIZE, %ecx
	rep	movsb

	/* --- Step 3: Set up boot loader info --- */
	movl	$BOOT_PARAMS_ADDR, %ebx
	movb	$0xff, (BP_TYPE_OF_LOADER)(%ebx)
	movb	(BP_LOADFLAGS)(%ebx), %al
	orb	$0x41, %al
	movb	%al, (BP_LOADFLAGS)(%ebx)
	movw	$0xfe00, (BP_HEAP_END_PTR)(%ebx)

	/* --- Step 4: Set up command line --- */
	leal	_sys_cmdline, %esi
	movl	$CMDLINE_ADDR, %edi
2:	lodsb
	stosb
	testb	%al, %al
	jnz	2b
	movl	$CMDLINE_ADDR, (BP_CMDLINE_PTR)(%ebx)

	/* --- Step 5: Set up e820 memory map --- */
	/* 3 entries (shared memory accessed via /dev/mem, not in e820):
	 *   0) 0x0000_0000 .. 0x0009_FFFF  (640KB)   usable (conventional)
	 *   1) 0x000A_0000 .. 0x000F_FFFF  (384KB)   reserved (VGA/ROM)
	 *   2) 0x0600_0000 .. 0x0DFF_FFFF  (128MB)   usable (partition RAM)
	 * Note: Shared memory at 0x16000000+ is NOT in e820.
	 *       Access shared memory via /dev/mem mmap instead.
	 */
	movl	$3, (BP_E820_ENTRIES)(%ebx)
	movl	$(BP_E820_TABLE), %edi
	addl	%ebx, %edi

	/* Entry 0: Conventional 640KB */
	movl	$0x00000000, 0(%edi)
	movl	$0x00000000, 4(%edi)
	movl	$0x000A0000, 8(%edi)
	movl	$0x00000000, 12(%edi)
	movl	$1, 16(%edi)

	/* Entry 1: Reserved VGA/ROM */
	movl	$0x000A0000, 20(%edi)
	movl	$0x00000000, 24(%edi)
	movl	$0x00060000, 28(%edi)
	movl	$0x00000000, 32(%edi)
	movl	$2, 36(%edi)

	/* Entry 2: Partition RAM (128MB at 0x6000000) */
	movl	$0x06000000, 40(%edi)
	movl	$0x00000000, 44(%edi)
	movl	$0x08000000, 48(%edi)
	movl	$0x00000000, 52(%edi)
	movl	$1, 56(%edi)

	/* --- Step 5b: Set up initrd (rootfs overlay CPIO) --- */
	/* Linux boot protocol: ramdisk_image = physical address of initrd,
	 * ramdisk_size = byte length. Linux will unpack this CPIO on top of
	 * the built-in initramfs, overlaying /usr/bin/prtos_manager etc.
	 */
	movl	$_initrd_sys_start, (BP_RAMDISK_IMAGE)(%ebx)
	movl	$_initrd_sys_end, %eax
	subl	$_initrd_sys_start, %eax
	movl	%eax, (BP_RAMDISK_SIZE)(%ebx)

	/* --- Step 6: Calculate compressed kernel entry --- */
	movl	$_bzimage_sys_start, %eax
	movzbl	HDR_SETUP_SECTS(%eax), %ecx
	testl	%ecx, %ecx
	jnz	3f
	movl	$4, %ecx
3:	incl	%ecx
	shll	$9, %ecx
	addl	$_bzimage_sys_start, %ecx

	/* --- Step 7: Jump to Linux startup_32 --- */
	movl	$BOOT_PARAMS_ADDR, %esi
	jmp	*%ecx

/* Command line: System partition with virtio backend role
 * Guest outputs to VGA (separate console), so no need for quiet. */
	.section .rodata
_sys_cmdline:
	.asciz "console=ttyS0,115200 quiet loglevel=0 nokaslr noapic nolapic prtos_role=system prtos_shmem_base=0x16000000 prtos_shmem_size=0x540000"

/* Embedded bzImage for System partition */
	.section .bzimage_sys, "ax", @progbits
	.balign 4096
	.globl _bzimage_sys_start
_bzimage_sys_start:
	.incbin "bzImage"
	.globl _bzimage_sys_end
_bzimage_sys_end:

/* Embedded initrd CPIO overlay for System partition.
 * Contains /usr/bin/prtos_manager and /usr/bin/virtio_backend.
 * Linux unpacks this on top of the built-in initramfs.
 */
	.section .initrd_sys, "a", @progbits
	.balign 4096
	.globl _initrd_sys_start
_initrd_sys_start:
	.incbin "rootfs_overlay.cpio"
	.globl _initrd_sys_end
_initrd_sys_end:
