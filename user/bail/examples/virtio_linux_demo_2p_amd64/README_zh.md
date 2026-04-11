[English](README.md) | **中文**

# Virtio Linux 演示 - 2 个 SMP 分区（amd64）

## 概述

本演示展示了在 PRTOS Type-1 Hypervisor 上进行的 **Virtio 设备虚拟化**：两个 SMP Linux 分区通过共享内存在 amd64（x86_64）平台上通信，底层使用硬件辅助虚拟化（Intel VT-x / VMX）。

**System 分区** 拥有全部硬件资源（PCI、传统 I/O、IRQ），并运行 virtio 后端守护进程，通过共享内存向 **Guest 分区** 提供虚拟设备服务。Guest 运行一个 **用户态前端守护进程**（`virtio_frontend`），将自定义共享内存协议桥接到标准 Linux 设备（`/dev/vda` 通过 NBD，`/dev/hvc0` 通过 PTY，`tap0`/`tap1`/`tap2` 通过 TUN/TAP）。两个分区都运行完整的 Linux（内核 6.19.9），并支持双控制台：System 使用 UART，Guest 使用 VGA+telnet。所有服务均通过 init 脚本自动启动。

## 架构

```
┌────────────────────────────────────────────────────────────────┐
│  L0: Host Linux + QEMU (x86_64, KVM, 4 pCPU, 1024MB RAM)       │
├────────────────────────────────────────────────────────────────┤
│  L1: PRTOS Type-1 Hypervisor (12MB @ 0x1000000)                │
│  ┌───────────────────────┐  ┌────────────────────────┐         │
│  │ Partition 0 (System)  │  │ Partition 1 (Guest)    │         │
│  │ Linux + Virtio Backend│  │ Linux + Virtio Frontend│         │
│  │ 2 vCPU (pCPU 0-1)     │  │ 2 vCPU (pCPU 2-3)      │         │
│  │ 128MB @ 0x6000000     │  │ 128MB @ 0xE000000      │         │
│  │ console=ttyS0 (UART)  │  │ console=tty0 (VGA)     │         │
│  │                       │  │ + ttyS1 (COM2/telnet)  │         │
│  │ 自动启动服务：         │  │                        │         │
│  │ - prtos_manager       │  │ Virtio Frontend：      │         │
│  │ - virtio_backend      │  │ - virtio_frontend      │         │
│  │   - Console backend   │  │   - NBD (/dev/vda)     │         │
│  │   - 3x Net backend    │  │   - PTY (/dev/hvc0)    │         │
│  │   - Blk backend       │  │   - TAP (tap0/1/2)     │         │
│  │   tap0: 10.0.1.1/24   │  │   tap0: 10.0.1.2/24    │         │
│  │   tap1: 10.0.2.1/24   │  │   tap1: 10.0.2.2/24    │         │
│  │   tap2: 10.0.3.1/24   │  │   tap2: 10.0.3.2/24    │         │
│  │                       │  │ - /opt/virtio_test.sh  │         │
│  └──────────┬────────────┘  └──────────┬─────────────┘         │
│             │        Shared Memory     │                       │
│             │  ┌──────────────────────┐│                       │
│             └──┤ ~5.25MB @ 0x16000000 ├┘                       │
│                │ 5 个 Virtio 区域     │                        │
│                └──────────────────────┘                        │
│                                                                │
│  IPVI 0-4: Guest→System（每个设备一个 doorbell）               │
│  IPVI 5:   System→Guest（完成通知 doorbell）                   │
└────────────────────────────────────────────────────────────────┘
```

## 内存布局

| 区域 | GPA 起始地址 | 大小 | GPA 结束地址 |
|---------------------|-------------|--------|-------------|
| PRTOS Hypervisor    | 0x01000000  | 12MB   | 0x01BFFFFF  |
| System Partition    | 0x06000000  | 128MB  | 0x0DFFFFFF  |
| Guest Partition     | 0x0E000000  | 128MB  | 0x15FFFFFF  |
| Shared Memory       | 0x16000000  | ~5.25MB| 0x1653FFFF  |
| **QEMU RAM 总量**   |             | 1024MB |             |

