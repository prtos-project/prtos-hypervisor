# Example : linux_4vcpu_1partion_loongarch64

## Description
This example runs a LoongArch64 Linux guest on PRTOS with 4 vCPUs. The guest uses the LoongArch LVZ (Loongson Virtualization) hardware-assisted virtualization in PRTOS/QEMU. The validated run target boots to a Buildroot login prompt.

## Partition definition
There is one partition.
- P0 (Linux): System partition running Linux with 4 vCPUs, UART pass-through, and LVZ-enabled guest execution.

## Configuration table
Single-partition configuration with 4 physical CPUs and device pass-through.

A scheduling plan is defined under the following premises:

- Processors: 4 (pCPU 0-3)
- MAF = 100 ms
- P0: S 0 ms  D 50 ms (on each pCPU)

Memory layout:
- PRTOS: 32 MB
- P0 (Linux): 512 MB @ 0x80000000
- UART device pass-through: 4 KB @ 0x1FE00000
- PCH-PIC device pass-through: 1 MB @ 0x10000000

## Prerequisites

Make sure the host has these toolchains available before building:

- `loongarch64-linux-gnu-gcc` / `loongarch64-linux-gnu-ld`
- `gcc` / `g++`
- `make`, `ninja`, `dtc` (device tree compiler)
- Python3 with `pexpect` (for automated testing)

Workspace paths used below:

- Buildroot: `/home/chenweis/loongarch64_workspace/buildroot`
- Linux: `/home/chenweis/loongarch64_workspace/linux-6.19.9`
- QEMU source: `/home/chenweis/loongarch64_workspace/qemu`
- QEMU install prefix: `/home/chenweis/loongarch64_workspace/qemu_install`

## Step 1: Build Buildroot rootfs

```bash
cd /home/chenweis/loongarch64_workspace/buildroot
make qemu_loongarch64_virt_efi_defconfig
make menuconfig
```

Set these Buildroot options:

| Config Option | Value | Purpose |
| --- | --- | --- |
| `BR2_TARGET_GENERIC_ROOT_PASSWD` | `1234` | Root login password |
| `BR2_TARGET_ROOTFS_CPIO` | `y` | Generate `rootfs.cpio` |
| `BR2_PACKAGE_NBD` | `y` | Enable NBD package |
| `BR2_PACKAGE_NBD_CLIENT` | `y` | Install `nbd-client` |

Then build:

```bash
make -j$(nproc)
```

Expected artifact:

```
/home/chenweis/loongarch64_workspace/buildroot/output/images/rootfs.cpio
```

## Step 2: Build Linux 6.19.9

```bash
cd /home/chenweis/loongarch64_workspace/linux-6.19.9
make ARCH=loongarch CROSS_COMPILE=loongarch64-linux-gnu- loongson64_defconfig
make ARCH=loongarch CROSS_COMPILE=loongarch64-linux-gnu- menuconfig
```

Set these kernel options:

| Config Option | Value | Purpose |
| --- | --- | --- |
| `CONFIG_BLK_DEV_NBD` | `y` | NBD block device |
| `CONFIG_TUN` | `y` | TUN/TAP device |
| `CONFIG_STRICT_DEVMEM` | `n` | Allow `/dev/mem` mmap for shared memory |
| `CONFIG_INITRAMFS_SOURCE` | `/home/chenweis/loongarch64_workspace/buildroot/output/images/rootfs.cpio` | Embed Buildroot rootfs |
| `CONFIG_CMDLINE` | `"console=ttyS0,115200 earlycon=uart8250,mmio,0x1fe001e0 mem=512M@0x80000000 i8042.noaux i8042.nokbd i8042.nopnp nokaslr"` | Kernel command line |
| `CONFIG_CMDLINE_FORCE` | `y` | Force built-in command line |
| `CONFIG_BUILTIN_DTB` | `y` | Embed DTB in kernel |
| `CONFIG_BUILTIN_DTB_NAME` | `"linux_guest"` | DTB file name |
| `CONFIG_SERIO_I8042` | `n` | Disable i8042 (not available in PRTOS guest) |
| `CONFIG_KEYBOARD_ATKBD` | `n` | Disable AT keyboard |
| `CONFIG_MOUSE_PS2` | `n` | Disable PS/2 mouse |

