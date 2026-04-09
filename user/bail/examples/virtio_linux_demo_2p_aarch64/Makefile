# Virtio Linux Demo (AArch64): 2 SMP Linux Partitions with Virtio Device Virtualization
#
# Architecture:
#   L0: Host Linux + QEMU (aarch64, virt, GICv3)
#   L1: PRTOS Type-1 Hypervisor (32MB)
#   L2-System: Partition 0 (System) - Linux AArch64, Virtio Backend (hw-virt)
#              2 vCPU on pCPU 0-1, 128MB RAM at IPA 0x10000000
#              Console: PL011 UART (ttyAMA0) -> serial stdio
#   L2-Guest:  Partition 1 (Guest)  - Linux AArch64, Virtio Frontend (hw-virt)
#              2 vCPU on pCPU 2-3, 128MB RAM at IPA 0x18000000
#              Console: none (uses virtio-console via shared memory)
#
# Shared Memory (5 separate regions):
#   Virtio_Net0:  1MB   @ 0x20000000
#   Virtio_Net1:  1MB   @ 0x20100000
#   Virtio_Net2:  1MB   @ 0x20200000
#   Virtio_Blk:   2MB   @ 0x20300000
#   Virtio_Con:   256KB @ 0x20500000
#
# Usage:
#   make                     # Build container.bin and resident_sw
#   make run.aarch64         # Build and run on QEMU (UART + telnet)
#   make run.aarch64.nographic # Nographic mode (for testing)
#   make clean               # Clean all build artifacts

BAIL_PATH=../../

all: container.bin resident_sw
include $(BAIL_PATH)/common/rules.mk

# XMLCF: PRTOS build system expects prtos_cf.$(ARCH).xml
XMLCF=prtos_cf.$(ARCH).xml

$(XMLCF): config/resident_sw.xml
	@ln -sf config/resident_sw.xml $@

PARTITIONS=partition_system.pef partition_guest.pef

# ============================================================================
# Linux Workspace Paths
# ============================================================================
LINUX_WORKSPACE=/home/chenweis/hdd/Repo/aarch64_linux_workspace
LINUX_IMAGE=$(LINUX_WORKSPACE)/linux-6.19.9/arch/arm64/boot/Image

# ============================================================================
# Toolchain
# ============================================================================
CROSS = aarch64-linux-gnu
LNX_CC = $(CROSS)-gcc
LNX_LD = $(CROSS)-ld
DTC = dtc

LNX_CFLAGS = -mcpu=cortex-a57 -ffreestanding -Wall -g -fno-pie
LNX_ASFLAGS = -mcpu=cortex-a57 -ffreestanding -g -fno-pie

# Backend daemon (runs inside System Partition Linux, cross-compiled)
BE_CC = $(CROSS)-gcc
BE_CFLAGS = -O2 -Wall -Wextra -I./system_partition/include -I./lib_prtos_manager/include -I./lib_prtos_manager/common
BE_LDFLAGS = -static

# PRTOS Manager (runs inside System Partition Linux, cross-compiled)
MGR_CC = $(CROSS)-gcc
MGR_CFLAGS = -O2 -Wall -Wextra -I./lib_prtos_manager/include -I./lib_prtos_manager/common
MGR_LDFLAGS = -static

OBJDIR = obj/

# ============================================================================
# Build Rules
# ============================================================================

$(OBJDIR):
	mkdir -p $@

# Copy Linux Image to local directory for .incbin
Image: $(LINUX_IMAGE)
	cp $< $@

# Compile device trees (inject initrd addresses from linker-placed CPIO overlays)
# System initrd: starts at 0x14000000 (linker_system.ld), size = rootfs_overlay.cpio
# Guest  initrd: starts at 0x1C000000 (linker_guest.ld),  size = guest_rootfs_overlay.cpio
linux_system.dtb: linux_system.dts rootfs_overlay.cpio
	@CPIO_SZ=$$(stat -c%s rootfs_overlay.cpio); \
	END_ADDR=$$(printf '0x%x' $$((0x14000000 + $$CPIO_SZ))); \
	sed '/bootargs/a\		linux,initrd-start = <0x0 0x14000000>;\n\t\tlinux,initrd-end = <0x0 '"$$END_ADDR"'>;' $< > _tmp_system.dts && \
	$(DTC) -I dts -O dtb -o $@ _tmp_system.dts && \
	rm -f _tmp_system.dts
	@echo "[Build] System DTB with initrd @ 0x14000000 (size $$(stat -c%s rootfs_overlay.cpio) bytes)"

