**English** | [中文](README_zh.md)
## 1. Introduction

**PRTOS Hypervisor** is an open-source, lightweight embedded Type-1 (bare-metal) Hypervisor built on a **Separation Kernel** architecture, specifically designed for real-time and safety-critical systems. Through strict spatial and temporal partitioning, PRTOS enables multiple applications to coexist securely and collaborate efficiently on a single hardware platform, completely eliminating mutual interference between applications.

The core design principle of PRTOS is **determinism and static configuration**: critical resources such as CPU, memory, and I/O devices are statically allocated at system instantiation time, and scheduling follows a predefined Cyclic Scheduling Table, making system behavior fully predictable, analyzable, and verifiable. For the theoretical foundations and engineering implementation of this design principle, refer to *[Embedded Hypervisor: Architecture, Principles, and Implementation](http://www.prtos.org/embedded_hypervisor_book/)*.

PRTOS follows the open-source spirit, drawing technical inspiration from [XtratuM](https://en.wikipedia.org/wiki/XtratuM), [Xen Hypervisor](https://xenproject.org/), [Lguest Hypervisor](http://lguest.ozlabs.org), and [Linux Kernel](https://www.linux.org/), released under the GPL license.


## 2. PRTOS Hypervisor Architecture

PRTOS is a lightweight real-time hypervisor. Its architecture is as follows:

![architecturezh](./doc/figures/prtos_architecture_zh.jpg)


## 3. PRTOS Hypervisor Features

- Real-time capabilities: PRTOS Hypervisor is specifically designed for real-time and safety-critical applications, providing deterministic and predictable execution of tasks.
- Partitioning and isolation: The hypervisor allows for the partitioning of resources, such as CPU, memory, and devices, into separate domains or partitions. Each partition can run its own real-time operating system and applications, ensuring isolation and fault containment.
- Minimal footprint: PRTOS Hypervisor has a small memory footprint, making it suitable for resource-constrained embedded systems.
- Static resources configuration: PRTOS Hypervisor supports static configuration of partitions. Resources are statically partitioned and assigned at VM instantiation time.
- Inter-partition communication: PRTOS Hypervisor provides mechanisms for inter-partition communication, allowing partitions to exchange data and synchronize their activities.


**Currently supported platforms**
- [x] QEMU 32bit X86 platform
- [x] QEMU ARMv8 virt platform
- [x] QEMU RISC-V virt platform
- [x] QEMU 64bit X86 platform (AMD64)
- [x] QEMU LoongArch64 virt platform

**Plan to support platforms**
- [x] Raspberry Pi 4b/5b Single-board Computer


**PRTOS Hypervisor directory structure**
| Name          | Description                                             |
| ------------- | ------------------------------------------------------- |
| core          | The source code of the PRTOS Hypervisor.                |
| scripts       | The assist tools to configure PRTOS source code.        |
| user/tools    | User space utilities (libprtos, tools, examples, etc).  |
| user/bail     | User' Bare-metal Application Interface Library.         |
| doc           | PRTOS related documents.                                 |

> **Note**: BAIL (Bare-metal Application Interface Library) is a minimal partition development environment for writing "C" programs directly on top of the PRTOS hypervisor. It provides the basic services required to set up a minimal "C" execution environment. BAIL is useful for partitions that do not need an operating system and only need to test PRTOS hypercall APIs.


## 4. Getting Started


### 4.1 Development and Runtime Environment Setup

#### 4.1.1 Common Dependencies Installation
```
# Basic compilation tools, version control, and scripting environment
sudo apt-get update
sudo apt-get install -y build-essential make perl gawk git subversion \
bc bison flex cpio unzip rsync wget makeself xorriso mtools \
python3-dev python3-setuptools python3-libxml2 \
libncurses5-dev libncurses-dev libssl-dev libxml2-dev libxml2-utils libgnutls28-dev \
gdb-multiarch
```
#### 4.1.2 x86 (i386/32-bit) Platform
##### 4.1.2.1 Dependency Installation for x86 (i386/32-bit)

```
# 32-bit cross-compilation support, multi-arch libraries, and i386 emulator
sudo apt-get install -y gcc-multilib g++-multilib qemu-system-i386 \
grub-pc-bin
```

##### 4.1.2.2 Compiling and Running PRTOS `helloworld` Example
```
git clone https://github.com/prtos-project/prtos-hypervisor.git
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

#### 4.1.3 ARMv8 (AArch64) Platform
##### 4.1.3.1 Dependency Installation for ARMv8 (AArch64)

```
sudo apt-get install -y gcc-aarch64-linux-gnu qemu-system-aarch64
sudo apt-get install u-boot-tools

```
##### 4.1.3.2 Compiling and Running PRTOS `linux-smp` partition with 4 `vCPU`

```
git clone https://github.com/prtos-project/prtos-hypervisor.git
cd prtos-hypervisor
cp prtos_config.aarch64 prtos_config
make defconfig
make
cd user/bail/examples/linux_4vcpu_1partion_aarch64/
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

#### 4.1.4 RISC-V 64 Platform
##### 4.1.4.1 Dependency Installation for RISC-V 64

```
sudo apt-get install -y gcc-riscv64-linux-gnu device-tree-compiler qemu-system-misc

```

##### 4.1.4.2 Compiling and Running PRTOS `linux-smp` partition with 4 `vCPU`
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

#### 4.1.5 AMD64 (x86_64) Platform
##### 4.1.5.1 Dependency Installation for AMD64 (x86_64)

```
sudo apt-get install -y qemu-system-x86 grub-pc-bin
```
> **Note**:
AMD64 hardware virtualization tests require the `-enable-kvm` flag to leverage the host's `KVM` acceleration. Please add your user to the `kvm` group to grant the necessary permissions:
```
# Authorization and verification
sudo usermod -a -G kvm $USER
grep 'kvm' /etc/group  # Verify group membership
```

##### 4.1.5.2 Compiling and Running PRTOS `linux-smp` partition with 4 `vCPU`
```
git clone https://github.com/prtos-project/prtos-hypervisor.git
cd prtos-hypervisor
cp prtos_config.amd64 prtos_config
make defconfig
make
cd user/bail/examples/linux_4vcpu_1partion_amd64
make run.amd64.kvm.nographic
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

##### 4.1.5.3 Compiling and Running PRTOS `virtio_linux_demo_2p_amd64` (Dual SMP Linux + Virtio)
```
cd prtos-hypervisor
cp prtos_config.amd64 prtos_config
make defconfig
make
cd user/bail/examples/virtio_linux_demo_2p_amd64
make run.amd64
```
This launches QEMU with three access points:
- **Terminal** (this window): System Partition COM1 login (`root`/`1234`)
- **VNC** `vnc://localhost:5901`: Guest Partition VGA display
- **Telnet** `telnet localhost 4321`: Guest Partition COM2 login (`root`/`1234`)

All virtio services auto-start via init scripts (`S99virtio_backend` on System, `S99virtio_guest` on Guest). No manual steps are required.

The backend detects Guest partition halt and automatically disconnects TCP clients with a notification message.

See `user/bail/examples/virtio_linux_demo_2p_amd64/README.md` for full documentation.

##### 4.1.5.4 Compiling and Running PRTOS `virtio_linux_demo_2p_aarch64` (Dual SMP Linux + Virtio)
```
cd prtos-hypervisor
cp prtos_config.aarch64 prtos_config
make defconfig
make
cd user/bail/examples/virtio_linux_demo_2p_aarch64
make run.aarch64
```
System Partition PL011 UART console on stdio. Login `root`/`1234`.

See `user/bail/examples/virtio_linux_demo_2p_aarch64/README.md` for full documentation.

##### 4.1.5.5 Compiling and Running PRTOS `virtio_linux_demo_2p_riscv64` (Dual SMP Linux + Virtio)
```
cd prtos-hypervisor
cp prtos_config.riscv64 prtos_config
make defconfig
make
cd user/bail/examples/virtio_linux_demo_2p_riscv64
make run.riscv64
```
System Partition NS16550 UART console on stdio. Login `root`/`1234`.

See `user/bail/examples/virtio_linux_demo_2p_riscv64/README.md` for full documentation.

#### 4.1.6 LoongArch64 Platform
##### 4.1.6.1 Dependency Installation for LoongArch64

```
sudo apt-get install -y gcc-loongarch64-linux-gnu qemu-system-loongarch64
```

> **Note**: LoongArch64 uses trap-and-emulate para-virtualization. The guest Linux kernel runs at PLV3, with all privileged CSR/TLB/timer operations trapped and emulated by the PRTOS hypervisor.

##### 4.1.6.2 Building Buildroot (rootfs)

```bash
cd /path/to/buildroot

# Use QEMU LoongArch64 virt defconfig as a starting point
make qemu_loongarch64_virt_efi_defconfig

# Customize configuration
make menuconfig
# Set the following options:
#   Target options -> Architecture: LoongArch (64-bit)
#   Toolchain -> C library: musl
#   Filesystem images -> cpio the root filesystem: [*]
#   Target packages -> System tools -> htop: [*]  (optional)

make -j$(nproc)
```

The rootfs CPIO image will be at `output/images/rootfs.cpio`.

##### 4.1.6.3 Building Linux Kernel

```bash
cd /path/to/linux-6.19.9

# Use loongson64 defconfig as base
make ARCH=loongarch CROSS_COMPILE=loongarch64-linux-gnu- loongson64_defconfig

# Customize configuration
make ARCH=loongarch CROSS_COMPILE=loongarch64-linux-gnu- menuconfig
# Set the following options:
#   General setup -> Initial RAM filesystem and RAM disk support -> Initramfs source file(s):
#     /path/to/buildroot/output/images/rootfs.cpio
#   Device Drivers -> Input device support -> Hardware I/O ports -> i8042 PC Keyboard controller: [ ]
#   Device Drivers -> Input device support -> Keyboards -> AT keyboard: [ ]
#   Device Drivers -> Input device support -> Mice -> PS/2 mouse: [ ]
#   Kernel hacking -> printk and dmesg options -> Enable dynamic printk() support: [ ]
#   Boot options -> Built-in kernel command string:
#     console=ttyS0,115200 earlycon mem=512M@0x80000000 i8042.noaux i8042.nokbd i8042.nopnp

make ARCH=loongarch CROSS_COMPILE=loongarch64-linux-gnu- vmlinux -j$(nproc)
```

##### 4.1.6.4 LoongArch64 Boot Loader

LoongArch64 on PRTOS does **not** use U-Boot. Instead, PRTOS uses its own **RSW (Resident Software)** boot loader located at `user/bootloaders/rsw/loongarch64/`. The RSW is a lightweight stub that:

1. Runs in Direct Address (DA) mode at reset
2. Parses the PRTOS container image
3. Loads the PRTOS hypervisor core and partition images
4. Transfers control to the hypervisor

QEMU's `-kernel resident_sw` option loads the RSW directly, which in turn boots the hypervisor and all partitions.

##### 4.1.6.5 Compiling and Running PRTOS `linux-smp` partition with 4 `vCPU`

```
git clone https://github.com/prtos-project/prtos-hypervisor.git
cd prtos-hypervisor
cp prtos_config.loongarch64 prtos_config
make defconfig
make

# Copy Linux kernel to example directory
cp /path/to/linux-6.19.9/vmlinux \
   user/bail/examples/linux_4vcpu_1partion_loongarch64/

cd user/bail/examples/linux_4vcpu_1partion_loongarch64
make clean && make
make run.loongarch64
```
The output is as follows:

```
Welcome to Buildroot
(none) login:
```

##### 4.1.6.6 Compiling and Running PRTOS `virtio_linux_demo_2p_loongarch64` (Dual SMP Linux + Virtio)
```
cd prtos-hypervisor
cp prtos_config.loongarch64 prtos_config
make defconfig
make
cd user/bail/examples/virtio_linux_demo_2p_loongarch64
make run.loongarch64
```
System Partition UART console on stdio. Login `root`/`1234`.

See `user/bail/examples/virtio_linux_demo_2p_loongarch64/README.md` for full documentation.

### 4.2 Commands to Automatically Run Test Suites for All Platforms
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
### 4.3 Expected Test Reports


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

The test report of run `bash scripts/run_test.sh --arch loongarch64 check-all` should be:
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

[PRTOS Hypervisor Programming Guide](http://www.prtos.org/prtos_hypervisor_x86_user_guide/) | [PRTOS Hypervisor Samples](https://github.com/prtos-project/prtos-hypervisor/tree/main/user/bail/examples)

[Debug the PRTOS Hypervisor and assistant tools](doc/debug/how_to_debug_prtos_hypervisor_and_assistant_tools.md)

## 5. Community

PRTOS Hypervisor is very grateful for the support from all community developers, and if you have any ideas, suggestions or questions in the process of using PRTOS Hypervisor, PRTOS Hypervisor can be reached by the following means, and we are also updating PRTOS Hypervisor in real time on these channels. At the same time, any questions can be asked in the [issue section of PRTOS Hypervisor repository](https://github.com/prtos-project/prtos-hypervisor/issues) or [PRTOS Hypervisor forum](http://www.prtos.org), and community members will answer them.

## 6. Contribution

If you are interested in PRTOS Hypervisor and want to join in the development of PRTOS Hypervisor and become a code contributor,please refer to the [Code Contribution Guide](doc/contribution_guide/contribution_guide.md).
