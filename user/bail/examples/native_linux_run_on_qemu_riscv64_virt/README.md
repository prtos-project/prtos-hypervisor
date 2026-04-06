# Example : native_linux_run_on_qemu_riscv64_virt

## Description
This example runs Linux kernel (6.19.9) natively on QEMU RISC-V 64 virt machine without the PRTOS hypervisor. It uses a Buildroot-generated initramfs embedded in the kernel image, serving as a baseline reference for the linux_4vcpu_1partion_riscv64 example.

## Partition definition
No PRTOS partitioning — Linux runs directly on QEMU.

## Configuration table
No PRTOS XML configuration. Linux is built with embedded Buildroot rootfs.

- Target: RISC-V 64-bit
- CPU: 4 vCPUs (SMP)
- Kernel: linux-6.19.9 with CONFIG_INITRAMFS_SOURCE=buildroot rootfs
- Rootfs: Buildroot with htop, root password "1234"

## Expected results
Linux boots directly on QEMU to a login prompt (root/1234), providing a performance baseline without hypervisor overhead.