linux_guest.dtb: linux_guest.dts guest_rootfs_overlay.cpio
	@CPIO_SZ=$$(stat -c%s guest_rootfs_overlay.cpio); \
	END_ADDR=$$(printf '0x%x' $$((0x1C000000 + $$CPIO_SZ))); \
	sed '/bootargs/a\\t\tlinux,initrd-start = <0x0 0x1C000000>;\n\t\tlinux,initrd-end = <0x0 '"$$END_ADDR"'>;' $< > _tmp_guest.dts && \
	$(DTC) -I dts -O dtb -o $@ _tmp_guest.dts && \
	rm -f _tmp_guest.dts
	@echo "[Build] Guest DTB with initrd @ 0x1C000000 (size $$(stat -c%s guest_rootfs_overlay.cpio) bytes)"

# ============================================================================
# System Partition Userspace: Virtio Backend Daemon
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
# ============================================================================

rootfs_overlay.cpio: prtos_manager virtio_backend \
                     system_partition/rootfs_overlay/etc/init.d/S99virtio_backend \
                     system_partition/rootfs_overlay/opt/diag_tcp_bridge.sh
	@rm -rf _overlay_root
	@mkdir -p _overlay_root/usr/bin
	@mkdir -p _overlay_root/etc/init.d
	@mkdir -p _overlay_root/opt
	@cp prtos_manager  _overlay_root/usr/bin/
	@cp virtio_backend _overlay_root/usr/bin/
	@cp system_partition/rootfs_overlay/etc/init.d/S99virtio_backend _overlay_root/etc/init.d/
	@cp system_partition/rootfs_overlay/opt/diag_tcp_bridge.sh _overlay_root/opt/
	@chmod 755 _overlay_root/usr/bin/prtos_manager
	@chmod 755 _overlay_root/usr/bin/virtio_backend
	@chmod 755 _overlay_root/etc/init.d/S99virtio_backend
	@chmod 755 _overlay_root/opt/diag_tcp_bridge.sh
	@(cd _overlay_root && find . | cpio -o -H newc --quiet) > $@
	@rm -rf _overlay_root
	@echo "[Build] System rootfs overlay CPIO built: $@ ($$(stat -c%s $@) bytes)"

# ============================================================================
# Guest Partition Rootfs CPIO Overlay
# ============================================================================

set_serial_poll: set_serial_poll.c
	@$(CROSS)-gcc -static -O2 -o $@ $<
	@echo "[Build] $@ built"

virtio_frontend: guest_partition/src/virtio_frontend.c
	@$(CROSS)-gcc -static -O2 -Wall -lpthread -o $@ $<
	@echo "[Build] $@ built"

guest_rootfs_overlay.cpio: guest_partition/rootfs_overlay/opt/virtio_test.sh \
                           guest_partition/rootfs_overlay/etc/init.d/S99virtio_guest \
                           set_serial_poll \
                           virtio_frontend
	@rm -rf _guest_overlay_root
	@mkdir -p _guest_overlay_root/opt
	@mkdir -p _guest_overlay_root/etc/init.d
	@mkdir -p _guest_overlay_root/usr/bin
	@cp guest_partition/rootfs_overlay/opt/virtio_test.sh _guest_overlay_root/opt/
	@cp guest_partition/rootfs_overlay/etc/init.d/S99virtio_guest _guest_overlay_root/etc/init.d/
	@cp set_serial_poll _guest_overlay_root/opt/
	@cp virtio_frontend _guest_overlay_root/usr/bin/
	@chmod 755 _guest_overlay_root/opt/virtio_test.sh
	@chmod 755 _guest_overlay_root/etc/init.d/S99virtio_guest
	@chmod 755 _guest_overlay_root/opt/set_serial_poll
	@chmod 755 _guest_overlay_root/usr/bin/virtio_frontend
	@(cd _guest_overlay_root && find . | cpio -o -H newc --quiet) > $@
	@rm -rf _guest_overlay_root
	@echo "[Build] Guest rootfs overlay CPIO built: $@ ($$(stat -c%s $@) bytes)"

# ============================================================================
# System Partition (Partition 0) - PRTOS PEF Image
# ============================================================================

$(OBJDIR)/start_system.o: start_system.S linux_system.dtb Image rootfs_overlay.cpio | $(OBJDIR)
	$(LNX_CC) -c $(LNX_ASFLAGS) $< -o $@

$(OBJDIR)/hdr_system.o: hdr_system.c | $(OBJDIR)
	$(LNX_CC) -c $(LNX_CFLAGS) $< -o $@

SYS_OBJS = $(OBJDIR)/hdr_system.o $(OBJDIR)/start_system.o

EXTERNAL_LDFLAGS=y

partition_system: $(SYS_OBJS) linker_system.ld
	$(LNX_LD) -T linker_system.ld $(SYS_OBJS) -o $@

partition_system.pef: partition_system
	$(PEF) $< -o $@

