# Virtio Linux Demo - 2 SMP Partitions (amd64)

## Overview

This demo demonstrates **Virtio device virtualization** on the PRTOS Type-1 Hypervisor using two SMP Linux partitions communicating through shared memory on the amd64 (x86_64) platform with hardware-assisted virtualization (Intel VT-x / VMX).

The **System Partition** owns all hardware resources (PCI, legacy I/O, IRQs) and runs virtio backend daemons that serve virtualized devices to the **Guest Partition** via shared memory regions. The Guest runs a **userspace frontend daemon** (`virtio_frontend`) that bridges the custom shared-memory protocol to standard Linux devices (`/dev/vda` via NBD, `/dev/hvc0` via PTY, `tap0`/`tap1`/`tap2` via TUN/TAP). Both partitions run full Linux (kernel 6.19.9) with dual-console support: UART for System, VGA+telnet for Guest. All services auto-start via init scripts.

## Architecture

```
┌────────────────────────────────────────────────────────────────┐
│  L0: Host Linux + QEMU (x86_64, KVM, 4 pCPUs, 1024MB RAM)      │
├────────────────────────────────────────────────────────────────┤
│  L1: PRTOS Type-1 Hypervisor (12MB @ 0x1000000)                │
│  ┌───────────────────────┐  ┌────────────────────────┐         │
│  │ Partition 0 (System)  │  │ Partition 1 (Guest)    │         │
│  │ Linux + Virtio Backend│  │ Linux + Virtio Frontend│         │
│  │ 2 vCPU (pCPU 0-1)     │  │ 2 vCPU (pCPU 2-3)      │         │
│  │ 128MB @ 0x6000000     │  │ 128MB @ 0xE000000      │         │
│  │ console=ttyS0 (UART)  │  │ console=tty0 (VGA)     │         │
│  │                       │  │ + ttyS1 (COM2/telnet)  │         │
│  │ Services (auto-start): │  │                        │         │
│  │ - prtos_manager       │  │ Virtio Frontend:       │         │
│  │ - virtio_backend      │  │ - virtio_frontend      │         │
│  │   - Console backend   │  │   - NBD (/dev/vda)     │         │
│  │   - 3x Net backend    │  │   - PTY (/dev/hvc0)    │         │
│  │   - Blk backend       │  │   - TAP (tap0/1/2)     │         │
│  │   tap0: 10.0.1.1/24   │  │   tap0: 10.0.1.2/24   │         │
│  │   tap1: 10.0.2.1/24   │  │   tap1: 10.0.2.2/24   │         │
│  │   tap2: 10.0.3.1/24   │  │   tap2: 10.0.3.2/24   │         │
│  │                       │  │ - /opt/virtio_test.sh  │         │
│  └──────────┬────────────┘  └──────────┬─────────────┘         │
│             │     Shared Memory        │                       │
│             │  ┌──────────────────────┐│                       │
│             └──┤ ~5.25MB @ 0x16000000 ├┘                       │
│                │ 5 Virtio Regions     │                        │
│                └──────────────────────┘                        │
│                                                                │
│  IPVI 0-4: Guest→System (per-device doorbell)                  │
│  IPVI 5:   System→Guest (completion doorbell)                  │
└────────────────────────────────────────────────────────────────┘
```

## Memory Layout

| Region              | GPA Start    | Size   | GPA End    |
|---------------------|-------------|--------|-------------|
| PRTOS Hypervisor    | 0x01000000  | 12MB   | 0x01BFFFFF  |
| System Partition    | 0x06000000  | 128MB  | 0x0DFFFFFF  |
| Guest Partition     | 0x0E000000  | 128MB  | 0x15FFFFFF  |
| Shared Memory       | 0x16000000  | ~5.25MB| 0x1653FFFF  |
| **QEMU RAM Total**  |             | 1024MB |             |

All addresses are within the first 1GB, required by PRTOS EPT identity-map (single PDPT entry covering 0x0–0x3FFFFFFF).

## Shared Memory Layout (5 Regions at GPA 0x16000000+)

