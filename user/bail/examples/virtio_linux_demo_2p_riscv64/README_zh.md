# Virtio Linux 演示 - 双 SMP 分区（RISC-V 64）
[English](README.md) | **中文**
## 概述

本演示展示了在 PRTOS Type-1 管理程序上基于 RISC-V 64 位平台（支持硬件辅助虚拟化 H 扩展）的 **Virtio 设备虚拟化**，两个 SMP Linux 分区通过共享内存进行通信。

**系统分区**运行 virtio 后端守护进程，通过共享内存区域向**客户分区**提供虚拟化设备。客户分区运行**用户空间前端守护进程**（`virtio_frontend`），将自定义的共享内存协议桥接到标准 Linux 设备（通过 NBD 桥接 `/dev/vda`、通过 PTY 桥接 `/dev/hvc0`、通过 TUN/TAP 桥接 `tap0`/`tap1`/`tap2`）。两个分区均运行完整 Linux（内核 6.19.9）及 Buildroot 根文件系统。系统分区拥有 NS16550 UART 控制台；客户分区无直接控制台（使用 virtio-console）。所有服务通过 init 脚本自动启动。

## 架构

```
┌────────────────────────────────────────────────────────────────┐
│  L0: 宿主 Linux + QEMU (riscv64, virt, 1GB 内存)               │
├────────────────────────────────────────────────────────────────┤
│  L1: PRTOS Type-1 管理程序 (64MB @ 0x84000000)                  │
│  ┌───────────────────────┐  ┌────────────────────────┐         │
│  │ 分区 0 (系统)         │  │ 分区 1 (客户)           │         │
│  │ Linux + Virtio 后端   │  │ Linux + Virtio 前端     │         │
│  │ 2 vCPU (pCPU 0-1)    │  │ 2 vCPU (pCPU 2-3)      │         │
│  │ 128MB @ 0x88000000   │  │ 128MB @ 0x90000000     │         │
│  │ 控制台=NS16550 UART  │  │ 控制台=无 (virtio)      │         │
│  │                       │  │                        │         │
│  │ 服务 (自动启动):      │  │ Virtio 前端:           │         │
│  │ - prtos_manager       │  │ - virtio_frontend      │         │
│  │ - virtio_backend      │  │   - NBD (/dev/vda)     │         │
│  │   - Console 后端      │  │   - PTY (/dev/hvc0)    │         │
│  │   - 3x Net 后端       │  │   - TAP (tap0/1/2)     │         │
│  │   - Blk 后端          │  │                        │         │
│  │   tap0: 10.0.1.1/24   │  │   tap0: 10.0.1.2/24    │         │
│  │   tap1: 10.0.2.1/24   │  │   tap1: 10.0.2.2/24    │         │
│  │   tap2: 10.0.3.1/24   │  │   tap2: 10.0.3.2/24    │         │
│  └──────────┬────────────┘  └──────────┬─────────────┘         │
│             │     共享内存            │                        │
│             │  ┌──────────────────────┐│                       │
│             └──┤ ~5.25MB @ 0x98000000 ├┘                       │
│                │ 5 个 Virtio 区域     │                        │
│                └──────────────────────┘                        │
│                                                                │
│  IPVI 0-4: 客户→系统 (每设备门铃)                               │
│  IPVI 5:   系统→客户 (完成通知)                                  │
└────────────────────────────────────────────────────────────────┘
```

## 内存布局

| 区域                | 物理地址起始  | 大小   | 物理地址结束  |
|---------------------|-------------|--------|--------------|
| OpenSBI 固件        | 0x80000000  | 352KB  | 0x80057FFF   |
| RSW 引导程序        | 0x80200000  | 512KB  | 0x8027FFFF   |
| PRTOS 管理程序      | 0x84000000  | 64MB   | 0x87FFFFFF   |
| 系统分区            | 0x88000000  | 128MB  | 0x8FFFFFFF   |
| 客户分区            | 0x90000000  | 128MB  | 0x97FFFFFF   |
| 共享内存            | 0x98000000  | ~5.25MB| 0x9853FFFF   |
| **QEMU 内存总量**   |             | 1GB    |              |

