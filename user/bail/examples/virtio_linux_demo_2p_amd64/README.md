# Virtio Linux Demo - 2 SMP Partitions (amd64)

## Overview

This demo demonstrates **Virtio device virtualization** on the PRTOS Type-1 Hypervisor using two SMP Linux partitions communicating through shared memory on the amd64 (x86_64) platform with hardware-assisted virtualization (Intel VT-x / VMX).

The **System Partition** owns all hardware resources (PCI, legacy I/O, IRQs) and runs virtio backend daemons that serve virtualized devices to the **Guest Partition** via shared memory regions. Both partitions run full Linux (kernel 6.19.9) with dual-console support: UART for System, VGA+telnet for Guest.

## Architecture

```
┌──────────────────────────────────────────────────────────────┐
│  L0: Host Linux + QEMU (x86_64, KVM, 4 pCPUs, 1024MB RAM)  │
├──────────────────────────────────────────────────────────────┤
│  L1: PRTOS Type-1 Hypervisor (12MB @ 0x1000000)             │
│  ┌──────────────────────┐  ┌──────────────────────┐         │
│  │ Partition 0 (System)  │  │ Partition 1 (Guest)   │         │
│  │ Linux + Virtio Backend│  │ Linux + Virtio Frontend│         │
│  │ 2 vCPU (pCPU 0-1)    │  │ 2 vCPU (pCPU 2-3)    │         │
│  │ 128MB @ 0x6000000    │  │ 128MB @ 0xE000000    │         │
│  │ console=ttyS0 (UART) │  │ console=tty0 (VGA)   │         │
│  │                      │  │ + ttyS1 (COM2/telnet) │         │
│  │ Services:            │  │                      │         │
│  │ - prtos_manager      │  │ Virtio Frontend:     │         │
│  │ - virtio_backend     │  │ - 3x virtio-net      │         │
│  │   - Console backend  │  │ - virtio-blk         │         │
│  │   - 3x Net backend   │  │ - virtio-console     │         │
│  │   - Blk backend      │  │ - /opt/virtio_test.sh│         │
│  └──────────┬───────────┘  └──────────┬───────────┘         │
│             │     Shared Memory        │                     │
│             │  ┌───────────────────┐   │                     │
│             └──┤ ~5.25MB @ 0x16000000 ├┘                     │
│                │ 5 Virtio Regions   │                        │
│                └───────────────────┘                         │
│                                                              │
│  IPVI 0-4: Guest→System (per-device doorbell)                │
│  IPVI 5:   System→Guest (completion doorbell)                │
└──────────────────────────────────────────────────────────────┘
```

## Memory Layout

| Region              | GPA Start    | Size   | GPA End      |
|---------------------|-------------|--------|-------------|
| PRTOS Hypervisor    | 0x01000000  | 12MB   | 0x01BFFFFF  |
| System Partition    | 0x06000000  | 128MB  | 0x0DFFFFFF  |
| Guest Partition     | 0x0E000000  | 128MB  | 0x15FFFFFF  |
| Shared Memory       | 0x16000000  | ~5.25MB| 0x1653FFFF  |
| **QEMU RAM Total**  |             | 1024MB |             |

All addresses are within the first 1GB, required by PRTOS EPT identity-map (single PDPT entry covering 0x0–0x3FFFFFFF).

## Shared Memory Layout (5 Regions at GPA 0x16000000+)

| Region       | GPA Start    | Size   | Description                     |
|-------------|-------------|--------|---------------------------------|
| Virtio_Net0 | 0x16000000  | 1MB    | virtio-net bridge (TAP backend) |
| Virtio_Net1 | 0x16100000  | 1MB    | virtio-net NAT (loopback)       |
| Virtio_Net2 | 0x16200000  | 1MB    | virtio-net p2p (loopback)       |
| Virtio_Blk  | 0x16300000  | 2MB    | virtio-blk (file or RAM disk)   |
| Virtio_Con  | 0x16500000  | 256KB  | virtio-console (char ring)      |

Each region uses `flags="shared"` in the XML config and is EPT-mapped into both partitions. Shared memory is NOT in the e820 memory map — accessed via `/dev/mem` mmap from Linux userspace to avoid kernel sparse-memory bugs.

## CPU Assignment (SMP)

| Physical CPU | Partition          | Virtual CPU |
|-------------|-------------------|-------------|
| pCPU 0      | System (P0)       | vCPU 0      |
| pCPU 1      | System (P0)       | vCPU 1      |
| pCPU 2      | Guest  (P1)       | vCPU 0      |
| pCPU 3      | Guest  (P1)       | vCPU 1      |

Scheduler: 10ms major frame, dedicated pCPU mapping (each pCPU runs one vCPU full-time).

## Console Assignment

| Partition | Console   | QEMU Device                        | Access Method            |
|-----------|----------|------------------------------------|--------------------------|
| System    | UART     | `-serial mon:stdio`                | Terminal (SSH)            |
| Guest     | VGA      | `-vga std -vnc :1`                 | VNC `localhost:5901`      |
| Guest     | COM2     | `-serial telnet::4321,server,nowait`| `telnet localhost 4321`  |

