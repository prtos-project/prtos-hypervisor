# PRTOS Hypervisor — Getting Started

**English** | [中文](README_zh.md)

Welcome to the PRTOS Hypervisor Getting Started documentation. This directory provides platform-specific quick start guides and API references for all supported architectures.

## Supported Platforms

| Platform | Getting Started | API Reference |
|---|---|---|
| **x86 (32-bit)** | [English](x86_getting_started.md) / [中文](x86_getting_started_zh.md) | [English](x86_api_reference.md) / [中文](x86_api_reference_zh.md) |
| **AMD64 (x86_64)** | [English](amd64_getting_started.md) / [中文](amd64_getting_started_zh.md) | [English](amd64_api_reference.md) / [中文](amd64_api_reference_zh.md) |
| **AArch64 (ARMv8)** | [English](aarch64_getting_started.md) / [中文](aarch64_getting_started_zh.md) | [English](aarch64_api_reference.md) / [中文](aarch64_api_reference_zh.md) |
| **RISC-V 64** | [English](riscv64_getting_started.md) / [中文](riscv64_getting_started_zh.md) | [English](riscv64_api_reference.md) / [中文](riscv64_api_reference_zh.md) |

## Choosing a Platform

- **x86 (i386)**: Best for getting started with PRTOS fundamentals using para-virtualization. No cross-compiler needed — uses system GCC with `-m32` flag. Ideal for learning hypercall APIs, partition management, and IPC mechanisms.

- **AArch64 (ARMv8)**: Full hardware-assisted virtualization via EL2, Stage-2 page tables, and vGICv3. Supports running unmodified Linux 6.19 and FreeRTOS as guests. Best for embedded and automotive scenarios.

- **RISC-V 64 (RV64)**: Hardware-assisted virtualization via H-extension with G-stage address translation. Targets the growing RISC-V embedded ecosystem. Requires `riscv64-linux-gnu-gcc` cross-compiler.

- **AMD64 (x86_64)**: Hardware-assisted virtualization via Intel VT-x/VMX with EPT. Runs on standard x86_64 hosts with KVM acceleration. Supports PCI device passthrough and Virtio I/O virtualization.

## Quick Start (All Platforms)

```bash
# 1. Install common dependencies (Ubuntu 24.04)
sudo apt-get update
sudo apt-get install -y build-essential make perl gawk git bc bison flex cpio \
    unzip rsync wget xorriso mtools python3-dev python3-libxml2 \
    libncurses5-dev libssl-dev libxml2-dev libxml2-utils gdb-multiarch

# 2. Clone the repository
git clone https://github.com/prtos-project/prtos-hypervisor.git
cd prtos-hypervisor

# 3. Choose your platform and build
cp prtos_config.<arch> prtos_config   # arch: x86, aarch64, riscv64, amd64
make defconfig
make

# 4. Run an example
cd user/bail/examples/helloworld
make run.<arch>                        # arch: x86, aarch64, riscv64, amd64
```

## Further Reading

- [PRTOS Technical Report (中文)](../design/prtos_tech_report_zh.md) / [English](../design/prtos_tech_report.md)
- [Debugging Guide](../debug/how_to_debug_prtos_hypervisor_and_assistant_tools.md)
- [Contribution Guide](../contribution_guide/contribution_guide.md)
- Companion Book: [*Embedded Hypervisor: Architecture, Principles, and Implementation*](http://www.prtos.org/embedded_hypervisor_book/)
