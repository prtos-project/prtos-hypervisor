# AMD64 (x86_64) 平台 — 快速入门

## 前置要求

- **操作系统**：Ubuntu 24.04 LTS（推荐）
- **工具链**：原生 GCC（无需交叉编译器）

## 依赖安装

```bash
# 通用依赖
sudo apt-get update
sudo apt-get install -y build-essential make perl gawk git bc bison flex cpio \
    unzip rsync wget xorriso mtools python3-dev python3-libxml2 \
    libncurses5-dev libssl-dev libxml2-dev libxml2-utils gdb-multiarch

# AMD64 专用：QEMU 和 GRUB 工具
sudo apt-get install -y qemu-system-x86 grub-pc-bin
```

## 虚拟化模式

AMD64 平台同时支持**硬件虚拟化和半虚拟化**：

- **硬件虚拟化**：利用 Intel VT-x/VMX 和扩展页表 (EPT) 实现内存隔离。可运行未修改的客户操作系统内核（如 Linux 6.19、FreeRTOS）。
- **半虚拟化**：客户分区通过 PRTOS Hypercall API 进行资源管理，需要对客户 OS 进行 PRTOS 适配修改。

在 XML 配置中设置 `hw_virt="true"` 可启用硬件虚拟化模式。

### EPT 约束

AMD64 硬件虚拟化仅恒等映射前 1GB 物理内存。所有分区和共享内存地址必须低于 `0x40000000`。

### 保留资源

以下资源由 PRTOS Hypervisor 在 AMD64 上保留：

- **IRQ**：2、4、24、26、27
- **I/O 端口**：0x20-0x21、0xA0-0xA1、0x3F8-0x3FC

## 构建与运行

### 构建 PRTOS Hypervisor

```bash
git clone https://github.com/prtos-project/prtos-hypervisor.git
cd prtos-hypervisor
cp prtos_config.amd64 prtos_config
make defconfig
make
```

### 运行 Hello World 示例

```bash
cd user/bail/examples/helloworld
make run.amd64
```

预期输出：
```
P0 ("Partition0":0:1) flags: [ SYSTEM ]:
    [0x6000000:0x6000000 - 0x60fffff:0x60fffff] flags: 0x0
[0] Hello World!
[0] Verification Passed
[HYPERCALL] (0x0) Halted
```

### 运行 SMP Hello World

```bash
cd user/bail/examples/helloworld_smp
make run.amd64
```

### 运行全部测试

```bash
cd prtos-hypervisor
bash scripts/run_test.sh --arch amd64 check-all
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
| `freertos_para_virt_amd64` | FreeRTOS 半虚拟化 |
| `freertos_hw_virt_amd64` | 原生 FreeRTOS 硬件虚拟化 |
| `linux_4vcpu_1partion_amd64` | 4 vCPU Linux 客户机 |
| `mix_os_demo_amd64` | 混合 OS：Linux + FreeRTOS 同时运行 |
| `virtio_linux_demo_2p_amd64` | VirtIO 两个 Linux 分区间网络通信 |

## QEMU 运行命令参考

```bash
qemu-system-x86_64 -m 1024 -cdrom resident_sw.iso -serial stdio -boot d -smp 4
```

| 选项 | 描述 |
|---|---|
| `-m 1024` | 1GB 内存 |
| `-cdrom` | 从 ISO 镜像启动 |
| `-serial stdio` | UART 输出到终端 |
| `-boot d` | 从 CD-ROM 启动 |
| `-smp 4` | 4 个 CPU 核心 |

### KVM 加速（可选）

在支持 Intel VT-x 的宿主机上可启用 KVM 加速：

```bash
qemu-system-x86_64 -enable-kvm -cpu host,-waitpkg -m 1024 -cdrom resident_sw.iso -serial stdio -boot d -smp 4
```

## 启动流程

1. GRUB 引导加载程序从 ISO 镜像加载 PRTOS
2. SeaBIOS/UEFI 初始化硬件，提供 ACPI 和 e820 内存映射
3. PRTOS 初始化 VMX（虚拟机扩展）和 EPT（扩展页表）
4. PRTOS 按照 XML 定义的调度计划启动分区
5. 硬件虚拟化客户机运行在 VMX non-root 模式；半虚拟化客户机使用 Hypercall

## XML 配置示例

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

## 后续步骤

- 阅读 [AMD64 API 参考手册](amd64_api_reference_zh.md) 了解 Hypercall API 详情
- 浏览 [PRTOS 技术报告](../design/prtos_tech_report.md)
- 深入学习 [《嵌入式 Hypervisor：架构、原理与应用》](http://www.prtos.org/embedded_hypervisor_book/)
