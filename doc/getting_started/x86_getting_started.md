# x86 (32-bit) Platform — Getting Started

## Prerequisites

- **OS**: Ubuntu 24.04 LTS (recommended)
- **Toolchain**: System GCC with 32-bit multilib support

## Dependency Installation

```bash
# Common dependencies
sudo apt-get update
sudo apt-get install -y build-essential make perl gawk git bc bison flex cpio \
    unzip rsync wget xorriso mtools python3-dev python3-libxml2 \
    libncurses5-dev libssl-dev libxml2-dev libxml2-utils gdb-multiarch

# x86-specific: 32-bit cross-compilation and QEMU
sudo apt-get install -y gcc-multilib g++-multilib qemu-system-i386 grub-pc-bin
```

## Virtualization Mode

The x86 (i386) platform uses **para-virtualization**. Guest partitions interact with the hypervisor via Hypercalls for resource management, I/O operations, and scheduling control. This mode requires guest OS modifications to use PRTOS-specific APIs.

## Build and Run

### Build PRTOS Hypervisor

```bash
git clone https://github.com/prtos-project/prtos-hypervisor.git
cd prtos-hypervisor
cp prtos_config.x86 prtos_config
make defconfig
make
```

### Run Hello World Example

```bash
cd user/bail/examples/helloworld
make run.x86
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
make run.x86
```

### Run All Tests

```bash
cd prtos-hypervisor
bash scripts/run_test.sh --arch x86 check-all
```

Expected: 11 Pass, 0 Fail, 16 Skip.

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

## QEMU Run Command Reference

```bash
qemu-system-i386 -m 512 -cdrom resident_sw.iso -serial stdio -boot d -smp 4
```

| Option        | Description |
|---------------|-------------|
| `-m 512`      | 512MB RAM |
| `-cdrom`      | Boot from ISO image |
| `-serial stdio` | UART output to terminal |
| `-boot d`     | Boot from CD-ROM |
| `-smp 4`      | 4 CPU cores |

## XML Configuration Example

```xml
<SystemDescription xmlns="http://www.prtos.org/prtos-1.x" version="1.0.0" name="helloworld">
    <HwDescription>
        <MemoryLayout>
            <Region type="ram" start="0x0" size="512MB" />
        </MemoryLayout>
        <ProcessorTable>
            <Processor id="0" frequency="200MHz">
                <CyclicPlanTable>
                    <Plan id="0" majorFrame="200ms">
                        <Slot id="0" start="0ms" duration="200ms" partitionId="0" />
                    </Plan>
                </CyclicPlanTable>
            </Processor>
        </ProcessorTable>
    </HwDescription>
    <PRTOSHypervisor console="Uart">
        <PhysicalMemoryArea size="8MB" />
    </PRTOSHypervisor>
    <PartitionTable>
        <Partition id="0" name="Partition0" flags="system">
            <PhysicalMemoryAreas>
                <Area start="0x6000000" size="1MB" />
            </PhysicalMemoryAreas>
            <TemporalRequirements period="200ms" duration="200ms" />
        </Partition>
    </PartitionTable>
</SystemDescription>
```

## Next Steps

- Read the [x86 API Reference](x86_api_reference.md) for Hypercall API details
- Explore the [PRTOS Technical Report](../design/prtos_tech_report_en.md)
- Study the architecture internals in [*Embedded Hypervisor: Architecture, Principles, and Implementation*](http://www.prtos.org/embedded_hypervisor_book/)
