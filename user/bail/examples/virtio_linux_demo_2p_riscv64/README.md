# Virtio Linux Demo - 2 SMP Partitions (RISC-V 64)
**English** | [中文](README_zh.md)
## Overview

This demo demonstrates **Virtio device virtualization** on the PRTOS Type-1 Hypervisor using two SMP Linux partitions communicating through shared memory on the RISC-V 64-bit platform with hardware-assisted virtualization (H-extension).

The **System Partition** runs virtio backend daemons that serve virtualized devices to the **Guest Partition** via shared memory regions. The Guest runs a **userspace frontend daemon** (`virtio_frontend`) that bridges the custom shared-memory protocol to standard Linux devices (`/dev/vda` via NBD, `/dev/hvc0` via PTY, `tap0`/`tap1`/`tap2` via TUN/TAP). Both partitions run full Linux (kernel 6.19.9) with Buildroot rootfs. System Partition has NS16550 UART console; Guest has no direct console (uses virtio-console). All services auto-start via init scripts.

## Architecture

```
┌────────────────────────────────────────────────────────────────┐
│  L0: Host Linux + QEMU (riscv64, virt, 1GB RAM)                │
├────────────────────────────────────────────────────────────────┤
│  L1: PRTOS Type-1 Hypervisor (64MB @ 0x84000000)               │
│  ┌───────────────────────┐  ┌────────────────────────┐         │
│  │ Partition 0 (System)  │  │ Partition 1 (Guest)    │         │
│  │ Linux + Virtio Backend│  │ Linux + Virtio Frontend│         │
│  │ 2 vCPU (pCPU 0-1)     │  │ 2 vCPU (pCPU 2-3)      │         │
│  │ 128MB @ 0x88000000    │  │ 128MB @ 0x90000000     │         │
│  │ console=NS16550 UART  │  │ console=none (virtio)  │         │
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
│             └──┤ ~5.25MB @ 0x98000000 ├┘                       │
│                │ 5 Virtio Regions     │                        │
│                └──────────────────────┘                        │
│                                                                │
│  IPVI 0-4: Guest→System (per-device doorbell)                  │
│  IPVI 5:   System→Guest (completion doorbell)                  │
└────────────────────────────────────────────────────────────────┘
```

## Memory Layout

| Region              | PA Start     | Size   | PA End       |
|---------------------|-------------|--------|--------------|
| OpenSBI Firmware    | 0x80000000  | 352KB  | 0x80057FFF   |
| RSW Bootloader      | 0x80200000  | 512KB  | 0x8027FFFF   |
| PRTOS Hypervisor    | 0x84000000  | 64MB   | 0x87FFFFFF   |
| System Partition    | 0x88000000  | 128MB  | 0x8FFFFFFF   |
| Guest Partition     | 0x90000000  | 128MB  | 0x97FFFFFF   |
| Shared Memory       | 0x98000000  | ~5.25MB| 0x9853FFFF   |
| **QEMU RAM Total**  |             | 1GB    |              |

**Note**: The container (PRTOS + partition PEFs) is embedded in the RSW binary at 0x80280000. With two full Linux partitions (~38MB each), the container would exceed the 61.5MB gap before PRTOS at 0x84000000. PEF compression (`-c` flag) reduces each PEF to ~25MB, keeping the container within limits. Rootfs CPIO overlays are also gzip-compressed.

## Shared Memory Layout (5 Regions)

| Region       | PA Start     | Size   | Description                   |
|-------------|-------------|--------|---------------------------------|
| Virtio_Net0 | 0x98000000  | 1MB    | virtio-net bridge (TAP backend) |
| Virtio_Net1 | 0x98100000  | 1MB    | virtio-net NAT (loopback)       |
| Virtio_Net2 | 0x98200000  | 1MB    | virtio-net p2p (loopback)       |
| Virtio_Blk  | 0x98300000  | 2MB    | virtio-blk (file or RAM disk)   |
| Virtio_Con  | 0x98500000  | 256KB  | virtio-console (char ring)      |

## CPU Assignment (SMP)

| Physical CPU | Partition        | Virtual CPU |
|-------------|-------------------|-------------|
| pCPU 0      | System (P0)       | vCPU 0      |
| pCPU 1      | System (P0)       | vCPU 1      |
| pCPU 2      | Guest  (P1)       | vCPU 0      |
| pCPU 3      | Guest  (P1)       | vCPU 1      |

