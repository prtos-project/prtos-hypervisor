# AMD64 (x86_64) Platform — Getting Started

## Prerequisites

- **OS**: Ubuntu 24.04 LTS (recommended)
- **Toolchain**: Native GCC (no cross-compiler needed)

## Dependency Installation

```bash
# Common dependencies
sudo apt-get update
sudo apt-get install -y build-essential make perl gawk git bc bison flex cpio \
    unzip rsync wget xorriso mtools python3-dev python3-libxml2 \
    libncurses5-dev libssl-dev libxml2-dev libxml2-utils gdb-multiarch

# AMD64-specific: QEMU and GRUB tools
sudo apt-get install -y qemu-system-x86 grub-pc-bin
```

## Virtualization Mode

The AMD64 platform supports **both hardware virtualization and para-virtualization**:

- **Hardware Virtualization**: Leverages Intel VT-x/VMX with Extended Page Tables (EPT) for memory isolation. Runs unmodified guest OS kernels (e.g., Linux 6.19, FreeRTOS).
- **Para-virtualization**: Guest partitions use PRTOS Hypercall API for resource management. Requires PRTOS-aware guest modifications.

Set `hw_virt="true"` in the XML configuration to enable hardware virtualization mode.

### EPT Constraint

AMD64 hardware virtualization identity-maps only the first 1GB of physical memory. All partition and shared memory addresses must be below `0x40000000`.

### Reserved Resources

The following resources are reserved by the PRTOS hypervisor on AMD64:

- **IRQs**: 2, 4, 24, 26, 27
- **I/O Ports**: 0x20-0x21, 0xA0-0xA1, 0x3F8-0x3FC

## Build and Run

### Build PRTOS Hypervisor

```bash
git clone https://github.com/prtos-project/prtos-hypervisor.git
cd prtos-hypervisor
cp prtos_config.amd64 prtos_config
make defconfig
make
```

### Run Hello World Example

```bash
cd user/bail/examples/helloworld
make run.amd64
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
make run.amd64
```

### Run All Tests

```bash
cd prtos-hypervisor
bash scripts/run_test.sh --arch amd64 check-all
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
| `freertos_para_virt_amd64` | FreeRTOS with para-virtualization |
| `freertos_hw_virt_amd64` | Native FreeRTOS with hardware virtualization |
| `linux_4vcpu_1partion_amd64` | Linux guest with 4 vCPUs |
| `mix_os_demo_amd64` | Mixed-OS: Linux + FreeRTOS running simultaneously |
| `virtio_linux_demo_2p_amd64` | VirtIO networking between two Linux partitions |

## QEMU Run Command Reference

```bash
qemu-system-x86_64 -m 1024 -cdrom resident_sw.iso -serial stdio -boot d -smp 4
```

| Option | Description |
|---|---|
| `-m 1024` | 1GB RAM |
| `-cdrom` | Boot from ISO image |
| `-serial stdio` | UART output to terminal |
| `-boot d` | Boot from CD-ROM |
| `-smp 4` | 4 CPU cores |

### KVM Acceleration (Optional)

For faster execution on Intel VT-x capable hosts:

```bash
qemu-system-x86_64 -enable-kvm -cpu host,-waitpkg -m 1024 -cdrom resident_sw.iso -serial stdio -boot d -smp 4
```

## Boot Process

1. GRUB bootloader loads PRTOS from ISO image
2. SeaBIOS/UEFI initializes hardware, provides ACPI and e820 memory map
3. PRTOS initializes VMX (Virtual Machine Extensions) and EPT (Extended Page Tables)
4. PRTOS boots partitions according to XML-defined scheduling plan
5. Hardware-virt guests run in VMX non-root mode; para-virt guests use hypercalls

## XML Configuration Example

```xml
<SystemDescription xmlns="http://www.prtos.org/prtos-1.x" version="1.0.0" name="helloworld">
    <HwDescription>
        <MemoryLayout>
            <Region type="ram" start="0x0" size="512MB" />
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
        <Devices>
            <Uart id="0" baudRate="115200" name="Uart" />
            <Vga name="Vga" />
        </Devices>
    </HwDescription>
    <PRTOSHypervisor console="Uart">
        <PhysicalMemoryArea size="8MB" />
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

- Read the [AMD64 API Reference](amd64_api_reference.md) for Hypercall API details
- Explore the [PRTOS Technical Report](../design/prtos_tech_report_en.md)
- Study the architecture internals in [*Embedded Hypervisor: Architecture, Principles, and Implementation*](http://www.prtos.org/embedded_hypervisor_book/)
