# Virtio Linux 演示 - 2 个 SMP 分区（AArch64）
[English](README.md) | **中文**
## 概述

该演示展示了在 PRTOS Type-1 Hypervisor 上，基于 AArch64（ARMv8）平台硬件辅助虚拟化（EL2/vGIC）能力，使用两个 SMP Linux 分区通过共享内存实现 **Virtio 设备虚拟化**。

**System Partition** 独占 PL011 UART，并运行 virtio 后端守护进程，通过共享内存区域向 **Guest Partition** 提供虚拟设备服务。Guest 运行一个 **用户态前端守护进程**（`virtio_frontend`），将自定义共享内存协议桥接为标准 Linux 设备（块设备通过 NBD 暴露为 `/dev/vda`，控制台通过 PTY 暴露为 `/dev/hvc0`，网络通过 TUN/TAP 暴露为 `tap0`/`tap1`/`tap2`）。两个分区都运行完整的 Linux（内核 6.19.9）。System 控制台使用 PL011 UART（stdio）。Guest 控制台通过 virtio 后端守护进程中的 **TCP 桥接** 访问，在 System Shell 中执行 `telnet 127.0.0.1 4321` 即可连接到 Guest 的 `/dev/hvc0`。所有服务都通过 init 脚本自动启动。

**Guest Virtio Frontend**：由于 PRTOS 在 AArch64 上的 HVC 指令只能从 EL1 及更高特权级工作，不能在 Linux 用户态（EL0）直接使用，因此无法使用标准 `virtio-mmio` 内核驱动。取而代之的是一个用户态守护进程（`virtio_frontend`），通过轮询方式将共享内存桥接为标准 Linux 设备，包括 NBD（块设备）、PTY（控制台）和 TUN/TAP（网络）。

## 架构

```
┌────────────────────────────────────────────────────────────────┐
│  L0: Host Linux + QEMU (aarch64, virt, GICv3, 4096MB RAM, 4 CPUs) │
├────────────────────────────────────────────────────────────────┤
│  L1: PRTOS Type-1 Hypervisor (32MB)                            │
│  ┌───────────────────────┐  ┌────────────────────────┐         │
│  │ Partition 0 (System)  │  │ Partition 1 (Guest)    │         │
│  │ Linux + Virtio Backend│  │ Linux + Virtio Frontend│         │
│  │ 2 vCPU (pCPU 0-1)     │  │ 2 vCPU (pCPU 2-3)      │         │
│  │ 128MB @ 0x10000000    │  │ 128MB @ 0x18000000     │         │
│  │ console=PL011 (UART)  │  │ console=/dev/hvc0      │         │
│  │                       │  │ (TCP bridge :4321)     │         │
│  │ Services (auto-start): │  │                       │         │
│  │ - prtos_manager       │  │ Virtio Frontend:       │         │
│  │ - virtio_backend      │  │ - virtio_frontend      │         │
│  │   - Console backend   │  │   - NBD (/dev/vda)     │         │
│  │   - 3x Net backend    │  │   - PTY (/dev/hvc0)    │         │
│  │   - Blk backend       │  │   - TAP (tap0/1/2)     │         │
│  │   tap0: 10.0.1.1/24   │  │   tap0: 10.0.1.2/24    │         │
│  │   tap1: 10.0.2.1/24   │  │   tap1: 10.0.2.2/24    │         │
│  │   tap2: 10.0.3.1/24   │  │   tap2: 10.0.3.2/24    │         │
│  │                       │  │ - /opt/virtio_test.sh  │         │
│  └──────────┬────────────┘  └──────────┬─────────────┘         │
│             │     Shared Memory        │                       │
│             │  ┌──────────────────────┐│                       │
│             └──┤ ~5.25MB @ 0x20000000 ├┘                       │
│                │ 5 Virtio Regions     │                        │
│                └──────────────────────┘                        │
│                                                                │
│  IPVI 0-4: Guest→System (per-device doorbell)                  │
│  IPVI 5:   System→Guest (completion doorbell)                  │
└────────────────────────────────────────────────────────────────┘
```

## 内存布局

