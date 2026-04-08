# Virtio Linux Demo - 2 SMP Partitions (AArch64)

## Overview

This demo demonstrates **Virtio device virtualization** on the PRTOS Type-1 Hypervisor using two SMP Linux partitions communicating through shared memory on the AArch64 (ARMv8) platform with hardware-assisted virtualization (EL2/vGIC).

The **System Partition** runs virtio backend daemons that serve virtualized devices to the **Guest Partition** via shared memory regions. The Guest runs a **userspace frontend daemon** (`virtio_frontend`) that bridges the custom shared-memory protocol to standard Linux devices (`/dev/vda` via NBD, `/dev/hvc0` via PTY, `tap0`/`tap1`/`tap2` via TUN/TAP). Both partitions run full Linux (kernel 6.19.9) with Buildroot rootfs. System Partition has PL011 UART console; Guest has no direct console (uses virtio-console). All services auto-start via init scripts.

## Architecture

```
┌────────────────────────────────────────────────────────────────┐
│  L0: Host Linux + QEMU (aarch64, virt, GICv3, 4096MB RAM)      │
├────────────────────────────────────────────────────────────────┤
│  L1: PRTOS Type-1 Hypervisor (32MB)                            │
│  ┌───────────────────────┐  ┌────────────────────────┐         │
│  │ Partition 0 (System)  │  │ Partition 1 (Guest)    │         │
│  │ Linux + Virtio Backend│  │ Linux + Virtio Frontend│         │
│  │ 2 vCPU (pCPU 0-1)     │  │ 2 vCPU (pCPU 2-3)      │         │
│  │ 128MB @ 0x10000000    │  │ 128MB @ 0x18000000     │         │
│  │ console=PL011 UART    │  │ console=none (virtio)  │         │
│  │                       │  │                        │         │
│  │ Services (auto-start): │  │ Virtio Frontend:       │         │
│  │ - prtos_manager       │  │ - virtio_frontend      │         │
│  │ - virtio_backend      │  │   - NBD (/dev/vda)     │         │
│  │   - Console backend   │  │   - PTY (/dev/hvc0)    │         │
│  │   - 3x Net backend    │  │   - TAP (tap0/1/2)     │         │
│  │   - Blk backend       │  │                        │         │
│  │   tap0: 10.0.1.1/24   │  │   tap0: 10.0.1.2/24    │         │
│  │   tap1: 10.0.2.1/24   │  │   tap1: 10.0.2.2/24    │         │
│  │   tap2: 10.0.3.1/24   │  │   tap2: 10.0.3.2/24    │         │
│  └──────────┬────────────┘  └──────────┬─────────────┘         │
│             │     Shared Memory        │                       │
│             │  ┌──────────────────────┐│                       │
│             └──┤ ~5.25MB @ 0x20000000 ├┘                       │
│                │ 5 Virtio Regions     │                        │
│                └──────────────────────┘                        │
│                                                                │
│  IPVI 0-4: Guest→System (per-device doorbell)                  │
│  IPVI 5:   System→Guest (completion doorbell)                  │
└────────────────────────────────────────────────────────────────┘
```

## Memory Layout

| Region              | IPA Start    | Size   | IPA End      |
|---------------------|-------------|--------|--------------|
| PRTOS Hypervisor    | (auto)      | 32MB   |              |
| System Partition    | 0x10000000  | 128MB  | 0x17FFFFFF   |
| Guest Partition     | 0x18000000  | 128MB  | 0x1FFFFFFF   |
| Shared Memory       | 0x20000000  | ~5.25MB| 0x2053FFFF   |
| **QEMU RAM Total**  |             | 4096MB |              |

## Shared Memory Layout (5 Regions)

| Region       | IPA Start    | Size   | Description                   |
|-------------|-------------|--------|---------------------------------|
| Virtio_Net0 | 0x20000000  | 1MB    | virtio-net bridge (TAP backend) |
| Virtio_Net1 | 0x20100000  | 1MB    | virtio-net NAT (loopback)       |
| Virtio_Net2 | 0x20200000  | 1MB    | virtio-net p2p (loopback)       |
| Virtio_Blk  | 0x20300000  | 2MB    | virtio-blk (file or RAM disk)   |
| Virtio_Con  | 0x20500000  | 256KB  | virtio-console (char ring)      |

## CPU Assignment (SMP)

| Physical CPU | Partition        | Virtual CPU |
|-------------|-------------------|-------------|
| pCPU 0      | System (P0)       | vCPU 0      |
| pCPU 1      | System (P0)       | vCPU 1      |
| pCPU 2      | Guest  (P1)       | vCPU 0      |
| pCPU 3      | Guest  (P1)       | vCPU 1      |

