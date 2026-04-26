# LoongArch64 Platform — Getting Started

## Prerequisites

- **OS**: Ubuntu 24.04 LTS (recommended)
- **Toolchain**: `loongarch64-linux-gnu-` cross-compiler
- **Emulator**: `qemu-system-loongarch64` (11.0.0 or later, with the LoongArch `la464` CPU model)

## Dependency Installation

```bash
# Common dependencies
sudo apt-get update
sudo apt-get install -y build-essential make perl gawk git bc bison flex cpio \
    unzip rsync wget xorriso mtools python3-dev python3-libxml2 \
    libncurses5-dev libssl-dev libxml2-dev libxml2-utils gdb-multiarch

# LoongArch64 specific: cross-compiler and QEMU
sudo apt-get install -y gcc-loongarch64-linux-gnu qemu-system-loongarch64
```

## Virtualization Mode

LoongArch64 currently uses **trap-and-emulate para-virtualization** only.

- The guest Linux kernel runs at **PLV3** (the lowest privilege level on LoongArch).
- All privileged operations — CSR access, TLB writes, timer programming, IPI/EOI on the LIO interrupt controller — trap into PRTOS for emulation.
- PRTOS owns the exception entry (CSR `EENTRY`) and dispatches between host traps, guest traps from PLV3, and (when present) LVZ guest exits.
- No U-Boot is used. PRTOS is loaded directly through QEMU's `-kernel resident_sw` option.

The XML configuration does not need an `hw_virt` attribute on LoongArch64 — partitions are para-virtualized by default.

## Build and Run

### Build PRTOS Hypervisor

```bash
git clone https://github.com/prtos-project/prtos-hypervisor.git
cd prtos-hypervisor
cp prtos_config.loongarch64 prtos_config
make defconfig
make
```

### Run Hello World Example

```bash
cd user/bail/examples/helloworld
make run.loongarch64
```

Expected output:
```
P0 ("Partition0":0:1) flags: [ SYSTEM ]:
    [0x6000000:0x6000000 - 0x60fffff:0x60fffff] flags: 0x0
[0] Hello World!
[0] Verification Passed
[HYPERCALL] (0x0) Halted
```

### Run SMP Hello World

```bash
cd user/bail/examples/helloworld_smp
make run.loongarch64
```

### Run All Tests

```bash
cd prtos-hypervisor
bash scripts/run_test.sh --arch loongarch64 check-all
```

Expected: 16 Pass, 0 Fail (16 cases for other architectures are reported as SKIP).

## Available Examples

| Example | Description |
|---|---|
| `helloworld` | Basic single-partition "Hello World" |
| `helloworld_smp` | SMP dual-core, dual-partition verification |
| `example.001` | Timer management (HW_CLOCK vs EXEC_CLOCK) |
| `example.002` | Basic partition execution |
| `example.003` | Partition interaction |
| `example.004` | Inter-partition communication (sampling/queuing ports, shared memory) |
| `example.005` | IPC ports |
| `example.006` | Scheduling plan switch |
| `example.007` | Health monitoring |
| `example.008` | Tracing service |
| `example.009` | Memory protection and shared memory access |
| `freertos_para_virt_loongarch64` | FreeRTOS with para-virtualization |
| `freertos_hw_virt_loongarch64` | FreeRTOS variant exercising the LVZ shim |
| `linux_4vcpu_1partion_loongarch64` | Linux guest with 4 vCPUs (para-virtualization) |
| `mix_os_demo_loongarch64` | Mixed-OS: Linux + FreeRTOS running simultaneously |
| `virtio_linux_demo_2p_loongarch64` | VirtIO networking between two Linux partitions |

## Building the Linux Guest

The Linux example partition (`linux_4vcpu_1partion_loongarch64`) requires a pre-built `vmlinux` ELF and an embedded Buildroot rootfs. Detailed steps:

### 1. Build Buildroot rootfs

```bash
cd /path/to/buildroot
make qemu_loongarch64_virt_efi_defconfig
make menuconfig          # optional adjustments
make -j$(nproc)
# Produces output/images/rootfs.cpio
```

