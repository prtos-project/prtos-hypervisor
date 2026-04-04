# Virtio Linux Demo - 2 Partitions (amd64)

## Overview

This demo demonstrates **Virtio device virtualization** on the PRTOS Type-1 Hypervisor using two Linux partitions communicating through shared memory on the amd64 (x86_64) platform with hardware-assisted virtualization (VMX).

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  L0: Host Linux + QEMU (x86_64, KVM, 2 pCPUs, 512MB RAM)  │
├─────────────────────────────────────────────────────────────┤
│  L1: PRTOS Type-1 Hypervisor (12MB @ 0x1000000)            │
│  ┌─────────────────────┐  ┌─────────────────────┐          │
│  │ Partition 0 (System) │  │ Partition 1 (Guest)  │          │
│  │ Linux + Backend      │  │ Linux + Frontend     │          │
│  │ 1 vCPU (pCPU 0)     │  │ 1 vCPU (pCPU 1)     │          │
│  │ 128MB @ 0x6000000   │  │ 128MB @ 0xe000000   │          │
│  │ console=ttyS0       │  │ (no serial console)  │          │
│  │                     │  │                     │          │
│  │  Virtio Backend:    │  │  Virtio Frontend:   │          │
│  │  - Console handler  │  │  - /dev/hvc0        │          │
│  │  - Net loopback     │  │  - virtio-net       │          │
│  │  - Blk RAM disk     │  │  - /dev/vda         │          │
│  └─────────┬───────────┘  └─────────┬───────────┘          │
│            │     Shared Memory       │                      │
│            │  ┌──────────────────┐   │                      │
│            └──┤ 8MB @ 0x16000000 ├───┘                      │
│               │ Virtio Rings     │                          │
│               │ + Data Buffers   │                          │
│               └──────────────────┘                          │
│                                                             │
│  IPVI0: Guest→System (doorbell)                             │
│  IPVI1: System→Guest (doorbell)                             │
└─────────────────────────────────────────────────────────────┘
```

## Memory Layout

| Region              | GPA Start    | Size  | End          |
|---------------------|-------------|-------|-------------|
| PRTOS Hypervisor    | 0x1000000   | 12MB  | 0x1bfffff   |
| System Partition    | 0x6000000   | 128MB | 0xdffffff   |
| Guest Partition     | 0xe000000   | 128MB | 0x15ffffff  |
| Shared Memory       | 0x16000000  | 8MB   | 0x167fffff  |
| **Total**           |             |~360MB | (< 512MB)   |

**Note**: Shared memory is NOT advertised in the e820 memory map to avoid kernel
sparse memory bugs with non-contiguous regions. Access shared memory via
`/dev/mem` mmap from userspace.

## Shared Memory Layout (8MB at GPA 0x16000000)

| Offset     | Size  | Description                    |
|------------|-------|--------------------------------|
| 0x000000   | 4KB   | Control block (status, features)|
| 0x001000   | 64KB  | Virtio-Console ring buffers    |
| 0x011000   | 1MB   | Virtio-Net ring + packet slots |
| 0x111000   | 1MB   | Virtio-Blk ring + request slots|
| 0x211000   | ~6MB  | Reserved                       |

## Virtio Devices

### Virtio-Console
- **Mechanism**: Character ring buffer in shared memory
- **Data flow**: Guest writes to `tx_buf` → Backend reads and prints to stdout
- **Verify**: `echo "Hello PRTOS" > /dev/hvc0` (from Guest)

### Virtio-Net
- **Mechanism**: Packet slot ring buffer (64 slots, up to 1536 bytes each)
- **Data flow**: Guest sends Ethernet frame → Backend receives and echoes (loopback)
- **Verify**: Run virtio_guest_test, check loopback response

### Virtio-Blk
- **Mechanism**: Block request ring (16 slots, sector-addressed)
- **Backend**: 1MB in-memory RAM disk
- **Data flow**: Guest sends read/write requests → Backend processes via memcpy
- **Verify**: Write to sector 0, read back, verify data integrity

## Building

```bash
# Ensure PRTOS is built for amd64 first:
cd prtos-hypervisor
cp prtos_config.amd64 prtos_config
make defconfig
make

# Build the demo:
cd user/bail/examples/virtio_linux_demo_2p_amd64
make
```

## Running

```bash
# Run with KVM (requires /dev/kvm access):
make run.amd64.nographic

# Or manually:
qemu-system-x86_64 -enable-kvm -cpu host,-waitpkg \
    -nographic -m 512 -smp 2 \
    -cdrom resident_sw.iso \
    -serial mon:stdio -boot d
```

## Testing

```bash
# Automated test:
python3 test_login.py

# Or via the test framework:
bash scripts/run_test.sh --arch amd64 check-virtio_linux_demo_2p_amd64
```

## Files

| File                | Description                                |
|---------------------|--------------------------------------------|
| `config/resident_sw.xml` | PRTOS system configuration (2 partitions) |
| `prtos_cf.amd64.xml`| Symlink to config/resident_sw.xml          |
| `start_system.S`   | Boot stub for System Partition             |
| `start_guest.S`    | Boot stub for Guest Partition (silent)     |
| `hdr_system.c`     | PRTOS image header (System)                |
| `hdr_guest.c`      | PRTOS image header (Guest)                 |
| `linker_system.ld` | Linker script (System, base 0x6000000)     |
| `linker_guest.ld`  | Linker script (Guest, base 0xe000000)      |
| `system_partition/src/main.c` | Backend daemon entry point        |
| `system_partition/src/virtio_console.c` | Console backend logic  |
| `system_partition/src/virtio_net.c` | Network backend logic       |
| `system_partition/src/virtio_blk.c` | Block device backend logic  |
| `system_partition/include/virtio_be.h` | Shared Virtio data structures |
| `guest_partition/rootfs_overlay/` | Guest rootfs overlay scripts   |
| `Makefile`          | Build system                               |
| `test_login.py`    | Automated test script                      |

## Inter-Partition Communication

The demo uses two mechanisms from PRTOS:

1. **Shared Memory**: An 8MB region at GPA 0x16000000 EPT-mapped into both
   partitions via `flags="shared"` in the XML config. Accessed from Linux
   userspace via `/dev/mem` mmap (not in e820 to avoid kernel memory bugs).

2. **IPVI (Inter-Partition Virtual Interrupts)**: Used as doorbells:
   - IPVI0: Guest → System (Guest signals new Virtio buffers available)
   - IPVI1: System → Guest (Backend signals completed requests)

## Design Notes

- **512MB RAM limit**: PRTOS amd64 hypervisor hangs during page table init with
  >512MB RAM. All partitions must fit within 512MB total.
- **Single serial console**: Only the System partition outputs to ttyS0. The Guest
  partition runs silently to avoid interleaved serial output corruption.
- **HPET disabled**: Both kernels use `hpet=disable clocksource=tsc tsc=reliable`
  to avoid HPET timer sharing issues between partitions.
- **Scheduler**: 100ms major frame, 50ms slots. Each partition gets 50% CPU time.

## Dependencies

- Linux kernel 6.19.9 with CONFIG_VIRTIO enabled (built via buildroot)
- PRTOS Hypervisor built for amd64
- QEMU with KVM support
- Host with VT-x (Intel VMX) support
