# Native Linux on QEMU amd64 (x86_64) - no hypervisor
#
# Runs a pre-built Linux bzImage (with embedded initramfs) directly
# on QEMU. Used as the baseline native reference and as the Linux
# image source for PRTOS hw-virt partition demos.
#
# Prerequisites:
#   - Linux bzImage at linux-6.19.9/arch/x86/boot/bzImage
#     (built with CONFIG_INITRAMFS_SOURCE pointing to buildroot rootfs.cpio)
#
# Usage:
#   make run      # Boot Linux natively on QEMU x86_64
#
# ============================================================================
# How to rebuild Linux kernel and Buildroot rootfs from scratch:
#
# 1. Build Buildroot (x86_64 rootfs with htop and root password "1234"):
#    cd /home/chenweis/amd64_linux_workspace/buildroot
#    make qemu_x86_64_defconfig
#    sed -i 's/^BR2_TARGET_GENERIC_ROOT_PASSWD=""$/BR2_TARGET_GENERIC_ROOT_PASSWD="1234"/' .config
#    sed -i 's/^# BR2_TARGET_ROOTFS_CPIO is not set$/BR2_TARGET_ROOTFS_CPIO=y/' .config
#    echo "BR2_PACKAGE_HTOP=y" >> .config
#    echo "BR2_PACKAGE_NCURSES=y" >> .config
#    make olddefconfig
#    make -j$(nproc)
#
# 2. Build Linux kernel (x86_64 with embedded initramfs):
#    cd /home/chenweis/amd64_linux_workspace/linux-6.19.9
#    make x86_64_defconfig
#    sed -i 's|^CONFIG_INITRAMFS_SOURCE=""$|CONFIG_INITRAMFS_SOURCE="/home/chenweis/amd64_linux_workspace/buildroot/output/images/rootfs.cpio"|' .config
#    scripts/config --enable CONFIG_PVH
#    scripts/config --enable CONFIG_SERIAL_8250
#    scripts/config --enable CONFIG_SERIAL_8250_CONSOLE
#    scripts/config --enable CONFIG_VIRTIO
#    scripts/config --enable CONFIG_VIRTIO_PCI
#    scripts/config --enable CONFIG_VIRTIO_NET
#    scripts/config --enable CONFIG_VIRTIO_BLK
#    scripts/config --enable CONFIG_SMP
#    make olddefconfig
#    make -j$(nproc) bzImage
#
# 3. Copy kernel image:
#    cp arch/x86/boot/bzImage \
#       <this_dir>/linux-6.19.9/arch/x86/boot/bzImage
# ============================================================================

KERNEL := linux-6.19.9/arch/x86/boot/bzImage

run:
	@echo "=== Starting native Linux on QEMU x86_64 ==="
	@echo "Kernel: $(KERNEL)"
	@echo "Press Ctrl-A X to exit QEMU"
	qemu-system-x86_64 \
		-m 512 \
		-smp 4 \
		-kernel $(KERNEL) \
		-append "console=ttyS0 earlycon" \
		-nographic \
		-no-reboot

.PHONY: run
