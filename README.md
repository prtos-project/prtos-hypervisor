**English** | [中文](README_zh.md)

## Introduction
------------

**PRTOS Hypervisor** is an open-source, lightweight embedded Type-1 Hypervisor dedicated to building secure and efficient real-time systems through virtualization technology. Utilizing a **Separation Kernel** architecture, PRTOS enables multiple applications to coexist and collaborate securely on a single hardware platform through strict spatial and temporal partitioning. By providing deep virtualization of critical resources—including CPU, memory, and I/O devices—PRTOS ensures system security while completely eliminating mutual interference between applications.

Regarding platform compatibility, PRTOS fully leverages hardware-assisted virtualization extensions on ARMv8 (AArch64), AMD64 (x86_64), and RISC-V (RV64). Furthermore, it provides comprehensive Para-virtualization support for 32-bit x86 as well as the three major 64-bit platforms mentioned above, offering exceptional flexibility for diverse deployment scenarios.

In PRTOS, partitions are defined as independent execution environments that operate in isolation. These partitions are managed via a predefined cyclic scheduling table for time-division multiplexing, with inter-partition communication (IPC) handled through an efficient message-passing mechanism. The system supports two types of partitions: System Partitions and Standard Partitions, where System Partitions possess the privileged authority to manage global states and other partitions. Additionally, PRTOS integrates a suite of advanced features, including fine-grained error detection, fault management, sophisticated diagnostic techniques, and a highly configurable health monitoring system, alongside real-time tracing services for in-depth debugging and system behavior monitoring.

