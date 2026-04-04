/*
 * Boot stub for Guest Partition (Partition 1) - Virtio Frontend client.
 *
 * PRTOS VMX launches the guest in 32-bit protected mode (PE=1, PG=0).
 * This stub bridges to the Linux boot protocol (startup_32).
 *
 * Memory layout (GPA space, base = 0xe000000):
 *   +0x000000  PRTOS image header
 *   +0x001000  Boot stub code (this file)
 *   +0x010000  boot_params (zero page, 4KB)
 *   +0x011000  Command line string
 *   +0x100000  bzImage (embedded via .incbin)
 *
 * Shared memory at GPA 0x16000000 (8MB) for Virtio rings.
 * The Guest Linux uses standard virtio-pci/virtio-mmio drivers.
 */

#define PARTITION_BASE      0xe000000
#define PARTITION_SIZE      0x08000000  /* 128MB */
#define BOOT_PARAMS_ADDR    0xe010000   /* Absolute GPA for boot_params */
#define CMDLINE_ADDR        0xe011000   /* Absolute GPA for command line */

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

	.code32
	.section .text
	.globl _boot
_boot:
	/* --- Guest partition: no UART debug output to avoid serial contention --- */

	/* --- Step 1: Clear boot_params (zero page) --- */
	movl	$BOOT_PARAMS_ADDR, %edi
	xorl	%eax, %eax
	movl	$(4096 / 4), %ecx
	cld
	rep	stosl

	/* --- Step 2: Copy setup header from bzImage into boot_params --- */
	movl	$(_bzimage_guest_start + HDR_START), %esi
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
	leal	_guest_cmdline, %esi
	movl	$CMDLINE_ADDR, %edi
2:	lodsb
	stosb
	testb	%al, %al
	jnz	2b
	movl	$CMDLINE_ADDR, (BP_CMDLINE_PTR)(%ebx)

	/* --- Step 5: Set up e820 memory map --- */
	/* 3 entries (matching working demos - shared memory accessed via /dev/mem):
	 *   0) 0x0000_0000  .. 0x0009_FFFF   (640KB)   usable (conventional)
	 *   1) 0x000A_0000  .. 0x000F_FFFF   (384KB)   reserved (VGA/ROM)
	 *   2) 0x0E00_0000 .. 0x15FF_FFFF   (128MB)   usable (partition RAM)
	 * Note: Shared memory at 0x16000000 (8MB) is NOT in e820.
	 *       Non-contiguous e820 regions trigger kernel sparse memory bugs.
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

	/* Entry 2: Partition RAM (128MB at 0xe000000) */
	movl	$0x0e000000, 40(%edi)
	movl	$0x00000000, 44(%edi)
	movl	$0x08000000, 48(%edi)
	movl	$0x00000000, 52(%edi)
	movl	$1, 56(%edi)

	/* --- Step 6: Calculate compressed kernel entry --- */
	movl	$_bzimage_guest_start, %eax
	movzbl	HDR_SETUP_SECTS(%eax), %ecx
	testl	%ecx, %ecx
	jnz	3f
	movl	$4, %ecx
3:	incl	%ecx
	shll	$9, %ecx
	addl	$_bzimage_guest_start, %ecx

	/* --- Step 7: Jump to Linux startup_32 --- */
	movl	$BOOT_PARAMS_ADDR, %esi
	jmp	*%ecx

/* Command line: Guest partition with virtio frontend role */
	.section .rodata
_guest_cmdline:
	.asciz "console=null 8250.nr_uarts=0 nokaslr nosmp noapic nolapic prtos_role=guest prtos_shmem_base=0x16000000 prtos_shmem_size=0x800000"

/* Embedded bzImage for Guest partition */
	.section .bzimage_guest, "ax", @progbits
	.balign 4096
	.globl _bzimage_guest_start
_bzimage_guest_start:
	.incbin "bzImage"
	.globl _bzimage_guest_end
_bzimage_guest_end:
