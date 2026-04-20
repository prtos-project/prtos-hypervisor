# Example : mix_os_demo_loongarch64

## Description
This example demonstrates PRTOS mixed-criticality isolation on LoongArch64 with heterogeneous workloads: Linux using CSR trap-and-emulate virtualization (hw-virt) and FreeRTOS using para-virtualization. This showcases the separation kernel architecture with different OS types running concurrently in isolated partitions.

## Partition definition
There are two partitions.
- P0 (Linux): System partition running Linux with 3 vCPUs using trap-and-emulate virtualization with UART pass-through on pCPU 0-2.
- P1 (FreeRTOS): System partition running FreeRTOS using para-virtualization on pCPU 3.

## Configuration table
Dual-partition mixed-OS configuration with 4 physical CPUs.

Memory layout:
- PRTOS: 64 MB
- P0 (Linux): 512 MB @ 0x80000000, with UART and PCH-PIC pass-through
- P1 (FreeRTOS): 2 MB @ 0x06000000

## Prerequisites

### 1. Build Buildroot (rootfs)

```bash
cd /home/chenweis/hdd/Repo/loongarch64_linux_workspace/buildroot
make qemu_loongarch64_virt_efi_defconfig
make menuconfig
# Set the following options:
#   Filesystem images -> cpio the root filesystem: [*]
#   Target packages -> BusyBox -> Build BusyBox as a static binary: [*]
#   Build options -> libraries -> static only: [*]
make -j$(nproc)
```

The rootfs CPIO image will be at `output/images/rootfs.cpio`.

### 2. Build Linux Kernel

```bash
cd /home/chenweis/hdd/Repo/loongarch64_linux_workspace/linux-6.19.9
make ARCH=loongarch CROSS_COMPILE=loongarch64-linux-gnu- loongson64_defconfig
make ARCH=loongarch CROSS_COMPILE=loongarch64-linux-gnu- menuconfig
# Set the following options:
#   General setup -> Initramfs source file(s): path to buildroot rootfs.cpio
#   Device Drivers -> Input device support -> i8042: [ ] (disable)
#   Boot options -> Built-in kernel command string:
#     console=ttyS0,115200 earlycon mem=512M@0x80000000 i8042.noaux i8042.nokbd i8042.nopnp rdinit=/bin/sh
#   Boot options -> Built-in command line override (CONFIG_CMDLINE_FORCE): [*]
make ARCH=loongarch CROSS_COMPILE=loongarch64-linux-gnu- vmlinux -j$(nproc)
```

### 3. LoongArch64 Boot Loader

LoongArch64 on PRTOS does **not** use U-Boot. The mainline U-Boot project does not yet support the LoongArch architecture. Instead, PRTOS uses its own RSW (Resident Software) boot loader located at `user/bootloaders/rsw/loongarch64/`. The RSW is loaded by QEMU's `-kernel` option and handles container unpacking and hypervisor bootstrap.

## Build & Run

```bash
# Build PRTOS
cd prtos-hypervisor
cp prtos_config.loongarch64 prtos_config
make defconfig && make

# Build and run the example
cd user/bail/examples/mix_os_demo_loongarch64
make clean && make
make run.loongarch64
```

## Expected results
PRTOS will load, initialise and run both partitions.
- FreeRTOS partition prints its RTOS banner output via para-virtualization console.
- Linux boots using CSR trap-and-emulate virtualization with 3 vCPUs (2 secondary CPUs started) and reaches a shell prompt (`~ # `).

**Note:** Due to trap-and-emulate overhead, Linux boot takes ~5 minutes wall time.