| Region       | GPA Start    | Size   | Description                   |
|-------------|-------------|--------|---------------------------------|
| Virtio_Net0 | 0x16000000  | 1MB    | virtio-net bridge (TAP backend) |
| Virtio_Net1 | 0x16100000  | 1MB    | virtio-net NAT (loopback)       |
| Virtio_Net2 | 0x16200000  | 1MB    | virtio-net p2p (loopback)       |
| Virtio_Blk  | 0x16300000  | 2MB    | virtio-blk (file or RAM disk)   |
| Virtio_Con  | 0x16500000  | 256KB  | virtio-console (char ring)      |

Each region uses `flags="shared"` in the XML config and is EPT-mapped into both partitions. Shared memory is NOT in the e820 memory map — accessed via `/dev/mem` mmap from Linux userspace to avoid kernel sparse-memory bugs.

## CPU Assignment (SMP)

| Physical CPU | Partition        | Virtual CPU |
|-------------|-------------------|-------------|
| pCPU 0      | System (P0)       | vCPU 0      |
| pCPU 1      | System (P0)       | vCPU 1      |
| pCPU 2      | Guest  (P1)       | vCPU 0      |
| pCPU 3      | Guest  (P1)       | vCPU 1      |

Scheduler: 10ms major frame, dedicated pCPU mapping (each pCPU runs one vCPU full-time).

## Console Assignment

| Partition | Console   | QEMU Device                        | Access Method            |
|-----------|----------|-------------------------------------|--------------------------|
| System    | UART     | `-serial mon:stdio`                 | Terminal (SSH)           |
| Guest     | VGA      | `-vga std -vnc :1`                  | VNC `localhost:5901`     |
| Guest     | COM2     | `-serial telnet::4321,server,nowait`| `telnet localhost 4321`  |

The Guest partition uses `set_serial_poll` (ioctl `TIOCSSERIAL` with `irq=0`) to enable polling mode for COM2/ttyS1, since PRTOS does not route IRQ3 to the Guest. An init script (`S99virtio_guest`) spawns `getty` on ttyS1 automatically.

## Virtio Devices

### Virtio-Console
- **Mechanism**: 4KB character ring buffer in shared memory (`Virtio_Con`)
- **Guest device**: `/dev/hvc0` (PTY pair created by `virtio_frontend`)
- **Data flow**: Guest writes to `/dev/hvc0` → `virtio_frontend` copies to `tx_buf` in shared memory → Backend reads and prints to System UART
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
- **Backend**: File-backed disk (`disk.img`) or 1MB in-memory RAM disk (default fallback)
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
| 0       | Guest → System     | virtio-net0 (bridge) doorbell|
| 1       | Guest → System     | virtio-net1 (NAT) doorbell   |
| 2       | Guest → System     | virtio-net2 (p2p) doorbell   |
| 3       | Guest → System     | virtio-blk doorbell          |
| 4       | Guest → System     | virtio-console doorbell      |
| 5       | System → Guest     | Completion notification      |

Additionally, two **Sampling Channels** provide control-plane messaging:
- `GuestToSystem`: Guest → System (8B messages)
- `SystemToGuest`: System → Guest (8B messages)

## Hardware Resources (System Partition)

The System Partition owns all non-reserved hardware:

**IRQs** (assigned via XML): 1, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
- Reserved by PRTOS (excluded): IRQ 2 (PIC cascade), IRQ 4 (COM1/UART), IRQ 24, 26, 27

**I/O Ports** (assigned via XML):

| Range          | Description                |
|----------------|----------------------------|
| 0x00–0x1F      | DMA controller             |
| 0x40–0x43      | PIT 8254 timer             |
| 0x60–0x64      | Keyboard controller        |
| 0x70–0x71      | RTC/CMOS                   |
| 0x80–0x8F      | DMA page registers         |
| 0x3FD–0x3FF    | COM1 status (partial)      |
| 0xCF8–0xCFF    | PCI config space           |

Reserved by PRTOS (excluded): 0x20–0x21 (PIC master), 0xA0–0xA1 (PIC slave), 0x3F8–0x3FC (COM1 data/control)

