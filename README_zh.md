[English](README.md) | **中文** 

## 简介

PRTOS是一款轻量级、开源嵌入式嵌入式Type 1 Hypervisor，旨在通过虚拟化技术实现安全高效的嵌入式实时系统。它允许不同的应用程序通过分区的时间和空间隔离来共享相同的硬件平台。PRTOS对系统资源进行分区虚拟化，包括CPU、内存和I/O设备，以确保系统安全，并防止应用程序之间的干扰。PRTOS采用分离内核架构实现，其中PRTOS内核在特权模式下运行，并为硬件和分区之间提供抽象层。

PRTOS的分区被定义为独立的执行环境，可以独立且安全地运行。它们按照预定义的循环调度表进行调度，分区之间的通信通过消息传递实现。PRTOS支持两种类型的分区：系统分区和标准分区，只有系统分区具有控制系统和其他分区状态的能力。

PRTOS还提供了多种其他功能，如细粒度的错误检测系统、故障管理、高级故障分析技术和高度可配置的健康监测系统。它还提供了一个跟踪服务，用于实时调试分区和监视系统行为。

PRTOS Hypervisor 站在巨人们的肩膀上实现，主要借鉴了一些经典开源软件项目，比如[XtratuM](https://en.wikipedia.org/wiki/XtratuM), [Xen Hypervisor](https://xenproject.org/),  [Lguest Hypervisor](http://lguest.ozlabs.org), 以及[Linux Kernel](https://www.linux.org/ )等开源软件项目，正因为如此PRTOS Hypervisor 以GPL许可证的方式发布, 同时一本与之配套的图书[《嵌入式Hypervisor：架构、原理与应用》](https://item.jd.com/10106992272683.html)详细介绍PRTOS Hypervisor的设计与实现技术，以方便大家更好的理解PRTOS Hypervisor，同时也希望借此形成一个PRTOS Hypervisor的开放社区，让更多的对Hypervisor感兴趣的同学和爱好者参与进来，以促进PRTOS Hypervisor的健康演化。尤其是后续对ARM-V8，RISC-V架构的支持，以及更多分区应用的适配。

## **PRTOS Hypervisor架构**

PRTOS Hypervisor架构如下：

![architecturezh](./doc/figures/prtos_architecture_zh.jpg)

## PRTOS Hypervisor的特点

- 实时性能：PRTOS Hypervisor专门为实时和安全关键应用程序设计，确保提供服务的确定性和可预测性。
- 分区和隔离：PRTOS将资源（如CPU、内存和设备）划分为单独的分区。每个分区可以运行自己的实时操作系统和应用程序，确保隔离和容错。
- 小内存占用：PRTOS Hypervisor具有较小的内存占用，适用于资源有限的嵌入式系统。
- 分区间通信：PRTOS Hypervisor提供了分区间通信的机制，允许分区交换数据并同步其活动。

## **代码目录**

PRTOS Hypervisor源代码目录结构如下图所示：

| 名称          | 描述                                                    |
| ------------  | -------------------------------------------------------|
| core          | PRTOS Hypervisor核心源码。                              |
| scripts       | 配置PRTOS Hypervisor的辅助工具。                         |
| user          | 用户级别工具。                                           |
| user/bail     | 用户裸机应用程序接口库。                                  |
| doc           | 相关文档。                                               |

**NOTE**:BAIL（Bare-metal Application Interface Library）是一个用于在PRTOS Hypervisor之上直接开发C程序的最小分区开发环境。BAIL提供了建立基本的"C"执行环境所需的基本服务。BAIL适用于那些不需要操作系统的分区、以及测试PROTS提供的超级调用服务接口。

## **硬件支持**

- [x] QEMU 32位 X86平台
- [x] QEMU ARMv8 仿真平台
- [x] QEMU RISC-V 仿真平台

**计划支持平台**
- [x] QEMU 64bit X86 platform(AMD64)
- [x] 树莓派4b/5b单板机

## **开发和运行环境搭建**

### 公共依赖包安装 (通用构建工具)
```
# 基础编译工具、版本控制与脚本环境
sudo apt-get update
sudo apt-get install -y build-essential make perl gawk git subversion \
bc bison flex cpio unzip rsync wget makeself xorriso mtools \
python3-dev python3-setuptools python3-libxml2 \
libncurses5-dev libncurses-dev libssl-dev libxml2-dev libxml2-utils libgnutls28-dev \
gdb-multiarch
```
### x86 (i386/32位) 平台
#### x86 (i386/32位) 平台依赖包安装

```
# 32位交叉编译支持、多架构库及 i386 模拟器
sudo apt-get install -y gcc-multilib g++-multilib qemu-system-i386 \
grub-pc-bin
```
#### PRTOS 编译和运行`helloworld`示例
```
cd prtos-hypervisor
cp prtos_config.x86 prtos_config
make defconfig
make
cd user/bail/examples/helloworld
make run.aarch64
```
输出如下:
```
P0 ("Partition0":0:1) flags: [ SYSTEM ]:
    [0x6000000:0x6000000 - 0x60fffff:0x60fffff] flags: 0x0
[0] Hello World!
[0] Verification Passed
[HYPERCALL] (0x0) Halted

```

### ARMv8 (AARCH64) 平台
#### ARMv8 (AARCH64) 平台依赖包安装

```
# ARM64 交叉编译器及 AARCH64 模拟器
sudo apt-get install -y gcc-aarch64-linux-gnu qemu-system-aarch64
sudo apt-get install u-boot-tools

```
#### PRTOS 编译和运行`linux`示例

```
git clone https://github.com/prtos-project/prtos-hypervisor.git
cd prtos-hypervisor
cp prtos_config.aarch64 prtos_config
make defconfig
make
cd user/bail/examples/linux_4vcpu_1partion
make run.aarch64
```
以用户名`root`, 密码`1234`登录，输出如下：
```

Welcome to Buildroot
buildroot login: root
Password:
# uname -a
Linux buildroot 6.19.9-gc017ea9ffcd3 #2 SMP PREEMPT Sun Mar 22 19:36:34 CST 2026 aarch64 GNU/Linux
#
# cat /proc/cpuinfo | grep processor
processor       : 0
processor       : 1
processor       : 2
processor       : 3
#

```

### RISC-V 64 平台
#### RISC-V 64 平台依赖包安装
```
# RISC-V 交叉编译器、设备树工具及专用模拟器
sudo apt-get install -y gcc-riscv64-linux-gnu device-tree-compiler qemu-system-misc

```
#### PRTOS 编译和运行`linux`示例

```
git clone https://github.com/prtos-project/prtos-hypervisor.git
cd prtos-hypervisor
cp prtos_config.riscv64 prtos_config
make defconfig
make
cd user/bail/examples/linux_4vcpu_1partion_riscv64
make run.riscv64
```
以用户名`root`, 密码`1234`登录，输出如下：
```

Welcome to Buildroot
buildroot login: root
Password:
# uname -a
Linux buildroot 6.19.9-g2ca376b91049 #1 SMP Fri Mar 27 20:55:17 CST 2026 riscv64 GNU/Linux
# cat /proc/cpuinfo |grep processor
processor       : 0
processor       : 1
processor       : 2
processor       : 3
#

```

### 自动运行所有平台测试集的命令：
```
chenweis@chenweis-M9-PRO:~/aarch64_port/prtos-hypervisor$ bash scripts/run_test.sh -h
Usage:
run_test.sh [options] <command>

Options:
  -h|--help              Display this help and exit.
  --arch <x86|aarch64>   Target architecture (default: x86).

Commands:
  check-<case>           Check a specific test case.
                         Available: helloworld, helloworld_smp,
                         example.001 ~ example.009,
                         freertos_para_virt (aarch64 only),
                         freertos_hw_virt (aarch64 only),
                         linux (aarch64 only),
                         linux_4vcpu_1partion (aarch64 only),
                         linux_4vcpu_1partion_riscv64 (riscv64 only),
                         mix_os_demo1 (aarch64 only),
                         mix_os_demo_riscv64 (riscv64 only)
  check-all              Check all test cases.

Examples:
  run_test.sh check-all                     # Run all x86 tests
  run_test.sh --arch aarch64 check-all      # Run all AArch64 tests
  run_test.sh --arch aarch64 check-001      # Run single AArch64 test
  run_test.sh check-helloworld              # Run x86 helloworld test

```

### 期望对应输出如：
The test report of run `bash scripts/run_test.sh --arch x86 check-all` should be:

```
======================================
  Test Report [x86]
======================================
  example.001          PASS
  example.002          PASS
  example.003          PASS
  example.004          PASS
  example.005          PASS
  example.006          PASS
  example.007          PASS
  example.008          PASS
  example.009          PASS
  helloworld           PASS
  helloworld_smp       PASS
  freertos_para_virt   SKIP
  freertos_hw_virt     SKIP
  freertos_para_virt_riscv SKIP
  freertos_hw_virt_riscv SKIP
  linux                SKIP
  linux_4vcpu_1partion SKIP
  linux_4vcpu_1partion_riscv64 SKIP
  mix_os_demo1         SKIP
  mix_os_demo_riscv64  SKIP
--------------------------------------
  Total: 20  Pass: 11  Fail: 0  Skip: 9
======================================

```

The test report of run `bash scripts/run_test.sh --arch aarch64 check-all` should be:
```

======================================
  Test Report [aarch64]
======================================
  example.001          PASS
  example.002          PASS
  example.003          PASS
  example.004          PASS
  example.005          PASS
  example.006          PASS
  example.007          PASS
  example.008          PASS
  example.009          PASS
  helloworld           PASS
  helloworld_smp       PASS
  freertos_para_virt   PASS
  freertos_hw_virt     PASS
  freertos_para_virt_riscv SKIP
  freertos_hw_virt_riscv SKIP
  linux                PASS
  linux_4vcpu_1partion PASS
  linux_4vcpu_1partion_riscv64 SKIP
  mix_os_demo1         PASS
  mix_os_demo_riscv64  SKIP
--------------------------------------
  Total: 20  Pass: 16  Fail: 0  Skip: 4
======================================
```

The test report of run `bash scripts/run_test.sh --arch riscv64 check-all` should be:
```
======================================
  Test Report [riscv64]
======================================
  example.001          PASS
  example.002          PASS
  example.003          PASS
  example.004          PASS
  example.005          PASS
  example.006          PASS
  example.007          PASS
  example.008          PASS
  example.009          PASS
  helloworld           PASS
  helloworld_smp       PASS
  freertos_para_virt   SKIP
  freertos_hw_virt     SKIP
  freertos_para_virt_riscv PASS
  freertos_hw_virt_riscv PASS
  linux                SKIP
  linux_4vcpu_1partion SKIP
  linux_4vcpu_1partion_riscv64 PASS
  mix_os_demo1         SKIP
  mix_os_demo_riscv64  PASS
--------------------------------------
  Total: 20  Pass: 15  Fail: 0  Skip: 5
======================================
```

[PRTOS用户手册](http://www.prtos.org/prtos_hypervisor_x86_user_guide/)

# 资源文档

## 文档

[文档中心](http://www.prtos.org ) | [编程指南](http://www.prtos.org/prtos_hypervisor_x86_user_guide/)

## 例程

[裸机应用示例](http://www.prtos.org/prtos_hypervisor_x86_user_guide/)  | [虚拟化Linux示例](https://github.com/prtos-project/prtos-demo/tree/main/partition_linux ) | [虚拟化uC/OS-II示例](https://github.com/prtos-project/prtos-demo/tree/main/partition_ucosii) | [虚拟化Linux内核源](https://github.com/prtos-project/prtos-linux-3.4.4) | [PRTOS Hypervisor API参考手册](http://www.prtos.org )


# 社区支持

PRTOS Hypervisor非常感谢所有社区小伙伴的支持，在阅读和开发PRTOS Hypervisor的过程中若您有任何的想法，建议或疑问都可通过以下方式联系到 PRTOS Hypervisor，我们也实时在这些频道更新PRTOS Hypervisor的最新讯息。同时，任何问题都可以在 [论坛](https://github.com/prtos-project/prtos-hypervisor/issues)或者[官网]( http://www.prtos.org) 中提出，社区成员将回答这些问题。


# 贡献代码

如果您对PRTOS Hypervisor感兴趣，并希望参与PRTOS的开发并成为代码贡献者，请参阅[代码贡献指南](doc/contribution_guide/contribution_guide_zh.md)。
