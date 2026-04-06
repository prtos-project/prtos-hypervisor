# Example : native_linux_run_on_qemu_amd64_virt

## Description
This example runs Linux kernel (6.19.9) natively on QEMU x86_64 without the PRTOS hypervisor. It uses a Buildroot-generated initramfs embedded in the kernel bzImage, serving as a baseline reference for the linux_4vcpu_1partion_amd64 example.

## Partition definition
No PRTOS partitioning — Linux runs directly on QEMU.

## Configuration table
No PRTOS XML configuration. Linux is built with embedded Buildroot rootfs.

- Target: x86_64 (AMD64)
- CPU: 4 vCPUs (SMP)
- RAM: 512 MB
- Console: Serial ttyS0 (earlycon)
- Kernel: linux-6.19.9 bzImage with CONFIG_INITRAMFS_SOURCE=buildroot rootfs
- Rootfs: Buildroot with htop, root password "1234"

QEMU launch:
```
qemu-system-x86_64 -m 512 -smp 4 \
    -kernel linux-6.19.9/arch/x86/boot/bzImage \
    -append "console=ttyS0 earlycon" \
    -nographic -no-reboot
```

## Expected results
Linux boots directly on QEMU to a login prompt (root/1234), providing a performance baseline without hypervisor overhead.