**注意**：容器（PRTOS + 分区 PEF）嵌入在 RSW 二进制文件的 0x80280000 处。两个完整 Linux 分区（各约 38MB）会使容器超过 PRTOS 起始地址 0x84000000 之前的 61.5MB 间隙。PEF 压缩（`-c` 标志）将每个 PEF 缩减至约 25MB，使容器保持在限制范围内。根文件系统 CPIO 覆盖层同样经过 gzip 压缩。

## 共享内存布局（5 个区域）

| 区域        | 物理地址起始  | 大小   | 描述                          |
|------------|-------------|--------|-------------------------------|
| Virtio_Net0 | 0x98000000  | 1MB    | virtio-net 桥接 (TAP 后端)    |
| Virtio_Net1 | 0x98100000  | 1MB    | virtio-net NAT (环回)         |
| Virtio_Net2 | 0x98200000  | 1MB    | virtio-net 点对点 (环回)      |
| Virtio_Blk  | 0x98300000  | 2MB    | virtio-blk (文件或内存盘)     |
| Virtio_Con  | 0x98500000  | 256KB  | virtio-console (字符环形缓冲) |

## CPU 分配（SMP）

| 物理CPU  | 分区            | 虚拟CPU  |
|---------|-----------------|---------|
| pCPU 0  | 系统 (P0)       | vCPU 0  |
| pCPU 1  | 系统 (P0)       | vCPU 1  |
| pCPU 2  | 客户 (P1)       | vCPU 0  |
| pCPU 3  | 客户 (P1)       | vCPU 1  |

调度器：10ms 主帧，专用 pCPU 映射。

## 控制台分配

| 分区   | 控制台          | 访问方式                     |
|--------|----------------|------------------------------|
| 系统   | NS16550 UART   | 终端 (stdio)                 |
| 客户   | 无             | 通过 `/dev/hvc0` 的 virtio-console |

## Virtio 设备

### Virtio-Console
- **机制**：共享内存中的 4KB 字符环形缓冲区（`Virtio_Con`）
- **客户设备**：`/dev/hvc0`（由 `virtio_frontend` 创建的 PTY 对）
- **数据流**：客户写入 → 共享内存 → 后端读取 → 系统 UART

### Virtio-Net（×3）
- **机制**：每个实例 64 槽位的数据包环形缓冲区，通过 TUN/TAP 桥接
- **Net0**：系统 `tap0` (10.0.1.1) ↔ 客户 `tap0` (10.0.1.2)
- **Net1**：系统 `tap1` (10.0.2.1) ↔ 客户 `tap1` (10.0.2.2)
- **Net2**：系统 `tap2` (10.0.3.1) ↔ 客户 `tap2` (10.0.3.2)

### Virtio-Blk
- **机制**：16 槽位块请求环（按扇区寻址，512B 扇区）
- **后端**：1MB 内存盘（默认回退）
- **客户设备**：`/dev/vda`（符号链接至 `/dev/nbd0`）

## 分区间通信（IPVI）

| IPVI ID | 方向              | 用途                       |
|---------|-------------------|---------------------------|
| 0       | 客户 → 系统       | virtio-net0 门铃          |
| 1       | 客户 → 系统       | virtio-net1 门铃          |
| 2       | 客户 → 系统       | virtio-net2 门铃          |
| 3       | 客户 → 系统       | virtio-blk 门铃           |
| 4       | 客户 → 系统       | virtio-console 门铃       |
| 5       | 系统 → 客户       | 完成通知                   |

两个**采样通道**提供控制面消息传递（各 8 字节）。

## 前置条件

### 工作区布局

| 组件 | 路径 |
|------|------|
| Linux 内核源码 | `/home/chenweis/hdd/Repo/linux_workspace/linux-6.19.9/` |
| RISC-V Linux 工作区 | `/home/chenweis/hdd/Repo/riscv64_linux_workspace/` |
| RISC-V Buildroot 输出 | `/home/chenweis/hdd/Repo/riscv64_linux_workspace/buildroot_output/` |
| Buildroot 源码 | `/home/chenweis/hdd/Repo/aarch64_linux_workspace/buildroot/` |

### 步骤 1：构建 Buildroot 根文件系统（RISC-V 64）