# ============================================================================
# Guest Partition (Partition 1) - PRTOS PEF Image
# ============================================================================

$(OBJDIR)/start_guest.o: start_guest.S linux_guest.dtb Image guest_rootfs_overlay.cpio | $(OBJDIR)
	$(LNX_CC) -c $(LNX_ASFLAGS) $< -o $@

$(OBJDIR)/hdr_guest.o: hdr_guest.c | $(OBJDIR)
	$(LNX_CC) -c $(LNX_CFLAGS) $< -o $@

GUEST_OBJS = $(OBJDIR)/hdr_guest.o $(OBJDIR)/start_guest.o

partition_guest: $(GUEST_OBJS) linker_guest.ld
	$(LNX_LD) -T linker_guest.ld $(GUEST_OBJS) -o $@

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
# U-Boot Build (custom with larger CONFIG_SYS_BOOTM_LEN for ~103MB image)
# ============================================================================

UBOOT_SRC = /home/chenweis/hdd/Repo/fork_os/u-boot
UBOOT_BIN = u-boot/u-boot.bin

$(UBOOT_BIN): | u-boot
	@echo "=== Building U-Boot (CONFIG_SYS_BOOTM_LEN=0x10000000) ==="
	$(MAKE) -C $(UBOOT_SRC) qemu_arm64_defconfig > /dev/null 2>&1
	$(UBOOT_SRC)/scripts/config --file $(UBOOT_SRC)/.config \
		--set-val CONFIG_SYS_BOOTM_LEN 0x10000000
	$(UBOOT_SRC)/scripts/config --file $(UBOOT_SRC)/.config \
		--set-str CONFIG_PREBOOT 'bootm 0x40200000 - $${fdtcontroladdr}'
	$(MAKE) -C $(UBOOT_SRC) -j$$(nproc) CROSS_COMPILE=aarch64-linux-gnu- > /dev/null 2>&1
	cp $(UBOOT_SRC)/u-boot.bin $@
	@echo "[Build] U-Boot binary: $@ ($$(stat -c%s $@) bytes)"

u-boot:
	@mkdir -p $@

# ============================================================================
# QEMU Launch Targets
# ============================================================================

run.aarch64: $(UBOOT_BIN)
	@make clean
	@make resident_sw
	@make $(UBOOT_BIN)
	@echo "=== Creating bootable image ==="
	@aarch64-linux-gnu-objcopy -O binary -R .note -R .note.gnu.build-id -R .comment -S resident_sw resident_sw.bin
	@mkimage -A arm64 -O linux -C none -a 0x40200000 -e 0x40200000 -d resident_sw.bin resident_sw_image
	@echo "================================================================"
	@echo "  System Partition: this terminal (PL011/stdio) | root/1234"
	@echo "  Guest Partition:  from System shell, run:"
	@echo "      telnet 127.0.0.1 4321"
	@echo "    then login with root/1234"
	@echo "================================================================"
	qemu-system-aarch64 \
		-machine virt,gic_version=3 \
		-machine virtualization=true \
		-cpu cortex-a72 \
		-machine type=virt \
		-m 4096 \
		-smp 4 \
		-bios ./u-boot/u-boot.bin \
		-device loader,file=./resident_sw_image,addr=0x40200000,force-raw=on \
		-nographic -no-reboot

# Nographic mode for automated testing (same as run.aarch64)
run.aarch64.nographic: $(UBOOT_BIN)
	@make clean
	@make resident_sw
	@make $(UBOOT_BIN)
	@echo "=== Creating bootable image ==="
	@aarch64-linux-gnu-objcopy -O binary -R .note -R .note.gnu.build-id -R .comment -S resident_sw resident_sw.bin
	@mkimage -A arm64 -O linux -C none -a 0x40200000 -e 0x40200000 -d resident_sw.bin resident_sw_image
	qemu-system-aarch64 \
		-machine virt,gic_version=3 \
		-machine virtualization=true \
		-cpu cortex-a72 \
		-machine type=virt \
		-m 4096 \
		-smp 4 \
		-bios ./u-boot/u-boot.bin \
		-device loader,file=./resident_sw_image,addr=0x40200000,force-raw=on \
		-nographic -no-reboot

clean:
	@rm -f partition_system partition_guest virtio_backend prtos_manager virtio_frontend
	@rm -f rootfs_overlay.cpio guest_rootfs_overlay.cpio
	@rm -rf _overlay_root _guest_overlay_root
	@rm -f *.pef *.prtos_conf container.bin resident_sw
	@rm -f resident_sw.bin resident_sw_image
	@rm -f linux_system.dtb linux_guest.dtb Image set_serial_poll
	@rm -rf $(OBJDIR) u-boot

.PHONY: all clean run