**PCI Passthrough** (via QEMU, demo targets only):
- 3× `virtio-net-pci` with `disable-modern=on` (forces legacy INTx, no MSI-X)
- 1× `virtio-blk-pci` (via `-drive file=disk.img,if=virtio,format=raw`)

## Prerequisites: Linux Kernel & Buildroot

The demo requires a Linux kernel with an embedded initramfs (rootfs). The following steps build both from source.

### Step 1: Build Buildroot rootfs

```bash
cd buildroot
make qemu_x86_64_defconfig
```

Then apply the following configuration changes (`make menuconfig`):

| Config Option | Value | Purpose |
|---|---|---|
| `BR2_TARGET_GENERIC_ROOT_PASSWD` | `1234` | Root login password |
| `BR2_TARGET_ROOTFS_CPIO` | `y` | Generate rootfs.cpio for kernel embedding |
| `BR2_PACKAGE_NBD` | `y` | NBD client (required by virtio_frontend) |
| `BR2_PACKAGE_NBD_CLIENT` | `y` | NBD client binary |
| `BR2_PACKAGE_HTOP` | `y` | System monitoring (optional) |
| `BR2_PACKAGE_NCURSES` | `y` | Terminal library for htop |

```bash
make -j$(nproc)
# Output: output/images/rootfs.cpio (~12MB)
```

### Step 2: Build Linux kernel with embedded initramfs

```bash
cd linux-6.19.9
make x86_64_defconfig
```

Then apply extra kernel configs (`make menuconfig`):

| Config Option | Value | Purpose |
|---|---|---|
| `CONFIG_BLK_DEV_NBD` | `y` | NBD block device (for `/dev/nbd0` → `/dev/vda`) |
| `CONFIG_TUN` | `y` | TUN/TAP device (for virtio-net TAP interfaces) |
| `CONFIG_INITRAMFS_SOURCE` | `/path/to/buildroot/output/images/rootfs.cpio` | Embed rootfs into bzImage |

```bash
make -j$(nproc) bzImage
# Output: arch/x86/boot/bzImage (~19MB with embedded initramfs)
```

Copy the built kernel to the demo's expected location (see `Makefile` `BZIMAGE` variable for the path).

### Step 3: Build PRTOS Hypervisor

```bash
cd prtos-hypervisor
cp prtos_config.amd64 prtos_config
make defconfig
make
```

### Step 4: Build the Demo

```bash
cd user/bail/examples/virtio_linux_demo_2p_amd64
make
```

Build artifacts:
- `resident_sw.iso` — Bootable ISO (GRUB + PRTOS + both partitions)
- `virtio_backend` — Static binary for System Linux userspace
- `virtio_frontend` — Static binary for Guest Linux userspace (NBD + PTY bridge)
- `prtos_manager` — Static binary for partition management CLI
- `rootfs_overlay.cpio` — System overlay (backend + manager + S99virtio_backend init)
- `guest_rootfs_overlay.cpio` — Guest overlay (frontend + test script + S99virtio_guest init)
- `disk.img` — 64MB raw block device image (created by demo targets)

## Running

### Basic Mode (UART + VGA + Telnet)
```bash
make run.amd64
```
Opens three access points:
- **Terminal** (this window): System Partition COM1 login (`root`/`1234`)
- **VNC** `vnc://localhost:5901`: Guest Partition VGA display
- **Telnet** `telnet localhost 4321`: Guest Partition COM2 login (`root`/`1234`)

### Nographic Mode (automated testing)
```bash
make run.amd64.nographic
# Or:
make run.amd64.kvm.nographic
```
System UART only on stdio. Used by the test framework.

### Full Demo (with PCI devices)
```bash
# With TAP networking (requires root for tap0):
make run.amd64.demo

# Without TAP (NAT only, no root required):
make run.amd64.demo.nat
```
Adds QEMU PCI devices (virtio-net-pci ×3 + virtio-blk-pci) with `disable-modern=on,vectors=0` for the System Partition. MSI-X is disabled (`vectors=0`) because PRTOS does not support MSI-X routing to L2 partitions.

