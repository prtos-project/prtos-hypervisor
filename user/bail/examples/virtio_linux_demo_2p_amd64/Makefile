# Virtio Linux Demo (amd64): 2 Linux Partitions with Virtio Device Virtualization
#
# Architecture:
#   L0: Host Linux + QEMU (x86_64, KVM)
#   L1: PRTOS Type-1 Hypervisor
#   L2-B: Partition 0 (System) - Linux x86_64, Virtio Backend (hw-virt VMX)
#         1 vCPU on pCPU 0, 128MB RAM at GPA 0x6000000
#   L2-A: Partition 1 (Guest)  - Linux x86_64, Virtio Frontend (hw-virt VMX)
#         1 vCPU on pCPU 1, 128MB RAM at GPA 0xe000000
#   Shared Memory: 8MB at GPA 0x16000000 (Virtio rings + buffers)
#
# Directory structure:
#   config/resident_sw.xml       - PRTOS XML configuration
#   system_partition/src/         - Virtio Backend daemon source (runs inside System Linux)
#   system_partition/include/     - Shared Virtio data structure headers
#   guest_partition/rootfs_overlay/ - Scripts/binaries for Guest Linux rootfs
#
#
# Usage:
#   make          # Build container.bin and resident_sw.iso
#   make run      # Build and run on QEMU (requires KVM)
#   make clean    # Clean all build artifacts

BAIL_PATH=../../

all: container.bin resident_sw.iso
include $(BAIL_PATH)/common/rules.mk

# XMLCF: PRTOS build system expects prtos_cf.$(ARCH).xml
# We symlink to config/resident_sw.xml
XMLCF=prtos_cf.$(ARCH).xml

# Recreate symlink if distclean removed it (distclean deletes all symlinks)
$(XMLCF): config/resident_sw.xml
	@ln -sf config/resident_sw.xml $@

# PARTITIONS: both partition PEF files
PARTITIONS=partition_system.pef partition_guest.pef

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

# Backend daemon cross-compilation flags (runs inside System Partition Linux)
BE_CC = gcc
BE_CFLAGS = -O2 -Wall -Wextra -I./system_partition/include -I./lib_prtos_manager/include -I./lib_prtos_manager/common
BE_LDFLAGS = -static

# PRTOS Manager cross-compilation flags (runs inside System Partition Linux)
MGR_CC = gcc
MGR_CFLAGS = -O2 -Wall -Wextra -I./lib_prtos_manager/include -I./lib_prtos_manager/common
MGR_LDFLAGS = -static

OBJDIR = obj/

# ============================================================================
# Build Rules
# ============================================================================

$(OBJDIR):
	mkdir -p $@

# Copy bzImage to local directory for .incbin (same kernel for both partitions)
bzImage: $(LINUX_BZIMAGE)
	cp $< $@

# ============================================================================
# System Partition Userspace: Virtio Backend Daemon
# Cross-compiled as a static binary to embed in rootfs or run after boot.
# ============================================================================

BE_SRCS = system_partition/src/main.c \
          system_partition/src/virtio_console.c \
          system_partition/src/virtio_net.c \
          system_partition/src/virtio_blk.c \
          lib_prtos_manager/common/prtos_hv.c

BE_OBJS = $(addprefix $(OBJDIR)/, $(notdir $(BE_SRCS:.c=.o)))

$(OBJDIR)/%.o: system_partition/src/%.c system_partition/include/virtio_be.h | $(OBJDIR)
	$(BE_CC) -c $(BE_CFLAGS) $< -o $@

$(OBJDIR)/prtos_hv.o: lib_prtos_manager/common/prtos_hv.c lib_prtos_manager/include/prtos_hv.h | $(OBJDIR)
	$(BE_CC) -c $(BE_CFLAGS) $< -o $@

virtio_backend: $(BE_OBJS)
	$(BE_CC) $(BE_CFLAGS) $(BE_LDFLAGS) $^ -o $@
	@echo "[Build] Virtio Backend daemon built: $@"

# ============================================================================
# PRTOS Partition Manager
# Cross-compiled as a static binary for System Partition Linux.
# Provides CLI for partition management (list, halt, reset, resume, etc.)
# Uses vmcall for hypervisor interaction + shared memory mailbox.
# ============================================================================

MGR_SRCS = lib_prtos_manager/linux/prtos_manager_main.c \
           lib_prtos_manager/common/prtos_manager.c \
           lib_prtos_manager/common/hypervisor.c \
           lib_prtos_manager/common/prtos_hv.c

MGR_OBJS = $(addprefix $(OBJDIR)/mgr_, $(notdir $(MGR_SRCS:.c=.o)))

$(OBJDIR)/mgr_prtos_manager_main.o: lib_prtos_manager/linux/prtos_manager_main.c | $(OBJDIR)
	$(MGR_CC) -c $(MGR_CFLAGS) $< -o $@

$(OBJDIR)/mgr_prtos_manager.o: lib_prtos_manager/common/prtos_manager.c | $(OBJDIR)
	$(MGR_CC) -c $(MGR_CFLAGS) $< -o $@

$(OBJDIR)/mgr_hypervisor.o: lib_prtos_manager/common/hypervisor.c | $(OBJDIR)
	$(MGR_CC) -c $(MGR_CFLAGS) $< -o $@

$(OBJDIR)/mgr_prtos_hv.o: lib_prtos_manager/common/prtos_hv.c | $(OBJDIR)
	$(MGR_CC) -c $(MGR_CFLAGS) $< -o $@

prtos_manager: $(MGR_OBJS)
	$(MGR_CC) $(MGR_CFLAGS) $(MGR_LDFLAGS) $^ -o $@
	@echo "[Build] PRTOS Manager built: $@"