| 区域 | IPA 起始地址 | 大小 | IPA 结束地址 |
|---------------------|-------------|--------|--------------|
| PRTOS Hypervisor | （自动） | 32MB | |
| System Partition | 0x10000000 | 128MB | 0x17FFFFFF |
| Guest Partition | 0x18000000 | 128MB | 0x1FFFFFFF |
| 共享内存 | 0x20000000 | ~5.25MB | 0x2053FFFF |
| **QEMU RAM 总量** | | 4096MB | |

## 共享内存布局（5 个区域）

| 区域 | IPA 起始地址 | 大小 | 描述 |
|-------------|-------------|--------|---------------------------------|
| Virtio_Net0 | 0x20000000 | 1MB | virtio-net bridge（TAP 后端） |
| Virtio_Net1 | 0x20100000 | 1MB | virtio-net NAT（loopback） |
| Virtio_Net2 | 0x20200000 | 1MB | virtio-net p2p（loopback） |
| Virtio_Blk | 0x20300000 | 2MB | virtio-blk（文件或 RAM disk） |
| Virtio_Con | 0x20500000 | 256KB | virtio-console（字符环形缓冲） |

## CPU 分配（SMP）

| 物理 CPU | 分区 | 虚拟 CPU |
|-------------|-------------------|-------------|
| pCPU 0 | System (P0) | vCPU 0 |
| pCPU 1 | System (P0) | vCPU 1 |
| pCPU 2 | Guest (P1) | vCPU 0 |
| pCPU 3 | Guest (P1) | vCPU 1 |

调度器：10ms major frame，采用固定的 pCPU 映射。

## 控制台分配

| 分区 | 控制台 | 机制 | 访问方式 |
|-----------|---------------------|--------------------------------------------|--------------------------|
| System | PL011 UART (0x09000000) | `-nographic`（QEMU 串口映射到 stdio） | 终端直接访问 |
| Guest | /dev/hvc0 (PTY) | virtio-console 共享内存 + TCP 桥接 | 在 System 中执行 `telnet 127.0.0.1 4321` |

Guest 分区没有直接的硬件 UART。`virtio_frontend` 会创建一个 PTY 对（`/dev/hvc0`），并将其桥接到控制台共享内存区域。System 侧的 `virtio_backend` 负责读写共享内存，并在 System 分区内部监听 TCP 4321 端口。要访问 Guest 控制台，先登录 System 分区，然后执行 `telnet 127.0.0.1 4321`。初始化脚本（`S99virtio_guest`）会等待 `/dev/hvc0` 就绪，并自动在其上启动 `getty`。

## Virtio 设备

### Virtio-Console
- **机制**：共享内存区域 `Virtio_Con` 中的 4KB 双向字符环形缓冲区
- **Guest 设备**：`/dev/hvc0`（由 `virtio_frontend` 创建的 PTY 对）
- **后端 TCP 桥接**：`virtio_backend` 在 System 分区内监听 TCP 4321 端口
- **数据流（Guest → System）**：Guest 向 `/dev/hvc0` 写入 → `virtio_frontend` 拷贝到 `tx_buf` → 后端读取后输出到 System UART，并发送给 TCP 客户端
- **数据流（System → Guest）**：TCP 客户端发送数据 → 后端写入 `rx_buf` → `virtio_frontend` 读取 `rx_buf` → 写入 PTY master → Guest 从 `/dev/hvc0` 读取
- **交互访问**：在 System Shell 中执行 `telnet 127.0.0.1 4321` → 进入 Guest 的 getty 登录
- **验证**：在 Guest 中执行 `echo "Hello PRTOS" > /dev/hvc0`，应可在 System 控制台看到输出

### Virtio-Net（3 个实例）
- **机制**：每个实例使用 64 槽数据包环形缓冲区（每槽最大 1536 字节），两侧通过 TUN/TAP 桥接
- **Net0**：System `tap0`（10.0.1.1）↔ 共享内存 ↔ Guest `tap0`（10.0.1.2）
- **Net1**：System `tap1`（10.0.2.1）↔ 共享内存 ↔ Guest `tap1`（10.0.2.2）
- **Net2**：System `tap2`（10.0.3.1）↔ 共享内存 ↔ Guest `tap2`（10.0.3.2）
- **数据流**：Guest TAP → 共享内存中的 `tx_slots` → 后端读取 → 后端 TAP（接收方向反向相同）
- **验证**：在 Guest 中执行 `ping 10.0.x.1`，或在 System 中执行 `ping 10.0.x.2`