Scheduler: 10ms major frame, dedicated pCPU mapping.

## Console Assignment

| Partition | Console   | Access Method            |
|-----------|----------|--------------------------|
| System    | PL011 UART | Terminal (stdio)       |
| Guest     | None     | virtio-console via `/dev/hvc0` |

## Virtio Devices

### Virtio-Console
- **Mechanism**: 4KB character ring buffer in shared memory (`Virtio_Con`)
- **Guest device**: `/dev/hvc0` (PTY pair created by `virtio_frontend`)
- **Data flow**: Guest writes → shared memory → Backend reads → System UART

### Virtio-Net (×3)
- **Mechanism**: 64-slot packet ring buffer per instance, bridged via TUN/TAP
- **Net0**: System `tap0` (10.0.1.1) ↔ Guest `tap0` (10.0.1.2)
- **Net1**: System `tap1` (10.0.2.1) ↔ Guest `tap1` (10.0.2.2)
- **Net2**: System `tap2` (10.0.3.1) ↔ Guest `tap2` (10.0.3.2)

### Virtio-Blk
- **Mechanism**: 16-slot block request ring (sector-addressed, 512B sectors)
- **Backend**: 1MB in-memory RAM disk (default fallback)
- **Guest device**: `/dev/vda` (symlink to `/dev/nbd0`)

## Inter-Partition Communication (IPVI)

| IPVI ID | Direction          | Purpose                      |
|---------|--------------------|------------------------------|
| 0       | Guest → System     | virtio-net0 doorbell         |
| 1       | Guest → System     | virtio-net1 doorbell         |
| 2       | Guest → System     | virtio-net2 doorbell         |
| 3       | Guest → System     | virtio-blk doorbell          |
| 4       | Guest → System     | virtio-console doorbell      |
| 5       | System → Guest     | Completion notification      |

Two **Sampling Channels** provide control-plane messaging (8B each).

## Prerequisites

### Step 1: Build Buildroot rootfs (AArch64)

```bash
cd buildroot
make qemu_aarch64_virt_defconfig
```

Apply configuration (`make menuconfig`):

| Config Option | Value | Purpose |
|---|---|---|
| `BR2_TARGET_GENERIC_ROOT_PASSWD` | `1234` | Root login password |
| `BR2_TARGET_ROOTFS_CPIO` | `y` | Generate rootfs.cpio |
| `BR2_PACKAGE_NBD` | `y` | NBD client |
| `BR2_PACKAGE_NBD_CLIENT` | `y` | NBD client binary |

```bash
make -j$(nproc)
```

### Step 2: Build Linux kernel (AArch64)

```bash
cd linux-6.19.9
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig
```

Apply extra configs (`make menuconfig`):

| Config Option | Value | Purpose |
|---|---|---|
| `CONFIG_BLK_DEV_NBD` | `y` | NBD block device |
| `CONFIG_TUN` | `y` | TUN/TAP device |
| `CONFIG_INITRAMFS_SOURCE` | `/path/to/buildroot/output/images/rootfs.cpio` | Embed rootfs |

```bash
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j$(nproc) Image
```

### Step 3: Build PRTOS Hypervisor

```bash
cd prtos-hypervisor
cp prtos_config.aarch64 prtos_config
make defconfig
make
```

### Step 4: Build the Demo

```bash
cd user/bail/examples/virtio_linux_demo_2p_aarch64
make
```

Build artifacts:
- `resident_sw` — ELF binary (PRTOS + both partitions)
- `resident_sw_image` — mkimage Legacy Image for U-Boot boot
- `u-boot/u-boot.bin` — Custom U-Boot binary (auto-built from source)

### Step 5: Build U-Boot (automatic)

The Makefile automatically builds a custom U-Boot from the source tree at `../../../u-boot/` (relative to `prtos-hypervisor/`) with the following configuration changes:

| Config Option | Value | Purpose |
|---|---|---|
| `CONFIG_SYS_BOOTM_LEN` | `0x10000000` (256MB) | Allow booting the ~103MB resident_sw_image (default 128MB is insufficient) |
| `CONFIG_PREBOOT` | `bootm 0x40200000 - ${fdtcontroladdr}` | Auto-boot the PRTOS image loaded at 0x40200000 |

To build U-Boot manually:
```bash
cd u-boot  # (sibling directory of prtos-hypervisor)
make qemu_arm64_defconfig
scripts/config --set-val CONFIG_SYS_BOOTM_LEN 0x10000000
scripts/config --set-str CONFIG_PREBOOT 'bootm 0x40200000 - ${fdtcontroladdr}'
make -j$(nproc) CROSS_COMPILE=aarch64-linux-gnu-
```