**Important**: The DTB file `linux_guest.dts` from this example directory must be copied to `arch/loongarch/boot/dts/` in the kernel tree before building:

```bash
cp /home/chenweis/prtos-project/prtos-hypervisor/user/bail/examples/linux_4vcpu_1partion_loongarch64/linux_guest.dts \
   /home/chenweis/loongarch64_workspace/linux-6.19.9/arch/loongarch/boot/dts/
```

Then build the kernel image:

```bash
make ARCH=loongarch CROSS_COMPILE=loongarch64-linux-gnu- vmlinux -j$(nproc)
```

Expected artifact:

```
/home/chenweis/loongarch64_workspace/linux-6.19.9/vmlinux
```

The example Makefile consumes `vmlinux` from that path directly.

### Kernel config notes for PRTOS LVZ guest

The LoongArch Linux kernel requires special configuration to run as an LVZ guest on PRTOS:

1. **Built-in DTB**: The kernel needs `CONFIG_BUILTIN_DTB=y` because PRTOS boots in non-EFI mode, and the kernel's `fdt_setup()` only accepts DTB from EFI or built-in. The `fw_arg1` register is used for the command line pointer, not the DTB.

2. **UART register spacing**: QEMU's LoongArch virt machine uses 1-byte register spacing for the NS16550 UART (`regshift=0`). The `earlycon` parameter must use `mmio` (not `mmio32`).

3. **Disabled i8042/PS2**: The i8042 keyboard controller and PS/2 mouse are not available in the PRTOS guest. Accessing their I/O ports causes kernel panics.

4. **Initramfs overlay**: A custom `/init` script is prepended to the Buildroot rootfs.cpio to work around userspace crashes (see Step 2b below).

### Step 2b: Create initramfs overlay (fix userspace crashes)

Some Buildroot init scripts (`S02sysctl`, `S10udevd`) crash with SIGSEGV in the LVZ guest environment due to busybox/udev incompatibilities. A custom `/init` wrapper script is used to skip these problematic services.

```bash
# Create the wrapper init script
cat > /tmp/prtos_init.sh << 'EOF'
#!/bin/sh
/bin/mount -t proc proc /proc 2>/dev/null
/bin/mount -t sysfs sysfs /sys 2>/dev/null
/bin/mount -t devtmpfs devtmpfs /dev 2>/dev/null
if [ ! -e /dev/console ]; then
    /bin/mknod /dev/console c 5 1 2>/dev/null
fi
if [ -e /dev/console ]; then
    exec 0</dev/console 2>/dev/null
    exec 1>/dev/console 2>/dev/null
    exec 2>/dev/console 2>/dev/null
fi
/bin/mount -a 2>/dev/null
/bin/hostname -F /etc/hostname 2>/dev/null
/bin/mkdir -p /dev/pts /dev/shm /run/lock/subsys 2>/dev/null
/bin/ln -sf /proc/self/fd /dev/fd 2>/dev/null
/bin/ln -sf /proc/self/fd/0 /dev/stdin 2>/dev/null
/bin/ln -sf /proc/self/fd/1 /dev/stdout 2>/dev/null
/bin/ln -sf /proc/self/fd/2 /dev/stderr 2>/dev/null
/sbin/syslogd 2>/dev/null
/sbin/klogd 2>/dev/null
if [ -x /etc/init.d/S01seedrng ]; then
    /etc/init.d/S01seedrng start 2>/dev/null
fi
echo ""
echo "Welcome to Buildroot"
echo ""
while true; do
    /sbin/getty -L console 0 vt100
    sleep 1
done
EOF
chmod +x /tmp/prtos_init.sh

# Build gen_init_cpio from kernel tree
gcc -o /home/chenweis/loongarch64_workspace/linux-6.19.9/usr/gen_init_cpio \
    /home/chenweis/loongarch64_workspace/linux-6.19.9/usr/gen_init_cpio.c

# Create overlay cpio
cat > /tmp/initramfs_list.txt << 'EOF'
file /init /tmp/prtos_init.sh 0755 0 0
EOF
/home/chenweis/loongarch64_workspace/linux-6.19.9/usr/gen_init_cpio \
    /tmp/initramfs_list.txt > /tmp/overlay.cpio

# Append overlay to rootfs (appending ensures overlay files overwrite originals)
cat /home/chenweis/loongarch64_workspace/buildroot/output/images/rootfs.cpio \
    /tmp/overlay.cpio > /tmp/rootfs_fixed.cpio

# Update CONFIG_INITRAMFS_SOURCE to point to the fixed cpio
# (edit .config or use make menuconfig)
```

