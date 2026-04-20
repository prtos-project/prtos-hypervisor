# Linux on PRTOS - Hardware-Assisted Virtualization (4 vCPU, 1 Partition) [LoongArch64]
#
# Builds a PRTOS partition image containing:
#   - Boot stub (bridges PRTOS JMP_PARTITION to Linux boot protocol)
#   - Linux vmlinux.bin (flat binary, embedded via .incbin in start.S)
#
# The Linux kernel must be pre-built with:
#   CONFIG_INITRAMFS_SOURCE pointing to buildroot's rootfs.cpio
#   CONFIG_CMDLINE_FORCE=y with appropriate console/mem parameters
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
LINUX_WORKSPACE=/home/chenweis/loongarch64_workspace
LINUX_VMLINUX=$(LINUX_WORKSPACE)/linux-6.19.9/vmlinux.unstripped

# Prevent Make implicit rule from rebuilding external vmlinux via host cc
$(LINUX_VMLINUX): ;

# ============================================================================
# Toolchain
# ============================================================================
CROSS = loongarch64-linux-gnu
LNX_CC = $(CROSS)-gcc
LNX_LD = $(CROSS)-ld
LNX_OBJCOPY = $(CROSS)-objcopy

LNX_CFLAGS = -march=loongarch64 -mabi=lp64d -ffreestanding -Wall -g
OBJDIR = obj/

# ============================================================================
# Build Rules
# ============================================================================

$(OBJDIR):
	mkdir -p $@

# Extract flat binary from vmlinux ELF
vmlinux.bin: $(LINUX_VMLINUX)
	$(LNX_OBJCOPY) -O binary $< $@

# Compile boot stub (embeds Linux vmlinux.bin via .incbin)
$(OBJDIR)start.o: start.S vmlinux.bin | $(OBJDIR)
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
	@rm -f vmlinux.bin
	@rm -rf $(OBJDIR)

.PHONY: all clean run