```bash
cd /home/chenweis/hdd/Repo/aarch64_linux_workspace/buildroot
make O=/home/chenweis/hdd/Repo/riscv64_linux_workspace/buildroot_output qemu_riscv64_virt_defconfig
cd /home/chenweis/hdd/Repo/riscv64_linux_workspace/buildroot_output
```

应用配置（`make menuconfig`）：

| 配置选项 | 值 | 用途 |
|---------|-----|------|
| `BR2_TARGET_GENERIC_ROOT_PASSWD` | `1234` | Root 登录密码 |
| `BR2_TARGET_ROOTFS_CPIO` | `y` | 生成 rootfs.cpio |
| `BR2_PACKAGE_NBD` | `y` | NBD 客户端 |
| `BR2_PACKAGE_NBD_CLIENT` | `y` | NBD 客户端二进制 |

```bash
make -j$(nproc)
```

输出：`/home/chenweis/hdd/Repo/riscv64_linux_workspace/buildroot_output/images/rootfs.cpio`

### 步骤 2：构建 Linux 内核（RISC-V 64）

```bash
cd /home/chenweis/hdd/Repo/riscv64_linux_workspace/linux-6.19.9
make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- defconfig
```

应用额外配置（`make menuconfig`）：

| 配置选项 | 值 | 用途 |
|---------|-----|------|
| `CONFIG_BLK_DEV_NBD` | `y` | NBD 块设备 |
| `CONFIG_TUN` | `y` | TUN/TAP 设备 |
| `CONFIG_INITRAMFS_SOURCE` | `/home/chenweis/hdd/Repo/riscv64_linux_workspace/buildroot_output/images/rootfs.cpio` | 嵌入根文件系统 |

```bash
make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- -j$(nproc) Image
```

输出：`/home/chenweis/hdd/Repo/riscv64_linux_workspace/linux-6.19.9/arch/riscv/boot/Image`

### 步骤 3：构建 PRTOS 管理程序

```bash
cd prtos-hypervisor
cp prtos_config.riscv64 prtos_config
make defconfig
make
```

### 步骤 4：构建演示

```bash
cd user/bail/examples/virtio_linux_demo_2p_riscv64
make
```

构建产物：
- `resident_sw` — ELF 二进制文件（PRTOS + 两个分区）
- `resident_sw.bin` — 用于 QEMU `-kernel` 启动的原始二进制

## 运行

```bash
make run.riscv64
```

通过 OpenSBI（`-bios default`）+ QEMU `-kernel` 直接加载启动。系统分区的 NS16550 UART 输出到标准输入输出。

### 手动 QEMU 命令
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

## 演示流程

所有 virtio 服务通过 init 脚本自动启动（系统分区 `S99virtio_backend`，客户分区 `S99virtio_guest`）。无需手动启动后端或前端。

### 步骤 1：启动 QEMU
```bash
make run.riscv64
```

### 步骤 2：启动系统分区（UART/标准输入输出）
系统通过 `S99virtio_backend` 自动启动 `prtos_manager` 和 `virtio_backend`（输出重定向至 `/var/log/`）：
```
=== PRTOS System Partition ===

Welcome to Buildroot
buildroot login: root
Password: 1234
```

### 步骤 3：访问客户分区
```bash
# 从系统分区 shell（登录后）：
telnet 127.0.0.1 4321
# 登录：root / 1234
```
客户分区通过 `S99virtio_guest` 自动启动 `virtio_frontend`。前端等待后端初始化共享内存（轮询魔术值最多 300 秒），然后创建 `/dev/nbd0`（块设备）和 `/dev/hvc0`（控制台）。init 脚本等待 `/dev/hvc0` 并在其上启动 `getty`。后端的 TCP 桥接（端口 4321）通过共享内存将 telnet 连接到 `/dev/hvc0`。

> **注意**：RISC-V QEMU virt 仅有一个 NS16550 UART。客户控制台需通过系统分区的 shell 使用 virtio-console TCP 桥接访问。

