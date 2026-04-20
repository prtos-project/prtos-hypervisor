# Example : linux_4vcpu_1partion_loongarch64

## Description
This example demonstrates the Linux kernel running on PRTOS LoongArch64 with trap-and-emulate hardware-assisted virtualization (hw-virt). The guest Linux runs with 4 vCPUs, CSR/TLB/timer operations trapped and emulated by the hypervisor. UART is mapped via guest TLB pass-through.

## Partition definition
There is one partition.
- P0 (Linux): System partition running Linux with 4 vCPUs using CSR trap-and-emulate virtualization, with UART device pass-through.

## Configuration table
Single-partition configuration with 4 physical CPUs and device pass-through.

A scheduling plan is defined under the following premises:

- Processors: 4 (pCPU 0-3)
- MAF = 100 ms
- P0: S 0 ms  D 50 ms (on each pCPU)

Memory layout:
- PRTOS: 32 MB
- P0 (Linux): 512 MB @ 0x80000000
- UART device pass-through: 4 KB @ 0x1FE00000
- PCH-PIC device pass-through: 1 MB @ 0x10000000

## Prerequisites

### 1. Build Buildroot (rootfs)

```bash
cd /home/chenweis/hdd/Repo/loongarch64_linux_workspace/buildroot

# Use QEMU LoongArch64 virt defconfig as a starting point
make qemu_loongarch64_virt_efi_defconfig

# Customize configuration
make menuconfig
# Set the following options:
#   Target options -> Architecture: LoongArch (64-bit)
#   Filesystem images -> cpio the root filesystem: [*]
#   Target packages -> BusyBox -> Build BusyBox as a static binary: [*]
#   Build options -> libraries -> static only: [*]

make -j$(nproc)
```

The rootfs CPIO image will be at `output/images/rootfs.cpio`.

### 2. Build Linux Kernel

```bash
cd /home/chenweis/hdd/Repo/loongarch64_linux_workspace/linux-6.19.9

# Use loongson64 defconfig as base
make ARCH=loongarch CROSS_COMPILE=loongarch64-linux-gnu- loongson64_defconfig

# Customize configuration
make ARCH=loongarch CROSS_COMPILE=loongarch64-linux-gnu- menuconfig
# Set the following options:
#   General setup -> Initial RAM filesystem and RAM disk support -> Initramfs source file(s):
#     /home/chenweis/hdd/Repo/loongarch64_linux_workspace/buildroot/output/images/rootfs.cpio
#   General setup -> Configure standard kernel features -> Enable support for printk: [*]
#   Device Drivers -> Input device support -> Hardware I/O ports -> i8042 PC Keyboard controller: [ ]
#   Device Drivers -> Input device support -> Keyboards -> AT keyboard: [ ]
#   Device Drivers -> Input device support -> Mice -> PS/2 mouse: [ ]
#   Kernel hacking -> printk and dmesg options -> Enable dynamic printk() support: [ ]
#   Boot options -> Built-in kernel command string:
#     console=ttyS0,115200 earlycon mem=512M@0x80000000 i8042.noaux i8042.nokbd i8042.nopnp rdinit=/bin/sh
#   Boot options -> Built-in command line override (CONFIG_CMDLINE_FORCE): [*]

make ARCH=loongarch CROSS_COMPILE=loongarch64-linux-gnu- vmlinux -j$(nproc)
```

The Makefile automatically picks up `vmlinux` from the Linux workspace.

### 3. LoongArch64 Boot Loader

LoongArch64 on PRTOS does **not** use U-Boot. The mainline U-Boot project does not yet support the LoongArch architecture. Instead, PRTOS uses its own RSW (Resident Software) boot loader located at `user/bootloaders/rsw/loongarch64/`. The RSW is a lightweight stub that:

1. Runs in Direct Address (DA) mode at reset
2. Parses the PRTOS container image
3. Loads the PRTOS hypervisor core
4. Transfers control to the hypervisor

QEMU's `-kernel resident_sw` option loads the RSW directly, which in turn boots the hypervisor and all partitions.

## Build & Run

```bash
# Build PRTOS
cd prtos-hypervisor
cp prtos_config.loongarch64 prtos_config
make defconfig && make

# Build and run the example
cd user/bail/examples/linux_4vcpu_1partion_loongarch64
make clean && make
make run.loongarch64
```

## Expected results
PRTOS will load, initialise and run the Linux partition using CSR trap-and-emulate virtualization with 4 vCPUs.
Linux boots with SMP support (3 secondary CPUs started), reaches "Run /bin/sh as init process", and presents a shell prompt (`~ # `).

**Note:** Due to trap-and-emulate overhead, boot time is significantly longer than native (~3 minutes wall time).
