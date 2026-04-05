# Virtio Linux Demo (amd64): 2 SMP Linux Partitions with Virtio Device Virtualization
# UART + VGA Dual Console
#
# Architecture:
#   L0: Host Linux (Ubuntu 24.04) + QEMU (x86_64, KVM)
#   L1: PRTOS Type-1 Hypervisor (16MB)
#   L2-System: Partition 0 (System) - Linux x86_64, Virtio Backend (hw-virt VMX)
#              2 vCPU on pCPU 0-1, 512MB RAM at GPA 0x6000000
#              Console: UART (ttyS0,115200) -> SSH terminal
#   L2-Guest:  Partition 1 (Guest)  - Linux x86_64, Virtio Frontend (hw-virt VMX)
#              2 vCPU on pCPU 2-3, 512MB RAM at GPA 0x26000000
#              Console: VGA (tty0) -> VNC :1 remote display
#
# Shared Memory (5 separate regions):
#   Virtio_Net0:  1MB   @ 0x16000000  (bridge)
#   Virtio_Net1:  1MB   @ 0x16100000  (NAT)
#   Virtio_Net2:  1MB   @ 0x16200000  (p2p)
#   Virtio_Blk:   2MB   @ 0x16300000  (file-backed block device)
#   Virtio_Con:   256KB @ 0x16500000  (character console)
#
# Usage:
#   make                   # Build container.bin and resident_sw.iso
#   make run.amd64         # Basic QEMU launch (UART only)
#   make run.amd64.demo    # Full demo: UART + VGA (VNC :1) + networking
#   make clean             # Clean all build artifacts

BAIL_PATH=../../

all: container.bin resident_sw.iso
include $(BAIL_PATH)/common/rules.mk

# XMLCF: PRTOS build system expects prtos_cf.$(ARCH).xml
XMLCF=prtos_cf.$(ARCH).xml

$(XMLCF): config/resident_sw.xml
	@ln -sf config/resident_sw.xml $@

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

# Backend daemon (runs inside System Partition Linux)
BE_CC = gcc
BE_CFLAGS = -O2 -Wall -Wextra -I./system_partition/include -I./lib_prtos_manager/include -I./lib_prtos_manager/common
BE_LDFLAGS = -static

# PRTOS Manager (runs inside System Partition Linux)
MGR_CC = gcc
MGR_CFLAGS = -O2 -Wall -Wextra -I./lib_prtos_manager/include -I./lib_prtos_manager/common
MGR_LDFLAGS = -static

OBJDIR = obj/

# ============================================================================
# Build Rules
# ============================================================================

$(OBJDIR):
	mkdir -p $@

bzImage: $(LINUX_BZIMAGE)
	cp $< $@

# ============================================================================
# System Partition Userspace: Virtio Backend Daemon
# Manages 3 x net + 1 x blk + 1 x console via shared memory
# ============================================================================

BE_SRCS = system_partition/src/main.c \
          system_partition/src/virtio_console.c \
          system_partition/src/virtio_net.c \
          system_partition/src/virtio_blk.c \
          system_partition/src/doorbell.c \
          system_partition/src/manager_if.c \
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
# System Partition Rootfs CPIO Overlay
# Contains: /usr/bin/prtos_manager, /usr/bin/virtio_backend
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
	@echo "[Build] System rootfs overlay CPIO built: $@ ($$(stat -c%s $@) bytes)"

# ============================================================================
# Guest Partition Rootfs CPIO Overlay
# Contains: /opt/virtio_test.sh, /etc/init.d/S99virtio_guest
# ============================================================================

guest_rootfs_overlay.cpio: guest_partition/rootfs_overlay/opt/virtio_test.sh \
                           guest_partition/rootfs_overlay/etc/init.d/S99virtio_guest
	@rm -rf _guest_overlay_root
	@mkdir -p _guest_overlay_root/opt
	@mkdir -p _guest_overlay_root/etc/init.d
	@cp guest_partition/rootfs_overlay/opt/virtio_test.sh _guest_overlay_root/opt/
	@cp guest_partition/rootfs_overlay/etc/init.d/S99virtio_guest _guest_overlay_root/etc/init.d/
	@chmod 755 _guest_overlay_root/opt/virtio_test.sh
	@chmod 755 _guest_overlay_root/etc/init.d/S99virtio_guest
	@(cd _guest_overlay_root && find . | cpio -o -H newc --quiet) > $@
	@rm -rf _guest_overlay_root
	@echo "[Build] Guest rootfs overlay CPIO built: $@ ($$(stat -c%s $@) bytes)"

# ============================================================================
# System Partition (Partition 0) - PRTOS PEF Image
# boot stub + bzImage + System rootfs CPIO overlay
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
# boot stub + bzImage + Guest rootfs CPIO overlay
# ============================================================================

