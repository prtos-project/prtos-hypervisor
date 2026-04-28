# LoongArch64 平台 — 快速入门

## 前置要求

- **操作系统**：Ubuntu 24.04 LTS（推荐）
- **工具链**：`loongarch64-linux-gnu-` 交叉编译器
- **模拟器**：`qemu-system-loongarch64`（11.0.0 或更高版本，需支持 `la464` CPU 模型）

## 依赖安装

```bash
# 通用依赖
sudo apt-get update
sudo apt-get install -y build-essential make perl gawk git bc bison flex cpio \
    unzip rsync wget xorriso mtools python3-dev python3-libxml2 \
    libncurses5-dev libssl-dev libxml2-dev libxml2-utils gdb-multiarch

# LoongArch64 专用：交叉编译器与 QEMU
sudo apt-get install -y gcc-loongarch64-linux-gnu qemu-system-loongarch64
```

## 虚拟化模式

LoongArch64 当前**仅采用陷入模拟（trap-and-emulate）的半虚拟化方案**。

- 客户机 Linux 内核运行在 **PLV3**（LoongArch 的最低特权级）。
- 所有特权操作（CSR 访问、TLB 写入、定时器编程、LIO 中断控制器的 IPI/EOI 等）都会陷入 PRTOS 进行模拟。
- PRTOS 持有异常入口（CSR `EENTRY`），并在主机陷阱、来自 PLV3 的 Guest 陷阱以及（启用时）LVZ Guest 退出之间进行分发。
- 不使用 U-Boot。PRTOS 通过 QEMU 的 `-kernel resident_sw` 选项直接加载。

XML 配置无需在 LoongArch64 上设置 `hw_virt` 属性 —— 分区默认采用半虚拟化。

## 构建与运行

### 构建 PRTOS Hypervisor

```bash
git clone https://github.com/prtos-project/prtos-hypervisor.git
cd prtos-hypervisor
cp prtos_config.loongarch64 prtos_config
make defconfig
make
```

### 运行 Hello World 示例

```bash
cd user/bail/examples/helloworld
make run.loongarch64
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
make run.loongarch64
```

### 运行全部测试

```bash
cd prtos-hypervisor
bash scripts/run_test.sh --arch loongarch64 check-all
```

预期：16 通过，0 失败（其他架构对应的 16 个用例标记为 SKIP）。

### 运行 FreeRTOS 半虚拟化示例

```bash
cd user/bail/examples/freertos_para_virt_loongarch64
make run.loongarch64
```

### 运行 FreeRTOS 硬件虚拟化（LVZ Shim）示例

```bash
cd user/bail/examples/freertos_hw_virt_loongarch64
make run.loongarch64
```

### 运行 Linux 4 vCPU 示例

```bash
cd user/bail/examples/linux_4vcpu_1partion_loongarch64
make run.loongarch64
```

### 运行混合操作系统示例

```bash
cd user/bail/examples/mix_os_demo_loongarch64
make run.loongarch64
```

### 运行 Virtio 示例

```bash
cd user/bail/examples/virtio_linux_demo_2p_loongarch64
make run.loongarch64
```

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
| `freertos_para_virt_loongarch64` | FreeRTOS 半虚拟化 |
| `freertos_hw_virt_loongarch64` | 走 LVZ shim 路径的 FreeRTOS 变体 |
| `linux_4vcpu_1partion_loongarch64` | 4 vCPU Linux 客户机（半虚拟化） |
| `mix_os_demo_loongarch64` | 混合 OS：Linux + FreeRTOS 同时运行 |
| `virtio_linux_demo_2p_loongarch64` | 两个 Linux 分区之间的 VirtIO 网络通信 |

## 构建 Linux 客户机

`linux_4vcpu_1partion_loongarch64` 示例需要预先构建好的 `vmlinux` 和内嵌的 Buildroot rootfs。详细步骤如下：

### 1. 构建 Buildroot rootfs

```bash
cd /path/to/buildroot
make qemu_loongarch64_virt_efi_defconfig
make menuconfig          # 可选调整
make -j$(nproc)
# 输出 output/images/rootfs.cpio
```

### 2. 构建 Linux 内核