所有地址都位于前 1GB 范围内，这是 PRTOS EPT 恒等映射所必需的（单个 PDPT 条目覆盖 `0x0–0x3FFFFFFF`）。

## 共享内存布局（5 个区域，基址为 GPA 0x16000000+）

| 区域 | GPA 起始地址 | 大小 | 说明 |
|-------------|-------------|--------|---------------------------------|
| Virtio_Net0 | 0x16000000  | 1MB    | virtio-net bridge（TAP 后端） |
| Virtio_Net1 | 0x16100000  | 1MB    | virtio-net NAT（loopback） |
| Virtio_Net2 | 0x16200000  | 1MB    | virtio-net p2p（loopback） |
| Virtio_Blk  | 0x16300000  | 2MB    | virtio-blk（文件或 RAM disk） |
| Virtio_Con  | 0x16500000  | 256KB  | virtio-console（字符环形缓冲区） |

每个区域都在 XML 配置中使用 `flags="shared"`，并被 EPT 映射到两个分区。共享内存 **不** 在 e820 内存映射中，而是通过 Linux 用户态对 `/dev/mem` 进行 `mmap` 访问，以规避内核 sparse-memory 相关问题。

## CPU 分配（SMP）

| 物理 CPU | 分区 | 虚拟 CPU |
|-------------|-------------------|-------------|
| pCPU 0      | System (P0)       | vCPU 0      |
| pCPU 1      | System (P0)       | vCPU 1      |
| pCPU 2      | Guest  (P1)       | vCPU 0      |
| pCPU 3      | Guest  (P1)       | vCPU 1      |

调度器：10ms major frame，采用专用 pCPU 映射（每个 pCPU 始终运行一个 vCPU）。

## 控制台分配

| 分区 | 控制台 | QEMU 设备 | 访问方式 |
|-----------|----------|-------------------------------------|--------------------------|
| System    | UART     | `-serial mon:stdio`                 | 终端（SSH） |
| Guest     | VGA      | `-vga std -vnc :1`                  | VNC `localhost:5901` |
| Guest     | COM2     | `-serial telnet::4321,server,nowait`| `telnet localhost 4321` |

Guest 分区使用 `set_serial_poll`（ioctl `TIOCSSERIAL`，参数 `irq=0`）为 COM2/ttyS1 启用轮询模式，因为 PRTOS 不会将 IRQ3 路由给 Guest。init 脚本 `S99virtio_guest` 会自动在 ttyS1 上启动 `getty`。

## Virtio 设备

### Virtio-Console
- **机制**：共享内存中的 4KB 字符环形缓冲区（`Virtio_Con`）+ 4321 端口上的 TCP telnet 桥接
- **Guest 设备**：`/dev/hvc0`（由 `virtio_frontend` 创建 PTY 对）
- **数据流（TX）**：Guest 向 `/dev/hvc0` 写入 → `virtio_frontend` 复制到共享内存中的 `tx_buf` → 后端读取并输出到 System UART，同时发送给 TCP telnet 客户端
- **数据流（RX）**：TCP telnet 客户端发送数据 → 后端写入共享内存中的 `rx_buf` → Frontend 读取 `rx_buf` → 写入 PTY master → Guest 从 `/dev/hvc0` 读取
- **Telnet 访问**：在 System 分区 shell 中执行 `telnet 127.0.0.1 4321`（登录：`root` / `1234`）
- **验证**：执行 `echo "Hello PRTOS" > /dev/hvc0`（在 Guest 中）→ 内容会出现在 System 控制台和 telnet 客户端中

