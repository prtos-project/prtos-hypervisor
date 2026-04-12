# x86 (32 位) 平台 — 快速上手指南

## 前置条件

- **操作系统**：Ubuntu 24.04 LTS（推荐）
- **工具链**：系统 GCC，需 32 位 multilib 支持

## 安装依赖

```bash
# 通用依赖
sudo apt-get update
sudo apt-get install -y build-essential make perl gawk git bc bison flex cpio \
    unzip rsync wget xorriso mtools python3-dev python3-libxml2 \
    libncurses5-dev libssl-dev libxml2-dev libxml2-utils gdb-multiarch

# x86 专用：32 位交叉编译支持和 QEMU
sudo apt-get install -y gcc-multilib g++-multilib qemu-system-i386 grub-pc-bin
```

## 虚拟化模式

x86 (i386) 平台使用**半虚拟化**模式。Guest 分区通过 Hypercall（超级调用）与 Hypervisor 交互，实现资源管理、I/O 操作和调度控制。此模式需要 Guest OS 适配 PRTOS 专用 API。

## 编译与运行

### 编译 PRTOS Hypervisor

```bash
git clone https://github.com/prtos-project/prtos-hypervisor.git
cd prtos-hypervisor
cp prtos_config.x86 prtos_config
make defconfig
make
```

### 运行 Hello World 示例

```bash
cd user/bail/examples/helloworld
make run.x86
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
make run.x86
```

### 运行全部测试

```bash
cd prtos-hypervisor
bash scripts/run_test.sh --arch x86 check-all
```

预期结果：11 Pass, 0 Fail, 16 Skip。

## 可用示例

| 示例 | 说明 |
|---|---|
| `helloworld` | 单分区基础 "Hello World" |
| `helloworld_smp` | SMP 双核双分区验证 |
| `example.001` | 定时器管理 (HW_CLOCK vs EXEC_CLOCK) |
| `example.002` | 基础分区执行 |
| `example.003` | 分区交互 |
| `example.004` | 分区间通信（采样端口、队列端口、共享内存） |
| `example.005` | IPC 端口 |
| `example.006` | 调度计划切换 |
| `example.007` | 健康管理 |
| `example.008` | 追踪服务 |
| `example.009` | 内存保护与共享内存访问 |

## QEMU 运行命令参考

```bash
qemu-system-i386 -m 512 -cdrom resident_sw.iso -serial stdio -boot d -smp 4
```

## XML 配置示例

```xml
<SystemDescription xmlns="http://www.prtos.org/prtos-1.x" version="1.0.0" name="helloworld">
    <HwDescription>
        <MemoryLayout>
            <Region type="ram" start="0x0" size="512MB" />
        </MemoryLayout>
        <ProcessorTable>
            <Processor id="0" frequency="200MHz">
                <CyclicPlanTable>
                    <Plan id="0" majorFrame="200ms">
                        <Slot id="0" start="0ms" duration="200ms" partitionId="0" />
                    </Plan>
                </CyclicPlanTable>
            </Processor>
        </ProcessorTable>
    </HwDescription>
    <PRTOSHypervisor console="Uart">
        <PhysicalMemoryArea size="8MB" />
    </PRTOSHypervisor>
    <PartitionTable>
        <Partition id="0" name="Partition0" flags="system">
            <PhysicalMemoryAreas>
                <Area start="0x6000000" size="1MB" />
            </PhysicalMemoryAreas>
            <TemporalRequirements period="200ms" duration="200ms" />
        </Partition>
    </PartitionTable>
</SystemDescription>
```

## 进阶阅读

- [x86 API 参考手册](x86_api_reference_zh.md)
- [PRTOS 技术报告](../design/prtos_tech_report.md)
- 《[嵌入式 Hypervisor：架构、原理与应用](http://www.prtos.org/embedded_hypervisor_book/)》
