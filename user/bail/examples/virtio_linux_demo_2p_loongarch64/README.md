# Example : virtio_linux_demo_2p_loongarch64

## Description
This example demonstrates a two-partition Linux setup on PRTOS LoongArch64 with virtio-based inter-partition communication. Two Linux partitions run in isolated environments using CSR trap-and-emulate virtualization, communicating through shared memory virtio devices managed by the hypervisor.

## Partition definition
There are two partitions.
- P0 (Linux_Backend): System partition running Linux as the virtio backend, handling device I/O.
- P1 (Linux_Frontend): System partition running Linux as the virtio frontend, accessing virtual devices.

## Configuration table
Dual-partition configuration with shared memory for virtio communication.

Memory layout:
- PRTOS: 64 MB
- P0 (Linux_Backend): 512 MB @ 0x80000000, with UART pass-through
- P1 (Linux_Frontend): 256 MB
- Shared memory region for virtio queues

## Prerequisites

### 1. Build Buildroot (rootfs)

```bash
cd /home/chenweis/hdd/Repo/loongarch64_linux_workspace/buildroot
make qemu_loongarch64_virt_efi_defconfig
make menuconfig
# Set the following options:
#   Filesystem images -> cpio the root filesystem: [*]
#   Target packages -> BusyBox -> Build BusyBox as a static binary: [*]
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
#   Device Drivers -> Block devices -> Network block device support: [*]
#   Device Drivers -> Network device support -> Universal TUN/TAP device driver: [*]
#   Device Drivers -> Virtio drivers -> [*] all virtio options
#   Device Drivers -> Input device support -> i8042: [ ] (disable)
#   Boot options -> Built-in kernel command string:
#     console=ttyS0,115200 earlycon mem=512M@0x80000000 i8042.noaux i8042.nokbd i8042.nopnp rdinit=/bin/sh
#   Boot options -> Built-in command line override (CONFIG_CMDLINE_FORCE): [*]

make ARCH=loongarch CROSS_COMPILE=loongarch64-linux-gnu- vmlinux -j$(nproc)
```

### 3. LoongArch64 Boot Loader

LoongArch64 on PRTOS does **not** use U-Boot. The mainline U-Boot project does not yet support the LoongArch architecture. Instead, PRTOS uses its own RSW (Resident Software) boot loader located at `user/bootloaders/rsw/loongarch64/`.

The RSW boot loader:
1. Is loaded directly by QEMU via the `-kernel` option
2. Runs in Direct Address (DA) mode at physical addresses
3. Parses the PRTOS container image format
4. Loads the PRTOS hypervisor core and all partition images
5. Transfers control to the hypervisor which then schedules partitions

This eliminates the need for a secondary boot loader (U-Boot/UEFI) in the virtualization flow.

## Build & Run

```bash
# Build PRTOS
cd prtos-hypervisor
cp prtos_config.loongarch64 prtos_config
make defconfig && make

# Build and run
cd user/bail/examples/virtio_linux_demo_2p_loongarch64
make clean && make
make run.loongarch64
```

## Expected results
PRTOS will load and initialise both Linux partitions.
The System partition (P0) boots Linux with the virtio backend and reaches a shell prompt (`~ # `).
The Guest partition (P1) boots Linux with the virtio frontend.

**Note:** Due to trap-and-emulate overhead, boot takes ~5-8 minutes wall time.
