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
#
# ============================================================================
# How to (re)build the guest userspace and Linux kernel
# ============================================================================
#
# This partition embeds a prebuilt Linux 6.19.9 vmlinux that is itself
# produced from sources outside this repo, under:
#   $(LINUX_WORKSPACE) = /home/chenweis/hdd/Repo/loongarch64_linux_workspace
#
# The guest is built in two stages: (A) a Buildroot rootfs.cpio, then
# (B) a Linux kernel that embeds that cpio as its initramfs.
#
# ----------------------------------------------------------------------------
# (A) Build the Buildroot rootfs (musl, LoongArch64)
# ----------------------------------------------------------------------------
#   cd $(LINUX_WORKSPACE)/buildroot
#   make qemu_loongarch64_virt_efi_defconfig
#   # Optional: enable extra packages (e.g. htop) and set a rootfs overlay
#   #   BR2_PACKAGE_HTOP=y
#   #   BR2_ROOTFS_OVERLAY="board/qemu/loongarch64-virt-efi/rootfs_overlay"
#   # The overlay should provide etc/network/interfaces with only `lo`,
#   # because the PRTOS partition exposes no network device and the default
#   # buildroot interfaces file makes initscripts wait 15s on eth0.
#   make -j$(nproc)
#   # Output: output/images/rootfs_uncompressed.cpio (uncompressed cpio,
#   # required because the Linux kernel embeds it directly).
#
# If a full Buildroot rebuild is unavailable (e.g. missing gnu toolchain),
# you can patch the existing cpio in place: extract it, replace files, and
# repack with `find . | LC_ALL=C sort | cpio -o -H newc --owner 0:0`.
#
# ----------------------------------------------------------------------------
# (B) Build the Linux kernel (LoongArch64) that embeds the rootfs
# ----------------------------------------------------------------------------
#   cd $(LINUX_WORKSPACE)/linux-6.19.9
#   # .config must contain (already the case in this workspace):
#   #   CONFIG_INITRAMFS_SOURCE="<path-to-rootfs-dir-or-cpio> <extras.txt>"
#   #   CONFIG_INITRAMFS_COMPRESSION_NONE=y
#   #   CONFIG_CMDLINE_FORCE=y, CONFIG_CMDLINE="console=ttyS0,115200 ..."
#   # Force initramfs regeneration so any rootfs change is picked up:
#   rm -f usr/initramfs_data.cpio* usr/initramfs_inc_data
#   ARCH=loongarch CROSS_COMPILE=loongarch64-linux-gnu- make -j$(nproc) vmlinux
#   # Produces vmlinux and vmlinux.unstripped; this Makefile reads the latter
#   # via $(LINUX_VMLINUX) and turns it into a flat binary embedded in start.S.
#
# After (A) or (B) change, rebuild this partition with:
#   make clean && make
# Then run end-to-end with:
#   make run    # or: python3 test_login.py

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
LINUX_WORKSPACE=/home/chenweis/hdd/Repo/loongarch64_linux_workspace
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
# Compute entry offset from the unstripped vmlinux so it tracks every rebuild.
LINUX_KERNEL_ENTRY_OFFSET := $(shell $(CROSS)-nm $(LINUX_VMLINUX) 2>/dev/null | awk '/ T kernel_entry$$/{e=$$1} / T _text$$/{t=$$1} END{ if(e&&t) printf "0x%016s 0x%016s\n", e, t }' | python3 -c 'import sys; e,t=sys.stdin.read().split(); print(hex(int(e,16)-int(t,16)))')

$(OBJDIR)start.o: start.S vmlinux.bin | $(OBJDIR)
	$(LNX_CC) -c $(LNX_CFLAGS) -DLINUX_KERNEL_ENTRY_OFFSET=$(LINUX_KERNEL_ENTRY_OFFSET) $< -o $@

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
