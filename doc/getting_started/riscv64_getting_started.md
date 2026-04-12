# RISC-V 64 Platform — Getting Started

## Prerequisites

- **OS**: Ubuntu 24.04 LTS (recommended)
- **Toolchain**: `riscv64-linux-gnu-` cross-compiler

## Dependency Installation

```bash
# Common dependencies
sudo apt-get update
sudo apt-get install -y build-essential make perl gawk git bc bison flex cpio \
    unzip rsync wget xorriso mtools python3-dev python3-libxml2 \
    libncurses5-dev libssl-dev libxml2-dev libxml2-utils gdb-multiarch

# RISC-V64 specific: cross-compiler and QEMU
sudo apt-get install -y gcc-riscv64-linux-gnu qemu-system-misc
```

## Virtualization Mode

The RISC-V 64 platform supports **both hardware virtualization and para-virtualization**:

- **Hardware Virtualization**: Leverages the RISC-V H-extension (Hypervisor Extension), G-stage page tables (`hgatp`), VS-mode (Virtual Supervisor), and SBI (Supervisor Binary Interface). Runs unmodified guest OS kernels (e.g., Linux 6.19, FreeRTOS).
- **Para-virtualization**: Guest partitions use PRTOS Hypercall API for resource management. Requires PRTOS-aware guest modifications.

Set `hw_virt="true"` in the XML configuration to enable hardware virtualization mode.

## Build and Run

### Build PRTOS Hypervisor

```bash
git clone https://github.com/prtos-project/prtos-hypervisor.git
cd prtos-hypervisor
cp prtos_config.riscv64 prtos_config
make defconfig
make
```

### Run Hello World Example

```bash
cd user/bail/examples/helloworld
make run.riscv64
```

Expected output:
```
P0 ("Partition0":0:1) flags: [ SYSTEM ]:
    [0x86000000:0x86000000 - 0x860fffff:0x860fffff] flags: 0x0
[0] Hello World!
[0] Verification Passed
[HYPERCALL] (0x0) Halted
```

### Run SMP Hello World

```bash
cd user/bail/examples/helloworld_smp
make run.riscv64
```

### Run All Tests

```bash
cd prtos-hypervisor
bash scripts/run_test.sh --arch riscv64 check-all
```

Expected: 16 Pass, 0 Fail, 11 Skip.

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
| `freertos_para_virt_riscv` | FreeRTOS with para-virtualization |
| `freertos_hw_virt_riscv` | Native FreeRTOS with hardware virtualization |
| `linux_4vcpu_1partion_riscv64` | Linux guest with 4 vCPUs |
| `mix_os_demo_riscv64` | Mixed-OS: Linux + FreeRTOS running simultaneously |
| `virtio_linux_demo_2p_riscv64` | VirtIO networking between two Linux partitions |

## QEMU Run Command Reference

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

| Option | Description |
|---|---|
| `-machine virt` | RISC-V virt machine |
| `-cpu rv64` | RV64GC CPU model |
| `-smp 4` | 4 CPU cores |
| `-m 1G` | 1GB RAM |
| `-bios default` | Default OpenSBI firmware |
| `-kernel` | Load PRTOS binary image |
| `-nographic` | No graphical window, serial to terminal |
| `-serial stdio` | UART output to terminal |

## Boot Process

1. OpenSBI firmware starts in M-mode (Machine mode)
2. OpenSBI hands off to PRTOS kernel loaded at the kernel address
3. PRTOS initializes G-stage page tables (`hgatp`) and virtual timer
4. PRTOS boots partitions according to XML-defined scheduling plan
5. Guests run in VS-mode (Virtual Supervisor) under hypervisor control

## Memory Layout

RISC-V uses a high RAM base address:

| Component | Address Range |
|---|---|
| RAM Base | `0x80000000` |
| RAM Size | 512MB |
| PRTOS Hypervisor | 32MB reserved |
| Partition 0 | `0x86000000` (1MB) |

## XML Configuration Example

```xml
<SystemDescription xmlns="http://www.prtos.org/prtos-1.x" version="1.0.0" name="helloworld">
    <HwDescription>
        <MemoryLayout>
            <Region type="ram" start="0x80000000" size="512MB" />
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
                <Area start="0x86000000" size="1MB" />
            </PhysicalMemoryAreas>
            <TemporalRequirements period="2ms" duration="1ms" />
        </Partition>
    </PartitionTable>
</SystemDescription>
```

## Next Steps

- Read the [RISC-V64 API Reference](riscv64_api_reference.md) for Hypercall API details
- Explore the [PRTOS Technical Report](../design/prtos_tech_report_en.md)
- Study the architecture internals in [*Embedded Hypervisor: Architecture, Principles, and Implementation*](http://www.prtos.org/embedded_hypervisor_book/)
