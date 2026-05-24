# Virtio Linux Demo (LoongArch64): 2 SMP Linux Partitions with Virtio Device Virtualization
#
# Architecture:
#   L0: Host Linux + QEMU (loongarch64, virt)
#   L1: PRTOS Type-1 Hypervisor (32MB)
#   L2-System: Partition 0 (System) - Linux LoongArch64, Virtio Backend (hw-virt)
#              2 vCPU on pCPU 0-1, 512MB RAM at GPA 0x80000000
#              Console: NS16550 UART (ttyS0) -> serial stdio
#   L2-Guest:  Partition 1 (Guest)  - Linux LoongArch64, Virtio Frontend (hw-virt)
#              2 vCPU on pCPU 2-3, 512MB RAM at GPA 0xA0000000
#              Console: none (uses virtio-console via shared memory)
#
# Shared Memory (5 separate regions):
#   Virtio_Net0:  1MB   @ 0xC0000000
#   Virtio_Net1:  1MB   @ 0xC0100000
#   Virtio_Net2:  1MB   @ 0xC0200000
#   Virtio_Blk:   2MB   @ 0xC0300000
#   Virtio_Con:   256KB @ 0xC0500000
#
# Usage:
#   make                        # Build container.bin and resident_sw
#   make run.loongarch64        # Build and run on QEMU
#   make clean                  # Clean all build artifacts

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
LINUX_WORKSPACE=/home/chenweis/loongarch64_workspace
LINUX_SRC=$(LINUX_WORKSPACE)/linux-6.19.9
LINUX_VMLINUX=$(LINUX_SRC)/vmlinux

# Buildroot rootfs source tree compiled into the kernel via
# CONFIG_INITRAMFS_SOURCE.  rebuild_kernel.sh stages role-specific
# overlay files (virtio_backend / virtio_frontend / etc.) into this
# directory and forces a kernel re-link so each partition gets its own
# tailored rootfs.
ROOTFS_SOURCE?=/tmp/rootfs_repack

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
LNX_ASFLAGS = -march=loongarch64 -mabi=lp64d -ffreestanding -g

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

# ----------------------------------------------------------------------
# Per-partition Linux kernels.
#
# Each partition gets its OWN kernel build (via rebuild_kernel.sh) so
# (a) the embedded initramfs holds role-specific overlay binaries
#     (System: virtio_backend + prtos_manager + S99virtio_backend;
#      Guest:  virtio_frontend + S99virtio_guest + virtio_test.sh),
# (b) the built-in DTB is per-role: System uses prtos-virt-system (UART
#     + cpus reg=0,1) and Guest uses prtos-virt-guest (no UART, cpus
#     reg=2,3).  Per-role DTBs keep each Linux's csr_mail_send writes
#     inside its own pCPU set, avoiding cross-partition IOCSR MBUF
#     pollution.
# ----------------------------------------------------------------------

SYSTEM_OVERLAY_FILES = prtos_manager virtio_backend \
                       system_partition/rootfs_overlay/etc/init.d/S99virtio_backend

GUEST_OVERLAY_FILES  = virtio_frontend set_serial_poll \
                       guest_partition/rootfs_overlay/etc/init.d/S99virtio_guest \
                       guest_partition/rootfs_overlay/opt/virtio_test.sh

vmlinux_system.bin vmlinux_system.unstripped &: $(SYSTEM_OVERLAY_FILES) rebuild_kernel.sh
	@./rebuild_kernel.sh system $(LINUX_SRC)

vmlinux_guest.bin vmlinux_guest.unstripped &: $(GUEST_OVERLAY_FILES) rebuild_kernel.sh vmlinux_system.bin
	@./rebuild_kernel.sh guest $(LINUX_SRC)

# ============================================================================
# LoongArch64 Note: No DTB generation needed.
# Kernel uses CONFIG_CMDLINE_FORCE with built-in command line.
# ============================================================================

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
	$(BE_CC) $(BE_CFLAGS) $(BE_LDFLAGS) -s $^ -o $@
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
	$(MGR_CC) $(MGR_CFLAGS) $(MGR_LDFLAGS) -s $^ -o $@
	@echo "[Build] PRTOS Manager built: $@"

# ============================================================================
# Guest Partition userspace helpers (frontend, set_serial_poll)
# ============================================================================

set_serial_poll: set_serial_poll.c
	@$(CROSS)-gcc -static -O2 -s -o $@ $<
	@echo "[Build] $@ built"

virtio_frontend: guest_partition/src/virtio_frontend.c
	@$(CROSS)-gcc -static -O2 -Wall -s -lpthread -o $@ $<
	@echo "[Build] $@ built"

# Note: rootfs_overlay.cpio / guest_rootfs_overlay.cpio are no longer
# generated.  On LoongArch64 there is no firmware boot protocol to pass an
# initrd address to Linux (no DT, no EFI, cmdline is forced via
# CONFIG_CMDLINE_FORCE), so we instead embed the per-partition overlay
# directly into each kernel via CONFIG_INITRAMFS_SOURCE (see
# vmlinux_system.bin / vmlinux_guest.bin rules above).

# ============================================================================
# System Partition (Partition 0) - PRTOS PEF Image
# ============================================================================

