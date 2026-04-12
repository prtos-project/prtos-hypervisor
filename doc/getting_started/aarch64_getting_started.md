# AArch64 (ARMv8) Platform — Getting Started

## Prerequisites

- **OS**: Ubuntu 24.04 LTS (recommended)
- **Toolchain**: `aarch64-linux-gnu-` cross-compiler

## Dependency Installation

```bash
# Common dependencies
sudo apt-get update
sudo apt-get install -y build-essential make perl gawk git bc bison flex cpio \
    unzip rsync wget xorriso mtools python3-dev python3-libxml2 \
    libncurses5-dev libssl-dev libxml2-dev libxml2-utils gdb-multiarch

# AArch64-specific: cross-compiler, U-Boot tools, and QEMU
sudo apt-get install -y gcc-aarch64-linux-gnu qemu-system-arm u-boot-tools
```

## Virtualization Mode

The AArch64 platform supports **both hardware virtualization and para-virtualization**:

- **Hardware Virtualization**: Leverages ARMv8 VHE (Virtualization Host Extensions), EL2 exception level, Stage-2 page tables, and GICv3 virtual interrupt controller. Runs unmodified guest OS kernels (e.g., Linux 6.19, FreeRTOS).
- **Para-virtualization**: Guest partitions use PRTOS Hypercall API for resource management. Requires PRTOS-aware guest modifications.

Set `hw_virt="true"` in the XML configuration to enable hardware virtualization mode.

## Build and Run

### Build PRTOS Hypervisor

```bash
git clone https://github.com/prtos-project/prtos-hypervisor.git
cd prtos-hypervisor
cp prtos_config.aarch64 prtos_config
make defconfig
make
```

### Run Hello World Example

```bash
cd user/bail/examples/helloworld
make run.aarch64
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
make run.aarch64
```

### Run All Tests

```bash
cd prtos-hypervisor
bash scripts/run_test.sh --arch aarch64 check-all
```

Expected: 17 Pass, 0 Fail, 10 Skip.

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
| `freertos_para_virt_aarch64` | FreeRTOS with para-virtualization |
| `freertos_hw_virt_aarch64` | Native FreeRTOS with hardware virtualization |
| `linux_aarch64` | Linux guest (single vCPU, hardware virtualization) |
| `linux_4vcpu_1partion_aarch64` | Linux guest with 4 vCPUs |
| `mix_os_demo_aarch64` | Mixed-OS: Linux + FreeRTOS running simultaneously |
| `virtio_linux_demo_2p_aarch64` | VirtIO networking between two Linux partitions |

## QEMU Run Command Reference

```bash
qemu-system-aarch64 \
    -machine virt,gic_version=3 \
    -machine virtualization=true \
    -cpu cortex-a72 \
    -machine type=virt \
    -m 4096 \
    -smp 4 \
    -bios ./u-boot/u-boot.bin \
    -device loader,file=./resident_sw_image,addr=0x40200000,force-raw=on \
    -nographic -no-reboot \
    -chardev socket,id=qemu-monitor,host=localhost,port=8889,server=on,wait=off,telnet=on \
    -mon qemu-monitor,mode=readline
```

| Option | Description |
|---|---|
| `-machine virt,gic_version=3` | ARM virt machine with GICv3 |
| `-machine virtualization=true` | Enable EL2 virtualization extensions |
| `-cpu cortex-a72` | Cortex-A72 CPU model |
| `-m 4096` | 4GB RAM |
| `-smp 4` | 4 CPU cores |
| `-bios ./u-boot/u-boot.bin` | U-Boot bootloader |
| `-device loader` | Load PRTOS image at 0x40200000 |
| `-nographic` | No graphical window, serial to terminal |

## Boot Process

1. U-Boot starts at EL2 (ARM Exception Level 2)
2. U-Boot loads PRTOS image from the device loader
3. PRTOS initializes Stage-2 page tables and GICv3 virtual interrupt controller
4. PRTOS boots partitions according to XML-defined scheduling plan

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

- Read the [AArch64 API Reference](aarch64_api_reference.md) for Hypercall API details
- Explore the [PRTOS Technical Report](../design/prtos_tech_report_en.md)
- Study the architecture internals in [*Embedded Hypervisor: Architecture, Principles, and Implementation*](http://www.prtos.org/embedded_hypervisor_book/)