### 步骤 4：测试 Virtio 设备（客户分区）
```bash
/opt/virtio_test.sh   # 自动测试所有 virtio 设备
```
预期输出包括：
- 网络：3 个 TAP 接口（tap0/tap1/tap2）带 IP 地址，ping 系统分区均成功
- 块设备（`/dev/vda` → `/dev/nbd0`）：创建 ext2 文件系统、挂载、写入测试文件并验证
- 控制台（`/dev/hvc0`）：消息 "Hello PRTOS from Guest!" 转发到系统 UART
- 共享内存魔术值验证（NET0=0x4E455430, BLK0=0x424C4B30, CONS=0x434F4E53）
- `Verification Passed`

## 平台特定说明

- **超级调用机制**：RISC-V 从 VU 模式（Linux 用户空间）发出的 ecall 会被 VS 模式内核捕获，而非管理程序。`prtos_vmcall()` 函数被置为返回 -1 的桩函数。Virtio 以**轮询模式**运行（无 IPVI 门铃通知）。
- **容器大小约束**：RSW 容器（位于 0x80280000）必须在 PRTOS（位于 0x84000000）之前装入，间隙仅 61.5MB。两个 Linux 分区（未压缩各约 38MB）需要 PEF 压缩（`-c` 标志）；每个压缩后 PEF 约 25MB，使约 50MB 的容器保持在限制内。
- **启动方式**：OpenSBI 固件加载到 0x80000000，然后将控制权转移给 0x80200000 的 RSW，RSW 解包 PRTOS 容器。
- **UART 直通**：0x10000000 处的 NS16550 UART 直通到系统分区用于控制台输出。

## 测试

```bash
# 自动登录测试：
python3 test_login.py

# 客户控制台（TCP 桥接）测试：
python3 test_com2.py

# 控制台测试（干净输出、退格、Tab 补全）：
python3 test_console.py

# 通过测试框架：
cd ../../../../  # 返回 prtos-hypervisor 根目录
bash scripts/run_test.sh --arch riscv64 check-virtio_linux_demo_2p_riscv64

# 完整 riscv64 测试套件：
bash scripts/run_test.sh --arch riscv64 check-all
```

## 文件结构

| 文件/目录 | 描述 |
|-----------|------|
| `config/resident_sw.xml` | PRTOS 系统配置 |
| `Makefile` | 构建系统 |
| `start_system.S` | 系统分区启动桩（RISC-V 启动协议） |
| `start_guest.S` | 客户分区启动桩 |
| `hdr_system.c` / `hdr_guest.c` | PRTOS 镜像头 |
| `linker_system.ld` | 链接脚本（基址 `0x88000000`，initrd 在 +64MB） |
| `linker_guest.ld` | 链接脚本（基址 `0x90000000`，initrd 在 +64MB） |
| `linux_system.dts` | 设备树（128MB, 2 CPU, NS16550 UART） |
| `linux_guest.dts` | 设备树（128MB, 2 CPU, 无 UART） |
| `set_serial_poll.c` | 串口轮询模式工具 |
| `test_login.py` | 自动测试：QEMU 启动、UART 登录、`uname` 检查 |
| `test_com2.py` | 自动测试：通过系统分区 TCP 桥接访问客户控制台 |
| `test_console.py` | 控制台测试：干净输出（无后端噪声）、telnet 退格 + Tab 补全 |
| **`system_partition/`** | |
| `  include/virtio_be.h` | 共享数据结构（地址在 0x98xxxxxx） |
| `  src/` | 后端守护进程源码 |
| `  rootfs_overlay/` | 系统 init 脚本 |
| **`lib_prtos_manager/`** | |
| `  include/prtos_hv.h` | 超级调用 API（ecall 桩，邮箱在 0x98500000） |
| `  common/` | 管理器和超级调用实现 |
| **`guest_partition/`** | |
| `  src/virtio_frontend.c` | 用户空间前端守护进程 |
| `  rootfs_overlay/` | 客户 init 脚本和测试脚本 |

## 依赖

- **Linux 内核 6.19.9**（RISC-V Image），启用 `CONFIG_BLK_DEV_NBD=y`、`CONFIG_TUN=y`，嵌入 initramfs
- **Buildroot** 根文件系统，含 NBD 客户端，root 密码 `1234`，CPIO 格式
- **PRTOS 管理程序**，为 riscv64 构建
- **QEMU**（`qemu-system-riscv64`）virt 机器
- **交叉编译器**：`riscv64-linux-gnu-gcc`