### Virtio-Net（×3）
- **机制**：每个实例一个 64 槽的数据包环形缓冲区（每槽最大 1536 字节），通过 TUN/TAP 在两个分区之间桥接
- **Net0**：System `tap0`（10.0.1.1）↔ 共享内存 ↔ Guest `tap0`（10.0.1.2）
- **Net1**：System `tap1`（10.0.2.1）↔ 共享内存 ↔ Guest `tap1`（10.0.2.2）
- **Net2**：System `tap2`（10.0.3.1）↔ 共享内存 ↔ Guest `tap2`（10.0.3.2）
- **数据流**：Guest TAP → 共享内存中的 `tx_slots` → 后端读取 → 后端 TAP（反向 RX 同理）
- **验证**：在 Guest 中执行 `ping 10.0.x.1`，或在 System 中执行 `ping 10.0.x.2`

### Virtio-Blk
- **机制**：16 槽块请求环（按扇区寻址，每扇区 512B）
- **后端**：文件后端磁盘（`disk.img`）或 1MB 内存 RAM disk（默认回退）
- **Guest 设备**：`/dev/vda`（指向 `/dev/nbd0` 的符号链接，由 `virtio_frontend` 通过 NBD 协议提供）
- **操作**：IN（读）、OUT（写）、FLUSH、GET_ID
- **验证**：测试脚本 `virtio_test.sh` 会在 `/dev/vda` 上创建 ext2 文件系统，完成挂载、写入测试文件并校验内容

## IP 地址分配

| 网络 | System（tap） | Guest（tap） | 子网 |
|---------|-------------|------------|--------|
| Net0    | 10.0.1.1    | 10.0.1.2   | /24    |
| Net1    | 10.0.2.1    | 10.0.2.2   | /24    |
| Net2    | 10.0.3.1    | 10.0.3.2   | /24    |

IP 地址由 init 脚本自动分配：System 侧为 `S99virtio_backend`，Guest 侧为 `S99virtio_guest`。

## 分区间通信（IPVI）

| IPVI ID | 方向 | 用途 |
|---------|--------------------|------------------------------|
| 0       | Guest → System     | virtio-net0（bridge）doorbell |
| 1       | Guest → System     | virtio-net1（NAT）doorbell |
| 2       | Guest → System     | virtio-net2（p2p）doorbell |
| 3       | Guest → System     | virtio-blk doorbell |
| 4       | Guest → System     | virtio-console doorbell |
| 5       | System → Guest     | 完成通知 |

此外，还有两个 **Sampling Channel** 用于控制面消息传递：
- `GuestToSystem`：Guest → System（8B 消息）
- `SystemToGuest`：System → Guest（8B 消息）

## 硬件资源（System 分区）

System 分区拥有所有未保留的硬件资源。

**IRQ**（通过 XML 分配）：1、3、5、6、7、8、9、10、11、12、13、14、15
- 被 PRTOS 保留（不分配）：IRQ 2（PIC 级联）、IRQ 4（COM1/UART）、IRQ 24、26、27

**I/O 端口**（通过 XML 分配）：

| 范围 | 说明 |
|----------------|----------------------------|
| 0x00–0x1F      | DMA 控制器 |
| 0x40–0x43      | PIT 8254 定时器 |
| 0x60–0x64      | 键盘控制器 |
| 0x70–0x71      | RTC/CMOS |
| 0x80–0x8F      | DMA 页寄存器 |
| 0x3FD–0x3FF    | COM1 状态寄存器（部分） |
| 0xCF8–0xCFF    | PCI 配置空间 |

被 PRTOS 保留（不分配）：0x20–0x21（PIC 主片）、0xA0–0xA1（PIC 从片）、0x3F8–0x3FC（COM1 数据/控制）

**PCI 直通**（经由 QEMU，本演示仅作目标设备示范）：
- 3 个 `virtio-net-pci`，使用 `disable-modern=on`（强制 legacy INTx，不使用 MSI-X）
- 1 个 `virtio-blk-pci`（通过 `-drive file=disk.img,if=virtio,format=raw` 提供）