# ============================================================================
# Rootfs CPIO Overlay
# Creates a newc-format CPIO archive containing prtos_manager and
# virtio_backend under /usr/bin/. Linux unpacks this on top of the
# built-in initramfs when passed as an initrd via boot_params.
# ============================================================================

rootfs_overlay.cpio: prtos_manager virtio_backend
	@rm -rf _overlay_root
	@mkdir -p _overlay_root/usr/bin
	@cp prtos_manager  _overlay_root/usr/bin/
	@cp virtio_backend _overlay_root/usr/bin/
	@chmod 755 _overlay_root/usr/bin/prtos_manager
	@chmod 755 _overlay_root/usr/bin/virtio_backend
	@(cd _overlay_root && find . | cpio -o -H newc --quiet) > $@
	@rm -rf _overlay_root
	@echo "[Build] Rootfs overlay CPIO built: $@ ($$(stat -c%s $@) bytes)"

# ============================================================================
# System Partition (Partition 0) - PRTOS PEF Image
# Contains boot stub + embedded bzImage
# ============================================================================

$(OBJDIR)/start_system.o: start_system.S bzImage rootfs_overlay.cpio | $(OBJDIR)
	$(LNX_CC) -c $(LNX_ASFLAGS) $< -o $@

$(OBJDIR)/hdr_system.o: hdr_system.c | $(OBJDIR)
	$(LNX_CC) -c $(LNX_CFLAGS) $< -o $@

SYS_OBJS = $(OBJDIR)/hdr_system.o $(OBJDIR)/start_system.o

EXTERNAL_LDFLAGS=y

partition_system: $(SYS_OBJS) linker_system.ld
	$(LNX_LD) -m elf_x86_64 -T linker_system.ld $(SYS_OBJS) -o $@

partition_system.pef: partition_system
	$(PEF) $< -o $@

# ============================================================================
# Guest Partition (Partition 1) - PRTOS PEF Image
# Contains boot stub + embedded bzImage
# ============================================================================

$(OBJDIR)/start_guest.o: start_guest.S bzImage | $(OBJDIR)
	$(LNX_CC) -c $(LNX_ASFLAGS) $< -o $@

$(OBJDIR)/hdr_guest.o: hdr_guest.c | $(OBJDIR)
	$(LNX_CC) -c $(LNX_CFLAGS) $< -o $@

GUEST_OBJS = $(OBJDIR)/hdr_guest.o $(OBJDIR)/start_guest.o

partition_guest: $(GUEST_OBJS) linker_guest.ld
	$(LNX_LD) -m elf_x86_64 -T linker_guest.ld $(GUEST_OBJS) -o $@

partition_guest.pef: partition_guest
	$(PEF) $< -o $@

# ============================================================================
# PRTOS Packaging (2 partitions)
# Assembles: PRTOS core + XML config + partition PEF files -> bootable ISO
# ============================================================================

PACK_ARGS=-h $(PRTOS_CORE):prtos_cf.pef.prtos_conf \
	-p 0:partition_system.pef \
	-p 1:partition_guest.pef

container.bin: $(PARTITIONS) prtos_cf.pef.prtos_conf
	$(PRTOS_PACK) check prtos_cf.pef.prtos_conf $(PACK_ARGS)
	$(PRTOS_PACK) build $(PACK_ARGS) $@

include ../run.mk

# ============================================================================
# QEMU Launch (Override): KVM required, 512MB RAM, 2 pCPUs
# Additional QEMU devices for System Partition passthrough:
#   -netdev tap for physical network
#   -drive for physical block device
# ============================================================================

run.amd64:
	@$(MAKE) clean
	@$(MAKE) resident_sw.iso
	@qemu-system-x86_64 -enable-kvm -cpu host,-waitpkg \
		-m 512 -smp 2 \
		-cdrom resident_sw.iso \
		-serial stdio -boot d

run.amd64.nographic:
	@$(MAKE) clean
	@$(MAKE) resident_sw.iso
	@qemu-system-x86_64 -enable-kvm -cpu host,-waitpkg \
		-nographic -m 512 -smp 2 \
		-cdrom resident_sw.iso \
		-serial mon:stdio -boot d

# Alias for test framework and manual use
run.amd64.kvm.nographic:
	@$(MAKE) clean
	@$(MAKE) resident_sw.iso
	@qemu-system-x86_64 -enable-kvm -cpu host,-waitpkg \
		-nographic -m 512 -smp 2 \
		-cdrom resident_sw.iso \
		-serial mon:stdio -boot d

# Full demo with QEMU physical devices for System Partition passthrough
run.amd64.full:
	@$(MAKE) clean
	@$(MAKE) resident_sw.iso virtio_backend prtos_manager
	@echo "=== Launching PRTOS Virtio Demo (full) ==="
	@echo "System Partition: Virtio Backend + physical devices"
	@echo "Guest Partition:  Virtio Frontend (standard Linux)"
	@qemu-system-x86_64 -enable-kvm -cpu host,-waitpkg \
		-nographic -m 512 -smp 2 \
		-cdrom resident_sw.iso \
		-serial mon:stdio -boot d \
		-netdev user,id=net0 \
		-device virtio-net-pci,netdev=net0

clean:
	@rm -f partition_system partition_guest virtio_backend prtos_manager
	@rm -f rootfs_overlay.cpio
	@rm -rf _overlay_root
	@rm -f *.pef *.prtos_conf container.bin resident_sw resident_sw.iso
	@rm -f resident_sw.bin resident_sw_image
	@rm -f bzImage
	@rm -rf $(OBJDIR)

.PHONY: all clean run
