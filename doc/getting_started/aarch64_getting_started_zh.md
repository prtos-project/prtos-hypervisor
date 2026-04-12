# AArch64 (ARMv8) 平台 — 快速入门

## 前置要求

- **操作系统**：Ubuntu 24.04 LTS（推荐）
- **工具链**：`aarch64-linux-gnu-` 交叉编译器

## 依赖安装

```bash
# 通用依赖
sudo apt-get update
sudo apt-get install -y build-essential make perl gawk git bc bison flex cpio \
    unzip rsync wget xorriso mtools python3-dev python3-libxml2 \
    libncurses5-dev libssl-dev libxml2-dev libxml2-utils gdb-multiarch

# AArch64 专用：交叉编译器、U-Boot 工具和 QEMU
sudo apt-get install -y gcc-aarch64-linux-gnu qemu-system-arm u-boot-tools
```

## 虚拟化模式

AArch64 平台同时支持**硬件虚拟化和半虚拟化**：

- **硬件虚拟化**：利用 ARMv8 VHE（虚拟化宿主扩展）、EL2 异常级别、Stage-2 页表和 GICv3 虚拟中断控制器。可运行未修改的客户操作系统内核（如 Linux 6.19、FreeRTOS）。
- **半虚拟化**：客户分区通过 PRTOS Hypercall API 进行资源管理，需要对客户 OS 进行 PRTOS 适配修改。

在 XML 配置中设置 `hw_virt="true"` 可启用硬件虚拟化模式。

## 构建与运行

### 构建 PRTOS Hypervisor

```bash
git clone https://github.com/prtos-project/prtos-hypervisor.git
cd prtos-hypervisor
cp prtos_config.aarch64 prtos_config
make defconfig
make
```

### 运行 Hello World 示例

```bash
cd user/bail/examples/helloworld
make run.aarch64
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
make run.aarch64
```

### 运行全部测试

```bash
cd prtos-hypervisor
bash scripts/run_test.sh --arch aarch64 check-all
```

预期结果：17 通过，0 失败，10 跳过。

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
| `freertos_para_virt_aarch64` | FreeRTOS 半虚拟化 |
| `freertos_hw_virt_aarch64` | 原生 FreeRTOS 硬件虚拟化 |
| `linux_aarch64` | Linux 客户机（单 vCPU，硬件虚拟化） |
| `linux_4vcpu_1partion_aarch64` | 4 vCPU Linux 客户机 |
| `mix_os_demo_aarch64` | 混合 OS：Linux + FreeRTOS 同时运行 |
| `virtio_linux_demo_2p_aarch64` | VirtIO 两个 Linux 分区间网络通信 |

## QEMU 运行命令参考

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

| 选项 | 描述 |
|---|---|
| `-machine virt,gic_version=3` | ARM virt 机器，使用 GICv3 |
| `-machine virtualization=true` | 启用 EL2 虚拟化扩展 |
| `-cpu cortex-a72` | Cortex-A72 CPU 模型 |
| `-m 4096` | 4GB 内存 |
| `-smp 4` | 4 个 CPU 核心 |
| `-bios ./u-boot/u-boot.bin` | U-Boot 引导加载程序 |
| `-device loader` | 在 0x40200000 加载 PRTOS 镜像 |
| `-nographic` | 无图形窗口，串口输出到终端 |

## 启动流程

1. U-Boot 在 EL2（ARM 异常级别 2）启动
2. U-Boot 从设备加载器加载 PRTOS 镜像
3. PRTOS 初始化 Stage-2 页表和 GICv3 虚拟中断控制器
4. PRTOS 按照 XML 定义的调度计划启动分区

## XML 配置示例

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

## 后续步骤

- 阅读 [AArch64 API 参考手册](aarch64_api_reference_zh.md) 了解 Hypercall API 详情
- 浏览 [PRTOS 技术报告](../design/prtos_tech_report.md)
- 深入学习 [《嵌入式 Hypervisor：架构、原理与应用》](http://www.prtos.org/embedded_hypervisor_book/)