### Virtio-Blk
- **机制**：16 槽块请求环（按扇区寻址，512B 扇区）
- **后端**：默认使用 1MB 内存 RAM disk
- **Guest 设备**：`/dev/vda`（链接到 `/dev/nbd0`，由 `virtio_frontend` 通过 NBD 协议提供）
- **支持操作**：IN（读）、OUT（写）、FLUSH、GET_ID
- **验证**：测试脚本（`virtio_test.sh`）会在 `/dev/vda` 上创建 ext2 文件系统、挂载、写入测试文件，并校验文件内容

## IP 地址分配

| 网络 | System（tap） | Guest（tap） | 子网 |
|---------|-------------|------------|--------|
| Net0 | 10.0.1.1 | 10.0.1.2 | /24 |
| Net1 | 10.0.2.1 | 10.0.2.2 | /24 |
| Net2 | 10.0.3.1 | 10.0.3.2 | /24 |

IP 地址由初始化脚本自动配置：System 侧使用 `S99virtio_backend`，Guest 侧使用 `S99virtio_guest`。

## 分区间通信（IPVI）

| IPVI ID | 方向 | 用途 |
|---------|--------------------|------------------------------|
| 0 | Guest → System | virtio-net0 doorbell |
| 1 | Guest → System | virtio-net1 doorbell |
| 2 | Guest → System | virtio-net2 doorbell |
| 3 | Guest → System | virtio-blk doorbell |
| 4 | Guest → System | virtio-console doorbell |
| 5 | System → Guest | 完成通知 |

另有两个 **Sampling Channel** 用于控制平面消息传输（每个 8B）。

## 前置条件

### 步骤 1：构建 Buildroot 根文件系统（AArch64）

```bash
cd buildroot
make qemu_aarch64_virt_defconfig
```

应用以下配置（`make menuconfig`）：

| 配置项 | 值 | 用途 |
|---|---|---|
| `BR2_TARGET_GENERIC_ROOT_PASSWD` | `1234` | root 登录密码 |
| `BR2_TARGET_ROOTFS_CPIO` | `y` | 生成 `rootfs.cpio` |
| `BR2_PACKAGE_NBD` | `y` | NBD 客户端 |
| `BR2_PACKAGE_NBD_CLIENT` | `y` | NBD 客户端可执行文件 |

```bash
make -j$(nproc)
```

### 步骤 2：构建 Linux 内核（AArch64）

```bash
cd linux-6.19.9
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig
```

应用额外配置（`make menuconfig`）：

| 配置项 | 值 | 用途 |
|---|---|---|
| `CONFIG_BLK_DEV_NBD` | `y` | NBD 块设备 |
| `CONFIG_TUN` | `y` | TUN/TAP 设备 |
| `CONFIG_STRICT_DEVMEM` | `n` | 允许通过 `/dev/mem` mmap 共享内存 |
| `CONFIG_INITRAMFS_SOURCE` | `/path/to/buildroot/output/images/rootfs.cpio` | 内嵌 rootfs |

```bash
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j$(nproc) Image
```

### 步骤 3：构建 PRTOS Hypervisor

```bash
cd prtos-hypervisor
cp prtos_config.aarch64 prtos_config
make defconfig
make
```

### 步骤 4：构建 Demo

```bash
cd user/bail/examples/virtio_linux_demo_2p_aarch64
make
```

构建产物：
- `resident_sw`：ELF 二进制（PRTOS + 两个分区）
- `resident_sw_image`：供 U-Boot 启动的 mkimage Legacy Image
- `u-boot/u-boot.bin`：自定义 U-Boot 二进制（自动从源码构建）

### 步骤 5：构建 U-Boot（自动）

Makefile 会自动从 `../../../u-boot/`（相对于 `prtos-hypervisor/`）源码树构建定制 U-Boot，并应用如下配置修改：

| 配置项 | 值 | 用途 |
|---|---|---|
| `CONFIG_SYS_BOOTM_LEN` | `0x10000000`（256MB） | 允许启动约 103MB 的 `resident_sw_image`（默认 128MB 不足） |
| `CONFIG_PREBOOT` | `bootm 0x40200000 - ${fdtcontroladdr}` | 自动启动加载到 `0x40200000` 的 PRTOS 镜像 |