```bash
cd /path/to/linux-6.19.9
make ARCH=loongarch CROSS_COMPILE=loongarch64-linux-gnu- loongson64_defconfig
make ARCH=loongarch CROSS_COMPILE=loongarch64-linux-gnu- menuconfig
# 设置：
#   General setup -> Initial RAM filesystem and RAM disk support -> Initramfs source file(s):
#     /path/to/buildroot/output/images/rootfs.cpio
#   Device Drivers -> Input device support -> Hardware I/O ports -> i8042 PC Keyboard controller: [ ]
#   Device Drivers -> Input device support -> Keyboards -> AT keyboard: [ ]
#   Device Drivers -> Input device support -> Mice -> PS/2 mouse: [ ]
#   Kernel hacking -> printk and dmesg options -> Enable dynamic printk() support: [ ]
#   Boot options -> Built-in kernel command string:
#     console=ttyS0,115200 earlycon mem=512M@0x80000000 i8042.noaux i8042.nokbd i8042.nopnp
#   Boot options -> Built-in command line override (CONFIG_CMDLINE_FORCE): [*]

make ARCH=loongarch CROSS_COMPILE=loongarch64-linux-gnu- vmlinux -j$(nproc)
```

### 3. 把 `vmlinux` 拷入示例并运行

```bash
cp /path/to/linux-6.19.9/vmlinux \
   user/bail/examples/linux_4vcpu_1partion_loongarch64/vmlinux

cd user/bail/examples/linux_4vcpu_1partion_loongarch64
make clean && make
make run.loongarch64
```

预期：
```
Welcome to Buildroot
(none) login:
```

## 构建 U-Boot（参考）

> **说明**：PRTOS 在 LoongArch64 上**不**使用 U-Boot 作为引导加载器，而是使用位于 `user/bootloaders/rsw/loongarch64/` 的 RSW（Resident Software）引导桩。但如果您需要在物理 LoongArch 硬件上部署并需要 U-Boot，可以按以下方式构建：

```bash
cd /path/to/u-boot

# 配置 LoongArch64 virt 平台
make loongarch64_generic_defconfig

# 构建
make CROSS_COMPILE=loongarch64-linux-gnu- -j$(nproc)
# 生成 u-boot.bin
```

对于基于 QEMU 的 PRTOS 开发，无需 U-Boot —— RSW 引导桩由 QEMU 的 `-kernel` 选项直接加载。

## QEMU 运行命令参考

```bash
qemu-system-loongarch64 \
    -machine virt \
    -cpu la464 \
    -smp 4 \
    -m 4G \
    -accel tcg,thread=multi \
    -nographic -no-reboot \
    -kernel resident_sw \
    -monitor none \
    -serial stdio
```

| 选项 | 描述 |
|---|---|
| `-machine virt` | LoongArch 通用 `virt` 机器型号 |
| `-cpu la464` | Loongson 3A5000 等同的 CPU 模型 |
| `-smp 4` | 4 个逻辑 CPU |
| `-m 4G` | 4 GB 物理内存 |
| `-accel tcg,thread=multi` | 多线程 TCG 加速 |
| `-kernel resident_sw` | 直接加载 PRTOS RSW（无需 U-Boot） |
| `-nographic` | 串口控制台复用到 stdio |

## 启动流程

1. QEMU 在 LoongArch 复位向量处以 DA（Direct Address）模式跳入 RSW（`user/bootloaders/rsw/loongarch64/`）。
2. RSW 解析 PRTOS 容器镜像，加载：
   - PRTOS Hypervisor 核心
   - 所有配置的分区镜像（内核、BAIL 程序等）
3. RSW 把控制权交给 Hypervisor 入口，由其建立 CSR、TLB refill 处理程序与 per-CPU 状态。
4. PRTOS 按 XML 中的循环调度计划运行各分区。
5. 各分区从 PLV3 开始执行；特权操作陷入 Hypervisor 进行模拟。

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

- 阅读 [LoongArch64 API 参考手册](loongarch64_api_reference_zh.md) 了解 Hypercall API 详情
- 浏览 [PRTOS 技术报告](../design/prtos_tech_report_zh.md)
- 深入学习《[嵌入式 Hypervisor：架构、原理与应用](http://www.prtos.org/embedded_hypervisor_book/)》
