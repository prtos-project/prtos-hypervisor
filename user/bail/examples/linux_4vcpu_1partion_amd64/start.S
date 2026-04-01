/*
 * Boot stub for Linux guest running in a PRTOS hw-virt partition (amd64/x86_64).
 *
 * PRTOS VMX launches the guest in 32-bit protected mode (PE=1, PG=0):
 *   - CS: flat 32-bit code segment (base=0, limit=4GB)
 *   - DS/ES/SS: flat 32-bit data segments (base=0, limit=4GB)
 *   - EIP = partition entry point (start of .text)
 *   - ESP = partition_start + partition_size - 16
 *
 * Linux x86 boot protocol (32-bit entry of compressed kernel):
 *   - ESI = physical address of struct boot_params (zero page)
 *   - CS/DS/ES/SS: flat 32-bit segments (same as PRTOS gives us)
 *   - The compressed kernel (startup_32) is at bzImage + (setup_sects+1)*512
 *
 * Memory layout (GPA space, base = partition start):
 *   +0x000000  PRTOS image header
 *   +0x001000  Boot stub code (this file)
 *   +0x010000  boot_params (zero page, 4KB)
 *   +0x011000  Command line string
 *   +0x100000  bzImage (embedded via .incbin)
 *
 * The boot stub:
 *   1. Copies the real-mode setup header from bzImage into boot_params
 *   2. Sets up command line pointer
 *   3. Sets ESI = boot_params address
 *   4. Jumps to startup_32 (compressed kernel entry)
 */

#define BOOT_PARAMS_ADDR    0x6010000   /* Absolute GPA for boot_params */
#define CMDLINE_ADDR        0x6011000   /* Absolute GPA for command line */
#define BZIMAGE_ADDR        0x6100000   /* Absolute GPA where bzImage is embedded */

/* Offsets into bzImage/setup header (Documentation/arch/x86/boot.rst) */
#define HDR_SETUP_SECTS     0x1f1
#define HDR_START           0x1f1       /* Start of setup header */
#define HDR_SIZE            (0x0290 - 0x01f1)  /* setup header size */
#define BP_HDR_OFFSET       0x1f1       /* Offset of header within boot_params */

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
	/* --- Direct UART output for debug: 'L' for Linux boot stub --- */
	movl	$0x3f8, %edx
1:	movl	$0x3fd, %edx
	inb	%dx, %al
	testb	$0x20, %al
	jz	1b
	movl	$0x3f8, %edx
	movb	$'L', %al
	outb	%al, %dx

	/* --- Step 1: Clear boot_params (zero page) --- */
	movl	$BOOT_PARAMS_ADDR, %edi
	xorl	%eax, %eax
	movl	$(4096 / 4), %ecx
	cld
	rep	stosl

	/* --- Step 2: Copy setup header from bzImage into boot_params --- */
	/* Source: bzImage + 0x1f1 (the setup header) */
	movl	$(_bzimage_start + HDR_START), %esi
	/* Destination: boot_params + 0x1f1 */
	movl	$(BOOT_PARAMS_ADDR + BP_HDR_OFFSET), %edi
	movl	$HDR_SIZE, %ecx
	rep	movsb

	/* --- Step 3: Set up boot loader info --- */
	movl	$BOOT_PARAMS_ADDR, %ebx

	/* Type of loader: 0xFF = undefined/other */
	movb	$0xff, (BP_TYPE_OF_LOADER)(%ebx)

	/* Loadflags: set LOADED_HIGH (kernel loaded at 0x100000+) and KEEP_SEGMENTS */
	movb	(BP_LOADFLAGS)(%ebx), %al
	orb	$0x41, %al		/* bit 0: LOADED_HIGH, bit 6: KEEP_SEGMENTS */
	movb	%al, (BP_LOADFLAGS)(%ebx)

	/* Heap end pointer */
	movw	$0xfe00, (BP_HEAP_END_PTR)(%ebx)

	/* --- Step 4: Set up command line --- */
	/* Copy command line string to CMDLINE_ADDR */
	leal	_cmdline, %esi
	movl	$CMDLINE_ADDR, %edi
2:	lodsb
	stosb
	testb	%al, %al
	jnz	2b

	/* Set cmdline pointer in boot_params */
	movl	$CMDLINE_ADDR, (BP_CMDLINE_PTR)(%ebx)

	/* --- Step 5: Set up minimal e820 memory map --- */
	/* 3 entries:
	 *   0) 0x0000_0000 .. 0x0009_FFFF  (640KB)   usable (conventional)
	 *   1) 0x000A_0000 .. 0x000F_FFFF  (384KB)   reserved (VGA/ROM)
	 *   2) 0x0600_0000 .. 0x15FF_FFFF  (256MB)   usable (partition RAM)
	 */
	movl	$3, (BP_E820_ENTRIES)(%ebx)

	/* e820 entry at boot_params + 0x2d0: addr(8) + size(8) + type(4) = 20 bytes each */
	movl	$(BP_E820_TABLE), %edi
	addl	%ebx, %edi

	/* Entry 0: Conventional 640KB */
	movl	$0x00000000, 0(%edi)
	movl	$0x00000000, 4(%edi)
	movl	$0x000A0000, 8(%edi)
	movl	$0x00000000, 12(%edi)
	movl	$1, 16(%edi)		/* E820_TYPE_RAM */

	/* Entry 1: Reserved VGA/ROM area */
	movl	$0x000A0000, 20(%edi)
	movl	$0x00000000, 24(%edi)
	movl	$0x00060000, 28(%edi)	/* 384KB */
	movl	$0x00000000, 32(%edi)
	movl	$2, 36(%edi)		/* E820_TYPE_RESERVED */

	/* Entry 2: Partition RAM (256MB at 0x6000000) */
	movl	$0x06000000, 40(%edi)
	movl	$0x00000000, 44(%edi)
	movl	$0x10000000, 48(%edi)	/* 256MB */
	movl	$0x00000000, 52(%edi)
	movl	$1, 56(%edi)		/* E820_TYPE_RAM */

	/* --- Step 6: Calculate compressed kernel entry point --- */
	/* startup_32 offset = (setup_sects + 1) * 512 */
	movl	$_bzimage_start, %eax
	movzbl	HDR_SETUP_SECTS(%eax), %ecx	/* setup_sects */
	testl	%ecx, %ecx
	jnz	3f
	movl	$4, %ecx			/* default if 0 */
3:	incl	%ecx				/* +1 for boot sector */
	shll	$9, %ecx			/* * 512 */
	addl	$_bzimage_start, %ecx		/* ecx = startup_32 address */

	/* --- Step 7: Jump to Linux compressed kernel (startup_32) --- */
	/* ESI = boot_params physical address (Linux boot protocol) */
	movl	$BOOT_PARAMS_ADDR, %esi
	jmp	*%ecx

/* Command line string */
	.section .rodata
_cmdline:
	.asciz "console=ttyS0,115200 earlyprintk=serial,ttyS0,115200,keep loglevel=7 nokaslr noapic"

/* Embedded bzImage */
	.section .bzimage, "ax", @progbits
	.balign 4096
	.globl _bzimage_start
_bzimage_start:
	.incbin "bzImage"
	.globl _bzimage_end
_bzimage_end:
