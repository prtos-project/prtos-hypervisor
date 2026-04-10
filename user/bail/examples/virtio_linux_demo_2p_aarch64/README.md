# Virtio Linux Demo - 2 SMP Partitions (AArch64)

## Overview

This demo demonstrates **Virtio device virtualization** on the PRTOS Type-1 Hypervisor using two SMP Linux partitions communicating through shared memory on the AArch64 (ARMv8) platform with hardware-assisted virtualization (EL2/vGIC).

The **System Partition** owns the PL011 UART and runs virtio backend daemons that serve virtualized devices to the **Guest Partition** via shared memory regions. The Guest runs a **userspace frontend daemon** (`virtio_frontend`) that bridges the custom shared-memory protocol to standard Linux devices (`/dev/vda` via NBD, `/dev/hvc0` via PTY, `tap0`/`tap1`/`tap2` via TUN/TAP). Both partitions run full Linux (kernel 6.19.9). The System console is on the PL011 UART (stdio). The Guest console is accessible via a **TCP bridge** in the virtio backend daemon — from the System shell, run `telnet 127.0.0.1 4321` to reach the Guest's `/dev/hvc0`. All services auto-start via init scripts.

**Guest Virtio Frontend**: Standard `virtio-mmio` kernel drivers cannot be used because PRTOS's AArch64 HVC instruction only works from EL1+, not from Linux userspace (EL0). Instead, a userspace daemon (`virtio_frontend`) bridges shared memory to standard Linux devices via NBD (block), PTY (console), and TUN/TAP (network) using polling.

## Architecture

```
┌────────────────────────────────────────────────────────────────┐
│  L0: Host Linux + QEMU (aarch64, virt, GICv3, 4096MB RAM, 4 CPUs) │
├────────────────────────────────────────────────────────────────┤
│  L1: PRTOS Type-1 Hypervisor (32MB)                            │
│  ┌───────────────────────┐  ┌────────────────────────┐         │
│  │ Partition 0 (System)  │  │ Partition 1 (Guest)    │         │
│  │ Linux + Virtio Backend│  │ Linux + Virtio Frontend│         │
│  │ 2 vCPU (pCPU 0-1)     │  │ 2 vCPU (pCPU 2-3)      │         │
│  │ 128MB @ 0x10000000    │  │ 128MB @ 0x18000000     │         │
│  │ console=PL011 (UART)  │  │ console=/dev/hvc0      │         │
│  │                       │  │ (TCP bridge :4321)     │         │
│  │ Services (auto-start): │  │                       │         │
│  │ - prtos_manager       │  │ Virtio Frontend:       │         │
│  │ - virtio_backend      │  │ - virtio_frontend      │         │
│  │   - Console backend   │  │   - NBD (/dev/vda)     │         │
│  │   - 3x Net backend    │  │   - PTY (/dev/hvc0)    │         │
│  │   - Blk backend       │  │   - TAP (tap0/1/2)     │         │
│  │   tap0: 10.0.1.1/24   │  │   tap0: 10.0.1.2/24    │         │
│  │   tap1: 10.0.2.1/24   │  │   tap1: 10.0.2.2/24    │         │
│  │   tap2: 10.0.3.1/24   │  │   tap2: 10.0.3.2/24    │         │
│  │                       │  │ - /opt/virtio_test.sh  │         │
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

| Partition | Console              | Mechanism                                  | Access Method            |
|-----------|---------------------|--------------------------------------------|--------------------------|
| System    | PL011 UART (0x09000000)  | `-nographic` (QEMU serial → stdio)    | Terminal (direct)        |
| Guest     | /dev/hvc0 (PTY)     | virtio-console shared memory + TCP bridge  | `telnet 127.0.0.1 4321` (from System) |

The Guest partition has no direct hardware UART. Instead, `virtio_frontend` creates a PTY pair (`/dev/hvc0`) and bridges it to the console shared memory region. On the System side, `virtio_backend` reads/writes the shared memory and also listens on TCP port 4321 inside the System partition. To access the Guest console, log into the System partition first, then run `telnet 127.0.0.1 4321`. An init script (`S99virtio_guest`) waits for `/dev/hvc0` and spawns `getty` on it automatically.

## Virtio Devices

### Virtio-Console
- **Mechanism**: 4KB bidirectional character ring buffer in shared memory (`Virtio_Con`)
- **Guest device**: `/dev/hvc0` (PTY pair created by `virtio_frontend`)
- **Backend TCP bridge**: `virtio_backend` listens on TCP port 4321 inside System partition
- **Data flow (Guest → System)**: Guest writes to `/dev/hvc0` → `virtio_frontend` copies to `tx_buf` → Backend reads, prints to System UART and sends to TCP client
- **Data flow (System → Guest)**: TCP client sends data → Backend writes to `rx_buf` → `virtio_frontend` reads `rx_buf` → writes to PTY master → Guest reads from `/dev/hvc0`
- **Interactive access**: From System shell: `telnet 127.0.0.1 4321` → Guest getty login
- **Verify**: `echo "Hello PRTOS" > /dev/hvc0` (from Guest) → appears on System console

### Virtio-Net (×3)
- **Mechanism**: 64-slot packet ring buffer (up to 1536 bytes/slot) per instance, bridged via TUN/TAP on both partitions
- **Net0**: System `tap0` (10.0.1.1) ↔ shared memory ↔ Guest `tap0` (10.0.1.2)
- **Net1**: System `tap1` (10.0.2.1) ↔ shared memory ↔ Guest `tap1` (10.0.2.2)
- **Net2**: System `tap2` (10.0.3.1) ↔ shared memory ↔ Guest `tap2` (10.0.3.2)
- **Data flow**: Guest TAP → `tx_slots` in shared memory → Backend reads → Backend TAP (and reverse for RX)
- **Verify**: `ping 10.0.x.1` from Guest, or `ping 10.0.x.2` from System

### Virtio-Blk
- **Mechanism**: 16-slot block request ring (sector-addressed, 512B sectors)
- **Backend**: 1MB in-memory RAM disk (default fallback)
- **Guest device**: `/dev/vda` (symlink to `/dev/nbd0`, served by `virtio_frontend` via NBD protocol)
- **Operations**: IN (read), OUT (write), FLUSH, GET_ID
- **Verify**: The test script (`virtio_test.sh`) creates an ext2 filesystem on `/dev/vda`, mounts it, writes a test file, and verifies the contents

## IP Address Assignment

| Network | System (tap) | Guest (tap) | Subnet |
|---------|-------------|------------|--------|
| Net0    | 10.0.1.1    | 10.0.1.2   | /24    |
| Net1    | 10.0.2.1    | 10.0.2.2   | /24    |
| Net2    | 10.0.3.1    | 10.0.3.2   | /24    |

IP addresses are assigned automatically by init scripts (`S99virtio_backend` on System, `S99virtio_guest` on Guest).

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
| `CONFIG_STRICT_DEVMEM` | `n` | Allow /dev/mem mmap for shared memory |
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

### Interactive Mode
```bash
make run.aarch64
```
System Partition PL011 UART on stdio. Login with `root`/`1234`, then access the Guest from System shell:
```bash
telnet 127.0.0.1 4321
# Login: root / 1234
```

### Nographic Mode (automated testing)
```bash
make run.aarch64.nographic
```
Same as `run.aarch64`. System UART on stdio. Used by the test framework.

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

## Demo Workflow

All virtio services auto-start via init scripts (`S99virtio_backend` on System, `S99virtio_guest` on Guest). No manual steps are required to start the backend or frontend.

### Step 1: Launch QEMU
```bash
make run.aarch64
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