## 前置条件：Linux 内核与 Buildroot

该演示需要一个带嵌入式 initramfs（rootfs）的 Linux 内核。下面的步骤展示如何从源码构建二者。

### 第 1 步：构建 Buildroot rootfs

```bash
cd buildroot
make qemu_x86_64_defconfig
```

然后应用以下配置修改（`make menuconfig`）：

| 配置项 | 值 | 目的 |
|---|---|---|
| `BR2_TARGET_GENERIC_ROOT_PASSWD` | `1234` | root 登录密码 |
| `BR2_TARGET_ROOTFS_CPIO` | `y` | 生成用于内核嵌入的 `rootfs.cpio` |
| `BR2_PACKAGE_NBD` | `y` | NBD 客户端（`virtio_frontend` 需要） |
| `BR2_PACKAGE_NBD_CLIENT` | `y` | NBD 客户端二进制 |
| `BR2_PACKAGE_HTOP` | `y` | 系统监控（可选） |
| `BR2_PACKAGE_NCURSES` | `y` | htop 所需终端库 |

```bash
make -j$(nproc)
# 输出：output/images/rootfs.cpio（约 12MB）
```

### 第 2 步：构建带嵌入式 initramfs 的 Linux 内核

```bash
cd linux-6.19.9
make x86_64_defconfig
```

然后应用额外的内核配置（`make menuconfig`）：

| 配置项 | 值 | 目的 |
|---|---|---|
| `CONFIG_BLK_DEV_NBD` | `y` | NBD 块设备（用于 `/dev/nbd0` → `/dev/vda`） |
| `CONFIG_TUN` | `y` | TUN/TAP 设备（用于 virtio-net 的 TAP 接口） |
| `CONFIG_INITRAMFS_SOURCE` | `/path/to/buildroot/output/images/rootfs.cpio` | 将 rootfs 嵌入 bzImage |

```bash
make -j$(nproc) bzImage
# 输出：arch/x86/boot/bzImage（带嵌入 initramfs 后约 19MB）
```

将构建好的内核复制到该演示期望的位置（参见 `Makefile` 中的 `BZIMAGE` 变量）。

### 第 3 步：构建 PRTOS Hypervisor

```bash
cd prtos-hypervisor
cp prtos_config.amd64 prtos_config
make defconfig
make
```

### 第 4 步：构建演示程序

```bash
cd user/bail/examples/virtio_linux_demo_2p_amd64
make
```

构建产物：
- `resident_sw.iso` — 可启动 ISO（GRUB + PRTOS + 两个分区）
- `virtio_backend` — System Linux 用户态使用的静态二进制
- `virtio_frontend` — Guest Linux 用户态使用的静态二进制（NBD + PTY bridge）
- `prtos_manager` — 分区管理 CLI 的静态二进制
- `rootfs_overlay.cpio` — System 覆盖层（backend + manager + `S99virtio_backend` init）
- `guest_rootfs_overlay.cpio` — Guest 覆盖层（frontend + 测试脚本 + `S99virtio_guest` init）
- `disk.img` — 64MB 原始块设备镜像（由演示目标生成）

## 运行

### 基础模式（UART + VGA + Telnet）
```bash
make run.amd64
```
会打开三个访问入口：
- **终端**（当前窗口）：System 分区 COM1 登录（`root`/`1234`）
- **VNC** `vnc://localhost:5901`：Guest 分区 VGA 显示
- **Telnet** `telnet localhost 4321`：Guest 分区 COM2 登录（`root`/`1234`）

### 无图形模式（自动化测试）
```bash
make run.amd64.nographic
# 或者：
make run.amd64.kvm.nographic
```
仅在 stdio 上输出 System UART。测试框架使用该模式。