Scheduler: 10ms major frame, dedicated pCPU mapping.

## Console Assignment

| Partition | Console        | Access Method            |
|-----------|---------------|--------------------------|
| System    | NS16550 UART  | Terminal (stdio)         |
| Guest     | None          | virtio-console via `/dev/hvc0` |

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

### Workspace Layout

| Component | Path |
|-----------|------|
| Linux kernel source | `/home/chenweis/hdd/Repo/linux_workspace/linux-6.19.9/` |
| RISC-V Linux workspace | `/home/chenweis/hdd/Repo/riscv64_linux_workspace/` |
| RISC-V Buildroot output | `/home/chenweis/hdd/Repo/riscv64_linux_workspace/buildroot_output/` |
| Buildroot source | `/home/chenweis/hdd/Repo/aarch64_linux_workspace/buildroot/` |

### Step 1: Build Buildroot rootfs (RISC-V 64)

```bash
cd /home/chenweis/hdd/Repo/aarch64_linux_workspace/buildroot
make O=/home/chenweis/hdd/Repo/riscv64_linux_workspace/buildroot_output qemu_riscv64_virt_defconfig
cd /home/chenweis/hdd/Repo/riscv64_linux_workspace/buildroot_output
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

Output: `/home/chenweis/hdd/Repo/riscv64_linux_workspace/buildroot_output/images/rootfs.cpio`

### Step 2: Build Linux kernel (RISC-V 64)

```bash
cd /home/chenweis/hdd/Repo/riscv64_linux_workspace/linux-6.19.9
make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- defconfig
```

Apply extra configs (`make menuconfig`):

| Config Option | Value | Purpose |
|---|---|---|
| `CONFIG_BLK_DEV_NBD` | `y` | NBD block device |
| `CONFIG_TUN` | `y` | TUN/TAP device |
| `CONFIG_INITRAMFS_SOURCE` | `/home/chenweis/hdd/Repo/riscv64_linux_workspace/buildroot_output/images/rootfs.cpio` | Embed rootfs |

```bash
make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- -j$(nproc) Image
```

Output: `/home/chenweis/hdd/Repo/riscv64_linux_workspace/linux-6.19.9/arch/riscv/boot/Image`

### Step 3: Build PRTOS Hypervisor

```bash
cd prtos-hypervisor
cp prtos_config.riscv64 prtos_config
make defconfig
make
```

### Step 4: Build the Demo

```bash
cd user/bail/examples/virtio_linux_demo_2p_riscv64
make
```

Build artifacts:
- `resident_sw` — ELF binary (PRTOS + both partitions)
- `resident_sw.bin` — Raw binary for QEMU `-kernel` boot

## Running

```bash
make run.riscv64
```

Boots via OpenSBI (`-bios default`) + QEMU `-kernel` direct loading. System Partition NS16550 UART appears on stdio.

### Manual QEMU Command
```bash
qemu-system-riscv64 \
    -machine virt \
    -cpu rv64 \
    -smp 4 \
    -m 1G \
    -nographic -no-reboot \
    -bios default \
    -kernel resident_sw.bin \
    -monitor none \
    -serial stdio
```

## Demo Workflow

All virtio services auto-start via init scripts (`S99virtio_backend` on System, `S99virtio_guest` on Guest). No manual steps are required to start the backend or frontend.

### Step 1: Launch QEMU
```bash
make run.riscv64
```

### Step 2: Boot System Partition (UART/stdio)
System auto-starts `prtos_manager` and `virtio_backend` via `S99virtio_backend` (output redirected to `/var/log/`):
```
=== PRTOS System Partition ===