### Manual QEMU Command
```bash
qemu-system-x86_64 -enable-kvm -cpu host,-waitpkg \
    -m 1024 -smp 4 \
    -cdrom resident_sw.iso \
    -serial mon:stdio \
    -serial telnet::4321,server,nowait \
    -vga std -display none -vnc :1 \
    -boot d
```

## Demo Workflow

All virtio services auto-start via init scripts (`S99virtio_backend` on System, `S99virtio_guest` on Guest). No manual steps are required to start the backend or frontend.

### Step 1: Launch QEMU
```bash
make run.amd64
```

### Step 2: Boot System Partition (UART/stdio)
System auto-starts `prtos_manager` and `virtio_backend` via `S99virtio_backend`:
```
=== PRTOS System Partition ===
PRTOS Partition manager running on partition 0
=== PRTOS Virtio Backend Daemon ===
[Backend] All 5 Virtio devices initialized. Entering poll loop...

Welcome to Buildroot
buildroot login: root
Password: 1234
```

### Step 3: Access Guest Partition
```bash
# From another terminal:
telnet localhost 4321
# Login: root / 1234
```
Guest auto-starts `virtio_frontend` via `S99virtio_guest`. The frontend waits for the backend to initialize shared memory (polls magic values up to 300s), then creates `/dev/nbd0` (block) and `/dev/hvc0` (console).

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

## Testing

```bash
# Automated login test:
python3 test_login.py

# COM2/telnet test:
python3 test_com2.py

# Via the test framework:
cd ../../../../  # back to prtos-hypervisor root
bash scripts/run_test.sh --arch amd64 check-virtio_linux_demo_2p_amd64

# Full amd64 test suite:
bash scripts/run_test.sh --arch amd64 check-all
```

## File Structure

| File / Directory | Description |
|-----------------|-------------|
| `config/resident_sw.xml` | PRTOS system configuration (2 SMP partitions, 5 shared memory regions, 6 IPVIs, dual console) |
| `prtos_cf.amd64.xml` | Symlink → `config/resident_sw.xml` |
| `Makefile` | Build system (partitions, backend, manager, CPIO overlays, ISO, QEMU targets) |
| `start_system.S` | Boot stub for System Partition (UART debug, e820, bzImage+initrd) |
| `start_guest.S` | Boot stub for Guest Partition (VGA screen_info, e820, bzImage+initrd) |
| `hdr_system.c` | PRTOS image header (System, magic `0x24584d69`) |
| `hdr_guest.c` | PRTOS image header (Guest) |
| `linker_system.ld` | Linker script (System, base `0x6000000`, initrd at `0xA000000`) |
| `linker_guest.ld` | Linker script (Guest, base `0xE000000`, initrd at `0x12000000`) |
| `set_serial_poll.c` | Utility: `ioctl TIOCSSERIAL` to force `irq=0` polling mode for COM2 |
| `test_login.py` | Automated test: QEMU launch, COM1 login, `uname` check |
| `test_com2.py` | Automated test: COM2 telnet connection + Guest login prompt |
| **`system_partition/`** | |
| `  include/virtio_be.h` | Shared data structures: `net_shm` (64-slot ring), `blk_shm` (16-slot ring), `console_shm` (4KB ring) |
| `  src/main.c` | Backend daemon: mmap 5 `/dev/mem` regions, init all devices, 1ms poll loop |
| `  src/virtio_console.c` | Console backend: poll `tx_buf` ring → `putchar` |
| `  src/virtio_net.c` | Net backend: TAP for bridge, loopback for NAT/p2p |
| `  src/virtio_blk.c` | Block backend: file-backed (`pread`/`pwrite`) or 1MB RAM disk |
| `  src/doorbell.c` | IPVI signaling via hypercall (signal Guest via IPVI 5) |
| `  src/manager_if.c` | Manager wrapper: query Guest partition status |
| `  rootfs_overlay/etc/init.d/S99virtio_backend` | Init script: create `/dev/net/tun`, auto-start `prtos_manager` and `virtio_backend`, configure TAP IPs |
| **`lib_prtos_manager/`** | |
| `  include/prtos_hv.h` | Hypercall API: `vmcall` inline, 44 hypercall numbers, status structs |
| `  include/prtos_manager.h` | Manager device interface |
| `  common/prtos_hv.c` | Hypercall implementation: `/dev/mem` mmap mailbox, vmcall wrappers |
| `  common/prtos_manager.c` | Command dispatcher: help, list, partition ops, plan, write, quit |
| `  common/hypervisor.c` | Partition commands: list, halt, reset, resume, status, suspend |
| `  linux/prtos_manager_main.c` | Linux main: stdin/stdout, `-d` dry-run mode |
| **`guest_partition/`** | |
| `  src/virtio_frontend.c` | Userspace frontend daemon: maps shared memory via `/dev/mem`, creates NBD server for `/dev/nbd0` (block), PTY pair for `/dev/hvc0` (console), TUN/TAP devices for networking, polls for backend readiness |
| `  rootfs_overlay/etc/init.d/S99virtio_guest` | Init script: set COM2 polling, spawn `getty` on ttyS1, start `virtio_frontend`, configure TAP IPs, create `/dev/vda` symlink |
| `  rootfs_overlay/opt/virtio_test.sh` | Guest test script: network (3 ifaces), block, console, shmem check |