Adhering to the open-source spirit of "standing on the shoulders of giants," PRTOS draws significant technical inspiration from esteemed projects such as  [XtratuM](https://en.wikipedia.org/wiki/XtratuM), [Xen Hypervisor](https://xenproject.org/), [Lguest Hypervisor](http://lguest.ozlabs.org), and [Linux Kernel](https://www.linux.org/). Consequently, PRTOS is released under the GPL license. To assist developers in exploring its inner workings, the companion book,  [Embedded Hypervisor: Architecture, Principles, and Implemenation](https://item.jd.com/10106992272683.html), provides a detailed exposition of the design and implementation of PRTOS. We cordially invite technology enthusiasts to join the PRTOS open-source community to collectively drive the evolution of the system, particularly in advancing support for ARMv8 and RISC-V architectures and adapting a wider range of partitioned applications.


## PRTOS Hypervisor Architecture

PRTOS is a lightweight real-time hypervisor，Its architecture is as follows:

![architecturezh](./doc/figures/prtos_architecture_zh.jpg)


## PRTOS Hypervisor Features

 - Real-time capabilities: PRTOS Hypervisor is specifically designed for real-time and safety-critical applications, providing deterministic and predictable execution of tasks.

 - Partitioning and isolation: The hypervisor allows for the partitioning of resources, such as CPU, memory, and devices, into separate domains or partitions. Each partition can run its own real-time operating system and applications, ensuring isolation and fault containment.

 - Minimal footprint: PRTOS Hypervisor has a small memory footprint, making it suitable for resource-constrained embedded systems.

 - static resources configuration: PRTOS Hypervisor supports static configuration of partitions, resources are statically partitioned and assigned at VM instantiation time.

 - Inter-partition communication: PRTOS Hypervisor provides mechanisms for inter-partition communication, allowing partitions to exchange data and synchronize their activities.


**Currently supported platforms**
- [x] QEMU 32bit X86 platform
- [x] QEMU ARMv8 virt platform
- [x] QEMU RISC-V virt platform
- [x] QEMU 64bit X86 platform (AMD64)

**Plan to support platforms**
- [x] Raspberry Pi 4b/5b Single-board Computer


**PRTOS Hypervisor directory structure**
| Name          | Description                                             |
| ------------- | ------------------------------------------------------- |
| core          | The source code of the PRTOS Hypervisor.                |
| scripts       | The assist tools to configure PRTOS source code.        |
| user          | User space utilities (libprtos, tools, examples, etc).  |
| user/bail     | User' Bare-metal Application Interface Library.         |
| doc           | PRTOS related documents.                                 |

**NOTE**:BAIL(Bare-metal Application Interface Library) is a minimal partition developing environment for the development of "C" programs directly on top of PRTOS hypervisor. BAIL provides the basic and minimal services to setup a basic "C" execution environment. BAIL is useful for those partitions that are do not need an operating systems and want to do function test for PRTOS hypercall APIs.


# Getting Started


## **Development and Runtime Environment Setup**

### Common Dependencies Installation
```
# Basic compilation tools, version control, and scripting environment
sudo apt-get update
sudo apt-get install -y build-essential make perl gawk git subversion \
bc bison flex cpio unzip rsync wget makeself xorriso mtools \
python3-dev python3-setuptools python3-libxml2 \
libncurses5-dev libncurses-dev libssl-dev libxml2-dev libxml2-utils libgnutls28-dev \
gdb-multiarch
```
### x86 (i386/32-bit) Platform
#### Dependency Installation for x86 (i386/32-bit)

```
# 32-bit cross-compilation support, multi-arch libraries, and i386 emulator
sudo apt-get install -y gcc-multilib g++-multilib qemu-system-i386 \
grub-pc-bin
```

#### Compiling and Running PRTOS `helloworld` Example
```
cd prtos-hypervisor
cp prtos_config.x86 prtos_config
make defconfig
make
cd user/bail/examples/helloworld
make run.x86
```

The expected output is as follows:

```
P0 ("Partition0":0:1) flags: [ SYSTEM ]:
    [0x6000000:0x6000000 - 0x60fffff:0x60fffff] flags: 0x0
[0] Hello World!
[0] Verification Passed
[HYPERCALL] (0x0) Halted

```

### ARMv8 (AARCH64) Platform
#### Dependency Installation for ARMv8 (AARCH64)

```
sudo apt-get install -y gcc-aarch64-linux-gnu qemu-system-aarch64
sudo apt-get install u-boot-tools

```
#### Compiling and Running PRTOS `linux-smp` partition with 4 `vCPU`

```
git clone https://github.com/prtos-project/prtos-hypervisor.git
cd prtos-hypervisor
cp prtos_config.aarch64 prtos_config
make defconfig
make
cd user/bail/examples/linux_4vcpu_1partion
make run.aarch64
```
Log in with username `root` and password `1234`. The output is as follows:

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

### RISC-V 64 Platform
#### Dependency Installation for RISC-V 64

```
sudo apt-get install -y gcc-riscv64-linux-gnu device-tree-compiler qemu-system-misc

```

#### Compiling and Running PRTOS `linux-smp` partition with 4 `vCPU`
```
git clone https://github.com/prtos-project/prtos-hypervisor.git
cd prtos-hypervisor
cp prtos_config.riscv64 prtos_config
make defconfig
make
cd user/bail/examples/linux_4vcpu_1partion_riscv64
make run.riscv64
```
Log in with username `root` and password `1234`. The output is as follows:

```
Welcome to Buildroot
buildroot login: root
Password:
#uname -a
Linux buildroot 6.19.9-g2ca376b91049 #1 SMP Fri Mar 27 20:55:17 CST 2026 riscv64 GNU/Linux
#cat /proc/cpuinfo |grep processor
processor       : 0
processor       : 1
processor       : 2
processor       : 3

```

### AMD64 (x86_64) Platform
#### Dependency Installation for AMD64 (x86_64)

```
sudo apt-get install -y qemu-system-x86 grub-pc-bin

```

#### Compiling and Running PRTOS `linux-smp` partition with 4 `vCPU`
```
git clone https://github.com/prtos-project/prtos-hypervisor.git
cd prtos-hypervisor
cp prtos_config.amd64 prtos_config
make defconfig
make
cd user/bail/examples/linux_4vcpu_1partion_amd64
make run.amd64
```
Log in with username `root` and password `1234`. The output is as follows:

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

## **Commands to Automatically Run Test Suites for All Platforms**
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
                         mix_os_demo_amd64 (amd64 only)
  check-all              Check all test cases.

Examples:
  run_test.sh check-all                     # Run all x86 tests
  run_test.sh --arch aarch64 check-all      # Run all AArch64 tests
  run_test.sh --arch aarch64 check-001      # Run single AArch64 test
  run_test.sh check-helloworld              # Run x86 helloworld test

```
## **Expected Test Reports**


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
--------------------------------------
  Total: 24  Pass: 11  Fail: 0  Skip: 13
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
--------------------------------------
  Total: 24  Pass: 16  Fail: 0  Skip: 8
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
--------------------------------------
  Total: 24  Pass: 15  Fail: 0  Skip: 9
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
--------------------------------------
  Total: 24  Pass: 15  Fail: 0  Skip: 9
======================================

```

[PRTOS Hypervisor Programming Guide](http://www.prtos.org/prtos_hypervisor_x86_user_guide/) | [PRTOS Hypervisor Samples](https://github.com/prtos-project/prtos-hypervisor/tree/main/user/bail/examples)

[Debug the PRTOS Hypervisor and assistant tools](doc/debug/how_to_debug_prtos_hypervisor_and_assistant_tools.md)

# Community

PRTOS Hypervisor is very grateful for the support from all community developers, and if you have any ideas, suggestions or questions in the process of using PRTOS Hypervisor, PRTOS Hypervisor can be reached by the following means, and we are also updating PRTOS Hypervisor in real time on these channels. At the same time, any questions can be asked in the [issue section of PRTOS Hypervisor repository](https://github.com/prtos-project/prtos-hypervisor/issues) or [PRTOS Hypervisor forum](http://www.prtos.org), and community members will answer them.

# Contribution

If you are interested in PRTOS Hypervisor and want to join in the development of PRTOS Hypervisor and become a code contributor,please refer to the [Code Contribution Guide](doc/contribution_guide/contribution_guide.md).
