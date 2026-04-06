# Example : native_linux_run_on_qemu_a57_virt

## Description
This example runs Linux kernel (6.19.9) natively on QEMU ARM Cortex-A57 virt machine without the PRTOS hypervisor. It uses a Buildroot-generated initramfs embedded in the kernel image, serving as a baseline reference for the linux_aarch64 and linux_4vcpu_1partion_aarch64 examples.

## Partition definition
No PRTOS partitioning — Linux runs directly on QEMU.

## Configuration table
No PRTOS XML configuration. Linux is built with embedded Buildroot rootfs.

- Target: ARM Cortex-A57 (AArch64)
- CPU: 4 vCPUs (SMP)
- Kernel: linux-6.19.9 with CONFIG_INITRAMFS_SOURCE=buildroot rootfs
- Rootfs: Buildroot with htop, root password "1234"

Build steps:
1. Build Buildroot ARM64 rootfs with htop
2. Build Linux with CONFIG_INITRAMFS_SOURCE pointing to Buildroot output
3. Copy resulting Image to this example directory

## Expected results
Linux boots directly on QEMU to a login prompt (root/1234), providing a performance baseline without hypervisor overhead.