## Design Notes

- **1024MB QEMU RAM**: Total memory is 1GB. PRTOS EPT identity-maps the first 1GB via a single PDPT entry. All partition and shared memory addresses must be below `0x40000000`.
- **Dual console**: System Partition uses COM1/UART (stdio). Guest Partition uses VGA (VNC) + COM2 (telnet). COM2 operates in polling mode (`irq=0`) because PRTOS does not route IRQ3 to the Guest.
- **HPET disabled**: Both kernels use `nokaslr noapic nolapic` to avoid hardware timer and interrupt controller sharing issues between partitions.
- **Quiet System boot**: System kernel cmdline includes `quiet loglevel=0` to suppress boot messages for clean login experience.
- **PCI legacy mode**: Demo targets add QEMU PCI devices with `disable-modern=on,vectors=0` to force legacy INTx. Both MSI-X (`vectors=0`) and modern virtio (`disable-modern=on`) are disabled because PRTOS does not support MSI-X routing to L2 partitions.
- **Guest Virtio Frontend**: Standard `virtio-mmio` kernel drivers cannot be used because (1) the kernel's MMIO cmdline parser rejects `irq=0`, and (2) PRTOS VMX run loop does not inject IPVI doorbells as external interrupts. Instead, a userspace daemon (`virtio_frontend`) bridges shared memory to standard Linux devices via NBD (block), PTY (console), and TUN/TAP (network) using polling.
- **Networking**: Each virtio-net instance uses a pair of TUN/TAP devices (one on System, one on Guest) bridged through the shared memory packet ring. The backend creates `/dev/net/tun` via `mknod` and opens TAP devices for all 3 instances. IP addresses are assigned by init scripts.
- **Static linking**: `virtio_backend`, `virtio_frontend`, and `prtos_manager` are all statically linked for portability inside the partition rootfs.
- **Auto-start**: Both partitions use Buildroot init scripts (`S99virtio_backend`, `S99virtio_guest`) to automatically start all services at boot. No manual intervention is required.

## Dependencies

- **Linux kernel 6.19.9** with `CONFIG_BLK_DEV_NBD=y`, `CONFIG_TUN=y`, and embedded initramfs (see [Prerequisites](#prerequisites-linux-kernel--buildroot))
- **Buildroot** rootfs with NBD client, root password `1234`, CPIO format output
- **PRTOS Hypervisor** built for amd64 (`cp prtos_config.amd64 prtos_config && make defconfig && make`)
- **QEMU** with KVM support (`qemu-system-x86_64`)
- **Host** with Intel VT-x (VMX) support and `/dev/kvm` access

## Linux Kernel Command Lines

**System Partition** (`start_system.S`):
```
console=ttyS0,115200 quiet loglevel=0 nokaslr noapic nolapic prtos_role=system prtos_shmem_base=0x16000000 prtos_shmem_size=0x540000
```

**Guest Partition** (`start_guest.S`):
```
console=tty0 nokaslr noapic nolapic prtos_role=guest prtos_shmem_base=0x16000000 prtos_shmem_size=0x540000
```