# Compute kernel_entry offset from the per-partition unstripped vmlinux
# so the boot stubs auto-track every Linux rebuild (instead of being
# hardcoded).  Both kernels are bit-identical apart from the embedded
# initramfs data, so the offsets are typically the same -- but recompute
# per-kernel to stay safe.
kernel_entry_offset = $(shell $(CROSS)-nm $1 2>/dev/null | awk '/ T kernel_entry$$/{e=$$1} / T _text$$/{t=$$1} END{ if(e&&t) printf "0x%016s 0x%016s\n", e, t }' | python3 -c 'import sys; e,t=sys.stdin.read().split(); print(hex(int(e,16)-int(t,16)))')

$(OBJDIR)/start_system.o: start_system.S vmlinux_system.bin vmlinux_system.unstripped | $(OBJDIR)
	$(LNX_CC) -c $(LNX_ASFLAGS) -DLINUX_KERNEL_ENTRY_OFFSET=$(call kernel_entry_offset,vmlinux_system.unstripped) $< -o $@

$(OBJDIR)/hdr_system.o: hdr_system.c | $(OBJDIR)
	$(LNX_CC) -c $(LNX_CFLAGS) $< -o $@

SYS_OBJS = $(OBJDIR)/hdr_system.o $(OBJDIR)/start_system.o

EXTERNAL_LDFLAGS=y

partition_system: $(SYS_OBJS) linker_system.ld
	$(LNX_LD) -T linker_system.ld $(SYS_OBJS) -o $@

partition_system.pef: partition_system
	$(PEF) $< -c -o $@

# ============================================================================
# Guest Partition (Partition 1) - PRTOS PEF Image
# ============================================================================

$(OBJDIR)/start_guest.o: start_guest.S vmlinux_guest.bin vmlinux_guest.unstripped | $(OBJDIR)
	$(LNX_CC) -c $(LNX_ASFLAGS) -DLINUX_KERNEL_ENTRY_OFFSET=$(call kernel_entry_offset,vmlinux_guest.unstripped) $< -o $@

$(OBJDIR)/hdr_guest.o: hdr_guest.c | $(OBJDIR)
	$(LNX_CC) -c $(LNX_CFLAGS) $< -o $@

GUEST_OBJS = $(OBJDIR)/hdr_guest.o $(OBJDIR)/start_guest.o

partition_guest: $(GUEST_OBJS) linker_guest.ld
	$(LNX_LD) -T linker_guest.ld $(GUEST_OBJS) -o $@

partition_guest.pef: partition_guest
	$(PEF) $< -c -o $@

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
# QEMU Launch Target
# ============================================================================

# run.loongarch64:
# 	@$(MAKE) clean
# 	@$(MAKE) resident_sw
# 	@echo "=========================================================================="
# 	@echo "  === Starting QEMU (LoongArch64 TCG) ==="
# 	@echo ""
# 	@echo "  NOTE: First boot is SLOW under QEMU TCG (no KVM/LVZ acceleration)."
# 	@echo "        Approximate timeline (wall-clock, do NOT think it is hung):"
# 	@echo "          ~30s   -> [RSW] PRTOS header signatures OK"
# 	@echo "          ~75s   -> Linux kernel starts (System partition)"
# 	@echo "          ~210s  -> 'Welcome to Buildroot / buildroot login:' prompt"
# 	@echo ""
# 	@echo "  When you see the login prompt:"
# 	@echo "          login : root"
# 	@echo "          passwd: 1234"
# 	@echo ""
# 	@echo "  KNOWN LIMITATION (LoongArch64-only): the LVZ TCG shim in"
# 	@echo "  qemu c8d6d29131 implements only raw CSR read/write -- it does NOT"
# 	@echo "  isolate guest CSRs/timer between partitions.  When the Guest also"
# 	@echo "  runs Linux, QEMU MTTCG SIGSEGVs (~80s after kernel raid6 LSX init)."
# 	@echo "  Therefore start_guest.S keeps the Guest in _secondary_wait and"
# 	@echo "  virtio_backend in the System partition listens on TCP 4321 (so"
# 	@echo "  'telnet 127.0.0.1 4321' connects), but no Guest login prompt is"
# 	@echo "  delivered over the bridge.  The dual-kernel build infrastructure"
# 	@echo "  (per-role DTBs, rebuild_kernel.sh) is in place; remove the"
# 	@echo "  'b _secondary_wait' in start_guest.S after a full LVZ TCG"
# 	@echo "  implementation lands in QEMU upstream.  See README_zh.md."
# 	@echo ""
# 	@echo "  Exit QEMU: Ctrl-A x"
# 	@echo "=========================================================================="
# 	@qemu-system-loongarch64 \
# 		-machine virt \
# 		-cpu max \
# 		-smp 4 \
# 		-m 2G \
# 		-nographic -no-reboot \
# 		-kernel resident_sw \
# 		-monitor none \
# 		-serial stdio

clean:
	@rm -f partition_system partition_guest virtio_backend prtos_manager virtio_frontend
	@rm -f rootfs_overlay.cpio guest_rootfs_overlay.cpio
	@rm -rf _overlay_root _guest_overlay_root
	@rm -f *.pef *.prtos_conf container.bin resident_sw
	@rm -f resident_sw.bin resident_sw_image
	@rm -f vmlinux.bin Image set_serial_poll
	@rm -f vmlinux_system.bin vmlinux_system.unstripped
	@rm -f vmlinux_guest.bin vmlinux_guest.unstripped
	@rm -rf $(OBJDIR)

.PHONY: all clean run
