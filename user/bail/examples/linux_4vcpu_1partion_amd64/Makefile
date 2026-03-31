# Linux on PRTOS - Hardware-Assisted Virtualization (4 vCPU, 1 Partition) [amd64]
#
# Builds a PRTOS partition image containing:
#   - Boot stub (bridges PRTOS VMX 32-bit entry to Linux boot protocol)
#   - bzImage (Linux kernel with embedded initramfs, via .incbin)
#
# PRTOS VMX launches the guest in 32-bit protected mode (PE=1, PG=0).
# The boot stub sets up boot_params (zero page) and jumps to the
# compressed kernel's startup_32 entry, which handles the 32→64
# transition and kernel decompression.
#
# The Linux kernel must be pre-built with CONFIG_INITRAMFS_SOURCE
# pointing to buildroot's rootfs.cpio so rootfs is embedded in bzImage.
#
# Usage:
#   make                # Build container.bin and resident_sw.iso
#   make run            # Build and run on QEMU (requires KVM)
#   make clean          # Clean all build artifacts

BAIL_PATH=../../

all: container.bin resident_sw.iso
include $(BAIL_PATH)/common/rules.mk

# XMLCF: path to the XML configuration file
XMLCF=prtos_cf.$(ARCH).xml

# PARTITIONS: partition files (pef format)
PARTITIONS=partition.pef

# ============================================================================
# Linux Workspace Paths
# ============================================================================
LINUX_WORKSPACE=../native_linux_run_on_qemu_amd64_virt
LINUX_BZIMAGE=$(LINUX_WORKSPACE)/linux-6.19.9/arch/x86/boot/bzImage

# ============================================================================
# Toolchain
# ============================================================================
LNX_CC = gcc
LNX_LD = ld

LNX_CFLAGS = -m64 -ffreestanding -Wall -g -fno-pie -fno-stack-protector -mcmodel=kernel
LNX_ASFLAGS = -m64 -ffreestanding -g -fno-pie -fno-stack-protector
OBJDIR = obj/

# ============================================================================
# Build Rules
# ============================================================================

$(OBJDIR):
	mkdir -p $@

# Copy bzImage to local directory for .incbin
bzImage: $(LINUX_BZIMAGE)
	cp $< $@

# Compile boot stub (embeds bzImage via .incbin)
$(OBJDIR)start.o: start.S bzImage | $(OBJDIR)
	$(LNX_CC) -c $(LNX_ASFLAGS) $< -o $@

# Compile PRTOS image header
$(OBJDIR)hdr.o: hdr.c | $(OBJDIR)
	$(LNX_CC) -c $(LNX_CFLAGS) $< -o $@

# Link partition ELF
OBJS = $(OBJDIR)hdr.o $(OBJDIR)start.o

EXTERNAL_LDFLAGS=y

partition: $(OBJS) linker.ld
	$(LNX_LD) -m elf_x86_64 -T linker.ld $(OBJS) -o $@

# Override default PEF build rule: skip compression (-c) for large partition
# The large embedded bzImage makes compression unreliable in the RSW loader.
partition.pef: partition
	$(PEF) $< -o $@

# ============================================================================
# PRTOS Packaging
# ============================================================================

PACK_ARGS=-h $(PRTOS_CORE):prtos_cf.pef.prtos_conf \
	-p 0:partition.pef

container.bin: $(PARTITIONS) prtos_cf.pef.prtos_conf
	$(PRTOS_PACK) check prtos_cf.pef.prtos_conf $(PACK_ARGS)
	$(PRTOS_PACK) build $(PACK_ARGS) $@

include ../run.mk

# Override run.amd64: this example requires KVM for VMX hardware-assisted virtualization.
run.amd64:
	@$(MAKE) clean
	@$(MAKE) resident_sw.iso
	@qemu-system-x86_64 -enable-kvm -cpu host -m 512 -cdrom resident_sw.iso -serial stdio -boot d -smp 4

run.amd64.nographic:
	@$(MAKE) clean
	@$(MAKE) resident_sw.iso
	@qemu-system-x86_64 -enable-kvm -cpu host -nographic -m 512 -cdrom resident_sw.iso -serial mon:stdio -boot d -smp 4

clean:
	@rm -f partition *.pef *.prtos_conf container.bin resident_sw resident_sw.iso
	@rm -f resident_sw.bin resident_sw_image
	@rm -f bzImage
	@rm -rf $(OBJDIR)

.PHONY: all clean run
