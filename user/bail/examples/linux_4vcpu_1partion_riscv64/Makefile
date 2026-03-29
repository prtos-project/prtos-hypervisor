# Linux on PRTOS - Hardware-Assisted Virtualization (4 vCPU, 1 Partition) [RISC-V 64]
#
# Builds a PRTOS partition image containing:
#   - Boot stub (bridges PRTOS JMP_PARTITION to Linux boot protocol)
#   - Device tree blob (linux_guest.dtb)
#   - Linux Image (embedded via .incbin in start.S)
#
# The Linux kernel must be pre-built with CONFIG_INITRAMFS_SOURCE
# pointing to buildroot's rootfs.cpio so rootfs is embedded in Image.
#
# Usage:
#   make                # Build container.bin and resident_sw
#   make run            # Build and run on QEMU
#   make clean          # Clean all build artifacts

BAIL_PATH=../../

all: container.bin resident_sw
include $(BAIL_PATH)/common/rules.mk

# XMLCF: path to the XML configuration file
XMLCF=prtos_cf.$(ARCH).xml

# PARTITIONS: partition files (pef format)
PARTITIONS=partition.pef

# ============================================================================
# Linux Workspace Paths
# ============================================================================
LINUX_WORKSPACE=../native_linux_run_on_qemu_riscv64_virt
LINUX_IMAGE=$(LINUX_WORKSPACE)/linux-6.19.9/arch/riscv/boot/Image

# ============================================================================
# Toolchain
# ============================================================================
CROSS = riscv64-linux-gnu
LNX_CC = $(CROSS)-gcc
LNX_LD = $(CROSS)-ld
DTC = dtc

LNX_CFLAGS = -march=rv64gc -mabi=lp64d -mcmodel=medany -ffreestanding -Wall -g
OBJDIR = obj/

# ============================================================================
# Build Rules
# ============================================================================

$(OBJDIR):
	mkdir -p $@

# Compile device tree
linux_guest.dtb: linux_guest.dts
	$(DTC) -I dts -O dtb -o $@ $<

# Copy Linux Image to local directory for .incbin
Image: $(LINUX_IMAGE)
	cp $< $@

# Compile boot stub (embeds DTB and Linux Image via .incbin)
$(OBJDIR)start.o: start.S linux_guest.dtb Image | $(OBJDIR)
	$(LNX_CC) -c $(LNX_CFLAGS) $< -o $@

# Compile PRTOS image header
$(OBJDIR)hdr.o: hdr.c | $(OBJDIR)
	$(LNX_CC) -c $(LNX_CFLAGS) $< -o $@

# Link partition ELF
OBJS = $(OBJDIR)hdr.o $(OBJDIR)start.o

EXTERNAL_LDFLAGS=y

partition: $(OBJS) linker.ld
	$(LNX_LD) -T linker.ld $(OBJS) -o $@

# ============================================================================
# PRTOS Packaging
# ============================================================================

PACK_ARGS=-h $(PRTOS_CORE):prtos_cf.pef.prtos_conf \
	-p 0:partition.pef

container.bin: $(PARTITIONS) prtos_cf.pef.prtos_conf
	$(PRTOS_PACK) check prtos_cf.pef.prtos_conf $(PACK_ARGS)
	$(PRTOS_PACK) build $(PACK_ARGS) $@

include ../run.mk

clean:
	@rm -f partition *.pef *.prtos_conf container.bin resident_sw
	@rm -f resident_sw.bin resident_sw_image
	@rm -f linux_guest.dtb Image
	@rm -rf $(OBJDIR)

.PHONY: all clean run