The Guest partition uses `set_serial_poll` (ioctl `TIOCSSERIAL` with `irq=0`) to enable polling mode for COM2/ttyS1, since PRTOS does not route IRQ3 to the Guest. An init script (`S99virtio_guest`) spawns `getty` on ttyS1 automatically.

## Virtio Devices

### Virtio-Console
- **Mechanism**: 4KB character ring buffer in shared memory (`Virtio_Con`)
- **Data flow**: Guest writes to `tx_buf` → Backend reads and prints to stdout
- **Verify**: `echo "Hello PRTOS" > /dev/hvc0` (from Guest)

### Virtio-Net (×3)
- **Mechanism**: 64-slot packet ring buffer (up to 1536 bytes/slot) per instance
- **Net0 (bridge)**: Backend opens TAP device (`/dev/net/tun`), forwards packets
- **Net1 (NAT)**: Backend uses loopback echo (TX→RX copy)
- **Net2 (p2p)**: Backend uses loopback echo (TX→RX copy)
- **Verify**: Run `virtio_test.sh` from Guest

### Virtio-Blk
- **Mechanism**: 16-slot block request ring (sector-addressed, 512B sectors)
- **Backend**: 64MB file-backed disk (`disk.img`) or 1MB in-memory RAM disk
- **Operations**: IN (read), OUT (write), FLUSH, GET_ID
- **Verify**: `fdisk -l /dev/vda` or `dd` from Guest

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

## Building

```bash
# 1. Build PRTOS hypervisor for amd64:
cd prtos-hypervisor
cp prtos_config.amd64 prtos_config
make defconfig
make

# 2. Build the demo:
cd user/bail/examples/virtio_linux_demo_2p_amd64
make
```

Build artifacts:
- `resident_sw.iso` — Bootable ISO (GRUB + PRTOS + both partitions)
- `virtio_backend` — Static binary for System Linux userspace
- `prtos_manager` — Static binary for partition management CLI
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
Adds QEMU PCI devices (virtio-net-pci ×3 + virtio-blk) for the System Partition to use as real hardware backends.

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

### Step 1: Boot System Partition
```
Welcome to Buildroot
buildroot login: root
Password: 1234
```

### Step 2: Start Virtio Services (System)
```bash
prtos_manager &       # Partition management daemon
virtio_backend &      # Virtio device backend (maps shared memory, polls rings)
```

### Step 3: Access Guest Partition
```bash
# From another terminal:
telnet localhost 4321
# Login: root / 1234
```

### Step 4: Test Virtio Devices (Guest)
```bash
/opt/virtio_test.sh   # Automated test for all virtio devices
```

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
| **`lib_prtos_manager/`** | |
| `  include/prtos_hv.h` | Hypercall API: `vmcall` inline, 44 hypercall numbers, status structs |
| `  include/prtos_manager.h` | Manager device interface |
| `  common/prtos_hv.c` | Hypercall implementation: `/dev/mem` mmap mailbox, vmcall wrappers |
| `  common/prtos_manager.c` | Command dispatcher: help, list, partition ops, plan, write, quit |
| `  common/hypervisor.c` | Partition commands: list, halt, reset, resume, status, suspend |
| `  linux/prtos_manager_main.c` | Linux main: stdin/stdout, `-d` dry-run mode |
| **`guest_partition/`** | |
| `  rootfs_overlay/etc/init.d/S99virtio_guest` | Init script: set COM2 polling, spawn `getty` on ttyS1 |
| `  rootfs_overlay/opt/virtio_test.sh` | Guest test script: network (3 ifaces), block, console, shmem check |

## Design Notes

- **1024MB QEMU RAM**: Total memory is 1GB. PRTOS EPT identity-maps the first 1GB via a single PDPT entry. All partition and shared memory addresses must be below `0x40000000`.
- **Dual console**: System Partition uses COM1/UART (stdio). Guest Partition uses VGA (VNC) + COM2 (telnet). COM2 operates in polling mode (`irq=0`) because PRTOS does not route IRQ3 to the Guest.
- **HPET disabled**: Both kernels use `nokaslr noapic nolapic` to avoid hardware timer and interrupt controller sharing issues between partitions.
- **Quiet System boot**: System kernel cmdline includes `quiet loglevel=0` to suppress boot messages for clean login experience.
- **PCI legacy mode**: Demo targets add QEMU PCI devices with `disable-modern=on` to force legacy INTx (MSI-X is not supported by PRTOS for L2 partitions).
- **Static linking**: Both `virtio_backend` and `prtos_manager` are statically linked for portability inside the partition rootfs.

## Dependencies

- Linux kernel 6.19.9 (built via Buildroot, shared bzImage)
- PRTOS Hypervisor built for amd64 (`cp prtos_config.amd64 prtos_config && make defconfig && make`)
- QEMU with KVM support (`qemu-system-x86_64`)
- Host with Intel VT-x (VMX) support and `/dev/kvm` access

## Linux Kernel Command Lines

**System Partition** (`start_system.S`):
```
console=ttyS0,115200 quiet loglevel=0 nokaslr noapic nolapic prtos_role=system prtos_shmem_base=0x16000000 prtos_shmem_size=0x540000
```

**Guest Partition** (`start_guest.S`):
```
console=tty0 nokaslr noapic nolapic prtos_role=guest prtos_shmem_base=0x16000000 prtos_shmem_size=0x540000
```