### 2. Build the Linux kernel

```bash
cd /path/to/linux-6.19.9
make ARCH=loongarch CROSS_COMPILE=loongarch64-linux-gnu- loongson3_defconfig
make ARCH=loongarch CROSS_COMPILE=loongarch64-linux-gnu- menuconfig
# Set:
#   General setup -> Initramfs source file(s):
#     /path/to/buildroot/output/images/rootfs.cpio
#   Boot options -> Built-in kernel command string: console=ttyS0,115200 earlycon

make ARCH=loongarch CROSS_COMPILE=loongarch64-linux-gnu- vmlinux -j$(nproc)
```

### 3. Stage `vmlinux` into the example and run

```bash
cp /path/to/linux-6.19.9/vmlinux \
   user/bail/examples/linux_4vcpu_1partion_loongarch64/vmlinux

cd user/bail/examples/linux_4vcpu_1partion_loongarch64
make clean && make
make run.loongarch64
```

Expected:
```
Welcome to Buildroot
(none) login:
```

## QEMU Run Command Reference

```bash
qemu-system-loongarch64 \
    -machine virt \
    -cpu la464 \
    -smp 4 \
    -m 4G \
    -accel tcg,thread=multi \
    -nographic -no-reboot \
    -kernel resident_sw \
    -monitor none \
    -serial stdio
```

| Option | Description |
|---|---|
| `-machine virt` | LoongArch generic `virt` machine |
| `-cpu la464` | Loongson 3A5000-class CPU model |
| `-smp 4` | 4 logical CPUs |
| `-m 4G` | 4 GB physical memory |
| `-accel tcg,thread=multi` | Multi-threaded TCG acceleration |
| `-kernel resident_sw` | Load PRTOS RSW directly (no U-Boot) |
| `-nographic` | Serial console multiplexed onto stdio |

## Boot Process

1. QEMU drops execution into the RSW stub (`user/bootloaders/rsw/loongarch64/`) at the LoongArch reset vector in DA (Direct Address) mode.
2. RSW parses the PRTOS container image and loads:
   - the PRTOS hypervisor core,
   - all configured partition images (kernel, BAIL programs, etc.).
3. RSW transfers control to the hypervisor entry, which sets up CSRs, TLB refill handler, and per-CPU state.
4. PRTOS schedules partitions according to the cyclic plan in the XML configuration.
5. Each partition starts running in PLV3; privileged operations trap into the hypervisor for emulation.

## XML Configuration Example

```xml
<SystemDescription xmlns="http://www.prtos.org/prtos-1.x" version="1.0.0" name="helloworld">
    <HwDescription>
        <MemoryLayout>
            <Region type="ram" start="0x0" size="2048MB" />
        </MemoryLayout>
        <ProcessorTable>
            <Processor id="0">
                <CyclicPlanTable>
                    <Plan id="0" majorFrame="2ms">
                        <Slot id="0" start="0ms" duration="1ms" partitionId="0" />
                    </Plan>
                </CyclicPlanTable>
            </Processor>
        </ProcessorTable>
    </HwDescription>
    <PRTOSHypervisor console="Uart">
        <PhysicalMemoryArea size="32MB" />
    </PRTOSHypervisor>
    <PartitionTable>
        <Partition id="0" name="Partition0" flags="system" console="Uart">
            <PhysicalMemoryAreas>
                <Area start="0x6000000" size="1MB" />
            </PhysicalMemoryAreas>
            <TemporalRequirements period="2ms" duration="1ms" />
        </Partition>
    </PartitionTable>
</SystemDescription>
```

## Next Steps

- Read the [LoongArch64 API Reference](loongarch64_api_reference.md) for Hypercall API details
- Explore the [PRTOS Technical Report](../design/prtos_tech_report.md)
- Study the architecture internals in [*Embedded Hypervisor: Architecture, Principles, and Implementation*](http://www.prtos.org/embedded_hypervisor_book/)
