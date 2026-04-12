# PRTOS Hypervisor Technical Report
**English** | [中文](prtos_tech_report_zh.md)
## 1. Project Overview

**PRTOS Hypervisor** is an open-source, lightweight embedded Type-1 (bare-metal) Hypervisor built on a **Separation Kernel** architecture, specifically designed for real-time and safety-critical systems. Through strict spatial and temporal partitioning, PRTOS enables multiple applications to coexist securely and collaborate efficiently on a single hardware platform, completely eliminating mutual interference between applications.

The core design principle of PRTOS is **determinism and static configuration**: critical resources such as CPU, memory, and I/O devices are statically allocated at system instantiation time, and scheduling follows a predefined Cyclic Scheduling Table, making system behavior fully predictable, analyzable, and verifiable. For the theoretical foundations and engineering implementation of this design principle, refer to Chapter 11.2 of *[Embedded Hypervisor: Architecture, Principles, and Implementation](http://www.prtos.org/embedded_hypervisor_book/)*.

PRTOS follows the open-source spirit, drawing technical inspiration from [XtratuM](https://en.wikipedia.org/wiki/XtratuM), [Xen Hypervisor](https://xenproject.org/), [Lguest Hypervisor](http://lguest.ozlabs.org), and [Linux Kernel](https://www.linux.org/), released under the GPL license.

---

## 2. Multi-Architecture and Multi-Mode Adaptation

### 2.1 Platform Coverage

In terms of platform adaptation, PRTOS deeply leverages hardware-assisted virtualization extensions on ARMv8 (AArch64), AMD64 (x86_64), and RISC-V (RV64). Additionally, PRTOS provides comprehensive Para-virtualization support for 32-bit x86 as well as all three major 64-bit platforms, covering the mainstream processor architectures in the embedded domain and offering exceptional deployment flexibility for diverse industrial scenarios.

| Platform | Virtualization Mode | Hardware Extensions | Address Translation |
|---|---|---|---|
| x86 (i386) | Para-virtualization | — | Shadow Page Tables |
| AArch64 (ARMv8) | HW-Virt + Para-Virt | EL2/vGIC/Stage-2 | Stage-2 (VTTBR_EL2) |
| AMD64 (x86_64) | HW-Virt + Para-Virt | Intel VT-x/VMX/EPT | EPT |
| RISC-V 64 (RV64) | HW-Virt + Para-Virt | H-extension/SBI HSM | G-stage (hgatp/Sv39x4) |

### 2.2 Architecture Abstraction Layer Design

PRTOS employs a "common core + architecture-specific backend" layered architecture:

- **Common Layer** (`core/kernel/`, `core/objects/`, `core/drivers/`): Implements platform-independent logic such as scheduling, communication, and health management.
- **Architecture Layer** (`core/kernel/<arch>/`, `core/include/<arch>/`): Encapsulates hardware-dependent implementations including CPU virtualization, MMU management, and interrupt control.

This organization ensures that cross-architecture feature evolution does not lead to code divergence, and provides clear extensibility for future platforms such as LoongArch and ARMv9.

---

## 3. Virtualization Technology Roadmap

PRTOS provides three operating modes on ARMv8 / RISC-V / AMD64 platforms:

### 3.1 Hardware-Assisted Full Virtualization

Guest operating systems require no source code modifications. Whether Linux 6.19 or FreeRTOS, they can be deployed in their native kernel state, relying entirely on hardware features for resource isolation.

In XML configuration, hardware virtualization mode is specified via the `hw_virt="true"` attribute:

```xml
<Partition id="0" name="Linux" flags="system" hw_virt="true" noVCpus="4">
    <PhysicalMemoryAreas>
        <Area start="0x10000000" size="256MB" />
    </PhysicalMemoryAreas>
</Partition>
```

Architecture-specific hardware virtualization implementations:

- **AArch64**: Leverages EL2 privilege level, Stage-2 page tables, and vGICv3 virtual interrupt controller. `core/kernel/aarch64/mmu.c` implements Stage-2 address translation, `core/kernel/aarch64/vgic.c` provides complete vGIC emulation, and `core/kernel/aarch64/psci.c` handles CPU start/stop and multi-core control.
- **AMD64**: Based on Intel VT-x (VMX) technology, using VMCS to control guest execution and EPT for second-level address translation. `core/kernel/amd64/vmx.c` contains the complete logic for VMX initialization, VMCS configuration, EPT management, VM-exit handling, and virtual device emulation.
- **RISC-V 64**: Based on the H-extension virtualization extensions, using the `hgatp` register to configure G-stage (Sv39x4) address translation. `core/kernel/riscv64/prtos_sbi.c` implements SBI HSM, RFENCE, TIME, and other extension emulation.

### 3.2 Para-virtualization

Guest operating systems participate in I/O optimization and scheduling control through **Hypercalls**. In XML configuration, the `hw_virt` attribute defaults to `"false"`, indicating para-virtualization mode:

```xml
<Partition id="0" name="FreeRTOS" flags="system">
    <!-- hw_virt defaults to false, using para-virtualization -->
    <PhysicalMemoryAreas>
        <Area start="0x6000000" size="1MB" />
    </PhysicalMemoryAreas>
</Partition>
```

The advantage of para-virtualization lies in enabling finer-grained scheduling control and I/O optimization. For example, FreeRTOS para-virtualization demos (`freertos_para_virt_aarch64`, `freertos_para_virt_riscv`, `freertos_para_virt_amd64`) interact directly with the Hypervisor via Hypercalls to implement clock management, interrupt routing, and other functions.

### 3.3 Hybrid Mode

PRTOS supports concurrent operation of full-virtualization and para-virtualization partitions on the same platform. Each partition has independent scheduling time slices, enabling strong isolation and efficient collaboration between heterogeneous systems such as Linux and FreeRTOS.

This capability is particularly critical in Mixed-Criticality scenarios:

```xml
<!-- Hybrid Mode Example: Linux (HW-Virt) + FreeRTOS (Para-Virt) -->
<Partition id="0" name="Linux_HMI" flags="system" hw_virt="true" noVCpus="3">
    <!-- High-performance HMI/cloud communication using HW virtualization -->
</Partition>
<Partition id="1" name="FreeRTOS_RT">
    <!-- Real-time control tasks using para-virtualization for minimal latency -->
</Partition>
```

In the bundled Mixed-OS demos (`mix_os_demo_aarch64`, `mix_os_demo_riscv64`, `mix_os_demo_amd64`), Linux (3 vCPUs, hardware virtualization) handles HMI and communication management, while FreeRTOS (1 vCPU, para-virtualization) manages real-time motor control. The two exchange data through shared memory, achieving low-latency collaboration under physical isolation.

For the theoretical foundations of mixed-criticality systems and scheduling strategies, *[Embedded Hypervisor: Architecture, Principles, and Implementation](http://www.prtos.org/embedded_hypervisor_book/)* provides a systematic treatment.

---

## 4. Core System Features

### 4.1 Static Resource Configuration and Cyclic Scheduling

PRTOS adheres to the determinism principle with a static resource configuration approach. Partitions, memory regions, communication ports, and scheduling plans are determined at system instantiation time, with no dynamic allocation at runtime. This design provides a solid foundation for Worst-Case Execution Time (WCET) analysis.

Scheduling uses a predefined Cyclic Scheduling Table, where each processor core is associated with a scheduling plan, and predefined time slots determine the execution time for each partition:

```xml
<Processor id="0">
    <CyclicPlanTable>
        <Plan id="0" majorFrame="10ms">
            <Slot id="0" start="0ms" duration="5ms" partitionId="0" vCpuId="0" />
            <Slot id="1" start="5ms" duration="5ms" partitionId="1" vCpuId="0" />
        </Plan>
    </CyclicPlanTable>
</Processor>
```

For the design principles of static configuration and cyclic scheduling, refer to Chapter 11.2 and Chapter 7 of *[Embedded Hypervisor: Architecture, Principles, and Implementation](http://www.prtos.org/embedded_hypervisor_book/)*.

### 4.2 Spatial and Temporal Isolation

PRTOS leverages hardware-assisted second-level address translation (EPT/Stage-2/G-stage) for spatial isolation, combined with cyclic scheduling for temporal isolation. Each partition has an independent, non-crossable physical address space, and shared memory areas must be explicitly declared in the XML configuration.

### 4.3 SMP Multi-Core Support

PRTOS supports Symmetric Multi-Processing (SMP), allowing a single partition to utilize multiple vCPUs. The mapping between physical cores and virtual cores is fully static, ensuring predictable multi-core behavior:

- Physical-to-virtual core relationships are clear and fixed.
- Scheduling switches and inter-core wake-up paths are explicitly controllable.
- Supports up to 4-vCPU Linux SMP operation (verified on all platforms).

### 4.4 Inter-Partition Communication (IPC)

PRTOS provides multiple inter-partition communication mechanisms:

- **Sampling Port**: Last-value-wins semantics, suitable for periodic state transfer.
- **Queuing Port**: FIFO semantics, suitable for command/event delivery.
- **Shared Memory**: High-throughput data exchange, the communication foundation for Virtio devices.
- **Inter-Partition Virtual Interrupt (IPVI)**: Cross-partition notification/doorbell mechanism.

### 4.5 Health Management and Tracing

PRTOS integrates comprehensive fault detection, management, and tracing infrastructure:

- **Health Monitoring**: Supports multi-level fault events (FATAL_ERROR, SYSTEM_ERROR, PARTITION_ERROR, etc.) with configurable automatic recovery actions (cold reset, warm reset, suspend, switch to maintenance mode, etc.).
- **Tracing Service**: Real-time system behavior logging for debugging and performance analysis.
- **System Partition Control**: System Partition has authority to query, suspend, resume, reset, and shutdown other partitions.

### 4.6 I/O Virtualization and Virtio

PRTOS has implemented shared-memory-based Virtio device virtualization on all three 64-bit platforms:

- **virtio-net**: Virtual network devices supporting Bridge, NAT, and P2P modes.
- **virtio-blk**: Virtual block storage devices.
- **virtio-console**: Virtual console devices.

In the Virtio architecture, the System Partition runs the backend driver (virtio_backend), while the Guest Partition runs the frontend driver (virtio_frontend), with both communicating efficiently through PRTOS shared memory regions and IPVI interrupts.

---

## 5. Guest OS Compatibility

PRTOS has verified the following Guest OS configurations:

| Guest OS | Virtualization Mode | Platforms | Typical Scenarios |
|---|---|---|---|
| Linux 6.19 (unmodified) | HW Virtualization | AArch64, AMD64, RISC-V | SMP (1-4 vCPU), Virtio I/O |
| Linux 3.4.4 (paravirt kernel) | Para-virtualization | x86 | Legacy system support |
| FreeRTOS | HW-Virt + Para-Virt | AArch64, AMD64, RISC-V | Real-time control tasks |
| BAIL bare partition | Para-virtualization | All platforms | Functional verification, teaching |

Notably, in hardware virtualization mode, **guests require no source code modifications** to run directly, enabling PRTOS to remain compatible with future Linux kernel versions and other commercial RTOSes.

---

## 6. Engineering Practices and Toolchain

### 6.1 Build System

PRTOS uses a unified Makefile-based build system supporting one-command compilation and execution:

```bash
# Example for AArch64
git clone https://github.com/prtos-project/prtos-hypervisor.git
cd prtos-hypervisor
cp prtos_config.aarch64 prtos_config
make defconfig
make
cd user/bail/examples/linux_4vcpu_1partion_aarch64/
make run.aarch64
```

In an Ubuntu 24.04 development environment, the entire deployment process from scratch takes approximately 5 minutes. For detailed steps, see the [Quick Start Guide](https://github.com/prtos-project/prtos-hypervisor/blob/main/README.md).

### 6.2 Automated Testing

PRTOS provides a comprehensive multi-platform automated testing framework covering 27 test cases:

```bash
# Run platform-specific tests
bash scripts/run_test.sh --arch x86 check-all      # x86: 11 Pass
bash scripts/run_test.sh --arch aarch64 check-all   # AArch64: 17 Pass
bash scripts/run_test.sh --arch riscv64 check-all   # RISC-V: 16 Pass
bash scripts/run_test.sh --arch amd64 check-all     # AMD64: 16 Pass
```

### 6.3 Hypercall API

PRTOS provides a rich Hypercall API covering the following categories:

| Category | Typical APIs | Description |
|---|---|---|
| Time Management | `prtos_get_time()`, `prtos_set_timer()` | Hardware and execution clocks |
| Partition Control | `prtos_suspend_partition()`, `prtos_reset_partition()` | Partition lifecycle management |
| vCPU Management | `prtos_suspend_vcpu()`, `prtos_reset_vcpu()` | Virtual processor control |
| Communication Ports | `prtos_read_object()`, `prtos_write_object()` | IPC data read/write |
| Interrupt Management | `prtos_clear_irqmask()`, `prtos_route_irq()` | Interrupt routing and masking |
| IPVI | `prtos_raise_ipvi()` | Inter-partition virtual interrupts |
| Schedule Control | `prtos_switch_sched_plan()` | Online scheduling plan switch |
| System Control | `prtos_halt_system()`, `prtos_reset_system()` | System-level operations |

### 6.4 XML Configuration System

System resources are statically configured via XML files, validated and compiled into binary configuration by the `prtoscparser` tool:

```xml
<SystemDescription xmlns="http://www.prtos.org/prtos-1.x" version="1.0.0">
    <HwDescription>
        <MemoryLayout>
            <Region type="ram" start="0x0" size="1024MB" />
        </MemoryLayout>
        <ProcessorTable>
            <Processor id="0">
                <CyclicPlanTable>
                    <Plan id="0" majorFrame="10ms">
                        <Slot id="0" start="0ms" duration="5ms"
                              partitionId="0" vCpuId="0"/>
                    </Plan>
                </CyclicPlanTable>
            </Processor>
        </ProcessorTable>
    </HwDescription>
    <PRTOSHypervisor console="Uart">
        <PhysicalMemoryArea size="8MB" />
    </PRTOSHypervisor>
    <PartitionTable>
        <Partition id="0" name="App" flags="system" hw_virt="true" noVCpus="1">
            <PhysicalMemoryAreas>
                <Area start="0x6000000" size="128MB" />
            </PhysicalMemoryAreas>
        </Partition>
    </PartitionTable>
</SystemDescription>
```

---

## 7. Why Choose PRTOS Hypervisor?

### 7.1 Determinism and Static Configuration

PRTOS adheres to the determinism principle with a static resource configuration approach. All critical resources are allocated at system initialization time, and runtime behavior is fully predictable. This design principle is rooted in separation kernel theory and holds irreplaceable advantages in safety-critical scenarios such as avionics and industrial control.

For the technical principles and engineering trade-offs of the static configuration approach, Chapter 11.2 of *[Embedded Hypervisor: Architecture, Principles, and Implementation](http://www.prtos.org/embedded_hypervisor_book/)* provides an in-depth analysis.

### 7.2 Minimal Deployment

In an Ubuntu 24.04 development environment, the entire deployment process takes approximately 5 minutes, including toolchain installation, source compilation, and example execution — requiring no additional commercial licenses or complex configurations.

For detailed setup instructions, refer to the Quick Start Guide: [https://github.com/prtos-project/prtos-hypervisor/blob/main/README.md](https://github.com/prtos-project/prtos-hypervisor/blob/main/README.md)

### 7.3 Open-Source Contribution

The PRTOS community welcomes all forms of code contributions and technical discussions. The project is released under the GPL v2 license, encouraging academic collaboration, community participation, and industrial applications.

For details, refer to the Contribution Guide: [https://github.com/prtos-project/prtos-hypervisor/wiki/Contributing-code](https://github.com/prtos-project/prtos-hypervisor/wiki/Contributing-code)

### 7.4 Theoretical Foundation

For developers and researchers seeking a systematic understanding of embedded virtualization technology, the companion book *[Embedded Hypervisor: Architecture, Principles, and Implementation](http://www.prtos.org/embedded_hypervisor_book/)* is recommended. Covering processor virtualization principles, memory management, interrupt virtualization, and complete system design and implementation, it provides the comprehensive knowledge framework for understanding and using PRTOS.

---

## 8. Example Project Matrix

PRTOS provides 37 example projects covering all four platforms:

### 8.1 Core Feature Examples (All Platforms)

| Example | Description |
|---|---|
| `helloworld` | Single-partition basic verification |
| `helloworld_smp` | SMP dual-core dual-partition verification |
| `example.001` | Timer management (HW_CLOCK vs EXEC_CLOCK) |
| `example.004` | Inter-partition communication (sampling ports, queuing ports, shared memory) |
| `example.006` | Scheduling plan switching |
| `example.007` | Health management event handling |
| `example.008` | Tracing service |
| `example.009` | Memory protection and shared memory access |

### 8.2 Operating System Examples (64-bit Platforms)

| Example | Platforms | Description |
|---|---|---|
| `linux_4vcpu_1partion_*` | AArch64/AMD64/RISC-V | 4-vCPU Linux SMP |
| `freertos_hw_virt_*` | AArch64/AMD64/RISC-V | FreeRTOS hardware virtualization |
| `freertos_para_virt_*` | AArch64/AMD64/RISC-V | FreeRTOS para-virtualization |
| `mix_os_demo_*` | AArch64/AMD64/RISC-V | Linux + FreeRTOS mixed-OS |
| `virtio_linux_demo_2p_*` | AArch64/AMD64/RISC-V | Dual Linux partitions + Virtio devices |

---

## 9. Target Scenarios

### 9.1 Recommended Use Cases

- **Industrial Control and Edge Gateways**: Mixed-Criticality deployment with Linux management plane + RTOS control plane.
- **Automotive Domain Controllers**: ECU consolidation across different safety levels.
- **Avionics**: Deterministic, certifiable partitioned execution environments.
- **AArch64/RISC-V Virtualization Research**: Prototyping and joint R&D platforms.
- **Education and Research**: Embedded virtualization architecture studies and course instruction.

### 9.2 Technical Reference

For the technical architecture, deployment strategies, and engineering practices of the above scenarios, *[Embedded Hypervisor: Architecture, Principles, and Implementation](http://www.prtos.org/embedded_hypervisor_book/)* provides a complete reference framework from theory to practice, suitable as a systematic reference for project evaluation, technology selection, and engineering training.

---

## 10. Conclusion

PRTOS Hypervisor has established a complete virtualization technology roadmap covering four major platforms: x86/AArch64/AMD64/RISC-V, supporting hardware-assisted full virtualization, para-virtualization, and hybrid mode simultaneously on all three 64-bit platforms. Its static partition design based on separation kernel principles gives it significant advantages in real-time performance and determinism. Combined with multi-Guest OS verification on Linux 6.19 and FreeRTOS, Virtio device virtualization, and comprehensive health management and tracing capabilities, PRTOS is no longer a conceptual prototype but an embedded virtualization platform with real engineering deployment value.