> **Note**: Unlike AMD64 (which provides host-level `telnet localhost 4321` via COM2), AArch64 QEMU virt only has one PL011 UART. Guest console access is through the System partition's shell using the virtio-console TCP bridge.

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
CONFIG_STRICT_DEVMEM**: Must be disabled (`=n`) in the kernel config. The default ARM64 setting (`=y`) blocks `/dev/mem` mmap for addresses outside declared RAM, which prevents `virtio_frontend` from mapping the shared memory regions at 0x20000000+.
- **
- **Hypercall mechanism**: AArch64 HVC instruction only works from EL1+, not from Linux userspace (EL0). The `prtos_vmcall()` function is stubbed to return -1. Virtio operates in **polling mode** (no IPVI doorbell notifications).
- **Boot method**: Uses U-Boot with custom `CONFIG_SYS_BOOTM_LEN=0x10000000` (256MB) to accommodate the ~103MB image containing 2 Linux partitions. The standard qemu_arm64 U-Boot default (128MB) is insufficient.
- **GIC**: GICv3 with maintenance interrupt on IRQ 25.
- **Device tree**: Custom DTS files for each partition (`linux_system.dts`, `linux_guest.dts`) with `cortex-a57` CPU model, GICv3 interrupt controller.

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
bash scripts/run_test.sh --arch aarch64 check-virtio_linux_demo_2p_aarch64