$(OBJDIR)/start_guest.o: start_guest.S bzImage guest_rootfs_overlay.cpio | $(OBJDIR)
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
# ============================================================================

PACK_ARGS=-h $(PRTOS_CORE):prtos_cf.pef.prtos_conf \
	-p 0:partition_system.pef \
	-p 1:partition_guest.pef

container.bin: $(PARTITIONS) prtos_cf.pef.prtos_conf
	$(PRTOS_PACK) check prtos_cf.pef.prtos_conf $(PACK_ARGS)
	$(PRTOS_PACK) build $(PACK_ARGS) $@

include ../run.mk

# ============================================================================
# Block Device Image (for Virtio-Blk)
# ============================================================================

disk.img:
	qemu-img create -f raw $@ 64M
	@echo "[Build] Block device image created: $@ (64MB)"

# ============================================================================
# QEMU Launch Targets
# ============================================================================

# Basic launch: KVM, 2048MB RAM, 4 pCPUs, UART serial console only
run.amd64:
	@$(MAKE) clean
	@$(MAKE) resident_sw.iso
	@qemu-system-x86_64 -enable-kvm -cpu host,-waitpkg \
		-m 512 -smp 4 \
		-cdrom resident_sw.iso \
		-serial stdio -boot d

# Nographic mode for automated testing
run.amd64.nographic:
	@$(MAKE) clean
	@$(MAKE) resident_sw.iso
	@qemu-system-x86_64 -enable-kvm -cpu host,-waitpkg \
		-nographic -m 512 -smp 4 \
		-cdrom resident_sw.iso \
		-serial mon:stdio -boot d

# Alias for test framework
run.amd64.kvm.nographic:
	@$(MAKE) clean
	@$(MAKE) resident_sw.iso
	@qemu-system-x86_64 -enable-kvm -cpu host,-waitpkg \
		-nographic -m 512 -smp 4 \
		-cdrom resident_sw.iso \
		-serial mon:stdio -boot d

# Full demo: UART (System=serial) + VGA (Guest=VNC :1) + Networking + Block
# SSH Terminal -> System Linux (UART)
# VNC :1 (localhost:5901) -> Guest Linux (VGA)
run.amd64.demo: disk.img
	@$(MAKE) clean
	@$(MAKE) resident_sw.iso
	@echo "================================================================"
	@echo "  PRTOS Virtio Demo - UART + VGA Dual Console"
	@echo "================================================================"
	@echo "  System Partition: UART console (this terminal)"
	@echo "  Guest Partition:  VGA console (VNC localhost:5901)"
	@echo ""
	@echo "  After System boots, run:"
	@echo "    prtos_manager &"
	@echo "    virtio_backend &"
	@echo ""
	@echo "  Connect to Guest VGA:"
	@echo "    vncviewer localhost:5901"
	@echo "================================================================"
	@qemu-system-x86_64 -enable-kvm -cpu host,-waitpkg \
		-m 512 -smp 4 \
		-cdrom resident_sw.iso \
		-boot d \
		-serial mon:stdio \
		-serial null \
		-display none \
		-vga std \
		-vnc :1 \
		-netdev tap,id=net0,ifname=tap0,script=no,downscript=no \
		-device virtio-net-pci,netdev=net0 \
		-netdev user,id=net1 \
		-device virtio-net-pci,netdev=net1 \
		-netdev socket,id=net2,listen=:1234 \
		-device virtio-net-pci,netdev=net2 \
		-drive file=disk.img,if=virtio,id=vdisk,format=raw

# Demo without TAP (no root required, NAT only)
run.amd64.demo.nat: disk.img
	@$(MAKE) clean
	@$(MAKE) resident_sw.iso
	@echo "================================================================"
	@echo "  PRTOS Virtio Demo (NAT mode) - UART + VGA Dual Console"
	@echo "================================================================"
	@echo "  System: UART (this terminal) | Guest: VGA (VNC localhost:5901)"
	@echo "================================================================"
	@qemu-system-x86_64 -enable-kvm -cpu host,-waitpkg \
		-m 512 -smp 4 \
		-cdrom resident_sw.iso \
		-boot d \
		-serial mon:stdio \
		-serial null \
		-display none \
		-vga std \
		-vnc :1 \
		-netdev user,id=net0 \
		-device virtio-net-pci,netdev=net0 \
		-drive file=disk.img,if=virtio,id=vdisk,format=raw

clean:
	@rm -f partition_system partition_guest virtio_backend prtos_manager
	@rm -f rootfs_overlay.cpio guest_rootfs_overlay.cpio
	@rm -rf _overlay_root _guest_overlay_root
	@rm -f *.pef *.prtos_conf container.bin resident_sw resident_sw.iso
	@rm -f resident_sw.bin resident_sw_image
	@rm -f bzImage
	@rm -rf $(OBJDIR)

distclean: clean
	@rm -f disk.img

.PHONY: all clean distclean run