### 完整演示模式（包含 PCI 设备）
```bash
# 启用 TAP 网络（需要 root 权限用于 tap0）：
make run.amd64.demo

# 不启用 TAP（仅 NAT，不需要 root）：
make run.amd64.demo.nat
```
该模式会为 System 分区增加 QEMU PCI 设备（`virtio-net-pci` ×3 + `virtio-blk-pci`），并使用 `disable-modern=on,vectors=0`。由于 PRTOS 不支持向 L2 分区路由 MSI-X，因此关闭 MSI-X（`vectors=0`）。

### 手动 QEMU 命令
```bash
qemu-system-x86_64 -enable-kvm -cpu host,-waitpkg \
    -m 1024 -smp 4 \
    -cdrom resident_sw.iso \
    -serial mon:stdio \
    -serial telnet::4321,server,nowait \
    -vga std -display none -vnc :1 \
    -boot d
```

## 演示流程

所有 virtio 服务都通过 init 脚本自动启动：System 侧为 `S99virtio_backend`，Guest 侧为 `S99virtio_guest`。不需要手动启动 backend 或 frontend。

### 第 1 步：启动 QEMU
```bash
make run.amd64
```

### 第 2 步：启动 System 分区（UART/stdio）
System 会通过 `S99virtio_backend` 自动启动 `prtos_manager` 和 `virtio_backend`（输出重定向到 `/var/log/`）：
```
=== PRTOS System Partition ===

Welcome to Buildroot
buildroot login: root
Password: 1234
```

### 第 3 步：访问 Guest 分区

有两种方式访问 Guest 控制台：

**方式 A：从宿主机访问**（通过 QEMU COM2）：
```bash
telnet localhost 4321
# 登录：root / 1234
```

**方式 B：从 System 分区 shell 访问**（通过 TCP bridge）：
```bash
# 登录 System 之后（第 2 步）：
telnet 127.0.0.1 4321
# 登录：root / 1234
```
virtio 后端守护进程会在 System 分区内部监听 TCP 4321 端口，在 TCP socket 和 Guest 的 `/dev/hvc0` 之间通过共享内存进行双向桥接。

Guest 会通过 `S99virtio_guest` 自动启动 `virtio_frontend`。前端会等待后端完成共享内存初始化（最多轮询 300 秒以检查 magic 值），之后创建 `/dev/nbd0`（块设备）和 `/dev/hvc0`（控制台）。

### 第 4 步：测试 Virtio 设备（Guest）
```bash
/opt/virtio_test.sh   # 对所有 virtio 设备执行自动化测试
```
预期输出包括：
- 网络：3 个 TAP 接口（tap0/tap1/tap2）及其 IP，所有到 System 分区的 ping 均成功
- 块设备（`/dev/vda` → `/dev/nbd0`）：成功创建 ext2 文件系统、挂载、写入并校验测试文件
- 控制台（`/dev/hvc0`）：消息 `Hello PRTOS from Guest!` 被转发到 System UART
- 共享内存 magic 值校验成功（NET0=`0x4E455430`，BLK0=`0x424C4B30`，CONS=`0x434F4E53`）
- `Verification Passed`

## 测试

```bash
# 自动登录测试：
python3 test_login.py

# COM2/telnet 测试：
python3 test_com2.py

# 控制台测试（干净输出、退格、tab 补全）：
python3 test_console.py

# 通过测试框架：
cd ../../../../  # 返回 prtos-hypervisor 根目录
bash scripts/run_test.sh --arch amd64 check-virtio_linux_demo_2p_amd64

# 完整 amd64 测试套件：
bash scripts/run_test.sh --arch amd64 check-all
```

## 文件结构