# Full aarch64 test suite:
bash scripts/run_test.sh --arch aarch64 check-all
```

## File Structure

| File / Directory | Description |
|-----------------|-------------|
| `config/resident_sw.xml` | PRTOS system configuration (2 SMP partitions, 5 shared memory regions, 6 IPVIs, dual console) |
| `prtos_cf.aarch64.xml` | Symlink → `config/resident_sw.xml` |
| `Makefile` | Build system (partitions, backend, manager, CPIO overlays, QEMU targets) |
| `start_system.S` | Boot stub for System Partition (ARM64 boot protocol) |
| `start_guest.S` | Boot stub for Guest Partition |
| `hdr_system.c` / `hdr_guest.c` | PRTOS image headers (magic `0x24584d69`) |
| `linker_system.ld` | Linker script (System, base `0x10000000`, initrd at +64MB) |
| `linker_guest.ld` | Linker script (Guest, base `0x18000000`, initrd at +64MB) |
| `linux_system.dts` | Device tree (128MB, 2 CPUs, GICv3, PL011 UART @ 0x09000000) |
| `linux_guest.dts` | Device tree (128MB, 1 CPU, GICv3, no UART — uses virtio-console) |
| `set_serial_poll.c` | Utility for serial polling mode |
| `test_login.py` | Automated test: QEMU launch, PL011 login, `uname` check |
| `test_com2.py` | Automated test: Guest console access via TCP bridge from System |
| `test_console.py` | Console test: clean output (no backend noise), telnet backspace + tab completion |
| **`system_partition/`** | |
| `  include/virtio_be.h` | Shared data structures: `net_shm` (64-slot ring), `blk_shm` (16-slot ring), `console_shm` (4KB ring) |
| `  src/main.c` | Backend daemon: mmap 5 `/dev/mem` regions, init all devices, 1ms poll loop |
| `  src/virtio_console.c` | Console backend: poll `tx_buf` ring → `putchar` + TCP client; TCP client → `rx_buf` ring |
| `  src/virtio_net.c` | Net backend: TAP for bridge, loopback for NAT/p2p |
| `  src/virtio_blk.c` | Block backend: 1MB RAM disk |
| `  src/doorbell.c` | IPVI signaling (stubbed on AArch64, polling mode) |
| `  src/manager_if.c` | Manager wrapper: query Guest partition status |
| `  rootfs_overlay/etc/init.d/S99virtio_backend` | Init script: create `/dev/net/tun`, auto-start `prtos_manager` and `virtio_backend`, configure TAP IPs |
| **`lib_prtos_manager/`** | |
| `  include/prtos_hv.h` | Hypercall API: HVC stub (returns -1 from EL0), status structs |
| `  include/prtos_manager.h` | Manager device interface |
| `  common/prtos_hv.c` | Hypercall implementation: `/dev/mem` mmap mailbox |
| `  common/prtos_manager.c` | Command dispatcher: help, list, partition ops, plan, write, quit |
| `  common/hypervisor.c` | Partition commands: list, halt, reset, resume, status, suspend |
| `  linux/prtos_manager_main.c` | Linux main: stdin/stdout, `-d` dry-run mode |
| **`guest_partition/`** | |
| `  src/virtio_frontend.c` | Userspace frontend daemon: maps shared memory via `/dev/mem`, creates NBD server for `/dev/nbd0` (block), PTY pair for `/dev/hvc0` (console), TUN/TAP devices for networking |
| `  rootfs_overlay/etc/init.d/S99virtio_guest` | Init script: start `virtio_frontend`, wait for `/dev/hvc0`, spawn `getty`, configure TAP IPs, create `/dev/vda` symlink |
| `  rootfs_overlay/opt/virtio_test.sh` | Guest test script: network (3 ifaces), block, console, shmem check |

## Design Notes

- **4096MB QEMU RAM**: Total memory is 4GB. PRTOS Stage-2 page tables identity-map device MMIO regions.
- **Console architecture**: System Partition uses PL011 UART at 0x09000000 (stdio). Guest Partition has no direct UART — unlike AMD64 (which has COM2), AArch64 QEMU virt only provides one PL011. Guest console is accessible via a bidirectional **TCP bridge** in `virtio_backend`: the backend listens on TCP port 4321 inside the System partition and bridges data to/from the console shared memory. To reach the Guest, log into System and run `telnet 127.0.0.1 4321`.
- **Quiet System boot**: System kernel cmdline includes `quiet loglevel=0` to suppress boot messages for clean login experience.
- **Networking**: Each virtio-net instance uses a pair of TUN/TAP devices (one on System, one on Guest) bridged through the shared memory packet ring. IP addresses are assigned by init scripts.
- **Static linking**: `virtio_backend`, `virtio_frontend`, and `prtos_manager` are all statically linked for portability inside the partition rootfs.
- **Auto-start**: Both partitions use Buildroot init scripts (`S99virtio_backend`, `S99virtio_guest`) to automatically start all services at boot. No manual intervention is required.
- **Polling mode**: AArch64 HVC instruction only works from EL1+ (kernel mode). Userspace daemons cannot invoke hypercalls. IPVI doorbells are stubbed; virtio devices operate in polling mode with 1ms intervals.

## Dependencies

- **Linux kernel 6.19.9** (AArch64 Image) with `CONFIG_BLK_DEV_NBD=y`, `CONFIG_TUN=y`, `CONFIG_STRICT_DEVMEM=n`, embedded initramfs (see [Prerequisites](#prerequisites))
- **Buildroot** rootfs with NBD client, root password `1234`, CPIO format output
- **PRTOS Hypervisor** built for aarch64 (`cp prtos_config.aarch64 prtos_config && make defconfig && make`)
- **U-Boot source** at `../../../u-boot/` (sibling of `prtos-hypervisor/`), auto-built with custom config
- **QEMU** (`qemu-system-aarch64`) with virt machine, GICv3, platform bus support
- **Cross-compiler**: `aarch64-linux-gnu-gcc`

## Linux Kernel Command Lines

**System Partition** (`linux_system.dts`):
```
console=ttyAMA0 earlycon=pl011,0x09000000 quiet loglevel=0 nokaslr
```

**Guest Partition** (`linux_guest.dts`):
```
nokaslr
```
