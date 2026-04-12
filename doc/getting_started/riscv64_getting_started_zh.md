# RISC-V 64 平台 — 快速入门

## 前置要求

- **操作系统**：Ubuntu 24.04 LTS（推荐）
- **工具链**：`riscv64-linux-gnu-` 交叉编译器

## 依赖安装

```bash
# 通用依赖
sudo apt-get update
sudo apt-get install -y build-essential make perl gawk git bc bison flex cpio \
    unzip rsync wget xorriso mtools python3-dev python3-libxml2 \
    libncurses5-dev libssl-dev libxml2-dev libxml2-utils gdb-multiarch

# RISC-V64 专用：交叉编译器和 QEMU
sudo apt-get install -y gcc-riscv64-linux-gnu qemu-system-misc
```

## 虚拟化模式

RISC-V 64 平台同时支持**硬件虚拟化和半虚拟化**：

- **硬件虚拟化**：利用 RISC-V H 扩展（Hypervisor 扩展）、G-stage 页表（`hgatp`）、VS 模式（虚拟 Supervisor）和 SBI（Supervisor Binary Interface）。可运行未修改的客户操作系统内核（如 Linux 6.19、FreeRTOS）。
- **半虚拟化**：客户分区通过 PRTOS Hypercall API 进行资源管理，需要对客户 OS 进行 PRTOS 适配修改。

在 XML 配置中设置 `hw_virt="true"` 可启用硬件虚拟化模式。

## 构建与运行

### 构建 PRTOS Hypervisor

```bash
git clone https://github.com/prtos-project/prtos-hypervisor.git
cd prtos-hypervisor
cp prtos_config.riscv64 prtos_config
make defconfig
make
```

### 运行 Hello World 示例

```bash
cd user/bail/examples/helloworld
make run.riscv64
```

预期输出：
```
P0 ("Partition0":0:1) flags: [ SYSTEM ]:
    [0x86000000:0x86000000 - 0x860fffff:0x860fffff] flags: 0x0
[0] Hello World!
[0] Verification Passed
[HYPERCALL] (0x0) Halted
```

### 运行 SMP Hello World

```bash
cd user/bail/examples/helloworld_smp
make run.riscv64
```

### 运行全部测试

```bash
cd prtos-hypervisor
bash scripts/run_test.sh --arch riscv64 check-all
```

预期结果：16 通过，0 失败，11 跳过。

## 可用示例

| 示例 | 描述 |
|---|---|
| `helloworld` | 基础单分区 "Hello World" |
| `helloworld_smp` | SMP 双核双分区验证 |
| `example.001` | 定时器管理 (HW_CLOCK 与 EXEC_CLOCK) |
| `example.002` | 基础分区执行 |
| `example.003` | 分区交互 |
| `example.004` | 分区间通信（采样/排队端口、共享内存） |
| `example.005` | IPC 端口 |
| `example.006` | 调度计划切换 |
| `example.007` | 健康监控 |
| `example.008` | 跟踪服务 |
| `example.009` | 内存保护和共享内存访问 |
| `freertos_para_virt_riscv` | FreeRTOS 半虚拟化 |
| `freertos_hw_virt_riscv` | 原生 FreeRTOS 硬件虚拟化 |
| `linux_4vcpu_1partion_riscv64` | 4 vCPU Linux 客户机 |
| `mix_os_demo_riscv64` | 混合 OS：Linux + FreeRTOS 同时运行 |
| `virtio_linux_demo_2p_riscv64` | VirtIO 两个 Linux 分区间网络通信 |

## QEMU 运行命令参考

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

| 选项 | 描述 |
|---|---|
| `-machine virt` | RISC-V virt 虚拟机 |
| `-cpu rv64` | RV64GC CPU 模型 |
| `-smp 4` | 4 个 CPU 核心 |
| `-m 1G` | 1GB 内存 |
| `-bios default` | 默认 OpenSBI 固件 |
| `-kernel` | 加载 PRTOS 二进制镜像 |
| `-nographic` | 无图形窗口，串口输出到终端 |
| `-serial stdio` | UART 输出到终端 |

## 启动流程

1. OpenSBI 固件在 M 模式（Machine 模式）启动
2. OpenSBI 将控制权交给加载在内核地址的 PRTOS 内核
3. PRTOS 初始化 G-stage 页表（`hgatp`）和虚拟定时器
4. PRTOS 按照 XML 定义的调度计划启动分区
5. 客户机在 VS 模式（虚拟 Supervisor）下受 Hypervisor 控制运行

## 内存布局

RISC-V 使用高位 RAM 基地址：

| 组件 | 地址范围 |
|---|---|
| RAM 基地址 | `0x80000000` |
| RAM 大小 | 512MB |
| PRTOS Hypervisor | 预留 32MB |
| 分区 0 | `0x86000000`（1MB） |

## XML 配置示例

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

## 后续步骤

- 阅读 [RISC-V64 API 参考手册](riscv64_api_reference_zh.md) 了解 Hypercall API 详情
- 浏览 [PRTOS 技术报告](../design/prtos_tech_report.md)
- 深入学习 [《嵌入式 Hypervisor：架构、原理与应用》](http://www.prtos.org/embedded_hypervisor_book/)