The resulting `u-boot.bin` is placed in `u-boot/u-boot.bin` within the demo directory.

## Running

```bash
make run.aarch64
```

Boots via U-Boot (`-bios u-boot.bin`) with the PRTOS image loaded at 0x40200000 via QEMU device loader. U-Boot's `preboot` command automatically invokes `bootm` to boot the image. System Partition PL011 UART appears on stdio.

### Manual QEMU Command
```bash
qemu-system-aarch64 \
    -machine virt,gic_version=3 \
    -machine virtualization=true \
    -cpu cortex-a72 -machine type=virt \
    -m 4096 -smp 4 \
    -bios ./u-boot/u-boot.bin \
    -device loader,file=./resident_sw_image,addr=0x40200000,force-raw=on \
    -nographic -no-reboot
```

## U-Boot Boot Flow

1. QEMU starts U-Boot as BIOS firmware
2. QEMU device loader places `resident_sw_image` (mkimage Legacy Image) at address 0x40200000
3. U-Boot executes `preboot` command: `bootm 0x40200000 - ${fdtcontroladdr}`
4. U-Boot verifies the image checksum, loads it, and jumps to the PRTOS RSW entry point
5. RSW unpacks the container (PRTOS core + 2 partition PEFs) and starts the hypervisor

## Platform-Specific Notes

- **Hypercall mechanism**: AArch64 HVC instruction only works from EL1+, not from Linux userspace (EL0). The `prtos_vmcall()` function is stubbed to return -1. Virtio operates in **polling mode** (no IPVI doorbell notifications).
- **Boot method**: Uses U-Boot with custom `CONFIG_SYS_BOOTM_LEN=0x10000000` (256MB) to accommodate the ~103MB image containing 2 Linux partitions. The standard qemu_arm64 U-Boot default (128MB) is insufficient.
- **GIC**: GICv3 with maintenance interrupt on IRQ 25.
- **Device tree**: Custom DTS files for each partition (`linux_system.dts`, `linux_guest.dts`) with `cortex-a57` CPU model, GICv3 interrupt controller.

## Testing

```bash
# Via the test framework:
cd ../../../../  # back to prtos-hypervisor root
bash scripts/run_test.sh --arch aarch64 check-virtio_linux_demo_2p_aarch64

# Full aarch64 test suite:
bash scripts/run_test.sh --arch aarch64 check-all
```

## File Structure

| File / Directory | Description |
|-----------------|-------------|
| `config/resident_sw.xml` | PRTOS system configuration |
| `Makefile` | Build system |
| `start_system.S` | Boot stub for System Partition (ARM64 boot protocol) |
| `start_guest.S` | Boot stub for Guest Partition |
| `hdr_system.c` / `hdr_guest.c` | PRTOS image headers |
| `linker_system.ld` | Linker script (base `0x10000000`, initrd at +64MB) |
| `linker_guest.ld` | Linker script (base `0x18000000`, initrd at +64MB) |
| `linux_system.dts` | Device tree (128MB, 2 CPUs, GICv3, PL011 UART) |
| `linux_guest.dts` | Device tree (128MB, 2 CPUs, GICv3, no UART) |
| `set_serial_poll.c` | Utility for serial polling mode |
| **`system_partition/`** | |
| `  include/virtio_be.h` | Shared data structures (addresses at 0x20xxxxxx) |
| `  src/` | Backend daemon sources |
| `  rootfs_overlay/` | System init scripts |
| **`lib_prtos_manager/`** | |
| `  include/prtos_hv.h` | Hypercall API (HVC stub, mailbox at 0x20500000) |
| `  common/` | Manager and hypercall implementations |
| **`guest_partition/`** | |
| `  src/virtio_frontend.c` | Userspace frontend daemon |
| `  rootfs_overlay/` | Guest init scripts and test script |

## Dependencies

- **Linux kernel 6.19.9** (AArch64 Image) with `CONFIG_BLK_DEV_NBD=y`, `CONFIG_TUN=y`, embedded initramfs
- **Buildroot** rootfs with NBD client, root password `1234`, CPIO format
- **PRTOS Hypervisor** built for aarch64
- **U-Boot source** at `../../../u-boot/` (sibling of `prtos-hypervisor/`), auto-built with custom config
- **QEMU** (`qemu-system-aarch64`) with virt machine, GICv3
- **Cross-compiler**: `aarch64-linux-gnu-gcc`