手动构建 U-Boot：
```bash
cd u-boot  # （prtos-hypervisor 的同级目录）
make qemu_arm64_defconfig
scripts/config --set-val CONFIG_SYS_BOOTM_LEN 0x10000000
scripts/config --set-str CONFIG_PREBOOT 'bootm 0x40200000 - ${fdtcontroladdr}'
make -j$(nproc) CROSS_COMPILE=aarch64-linux-gnu-
```

生成的 `u-boot.bin` 会放在 demo 目录下的 `u-boot/u-boot.bin`。

## 运行

### 交互模式
```bash
make run.aarch64
```
System Partition 的 PL011 UART 映射到 stdio。使用 `root`/`1234` 登录后，在 System Shell 中访问 Guest：
```bash
telnet 127.0.0.1 4321
# Login: root / 1234
```

### Nographic 模式（自动化测试）
```bash
make run.aarch64.nographic
```
与 `run.aarch64` 相同，System UART 映射到 stdio。测试框架使用该模式。

### 手动 QEMU 命令
```bash
qemu-system-aarch64 \
    -machine virt,gic_version=3 \
    -machine virtualization=true \
    -cpu cortex-a72 -machine type=virt \
    -m 4096 -smp 4 \
    -bios ./u-boot/u-boot.bin \
    -device loader,file=./resident_sw_image,addr=0x40200000,force-raw=on \
    -nographic -no-reboot
```

## U-Boot 启动流程

1. QEMU 启动 U-Boot 作为 BIOS 固件。
2. QEMU 的 device loader 将 `resident_sw_image`（mkimage Legacy Image）放置到地址 `0x40200000`。
3. U-Boot 执行 `preboot` 命令：`bootm 0x40200000 - ${fdtcontroladdr}`。
4. U-Boot 校验镜像校验和，加载镜像，并跳转到 PRTOS RSW 入口点。
5. RSW 解包容器（PRTOS 核心 + 2 个分区 PEF），并启动 Hypervisor。

## Demo 工作流程

所有 virtio 服务都通过初始化脚本自动启动：System 侧为 `S99virtio_backend`，Guest 侧为 `S99virtio_guest`。无需手工启动 backend 或 frontend。

### 步骤 1：启动 QEMU
```bash
make run.aarch64
```

### 步骤 2：启动 System Partition（UART/stdio）
System 会通过 `S99virtio_backend` 自动启动 `prtos_manager` 和 `virtio_backend`（输出重定向到 `/var/log/`）：
```
=== PRTOS System Partition ===

Welcome to Buildroot
buildroot login: root
Password: 1234
```

### 步骤 3：访问 Guest Partition
```bash
# 在 System 分区 shell 中执行（登录后）：
telnet 127.0.0.1 4321
# Login: root / 1234
```
Guest 会通过 `S99virtio_guest` 自动启动 `virtio_frontend`。前端会等待后端初始化共享内存（轮询 magic 值，最长 300 秒），随后创建 `/dev/nbd0`（块设备）和 `/dev/hvc0`（控制台）。初始化脚本会等待 `/dev/hvc0` 就绪并在其上启动 `getty`。后端在 4321 端口提供 TCP 桥接，将 telnet 会话通过共享内存连接到 `/dev/hvc0`。

> **注意**：与 AMD64 平台不同（AMD64 可通过 COM2 提供宿主机级 `telnet localhost 4321`），AArch64 QEMU virt 只有一个 PL011 UART。Guest 控制台必须通过 System 分区中的 virtio-console TCP 桥接访问。

### 步骤 4：测试 Virtio 设备（Guest）
```bash
/opt/virtio_test.sh   # 对所有 virtio 设备执行自动化测试
```
预期输出包括：
- 网络：3 个 TAP 接口（tap0/tap1/tap2）已配置 IP，且都能成功 ping 通 System 分区
- 块设备（`/dev/vda` → `/dev/nbd0`）：成功创建并挂载 ext2 文件系统，写入并校验测试文件
- 控制台（`/dev/hvc0`）：消息 `Hello PRTOS from Guest!` 被转发到 System UART
- 共享内存 magic 值校验通过（NET0=`0x4E455430`，BLK0=`0x424C4B30`，CONS=`0x434F4E53`）
- `Verification Passed`

## 平台特定说明

