# PRTOS Hypervisor — 入门文档

**中文** | [English](README.md)

欢迎阅读 PRTOS Hypervisor 入门文档。本目录为所有已支持架构提供按平台划分的快速开始指南与 API 参考文档。

## 支持的平台

| 平台 | 入门指南 | API 参考 |
|---|---|---|
| **x86（32 位）** | [English](x86_getting_started.md) / [中文](x86_getting_started_zh.md) | [English](x86_api_reference.md) / [中文](x86_api_reference_zh.md) |
| **AMD64（x86_64）** | [English](amd64_getting_started.md) / [中文](amd64_getting_started_zh.md) | [English](amd64_api_reference.md) / [中文](amd64_api_reference_zh.md) |
| **AArch64（ARMv8）** | [English](aarch64_getting_started.md) / [中文](aarch64_getting_started_zh.md) | [English](aarch64_api_reference.md) / [中文](aarch64_api_reference_zh.md) |
| **RISC-V 64** | [English](riscv64_getting_started.md) / [中文](riscv64_getting_started_zh.md) | [English](riscv64_api_reference.md) / [中文](riscv64_api_reference_zh.md) |

## 如何选择平台

- **x86（i386）**：最适合使用 para-virtualization 方式学习 PRTOS 的基础能力。不需要交叉编译器，直接使用系统 GCC 并配合 `-m32` 参数即可。适合学习 hypercall API、分区管理与 IPC 机制。

- **AArch64（ARMv8）**：通过 EL2、Stage-2 页表和 vGICv3 提供完整的硬件辅助虚拟化能力。支持运行未修改的 Linux 6.19 和 FreeRTOS Guest。最适合嵌入式与汽车电子场景。

- **RISC-V 64（RV64）**：通过 H-extension 与 G-stage 地址转换提供硬件辅助虚拟化能力，面向快速发展的 RISC-V 嵌入式生态。需要 `riscv64-linux-gnu-gcc` 交叉编译器。

- **AMD64（x86_64）**：通过 Intel VT-x/VMX 与 EPT 提供硬件辅助虚拟化能力。可在标准 x86_64 主机上配合 KVM 加速运行。支持 PCI 设备直通与 Virtio I/O 虚拟化。

## 快速开始（适用于全部平台）

```bash
# 1. 安装通用依赖（Ubuntu 24.04）
sudo apt-get update
sudo apt-get install -y build-essential make perl gawk git bc bison flex cpio \
    unzip rsync wget xorriso mtools python3-dev python3-libxml2 \
    libncurses5-dev libssl-dev libxml2-dev libxml2-utils gdb-multiarch

# 2. 克隆仓库
git clone https://github.com/prtos-project/prtos-hypervisor.git
cd prtos-hypervisor

# 3. 选择平台并构建
cp prtos_config.<arch> prtos_config   # arch: x86, aarch64, riscv64, amd64
make defconfig
make

# 4. 运行示例
cd user/bail/examples/helloworld
make run.<arch>                        # arch: x86, aarch64, riscv64, amd64
```

## 延伸阅读

- [PRTOS Technical Report (中文)](../design/prtos_tech_report_zh.md) / [English](../design/prtos_tech_report.md)
- [调试指南](../debug/how_to_debug_prtos_hypervisor_and_assistant_tools.md)
- [贡献指南](../contribution_guide/contribution_guide.md)
- 配套书籍：[*Embedded Hypervisor: Architecture, Principles, and Implementation*](http://www.prtos.org/embedded_hypervisor_book/)
