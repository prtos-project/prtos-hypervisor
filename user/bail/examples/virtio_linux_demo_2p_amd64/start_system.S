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
#define BP_ACPI_RSDP_ADDR   0x070   /* __u64 acpi_rsdp_addr (since boot protocol 2.14) */

/* ACPI table region placed by QEMU at top of first 1GB RAM */
#define ACPI_RSDP_ADDR      0x000F5290  /* RSDP in BIOS ROM area */
#define ACPI_TABLES_START   0x3FFE0000  /* RSDT/FACP/DSDT/MADT/HPET/WAET */
#define ACPI_TABLES_SIZE    0x00020000  /* 128KB covers all ACPI tables */

/* e820 memory types */
#define E820_TYPE_RAM       1
#define E820_TYPE_RESERVED  2
#define E820_TYPE_ACPI      3   /* ACPI reclaimable */
#define E820_TYPE_NVS       4   /* ACPI Non-Volatile Storage */

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

	/* --- Step 5: Set up ACPI RSDP address in boot_params ---
	 * Linux 5.2+ (boot protocol 2.14+) uses this to find ACPI tables.
	 * QEMU places RSDP at 0xF5290 in the BIOS ROM area (0xE0000-0xFFFFF).
	 * EPT identity-maps the first 1GB, making both RSDP and ACPI tables
	 * (at 0x3FFE0000+) accessible to the partition.
	 */
	movl	$ACPI_RSDP_ADDR, (BP_ACPI_RSDP_ADDR)(%ebx)
	movl	$0x00000000, (BP_ACPI_RSDP_ADDR + 4)(%ebx)

	/* --- Step 6: Set up e820 memory map --- */
	/* 4 entries:
	 *   0) 0x0000_0000 .. 0x0009_FFFF  (640KB)   usable  (conventional)
	 *   1) 0x000A_0000 .. 0x000F_FFFF  (384KB)   reserved (VGA/ROM/BIOS)
	 *   2) 0x0600_0000 .. 0x0DFF_FFFF  (128MB)   usable  (partition RAM)
	 *   3) 0x3FFE_0000 .. 0x3FFF_FFFF  (128KB)   ACPI    (RSDT/FACP/DSDT/MADT/HPET/WAET)
	 * Note: RSDP at 0xF5290 is within entry 1 (reserved BIOS area).
	 *       Shared memory at 0x16000000+ accessed via /dev/mem, not in e820.
	 */
	movl	$4, (BP_E820_ENTRIES)(%ebx)
	movl	$(BP_E820_TABLE), %edi
	addl	%ebx, %edi

	/* Entry 0: Conventional 640KB */
	movl	$0x00000000, 0(%edi)
	movl	$0x00000000, 4(%edi)
	movl	$0x000A0000, 8(%edi)
	movl	$0x00000000, 12(%edi)
	movl	$E820_TYPE_RAM, 16(%edi)

	/* Entry 1: Reserved VGA/ROM/BIOS (includes RSDP at 0xF5290) */
	movl	$0x000A0000, 20(%edi)
	movl	$0x00000000, 24(%edi)
	movl	$0x00060000, 28(%edi)
	movl	$0x00000000, 32(%edi)
	movl	$E820_TYPE_RESERVED, 36(%edi)

	/* Entry 2: Partition RAM (128MB at 0x6000000) */
	movl	$0x06000000, 40(%edi)
	movl	$0x00000000, 44(%edi)
	movl	$0x08000000, 48(%edi)
	movl	$0x00000000, 52(%edi)
	movl	$E820_TYPE_RAM, 56(%edi)

	/* Entry 3: ACPI tables (QEMU places RSDT/FACP/DSDT/MADT/HPET/WAET here) */
	movl	$ACPI_TABLES_START, 60(%edi)
	movl	$0x00000000, 64(%edi)
	movl	$ACPI_TABLES_SIZE, 68(%edi)
	movl	$0x00000000, 72(%edi)
	movl	$E820_TYPE_ACPI, 76(%edi)

	/* --- Step 7: Set up initrd (rootfs overlay CPIO) --- */
	/* Linux boot protocol: ramdisk_image = physical address of initrd,
	 * ramdisk_size = byte length. Linux will unpack this CPIO on top of
	 * the built-in initramfs, overlaying /usr/bin/prtos_manager etc.
	 */
	movl	$_initrd_sys_start, (BP_RAMDISK_IMAGE)(%ebx)
	movl	$_initrd_sys_end, %eax
	subl	$_initrd_sys_start, %eax
	movl	%eax, (BP_RAMDISK_SIZE)(%ebx)

	/* --- Step 8: Calculate compressed kernel entry --- */
	movl	$_bzimage_sys_start, %eax
	movzbl	HDR_SETUP_SECTS(%eax), %ecx
	testl	%ecx, %ecx
	jnz	3f
	movl	$4, %ecx
3:	incl	%ecx
	shll	$9, %ecx
	addl	$_bzimage_sys_start, %ecx

	/* --- Step 9: Jump to Linux startup_32 --- */
	movl	$BOOT_PARAMS_ADDR, %esi
	jmp	*%ecx

/* Command line: System partition with virtio backend role
 * Guest outputs to VGA (separate console), so no need for quiet. */
	.section .rodata
_sys_cmdline:
	.asciz "console=ttyS0,115200 earlyprintk=serial,ttyS0,115200 loglevel=7 nokaslr no_timer_check nohpet tsc=reliable clocksource=tsc nr_cpus=2 prtos_role=system prtos_shmem_base=0x16000000 prtos_shmem_size=0x540000"

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