- **`CONFIG_STRICT_DEVMEM`**：必须在内核配置中禁用（`=n`）。ARM64 默认值（`=y`）会阻止对声明 RAM 之外地址的 `/dev/mem` mmap，这会导致 `virtio_frontend` 无法映射 `0x20000000+` 的共享内存区域。
- **Hypercall 机制**：AArch64 HVC 指令只能从 EL1 及以上执行，不能从 Linux 用户态（EL0）调用。`prtos_vmcall()` 被桩实现为返回 `-1`。因此 virtio 采用 **轮询模式** 运行（不使用 IPVI doorbell 通知）。
- **启动方式**：使用定制 U-Boot，并将 `CONFIG_SYS_BOOTM_LEN=0x10000000`（256MB），以容纳包含 2 个 Linux 分区、总大小约 103MB 的镜像。标准 `qemu_arm64` U-Boot 默认的 128MB 不足。
- **GIC**：使用 GICv3，维护中断为 IRQ 25。
- **设备树**：每个分区都使用自定义 DTS 文件（`linux_system.dts`、`linux_guest.dts`），其中配置了 `cortex-a57` CPU 模型、GICv3 中断控制器等。

## 测试

```bash
# 自动登录测试：
python3 test_login.py

# Guest 控制台（TCP bridge）测试：
python3 test_com2.py

# 控制台测试（干净输出、退格、Tab 补全）：
python3 test_console.py

# 通过测试框架执行：
cd ../../../../  # 返回 prtos-hypervisor 根目录
bash scripts/run_test.sh --arch aarch64 check-virtio_linux_demo_2p_aarch64

# 执行完整 aarch64 测试集：
bash scripts/run_test.sh --arch aarch64 check-all
```

## 文件结构

| 文件 / 目录 | 描述 |
|-----------------|-------------|
| `config/resident_sw.xml` | PRTOS 系统配置（2 个 SMP 分区、5 个共享内存区域、6 个 IPVI、双控制台） |
| `prtos_cf.aarch64.xml` | 指向 `config/resident_sw.xml` 的符号链接 |
| `Makefile` | 构建系统（分区、backend、manager、CPIO overlay、QEMU 目标） |
| `start_system.S` | System Partition 启动桩（ARM64 启动协议） |
| `start_guest.S` | Guest Partition 启动桩 |
| `hdr_system.c` / `hdr_guest.c` | PRTOS 镜像头（magic `0x24584d69`） |
| `linker_system.ld` | 链接脚本（System，基址 `0x10000000`，initrd 位于 `+64MB`） |
| `linker_guest.ld` | 链接脚本（Guest，基址 `0x18000000`，initrd 位于 `+64MB`） |
| `linux_system.dts` | 设备树（128MB，2 CPU，GICv3，PL011 UART @ `0x09000000`） |
| `linux_guest.dts` | 设备树（128MB，1 CPU，GICv3，无 UART，使用 virtio-console） |
| `set_serial_poll.c` | 串口轮询模式辅助工具 |
| `test_login.py` | 自动化测试：启动 QEMU、PL011 登录、检查 `uname` |
| `test_com2.py` | 自动化测试：从 System 侧通过 TCP bridge 访问 Guest 控制台 |
| `test_console.py` | 控制台测试：干净输出（无 backend 噪声）、telnet 退格与 Tab 补全 |
| **`system_partition/`** | |
| `  include/virtio_be.h` | 共享数据结构：`net_shm`（64 槽环）、`blk_shm`（16 槽环）、`console_shm`（4KB 环） |
| `  src/main.c` | Backend 守护进程：mmap 5 个 `/dev/mem` 区域，初始化所有设备，1ms 轮询循环 |
| `  src/virtio_console.c` | 控制台后端：轮询 `tx_buf` 环并输出到 `putchar` + TCP 客户端；TCP 客户端数据写入 `rx_buf` 环 |
| `  src/virtio_net.c` | 网络后端：bridge 使用 TAP，NAT/p2p 使用 loopback |
| `  src/virtio_blk.c` | 块设备后端：1MB RAM disk |
| `  src/doorbell.c` | IPVI 信号处理（AArch64 上为桩实现，采用轮询模式） |
| `  src/manager_if.c` | Manager 封装：查询 Guest 分区状态 |
| `  rootfs_overlay/etc/init.d/S99virtio_backend` | 初始化脚本：创建设备 `/dev/net/tun`，自动启动 `prtos_manager` 与 `virtio_backend`，并配置 TAP IP |
| **`lib_prtos_manager/`** | |
| `  include/prtos_hv.h` | Hypercall API：HVC 桩实现（EL0 下返回 `-1`）、状态结构 |
| `  include/prtos_manager.h` | Manager 设备接口 |
| `  common/prtos_hv.c` | Hypercall 实现：通过 `/dev/mem` mmap mailbox |
| `  common/prtos_manager.c` | 命令分发器：help、list、partition 操作、plan、write、quit |
| `  common/hypervisor.c` | 分区命令：list、halt、reset、resume、status、suspend |
| `  linux/prtos_manager_main.c` | Linux 主程序：stdin/stdout，支持 `-d` dry-run 模式 |
| **`guest_partition/`** | |
| `  src/virtio_frontend.c` | 用户态前端守护进程：通过 `/dev/mem` 映射共享内存，创建用于 `/dev/nbd0` 的 NBD 服务（块设备）、用于 `/dev/hvc0` 的 PTY 对（控制台）、以及用于网络的 TUN/TAP 设备 |
| `  rootfs_overlay/etc/init.d/S99virtio_guest` | 初始化脚本：启动 `virtio_frontend`，等待 `/dev/hvc0`，启动 `getty`，配置 TAP IP，并创建 `/dev/vda` 符号链接 |
| `  rootfs_overlay/opt/virtio_test.sh` | Guest 测试脚本：网络（3 个接口）、块设备、控制台、共享内存检查 |