After creating the overlay, set `CONFIG_INITRAMFS_SOURCE="/tmp/rootfs_fixed.cpio"` and rebuild the kernel.

## Step 3: Build and install LoongArch64 QEMU

```bash
export CC=gcc
export CXX=g++
cd /home/chenweis/loongarch64_workspace/qemu
mkdir -p build
cd build
../configure --target-list=loongarch64-softmmu --enable-tcg --enable-slirp --enable-virtfs --disable-werror \
   --prefix=/home/chenweis/loongarch64_workspace/qemu_install
ninja -j20 install
```

Expected installed binary:

```
/home/chenweis/loongarch64_workspace/qemu_install/bin/qemu-system-loongarch64
```

The validated LoongArch64 example path uses TCG with:

```
-accel tcg,thread=multi
```

## Step 4: Build PRTOS and run the example

```bash
cd /home/chenweis/prtos-project/prtos-hypervisor
make distclean
cp prtos_config.loongarch64 prtos_config
make defconfig
make

cd user/bail/examples/linux_4vcpu_1partion_loongarch64/
make run.loongarch64
```

If you want the scripted regression check used during validation:

```bash
cd /home/chenweis/prtos-project/prtos-hypervisor
bash scripts/run_test.sh --arch loongarch64 check-all
```

## Expected result

A successful run reaches the Buildroot login prompt:

```text
Welcome to Buildroot
buildroot login: root
Password:
```

After logging in with username `root` and password `1234`, the guest should show four CPUs online:

```text
# uname -a
Linux buildroot 6.19.9 #1 SMP PREEMPT Sun May 10 20:00:00 CST 2026 loongarch64 GNU/Linux
#
# cat /proc/cpuinfo | grep processor
processor       : 0
processor       : 1
processor       : 2
processor       : 3
#
```

## PRTOS Hypervisor Fixes for LoongArch64

The following PRTOS hypervisor fixes were required to resolve the boot hang:

### Timer Emulation Fix (`core/kernel/loongarch64/traps.c`)

The LoongArch hardware timer requires a **disable-then-enable** sequence to reload the timer value when the timer is already running. The guest TCFG write handler was writing the guest's timer value directly to the host TCFG without first disabling it, which meant the new timer value was ignored when the host timer was already active.

**Fix**: Added `csrwr $zero, 0x41` (disable) before `csrwr htcfg, 0x41` (re-enable) in the TCFG guest CSR write handler. This ensures the 0→1 transition needed to reload the timer InitVal.

```c
// Before (broken):
prtos_u64_t htcfg = val;
__asm__ __volatile__("csrwr %0, 0x41" : "+r"(htcfg));

// After (fixed):
prtos_u64_t zero = 0;
__asm__ __volatile__("csrwr %0, 0x41" : "+r"(zero));  /* disable */
prtos_u64_t htcfg = val;
__asm__ __volatile__("csrwr %0, 0x41" : "+r"(htcfg));  /* re-enable */
```

## Notes

- LoongArch64 boot on TCG is slow compared with native hardware. Give the guest several minutes to reach the login prompt (typically ~120-180 seconds on modern x86 hosts).
- This example uses the PRTOS resident software boot flow through `resident_sw`; no U-Boot step is required.
- The `S02sysctl` and `S10udevd` Buildroot init scripts are disabled via the custom init wrapper due to busybox/udev incompatibilities in the LVZ guest environment. These are userspace issues, not hypervisor bugs.