| 文件 / 目录 | 说明 |
|-----------------|-------------|
| `config/resident_sw.xml` | PRTOS 系统配置（2 个 SMP 分区、5 个共享内存区域、6 个 IPVI、双控制台） |
| `prtos_cf.amd64.xml` | 符号链接 → `config/resident_sw.xml` |
| `Makefile` | 构建系统（分区、backend、manager、CPIO 覆盖层、ISO、QEMU 目标） |
| `start_system.S` | System 分区启动 stub（UART 调试、e820、bzImage+initrd） |
| `start_guest.S` | Guest 分区启动 stub（VGA screen_info、e820、bzImage+initrd） |
| `hdr_system.c` | PRTOS 镜像头（System，magic `0x24584d69`） |
| `hdr_guest.c` | PRTOS 镜像头（Guest） |
| `linker_system.ld` | 链接脚本（System，基址 `0x6000000`，initrd 位于 `0xA000000`） |
| `linker_guest.ld` | 链接脚本（Guest，基址 `0xE000000`，initrd 位于 `0x12000000`） |
| `set_serial_poll.c` | 工具：通过 `ioctl TIOCSSERIAL` 将 COM2 强制设为 `irq=0` 轮询模式 |
| `test_login.py` | 自动化测试：启动 QEMU、COM1 登录、检查 `uname` |
| `test_com2.py` | 自动化测试：连接 COM2 telnet 并检查 Guest 登录提示 |
| `test_console.py` | 控制台测试：检查输出是否干净（无 backend 噪音），以及 COM2 telnet 的退格和 tab 补全 |
| **`system_partition/`** | |
| `  include/virtio_be.h` | 共享数据结构：`net_shm`（64 槽 ring）、`blk_shm`（16 槽 ring）、`console_shm`（4KB ring） |
| `  src/main.c` | 后端守护进程：通过 `/dev/mem` 映射 5 个区域，初始化全部设备，1ms 轮询循环 |
| `  src/virtio_console.c` | 控制台后端：轮询 `tx_buf` ring → `putchar` + 在 4321 端口提供 TCP telnet bridge（双向） |
| `  src/virtio_net.c` | 网络后端：bridge 使用 TAP，NAT/p2p 使用 loopback |
| `  src/virtio_blk.c` | 块后端：文件后端（`pread`/`pwrite`）或 1MB RAM disk |
| `  src/doorbell.c` | 通过 hypercall 进行 IPVI 信号通知（使用 IPVI 5 通知 Guest） |
| `  src/manager_if.c` | manager 封装：查询 Guest 分区状态 |
| `  rootfs_overlay/etc/init.d/S99virtio_backend` | init 脚本：创建设备 `/dev/net/tun`，自动启动 `prtos_manager` 和 `virtio_backend`，并配置 TAP IP |
| **`lib_prtos_manager/`** | |
| `  include/prtos_hv.h` | Hypercall API：内联 `vmcall`，44 个 hypercall 编号，状态结构体 |
| `  include/prtos_manager.h` | manager 设备接口 |
| `  common/prtos_hv.c` | Hypercall 实现：通过 `/dev/mem` 映射邮箱，封装 vmcall wrapper |
| `  common/prtos_manager.c` | 命令分发器：help、list、partition 操作、plan、write、quit |
| `  common/hypervisor.c` | 分区命令：list、halt、reset、resume、status、suspend |
| `  linux/prtos_manager_main.c` | Linux 主程序：stdin/stdout，支持 `-d` dry-run 模式 |
| **`guest_partition/`** | |
| `  src/virtio_frontend.c` | 用户态前端守护进程：通过 `/dev/mem` 映射共享内存，为 `/dev/nbd0` 创建 NBD 服务（块设备），为 `/dev/hvc0` 创建 PTY 对（控制台），为网络创建设备 TUN/TAP，并在检测后端就绪后进入轮询 |
| `  rootfs_overlay/etc/init.d/S99virtio_guest` | init 脚本：将 COM2 设为轮询、在 ttyS1 上启动 `getty`、启动 `virtio_frontend`、配置 TAP IP，并创建 `/dev/vda` 符号链接 |
| `  rootfs_overlay/opt/virtio_test.sh` | Guest 测试脚本：网络（3 个接口）、块设备、控制台、共享内存检查 |

## 设计说明