## 设计说明

- **4096MB QEMU RAM**：总内存为 4GB。PRTOS Stage-2 页表对设备 MMIO 区域进行恒等映射。
- **控制台架构**：System Partition 使用位于 `0x09000000` 的 PL011 UART（stdio）。Guest Partition 没有直连 UART。不同于 AMD64 平台拥有 COM2，AArch64 QEMU virt 只提供一个 PL011。Guest 控制台通过 `virtio_backend` 中的双向 **TCP bridge** 暴露：后端在 System 分区内监听 TCP 4321 端口，并将数据与控制台共享内存双向桥接。要访问 Guest，先登录 System，再执行 `telnet 127.0.0.1 4321`。
- **安静的 System 启动**：System 内核命令行包含 `quiet loglevel=0`，以抑制启动日志，获得更干净的登录体验。
- **网络**：每个 virtio-net 实例都使用一对 TUN/TAP 设备（System 和 Guest 各一个），通过共享内存数据包环进行桥接。IP 地址由 init 脚本自动配置。
- **静态链接**：`virtio_backend`、`virtio_frontend` 和 `prtos_manager` 均采用静态链接，以便在分区 rootfs 中更好地移植运行。
- **自动启动**：两个分区都使用 Buildroot init 脚本（`S99virtio_backend`、`S99virtio_guest`）在启动时自动拉起全部服务，无需人工干预。
- **轮询模式**：AArch64 HVC 指令只能从 EL1+（内核态）执行，用户态守护进程无法发起 hypercall，因此 IPVI doorbell 在该 demo 中使用桩实现，virtio 设备以 1ms 周期轮询运行。

## 依赖项

- **Linux kernel 6.19.9**（AArch64 Image），需启用 `CONFIG_BLK_DEV_NBD=y`、`CONFIG_TUN=y`、禁用 `CONFIG_STRICT_DEVMEM=n`，并内嵌 initramfs（见 [前置条件](#前置条件)）
- **Buildroot** 根文件系统，包含 NBD client，root 密码为 `1234`，输出格式为 CPIO
- **PRTOS Hypervisor** 的 aarch64 构建产物（`cp prtos_config.aarch64 prtos_config && make defconfig && make`）
- **U-Boot 源码** 位于 `../../../u-boot/`（与 `prtos-hypervisor/` 同级），会以定制配置自动构建
- **QEMU**（`qemu-system-aarch64`），需要支持 virt 机器、GICv3 和 platform bus
- **交叉编译器**：`aarch64-linux-gnu-gcc`

## Linux 内核命令行

**System Partition**（`linux_system.dts`）：
```
console=ttyAMA0 earlycon=pl011,0x09000000 quiet loglevel=0 nokaslr
```

**Guest Partition**（`linux_guest.dts`）：
```
nokaslr
```