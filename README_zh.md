[English](README.md) | **中文**
## 1. 简介

**PRTOS Hypervisor** 是一款开源、轻量级的嵌入式 Type-1（裸金属）Hypervisor，采用 **分离内核（Separation Kernel）** 架构，专为实时与安全关键系统设计。通过严格的空间隔离与时间隔离，PRTOS 在单一硬件平台上实现多应用的安全共存与高效协作，完全消除应用间的相互干扰。

PRTOS 的核心设计原则是 **确定性与静态配置**：CPU、内存、I/O 设备等关键资源在系统实例化时静态分配，调度采用预定义的循环调度表（Cyclic Scheduling Table），这使得系统行为完全可预测、可分析、可验证。关于这一设计原则的理论基础和工程实现，可参阅《[嵌入式 Hypervisor：架构、原理与应用](http://www.prtos.org/embedded_hypervisor_book/)》的详细论述。

PRTOS 项目遵循开源精神，在技术上吸收了 [XtratuM](https://en.wikipedia.org/wiki/XtratuM)、[Xen Hypervisor](https://xenproject.org/)、[Lguest Hypervisor](http://lguest.ozlabs.org) 及 [Linux Kernel](https://www.linux.org/) 的设计理念，以 GPL 许可证发布。

## 2. PRTOS Hypervisor 架构

PRTOS Hypervisor 架构如下：

![architecturezh](./doc/figures/prtos_architecture_zh.jpg)

## 3. PRTOS Hypervisor 的特点

- 实时性能：PRTOS Hypervisor专门为实时和安全关键应用程序设计，确保提供服务的确定性和可预测性。
- 分区和隔离：PRTOS将资源（如CPU、内存和设备）划分为单独的分区。每个分区可以运行自己的实时操作系统和应用程序，确保隔离和容错。
- 小内存占用：PRTOS Hypervisor具有较小的内存占用，适用于资源有限的嵌入式系统。
- 分区间通信：PRTOS Hypervisor提供了分区间通信的机制，允许分区交换数据并同步其活动。

## 4. 代码目录

PRTOS Hypervisor源代码目录结构如下图所示：

| 名称          | 描述                                                    |
| ------------  | -------------------------------------------------------|
| core          | PRTOS Hypervisor核心源码。                              |
| scripts       | 配置PRTOS Hypervisor的辅助工具。                         |
| user/tools    | 用户级别工具。                                           |
| user/bail     | 用户裸机应用程序接口库。                                  |
| doc           | 相关文档。                                               |

> **说明**：BAIL（Bare-metal Application Interface Library）是一个用于在 PRTOS Hypervisor 之上直接开发 C 程序的最小分区开发环境。BAIL 提供了建立基础 C 执行环境所需的基本服务，适用于不需要操作系统、仅需测试 PRTOS 超级调用接口的分区。

## 5. 硬件支持

- [x] QEMU 32位 X86平台
- [x] QEMU ARMv8 仿真平台
- [x] QEMU RISC-V 仿真平台
- [x] QEMU 64位 X86平台 (AMD64)
- [x] QEMU LoongArch64 仿真平台
**计划支持平台**
- [x] 树莓派4b/5b单板机

## 6. 开发和运行环境搭建

### 6.1 公共依赖包安装 (通用构建工具)
```
# 基础编译工具、版本控制与脚本环境
sudo apt-get update
sudo apt-get install -y build-essential make perl gawk git subversion \
bc bison flex cpio unzip rsync wget makeself xorriso mtools \
python3-dev python3-setuptools python3-libxml2 \
libncurses5-dev libncurses-dev libssl-dev libxml2-dev libxml2-utils libgnutls28-dev \
gdb-multiarch
```
### 6.2 x86 (i386/32位) 平台
#### 6.2.1 x86 (i386/32位) 平台依赖包安装

```
# 32位交叉编译支持、多架构库及 i386 模拟器
sudo apt-get install -y gcc-multilib g++-multilib qemu-system-i386 \
grub-pc-bin
```
#### 6.2.2 PRTOS 编译和运行 `helloworld` 示例
```
git clone https://github.com/prtos-project/prtos-hypervisor.git
cd prtos-hypervisor
cp prtos_config.x86 prtos_config
make defconfig
make
cd user/bail/examples/helloworld
make run.x86
```
输出如下:
```
P0 ("Partition0":0:1) flags: [ SYSTEM ]:
    [0x6000000:0x6000000 - 0x60fffff:0x60fffff] flags: 0x0
[0] Hello World!
[0] Verification Passed
[HYPERCALL] (0x0) Halted

```

### 6.3 ARMv8 (AArch64) 平台
#### 6.3.1 ARMv8 (AArch64) 平台依赖包安装

```
# ARM64 交叉编译器及 AARCH64 模拟器
sudo apt-get install -y gcc-aarch64-linux-gnu qemu-system-aarch64
sudo apt-get install u-boot-tools

```
#### 6.3.2 PRTOS 编译和运行 `linux` 示例

```
git clone https://github.com/prtos-project/prtos-hypervisor.git
cd prtos-hypervisor
cp prtos_config.aarch64 prtos_config
make defconfig
make
cd user/bail/examples/linux_4vcpu_1partion_aarch64/
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

### 6.4 RISC-V 64 平台
#### 6.4.1 RISC-V 64 平台依赖包安装
```
# RISC-V 交叉编译器、设备树工具及专用模拟器
sudo apt-get install -y gcc-riscv64-linux-gnu device-tree-compiler qemu-system-misc

```
#### 6.4.2 PRTOS 编译和运行 `linux` 示例

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

### 6.5 AMD64 (x86_64) 平台
#### 6.5.1 AMD64 (x86_64) 平台依赖包安装

```
# AMD64 模拟器
sudo apt-get install -y qemu-system-x86 grub-pc-bin

```
> **说明**：
在 AMD64 硬件虚拟化测试中，QEMU 需要通过 `-enable-kvm` 选项利用宿主机的 `KVM` 模块进行加速。为确保测试顺利运行，请务必将当前用户加入 `kvm` 权限组：

```
# 授权并检查
sudo usermod -a -G kvm $USER
grep 'kvm' /etc/group  # 确认用户组状态
```

#### 6.5.2 PRTOS 编译和运行 `linux` 示例
```
git clone https://github.com/prtos-project/prtos-hypervisor.git
cd prtos-hypervisor
cp prtos_config.amd64 prtos_config
make defconfig
make
cd user/bail/examples/linux_4vcpu_1partion_amd64
make run.amd64.kvm.nographic
```
以用户名`root`, 密码`1234`登录，输出如下：
```

Welcome to Buildroot
buildroot login: root
Password:
# uname -a
Linux buildroot 6.19.9 #1 SMP PREEMPT amd64 GNU/Linux
#
# cat /proc/cpuinfo | grep processor
processor       : 0
processor       : 1
processor       : 2
processor       : 3
#

```

#### 6.5.3 PRTOS 编译和运行 `virtio_linux_demo_2p_amd64`（双 SMP Linux + Virtio 设备虚拟化）
```
cd prtos-hypervisor
cp prtos_config.amd64 prtos_config
make defconfig
make
cd user/bail/examples/virtio_linux_demo_2p_amd64
make run.amd64
```
该命令启动 QEMU 并提供三个访问方式：
- **终端**（当前窗口）：系统分区 COM1 登录（`root`/`1234`）
- **VNC** `vnc://localhost:5901`：客户分区 VGA 显示
- **Telnet** `telnet localhost 4321`：客户分区 COM2 登录（`root`/`1234`）

所有 virtio 服务通过 init 脚本自动启动（系统分区 `S99virtio_backend`，客户分区 `S99virtio_guest`），无需手动操作。

后端会检测客户分区停止（halt）状态，并自动断开 TCP 客户端连接，发送通知消息。

详细文档参见 `user/bail/examples/virtio_linux_demo_2p_amd64/README.md`。

#### 6.5.4 PRTOS 编译和运行 `virtio_linux_demo_2p_aarch64`（双 SMP Linux + Virtio 设备虚拟化）
```
cd prtos-hypervisor
cp prtos_config.aarch64 prtos_config
make defconfig
make
cd user/bail/examples/virtio_linux_demo_2p_aarch64
make run.aarch64
```
系统分区 PL011 UART 控制台通过 stdio 输出。用户名 `root`，密码 `1234`。

详细文档参见 `user/bail/examples/virtio_linux_demo_2p_aarch64/README.md`。

#### 6.5.5 PRTOS 编译和运行 `virtio_linux_demo_2p_riscv64`（双 SMP Linux + Virtio 设备虚拟化）
```
cd prtos-hypervisor
cp prtos_config.riscv64 prtos_config
make defconfig
make
cd user/bail/examples/virtio_linux_demo_2p_riscv64
make run.riscv64
```
系统分区 NS16550 UART 控制台通过 stdio 输出。用户名 `root`，密码 `1234`。

详细文档参见 `user/bail/examples/virtio_linux_demo_2p_riscv64/README.md`。

### 6.6 LoongArch64 平台
#### 6.6.1 LoongArch64 平台依赖包安装

```
# LoongArch64 交叉编译器与仿真器
sudo apt-get install -y gcc-loongarch64-linux-gnu qemu-system-loongarch64
```

> **说明**：LoongArch64 采用陷入模拟的半虚拟化方式。客户机 Linux 内核运行于 PLV3，所有特权 CSR / TLB / 定时器操作由 PRTOS Hypervisor 陷入并模拟。

#### 6.6.2 构建 Buildroot（rootfs）

```bash
cd /path/to/buildroot

# 以 QEMU LoongArch64 virt defconfig 为起点
make qemu_loongarch64_virt_efi_defconfig

# 根据需要调整配置
make menuconfig

make -j$(nproc)
```

生成的 cpio 镜像位于 `output/images/rootfs.cpio`。

#### 6.6.3 构建 Linux 内核

```bash
cd /path/to/linux-6.19.9

make ARCH=loongarch CROSS_COMPILE=loongarch64-linux-gnu- loongson64_defconfig
make ARCH=loongarch CROSS_COMPILE=loongarch64-linux-gnu- menuconfig
# 需要设置：
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

#### 6.6.4 LoongArch64 引导加载器

LoongArch64 在 PRTOS 上 **不** 使用 U-Boot。PRTOS 使用位于 `user/bootloaders/rsw/loongarch64/` 的自有 RSW (Resident Software) 引导加载器。RSW 是一个轻量级引导桩，负责：

1. 在复位后以直接地址 (DA) 模式运行
2. 解析 PRTOS 容器镜像
3. 加载 PRTOS Hypervisor 核心与各分区镜像
4. 将控制权交给 Hypervisor

QEMU 的 `-kernel resident_sw` 选项直接加载 RSW，后者完成 Hypervisor 与全部分区的启动。

#### 6.6.5 PRTOS 编译和运行 4 vCPU `linux-smp` 示例

```
git clone https://github.com/prtos-project/prtos-hypervisor.git
cd prtos-hypervisor
cp prtos_config.loongarch64 prtos_config
make defconfig
make

# 复制 Linux 内核到示例目录
cp /path/to/linux-6.19.9/vmlinux \
   user/bail/examples/linux_4vcpu_1partion_loongarch64/

cd user/bail/examples/linux_4vcpu_1partion_loongarch64
make clean && make
make run.loongarch64
```

输出如下：

```
Welcome to Buildroot
(none) login:
```

#### 6.6.6 PRTOS 编译和运行 `virtio_linux_demo_2p_loongarch64`（双 SMP Linux + Virtio 设备虚拟化）

```
cd prtos-hypervisor
cp prtos_config.loongarch64 prtos_config
make defconfig
make
cd user/bail/examples/virtio_linux_demo_2p_loongarch64
make run.loongarch64
```

系统分区 UART 控制台通过 stdio 输出。用户名 `root`，密码 `1234`。

详细文档参见 `user/bail/examples/virtio_linux_demo_2p_loongarch64/README.md`。

## 7. 测试

### 7.1 自动运行所有平台测试集的命令
```
bash scripts/run_test.sh -h
Usage:
run_test.sh [options] <command>

Options:
  -h|--help              Display this help and exit.
  --arch <x86|aarch64>   Target architecture (default: x86).

Commands:
  check-<case>           Check a specific test case.
                         Available: helloworld, helloworld_smp,
                         example.001 ~ example.009,
                         freertos_para_virt_aarch64 (aarch64 only),
                         freertos_hw_virt_aarch64 (aarch64 only),
                         linux_aarch64 (aarch64 only),
                         linux_4vcpu_1partion_aarch64 (aarch64 only),
                         linux_4vcpu_1partion_riscv64 (riscv64 only),
                         linux_4vcpu_1partion_amd64 (amd64 only),
                         mix_os_demo_aarch64 (aarch64 only),
                         mix_os_demo_riscv64 (riscv64 only),
                         mix_os_demo_amd64 (amd64 only),
                         freertos_para_virt_loongarch64 (loongarch64 only),
                         freertos_hw_virt_loongarch64 (loongarch64 only),
                         linux_4vcpu_1partion_loongarch64 (loongarch64 only),
                         mix_os_demo_loongarch64 (loongarch64 only),
                         virtio_linux_demo_2p_loongarch64 (loongarch64 only)
  check-all              Check all test cases.

Examples:
  run_test.sh check-all                     # Run all x86 tests
  run_test.sh --arch aarch64 check-all      # Run all AArch64 tests
  run_test.sh --arch aarch64 check-001      # Run single AArch64 test
  run_test.sh check-helloworld              # Run x86 helloworld test
  run_test.sh --arch loongarch64 check-all  # Run all LoongArch64 tests

```

### 7.2 各平台期望测试报告
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
  freertos_para_virt_aarch64 SKIP
  freertos_hw_virt_aarch64 SKIP
  freertos_para_virt_riscv SKIP
  freertos_hw_virt_riscv SKIP
  freertos_para_virt_amd64 SKIP
  freertos_hw_virt_amd64 SKIP
  linux_aarch64        SKIP
  linux_4vcpu_1partion_aarch64 SKIP
  linux_4vcpu_1partion_riscv64 SKIP
  linux_4vcpu_1partion_amd64 SKIP
  mix_os_demo_aarch64  SKIP
  mix_os_demo_riscv64  SKIP
  mix_os_demo_amd64    SKIP
  virtio_linux_demo_2p_aarch64 SKIP
  virtio_linux_demo_2p_riscv64 SKIP
  virtio_linux_demo_2p_amd64 SKIP
--------------------------------------
  Total: 27  Pass: 11  Fail: 0  Skip: 16
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
  freertos_para_virt_aarch64 PASS
  freertos_hw_virt_aarch64 PASS
  freertos_para_virt_riscv SKIP
  freertos_hw_virt_riscv SKIP
  freertos_para_virt_amd64 SKIP
  freertos_hw_virt_amd64 SKIP
  linux_aarch64        PASS
  linux_4vcpu_1partion_aarch64 PASS
  linux_4vcpu_1partion_riscv64 SKIP
  linux_4vcpu_1partion_amd64 SKIP
  mix_os_demo_aarch64  PASS
  mix_os_demo_riscv64  SKIP
  mix_os_demo_amd64    SKIP
  virtio_linux_demo_2p_aarch64 PASS
  virtio_linux_demo_2p_riscv64 SKIP
  virtio_linux_demo_2p_amd64 SKIP
--------------------------------------
  Total: 27  Pass: 17  Fail: 0  Skip: 10
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
  freertos_para_virt_aarch64 SKIP
  freertos_hw_virt_aarch64 SKIP
  freertos_para_virt_riscv PASS
  freertos_hw_virt_riscv PASS
  freertos_para_virt_amd64 SKIP
  freertos_hw_virt_amd64 SKIP
  linux_aarch64        SKIP
  linux_4vcpu_1partion_aarch64 SKIP
  linux_4vcpu_1partion_riscv64 PASS
  linux_4vcpu_1partion_amd64 SKIP
  mix_os_demo_aarch64  SKIP
  mix_os_demo_riscv64  PASS
  mix_os_demo_amd64    SKIP
  virtio_linux_demo_2p_aarch64 SKIP
  virtio_linux_demo_2p_riscv64 PASS
  virtio_linux_demo_2p_amd64 SKIP
--------------------------------------
  Total: 27  Pass: 16  Fail: 0  Skip: 11
======================================
```

The test report of run `bash scripts/run_test.sh --arch amd64 check-all` should be:
```
======================================
  Test Report [amd64]
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
  freertos_para_virt_aarch64 SKIP
  freertos_hw_virt_aarch64 SKIP
  freertos_para_virt_riscv SKIP
  freertos_hw_virt_riscv SKIP
  freertos_para_virt_amd64 PASS
  freertos_hw_virt_amd64 PASS
  linux_aarch64        SKIP
  linux_4vcpu_1partion_aarch64 SKIP
  linux_4vcpu_1partion_riscv64 SKIP
  linux_4vcpu_1partion_amd64 PASS
  mix_os_demo_aarch64  SKIP
  mix_os_demo_riscv64  SKIP
  mix_os_demo_amd64    PASS
  virtio_linux_demo_2p_aarch64 SKIP
  virtio_linux_demo_2p_riscv64 SKIP
  virtio_linux_demo_2p_amd64 PASS
--------------------------------------
  Total: 27  Pass: 16  Fail: 0  Skip: 11
======================================
```

`bash scripts/run_test.sh --arch loongarch64 check-all` 的期望测试报告：
```
======================================
  Test Report [loongarch64]
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
  freertos_para_virt_aarch64 SKIP
  freertos_hw_virt_aarch64 SKIP
  freertos_para_virt_riscv SKIP
  freertos_hw_virt_riscv SKIP
  freertos_para_virt_amd64 SKIP
  freertos_hw_virt_amd64 SKIP
  linux_aarch64        SKIP
  linux_4vcpu_1partion_aarch64 SKIP
  linux_4vcpu_1partion_riscv64 SKIP
  linux_4vcpu_1partion_amd64 SKIP
  mix_os_demo_aarch64  SKIP
  mix_os_demo_riscv64  SKIP
  mix_os_demo_amd64    SKIP
  virtio_linux_demo_2p_aarch64 SKIP
  virtio_linux_demo_2p_riscv64 SKIP
  virtio_linux_demo_2p_amd64 SKIP
  freertos_para_virt_loongarch64 PASS
  freertos_hw_virt_loongarch64 PASS
  linux_4vcpu_1partion_loongarch64 PASS
  mix_os_demo_loongarch64 PASS
  virtio_linux_demo_2p_loongarch64 PASS
--------------------------------------
  Total: 32  Pass: 16  Fail: 0  Skip: 16
======================================
```

[PRTOS用户手册](http://www.prtos.org/prtos_hypervisor_x86_user_guide/)

## 8. 资源文档

### 8.1 文档

[文档中心](http://www.prtos.org ) | [编程指南](http://www.prtos.org/prtos_hypervisor_x86_user_guide/)

### 8.2 例程

[裸机应用示例](http://www.prtos.org/prtos_hypervisor_x86_user_guide/)  | [虚拟化Linux示例](https://github.com/prtos-project/prtos-demo/tree/main/partition_linux ) | [虚拟化uC/OS-II示例](https://github.com/prtos-project/prtos-demo/tree/main/partition_ucosii) | [虚拟化Linux内核源](https://github.com/prtos-project/prtos-linux-3.4.4) | [PRTOS Hypervisor API参考手册](http://www.prtos.org )


## 9. 社区支持

PRTOS Hypervisor非常感谢所有社区小伙伴的支持，在阅读和开发PRTOS Hypervisor的过程中若您有任何的想法，建议或疑问都可通过以下方式联系到 PRTOS Hypervisor，我们也实时在这些频道更新PRTOS Hypervisor的最新讯息。同时，任何问题都可以在 [论坛](https://github.com/prtos-project/prtos-hypervisor/issues)或者[官网]( http://www.prtos.org) 中提出，社区成员将回答这些问题。


## 10. 贡献代码

如果您对PRTOS Hypervisor感兴趣，并希望参与PRTOS的开发并成为代码贡献者，请参阅[代码贡献指南](doc/contribution_guide/contribution_guide_zh.md)。