- **1024MB QEMU RAM**：总内存为 1GB。PRTOS 通过单个 PDPT 条目对前 1GB 做 EPT 恒等映射，因此所有分区和共享内存地址都必须低于 `0x40000000`。
- **双控制台**：System 分区使用 COM1/UART（stdio）；Guest 分区使用 VGA（VNC）+ COM2（telnet）。由于 PRTOS 不会将 IRQ3 路由给 Guest，因此 COM2 采用 `irq=0` 的轮询模式。
- **启用 x2APIC**：两个内核都在启动后切换到 x2APIC 模式（通过 MSR 访问 LAPIC）。PRTOS 会虚拟化完整的 x2APIC MSR 范围（0x800–0x83F），包括 64 位 ICR（MSR 0x830）和 Self-IPI（MSR 0x83F）。同时保留 APIC-access page，用于实模式 AP 启动（xAPIC 阶段，在切换到 x2APIC 之前）。位于 `0xFEC00000` 的 IOAPIC 通过 EPT fault trapping 实现虚拟化。
- **安静的 System 启动**：System 内核命令行包含 `quiet loglevel=0`，以压制启动日志，提供更干净的登录体验。
- **PCI legacy 模式**：演示目标通过 `disable-modern=on,vectors=0` 向 QEMU 添加 PCI 设备以强制使用 legacy INTx。`make run.amd64.demo` 目标也可使用 modern virtio。
- **网络**：每个 virtio-net 实例都使用一对 TUN/TAP 设备（System 一个、Guest 一个），并通过共享内存数据包环进行桥接。后端会通过 `mknod` 创建 `/dev/net/tun`，并为 3 个实例打开全部 TAP 设备。IP 地址由 init 脚本分配。
- **静态链接**：`virtio_backend`、`virtio_frontend` 和 `prtos_manager` 都采用静态链接，以便在分区 rootfs 中移植使用。
- **自动启动**：两个分区都通过 Buildroot init 脚本（`S99virtio_backend`、`S99virtio_guest`）在启动时自动拉起全部服务，无需手工干预。

## 依赖项

- **Linux 内核 6.19.9**，启用 `CONFIG_BLK_DEV_NBD=y`、`CONFIG_TUN=y`，并带有嵌入式 initramfs（见[前置条件](#前置条件linux-内核与-buildroot)）
- **Buildroot** rootfs，包含 NBD client，root 密码为 `1234`，输出格式为 CPIO
- **PRTOS Hypervisor** 已针对 amd64 构建（`cp prtos_config.amd64 prtos_config && make defconfig && make`）
- **QEMU**，带 KVM 支持（`qemu-system-x86_64`）
- **宿主机** 支持 Intel VT-x（VMX），且可访问 `/dev/kvm`

## Linux 内核命令行

**System 分区**（`start_system.S`）：
```
console=ttyS0,115200 earlyprintk=serial,ttyS0,115200 loglevel=7 nokaslr no_timer_check nohpet tsc=reliable clocksource=tsc nr_cpus=2 prtos_role=system prtos_shmem_base=0x16000000 prtos_shmem_size=0x540000
```

**Guest 分区**（`start_guest.S`）：
```
console=tty0 nokaslr no_timer_check nohpet tsc=reliable clocksource=tsc nr_cpus=2 prtos_role=guest prtos_shmem_base=0x16000000 prtos_shmem_size=0x540000
```

关键参数：
- `nr_cpus=2`：将内核限制为 2 个虚拟 CPU（与 PRTOS 分区配置一致）
- `no_timer_check nohpet tsc=reliable clocksource=tsc`：使用 TSC 作为时钟源（PRTOS 通过 IOAPIC 对 PIT 进行虚拟化，HPET 不可用）
- PMU 通过 CPUID leaf 0x0A 和 MSR trapping 被隐藏（LBR 0x680–0x6CF，Uncore 0x700–0x7FF，0xE00–0xE3F）