Welcome to Buildroot
buildroot login: root
Password: 1234
```

### Step 3: Access Guest Partition
```bash
# From the System partition shell (after logging in):
telnet 127.0.0.1 4321
# Login: root / 1234
```
Guest auto-starts `virtio_frontend` via `S99virtio_guest`. The frontend waits for the backend to initialize shared memory (polls magic values up to 300s), then creates `/dev/nbd0` (block) and `/dev/hvc0` (console). The init script waits for `/dev/hvc0` and spawns `getty` on it. The backend's TCP bridge on port 4321 connects telnet to `/dev/hvc0` via shared memory.

> **Note**: RISC-V QEMU virt only has one NS16550 UART. Guest console access is through the System partition's shell using the virtio-console TCP bridge.

### Step 4: Test Virtio Devices (Guest)
```bash
/opt/virtio_test.sh   # Automated test for all virtio devices
```
Expected output includes:
- Network: 3 TAP interfaces (tap0/tap1/tap2) with IPs, all pings to System partition OK
- Block device (`/dev/vda` → `/dev/nbd0`): ext2 filesystem created, mounted, test file written and verified
- Console (`/dev/hvc0`): message "Hello PRTOS from Guest!" forwarded to System UART
- Shared memory magic values verified (NET0=0x4E455430, BLK0=0x424C4B30, CONS=0x434F4E53)
- `Verification Passed`

## Platform-Specific Notes

- **Hypercall mechanism**: RISC-V ecall from VU-mode (Linux userspace) is trapped by the VS-mode kernel, not the hypervisor. The `prtos_vmcall()` function is stubbed to return -1. Virtio operates in **polling mode** (no IPVI doorbell notifications).
- **Container size constraint**: The RSW container (at 0x80280000) must fit before PRTOS (at 0x84000000), a gap of only 61.5MB. With two Linux partitions (~38MB each uncompressed), PEF compression (`-c` flag) is required; each compressed PEF is ~25MB, keeping the ~50MB container within limits.
- **Boot method**: OpenSBI firmware loads at 0x80000000, then transfers control to the RSW at 0x80200000 which unpacks the PRTOS container.
- **UART passthrough**: NS16550 UART at 0x10000000 is passed through to the System Partition for console output.

## Testing

```bash
# Automated login test:
python3 test_login.py

# Guest console (TCP bridge) test:
python3 test_com2.py

# Console test (clean output, backspace, tab completion):
python3 test_console.py

# Via the test framework:
cd ../../../../  # back to prtos-hypervisor root
bash scripts/run_test.sh --arch riscv64 check-virtio_linux_demo_2p_riscv64

# Full riscv64 test suite:
bash scripts/run_test.sh --arch riscv64 check-all
```

## File Structure

| File / Directory | Description |
|-----------------|-------------|
| `config/resident_sw.xml` | PRTOS system configuration |
| `Makefile` | Build system |
| `start_system.S` | Boot stub for System Partition (RISC-V boot protocol) |
| `start_guest.S` | Boot stub for Guest Partition |
| `hdr_system.c` / `hdr_guest.c` | PRTOS image headers |
| `linker_system.ld` | Linker script (base `0x88000000`, initrd at +64MB) |
| `linker_guest.ld` | Linker script (base `0x90000000`, initrd at +64MB) |
| `linux_system.dts` | Device tree (128MB, 2 CPUs, NS16550 UART) |
| `linux_guest.dts` | Device tree (128MB, 2 CPUs, no UART) |
| `set_serial_poll.c` | Utility for serial polling mode |
| `test_login.py` | Automated test: QEMU launch, UART login, `uname` check |
| `test_com2.py` | Automated test: Guest console access via TCP bridge from System |
| `test_console.py` | Console test: clean output (no backend noise), telnet backspace + tab completion |
| **`system_partition/`** | |
| `  include/virtio_be.h` | Shared data structures (addresses at 0x98xxxxxx) |
| `  src/` | Backend daemon sources |
| `  rootfs_overlay/` | System init scripts |
| **`lib_prtos_manager/`** | |
| `  include/prtos_hv.h` | Hypercall API (ecall stub, mailbox at 0x98500000) |
| `  common/` | Manager and hypercall implementations |
| **`guest_partition/`** | |
| `  src/virtio_frontend.c` | Userspace frontend daemon |
| `  rootfs_overlay/` | Guest init scripts and test script |

## Dependencies

- **Linux kernel 6.19.9** (RISC-V Image) with `CONFIG_BLK_DEV_NBD=y`, `CONFIG_TUN=y`, embedded initramfs
- **Buildroot** rootfs with NBD client, root password `1234`, CPIO format
- **PRTOS Hypervisor** built for riscv64
- **QEMU** (`qemu-system-riscv64`) with virt machine
- **Cross-compiler**: `riscv64-linux-gnu-gcc`
