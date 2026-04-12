# PRTOS Hypervisor 技术报告
**中文** | [English](prtos_tech_report.md)
## 1. 项目概述

**PRTOS Hypervisor** 是一款开源、轻量级的嵌入式 Type-1（裸金属）Hypervisor，采用 **分离内核（Separation Kernel）** 架构，专为实时与安全关键系统设计。通过严格的空间隔离与时间隔离，PRTOS 在单一硬件平台上实现多应用的安全共存与高效协作，完全消除应用间的相互干扰。

PRTOS 的核心设计原则是 **确定性与静态配置**：CPU、内存、I/O 设备等关键资源在系统实例化时静态分配，调度采用预定义的循环调度表（Cyclic Scheduling Table），这使得系统行为完全可预测、可分析、可验证。关于这一设计原则的理论基础和工程实现，可参阅《[嵌入式 Hypervisor：架构、原理与应用](http://www.prtos.org/embedded_hypervisor_book/)》第 11.2 节的详细论述。

PRTOS 项目遵循开源精神，在技术上吸收了 [XtratuM](https://en.wikipedia.org/wiki/XtratuM)、[Xen Hypervisor](https://xenproject.org/)、[Lguest Hypervisor](http://lguest.ozlabs.org) 及 [Linux Kernel](https://www.linux.org/) 的设计理念，以 GPL 许可证发布。

---

## 2. 多架构与多模式适配

### 2.1 平台覆盖

在平台适配方面，PRTOS 深度挖掘 ARMv8 (AArch64)、AMD64 (x86_64) 及 RISC-V (RV64) 的硬件辅助虚拟化扩展能力。同时，针对 32 位 x86 及上述三大 64 位主流平台，PRTOS 均实现了完善的半虚拟化（Para-virtualization）支持，覆盖了当前嵌入式领域的主流处理器架构，为不同工业场景提供了极高的部署灵活性。

| 平台 | 虚拟化模式 | 硬件扩展 | 地址转换 |
|---|---|---|---|
| x86 (i386) | 半虚拟化 | — | 影子页表 |
| AArch64 (ARMv8) | 硬件虚拟化 + 半虚拟化 | EL2/vGIC/Stage-2 | Stage-2 (VTTBR_EL2) |
| AMD64 (x86_64) | 硬件虚拟化 + 半虚拟化 | Intel VT-x/VMX/EPT | EPT |
| RISC-V 64 (RV64) | 硬件虚拟化 + 半虚拟化 | H-extension/SBI HSM | G-stage (hgatp/Sv39x4) |

### 2.2 架构抽象层设计

PRTOS 采用"通用核心 + 架构专属后端"的分层架构：

- **通用层**（`core/kernel/`、`core/objects/`、`core/drivers/`）：实现调度、通信、健康管理等平台无关逻辑。
- **架构层**（`core/kernel/<arch>/`、`core/include/<arch>/`）：下沉 CPU 虚拟化、MMU 管理、中断控制等与底层硬件强相关的实现。

这种组织方式确保了跨架构功能演进不会导致代码分叉，且为未来扩展至 LoongArch、ARMv9 等新平台提供了清晰的可延续性。

---

## 3. 虚拟化技术路线

PRTOS 在 ARMv8 / RISC-V / AMD64 平台上提供三种运行模式：

### 3.1 硬件辅助全虚拟化

客户机无需修改源码。无论是 Linux 6.19 还是 FreeRTOS，均可保持原生内核状态直接部署，完全依赖硬件特性实现资源隔离。

在 XML 配置中，通过 `hw_virt="true"` 属性指定分区使用硬件虚拟化模式：

```xml
<Partition id="0" name="Linux" flags="system" hw_virt="true" noVCpus="4">
    <PhysicalMemoryAreas>
        <Area start="0x10000000" size="256MB" />
    </PhysicalMemoryAreas>
</Partition>
```

各架构的硬件虚拟化实现：

- **AArch64**：利用 EL2 特权级、Stage-2 页表、vGICv3 虚拟中断控制器。`core/kernel/aarch64/mmu.c` 实现 Stage-2 地址转换，`core/kernel/aarch64/vgic.c` 提供完整的 vGIC 模拟，`core/kernel/aarch64/psci.c` 实现 CPU 启停与多核控制。
- **AMD64**：基于 Intel VT-x (VMX) 技术，通过 VMCS 控制客户机执行，EPT 实现二级地址转换。`core/kernel/amd64/vmx.c` 包含 VMX 初始化、VMCS 配置、EPT 管理、VM-exit 处理及虚拟设备模拟的完整逻辑。
- **RISC-V 64**：基于 H-extension 虚拟化扩展，通过 `hgatp` 寄存器配置 G-stage (Sv39x4) 地址转换。`core/kernel/riscv64/prtos_sbi.c` 实现 SBI HSM、RFENCE、TIME 等扩展的仿真。

### 3.2 半虚拟化 (Para-virtualization)

客户机通过 **Hypercall（超级调用）** 深度参与 I/O 优化与调度控制。在 XML 配置中，`hw_virt` 属性默认为 `"false"`，表示分区使用半虚拟化模式：

```xml
<Partition id="0" name="FreeRTOS" flags="system">
    <!-- hw_virt 默认为 false，使用半虚拟化 -->
    <PhysicalMemoryAreas>
        <Area start="0x6000000" size="1MB" />
    </PhysicalMemoryAreas>
</Partition>
```

半虚拟化模式的优势在于可实现更精细的调度控制和 I/O 优化。例如，FreeRTOS 半虚拟化示例（`freertos_para_virt_aarch64`、`freertos_para_virt_riscv`、`freertos_para_virt_amd64`）通过 Hypercall 直接与 Hypervisor 交互，实现时钟管理、中断路由等功能。

### 3.3 混合模式 (Hybrid Mode)

PRTOS 支持全虚拟化分区与半虚拟化分区在同一平台并发运行。各分区拥有独立的调度时间片，实现异构系统（如 Linux 与 FreeRTOS）的强隔离与高效协作。

在混合关键性（Mixed-Criticality）场景中，这种能力尤为关键：

```xml
<!-- 混合模式示例：Linux (HW-Virt) + FreeRTOS (Para-Virt) -->
<Partition id="0" name="Linux_HMI" flags="system" hw_virt="true" noVCpus="3">
    <!-- 高性能 HMI/云端通信，使用硬件虚拟化运行原生 Linux -->
</Partition>
<Partition id="1" name="FreeRTOS_RT">
    <!-- 实时控制任务，使用半虚拟化实现极低延迟 -->
</Partition>
```

在 PRTOS 附带的混合操作系统示例（`mix_os_demo_aarch64`、`mix_os_demo_riscv64`、`mix_os_demo_amd64`）中，Linux（3 vCPU，硬件虚拟化）处理 HMI 与通信管理，FreeRTOS（1 vCPU，半虚拟化）负责实时电机控制，两者之间通过共享内存进行数据交换，在物理隔离的前提下实现低延迟协作。

关于混合关键性系统的理论基础和调度策略，《[嵌入式 Hypervisor：架构、原理与应用](http://www.prtos.org/embedded_hypervisor_book/)》中有系统化的论述。

---

## 4. 系统核心特性

### 4.1 静态资源配置与循环调度

PRTOS 坚持确定性原则，采用资源静态配置方案。分区、内存区、通信端口、调度计划在系统实例化时确定，运行时不进行动态分配。这一设计为最坏情况执行时间（WCET）分析提供了坚实基础。

调度采用预定义的循环调度表（Cyclic Scheduling Table），每个处理器核心关联一个调度计划，由预定义的时间槽（Slot）决定各分区的执行时间：

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

关于静态配置与循环调度的设计原理，可参阅《[嵌入式 Hypervisor：架构、原理与应用](http://www.prtos.org/embedded_hypervisor_book/)》第 11.2 节和第 7 章。

### 4.2 空间隔离与时间隔离

PRTOS 利用硬件辅助的二级地址转换（EPT/Stage-2/G-stage）实现空间隔离，结合循环调度机制实现时间隔离。每个分区拥有独立且不可越界的物理地址空间，共享内存区域需在 XML 配置中显式声明。

### 4.3 SMP 多核支持

PRTOS 支持对称多处理（SMP）模式，允许单个分区使用多个 vCPU。物理核与虚拟核之间的映射关系完全静态化，确保多核行为可预测：

- 物理核与虚拟核关系清晰且固定。
- 调度切换与核间唤醒路径显式可控。
- 支持最多 4 vCPU 的 Linux SMP 运行（各平台均已验证）。

### 4.4 分区间通信 (IPC)

PRTOS 提供多种分区间通信机制：

- **采样端口（Sampling Port）**：最新值覆盖语义，适用于周期性状态传递。
- **队列端口（Queuing Port）**：FIFO 语义，适用于命令/事件传递。
- **共享内存（Shared Memory）**：高吞吐数据交换，Virtio 设备的通信基础。
- **分区间虚拟中断（IPVI）**：跨分区通知/门铃机制。

### 4.5 健康管理与追踪

PRTOS 集成了完整的故障检测、管理与追踪基础设施：

- **健康管理（Health Monitoring）**：支持多级故障事件（FATAL_ERROR、SYSTEM_ERROR、PARTITION_ERROR 等），可配置自动恢复动作（冷复位、暖复位、暂停、切换至维护模式等）。
- **追踪服务（Tracing）**：实时记录系统行为，辅助调试与性能分析。
- **系统分区管控**：System Partition 具有查询、暂停、恢复、重置、关闭其他分区的权限。

### 4.6 I/O 虚拟化与 Virtio

PRTOS 已在三大 64 位平台上实现了基于共享内存的 Virtio 设备虚拟化方案：

- **virtio-net**：虚拟网络设备，支持 Bridge、NAT、P2P 模式。
- **virtio-blk**：虚拟块存储设备。
- **virtio-console**：虚拟控制台设备。

在 Virtio 架构中，System Partition 运行后端驱动（virtio_backend），Guest Partition 运行前端驱动（virtio_frontend），两者通过 PRTOS 共享内存区域和 IPVI 中断进行高效通信。

---

## 5. Guest OS 兼容性

PRTOS 已验证以下 Guest OS 的运行：

| Guest OS | 虚拟化模式 | 覆盖平台 | 典型场景 |
|---|---|---|---|
| Linux 6.19 (原生内核) | 硬件虚拟化 | AArch64, AMD64, RISC-V | SMP (1-4 vCPU)、Virtio I/O |
| Linux 3.4.4 (半虚拟化内核) | 半虚拟化 | x86 | 遗留系统支持 |
| FreeRTOS | 硬件虚拟化 + 半虚拟化 | AArch64, AMD64, RISC-V | 实时控制任务 |
| BAIL 裸分区 | 半虚拟化 | 全平台 | 功能验证、教学 |

值得注意的是，在硬件虚拟化模式下，**客户机无需任何源码修改**即可直接运行，这使得 PRTOS 能够兼容未来的 Linux 内核版本和其他商用 RTOS。

---

## 6. 工程实践与工具链

### 6.1 构建系统

PRTOS 采用基于 Makefile 的统一构建体系，支持一键编译与运行：

```bash
# 以 AArch64 为例
git clone https://github.com/prtos-project/prtos-hypervisor.git
cd prtos-hypervisor
cp prtos_config.aarch64 prtos_config
make defconfig
make
cd user/bail/examples/linux_4vcpu_1partion_aarch64/
make run.aarch64
```

在 Ubuntu 24.04 开发环境中，从零开始的全流程部署仅需约 5 分钟。具体操作参见[快速上手指南](https://github.com/prtos-project/prtos-hypervisor/blob/main/README_zh.md)。

### 6.2 自动化测试

PRTOS 提供了完整的多平台自动化测试框架，覆盖 27 个测试用例：

```bash
# 运行全平台测试
bash scripts/run_test.sh --arch x86 check-all      # x86: 11 Pass
bash scripts/run_test.sh --arch aarch64 check-all   # AArch64: 17 Pass
bash scripts/run_test.sh --arch riscv64 check-all   # RISC-V: 16 Pass
bash scripts/run_test.sh --arch amd64 check-all     # AMD64: 16 Pass
```

### 6.3 Hypercall API

PRTOS 提供了丰富的 Hypercall API，涵盖以下类别：

| 类别 | 典型 API | 说明 |
|---|---|---|
| 时钟管理 | `prtos_get_time()`, `prtos_set_timer()` | 硬件时钟与执行时钟 |
| 分区控制 | `prtos_suspend_partition()`, `prtos_reset_partition()` | 分区生命周期管理 |
| vCPU 管理 | `prtos_suspend_vcpu()`, `prtos_reset_vcpu()` | 虚拟处理器控制 |
| 通信端口 | `prtos_read_object()`, `prtos_write_object()` | IPC 数据读写 |
| 中断管理 | `prtos_clear_irqmask()`, `prtos_route_irq()` | 中断路由与屏蔽 |
| IPVI | `prtos_raise_ipvi()` | 分区间虚拟中断 |
| 调度控制 | `prtos_switch_sched_plan()` | 在线切换调度计划 |
| 系统控制 | `prtos_halt_system()`, `prtos_reset_system()` | 系统级操作 |

### 6.4 XML 配置系统

系统资源通过 XML 文件静态配置，经 `prtoscparser` 工具验证与编译为二进制配置：

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

## 7. 为什么选择 PRTOS Hypervisor？

### 7.1 确定性与静态配置

PRTOS 坚持确定性原则，采用资源静态配置方案。所有关键资源在系统初始化时完成分配，运行时行为完全可预测。这一设计原则植根于分离内核理论，在航空电子、工业控制等强确定性场景中具有不可替代的优势。

关于静态配置方案的技术原理和工程权衡，《[嵌入式 Hypervisor：架构、原理与应用](http://www.prtos.org/embedded_hypervisor_book/)》第 11.2 节提供了深入的分析。

### 7.2 极简部署

在 Ubuntu 24.04 开发环境中，全流程部署仅需约 5 分钟。包括工具链安装、源码编译、示例运行的完整链路，无需额外商业许可或复杂配置。

具体操作参考快速上手指南：[https://github.com/prtos-project/prtos-hypervisor/blob/main/README_zh.md](https://github.com/prtos-project/prtos-hypervisor/blob/main/README_zh.md)

### 7.3 开源贡献

PRTOS 社区欢迎任何形式的代码贡献与技术讨论。项目以 GPL v2 许可证发布，鼓励学术、社区协作和产业应用。

具体内容参考贡献指南：[https://github.com/prtos-project/prtos-hypervisor/wiki/Contributing-code](https://github.com/prtos-project/prtos-hypervisor/wiki/Contributing-code)

### 7.4 理论支撑

对于希望系统性掌握嵌入式虚拟化技术背景的开发者和研究者，推荐配套阅读《[嵌入式 Hypervisor：架构、原理与应用](http://www.prtos.org/embedded_hypervisor_book/)》。本书从处理器虚拟化原理、内存管理、中断虚拟化到完整的系统设计与实现，构成了理解和使用 PRTOS 的完整知识体系。

---

## 8. 示例工程矩阵

PRTOS 提供了 37 个示例工程，覆盖全部四个平台：

### 8.1 核心功能示例（全平台）

| 示例 | 功能说明 |
|---|---|
| `helloworld` | 单分区基础验证 |
| `helloworld_smp` | SMP 双核双分区验证 |
| `example.001` | 定时器管理（HW_CLOCK vs EXEC_CLOCK） |
| `example.004` | 分区间通信（采样端口、队列端口、共享内存） |
| `example.006` | 调度计划切换 |
| `example.007` | 健康管理事件处理 |
| `example.008` | 追踪服务 |
| `example.009` | 内存保护与共享内存访问 |

### 8.2 操作系统示例（64 位平台）

| 示例 | 平台 | 说明 |
|---|---|---|
| `linux_4vcpu_1partion_*` | AArch64/AMD64/RISC-V | 4 vCPU Linux SMP |
| `freertos_hw_virt_*` | AArch64/AMD64/RISC-V | FreeRTOS 硬件虚拟化 |
| `freertos_para_virt_*` | AArch64/AMD64/RISC-V | FreeRTOS 半虚拟化 |
| `mix_os_demo_*` | AArch64/AMD64/RISC-V | Linux + FreeRTOS 混合 |
| `virtio_linux_demo_2p_*` | AArch64/AMD64/RISC-V | 双 Linux 分区 + Virtio 设备 |

---

## 9. 适用场景

### 9.1 推荐场景

- **工业控制与边缘网关**：Linux 管理面 + RTOS 控制面的 Mixed-Criticality 部署。
- **车载域控**：不同安全等级的 ECU 整合。
- **航空电子**：强确定性、可认证的分区运行环境。
- **AArch64/RISC-V 虚拟化研究**：原型验证与联合研发平台。
- **教学与实验**：嵌入式虚拟化体系结构研究与课程教学。

### 9.2 技术参考

关于上述场景的技术架构、部署策略和工程实践，《[嵌入式 Hypervisor：架构、原理与应用](http://www.prtos.org/embedded_hypervisor_book/)》提供了从理论到实践的完整参考框架，适合作为项目评估、技术选型和工程培训的系统性参考资料。

---

## 10. 结论

PRTOS Hypervisor 已形成一条覆盖 x86/AArch64/AMD64/RISC-V 四大平台的完整虚拟化技术路线，在三个 64 位平台上同时支持硬件辅助全虚拟化、半虚拟化及混合模式。其以分离内核为基础的静态分区设计，使之在实时性和确定性方面具有显著优势。结合 Linux 6.19 与 FreeRTOS 的多 Guest OS 验证、Virtio 设备虚拟化方案、以及完善的健康管理与追踪能力，PRTOS 已不再是概念原型，而是具备真实工程部署价值的嵌入式虚拟化平台。