# Example: linux_4vcpu_1partion_loongarch64

## Description

This example runs a single LoongArch64 Linux guest on PRTOS with 4 vCPUs.
The guest boots from a Linux `vmlinux` image that already embeds its Buildroot
initramfs, and QEMU runs it through the LoongArch64 LVZ path.

The maintained build flow is:

1. build Buildroot rootfs
2. build Linux with that rootfs embedded
3. build/install LoongArch64 QEMU
4. build PRTOS and run the example

Do not use the older `/tmp/rootfs_fixed.cpio`, `rootfs_repack`, or
`initramfs_extras.txt` overlay workflow. The current tree builds a deterministic
guest image directly from the tracked Buildroot and Linux outputs.

## Paths used by this example

- Buildroot: `/home/chenweis/hdd/Repo/loongarch64_linux_workspace/buildroot`
- Linux: `/home/chenweis/hdd/Repo/loongarch64_linux_workspace/linux-6.19.9`
- QEMU source: `/home/chenweis/hdd/Repo/loongarch64_linux_workspace/qemu`
- QEMU install prefix: `/home/chenweis/hdd/Repo/loongarch64_linux_workspace/qemu_install`
- PRTOS repo: `/home/chenweis/hdd/Repo/fork_os/prtos-hypervisor`

## Prerequisites

Required host tools:

- `loongarch64-linux-gnu-gcc`, `loongarch64-linux-gnu-ld`, `loongarch64-linux-gnu-objcopy`, `loongarch64-linux-gnu-nm`
- `gcc`, `g++`
- `make`, `ninja`, `dtc`, `sed`, `file`, `gzip`
- Python3 with `pexpect`

## Step 1: Build Buildroot and Linux

The repository provides a helper script that configures both Buildroot and the
guest Linux kernel with the expected options.

```bash
bash /home/chenweis/hdd/Repo/loongarch64_linux_workspace/buildroot_linux_configure_build.sh
```

What this script does:

- configures Buildroot from `qemu_loongarch64_virt_efi_defconfig`
- sets the root password to `1234`
- enables `NBD`, `nbd-client`, and `htop`
- installs a rootfs overlay that keeps only loopback networking and spawns a
    serial getty on `ttyS0`
- builds `rootfs.cpio`
- copies `linux_guest.dtb` into the kernel tree, or generates it from
    `partition.dts` if needed
- configures Linux with:
    - `CONFIG_INITRAMFS_SOURCE=/home/chenweis/hdd/Repo/loongarch64_linux_workspace/buildroot/output/images/rootfs.cpio`
    - `CONFIG_INITRAMFS_COMPRESSION_NONE=y`
    - `CONFIG_BLK_DEV_NBD=y`
    - `CONFIG_TUN=y`
    - `CONFIG_CMDLINE_FORCE=y`
    - `CONFIG_BUILTIN_DTB=y`
    - `CONFIG_BUILTIN_DTB_NAME="linux_guest"`
    - `CONFIG_SERIO_I8042=n`
    - `CONFIG_KEYBOARD_ATKBD=n`
    - `CONFIG_MOUSE_PS2=n`

Expected artifacts after this step:

- `/home/chenweis/hdd/Repo/loongarch64_linux_workspace/buildroot/output/images/rootfs.cpio`
- `/home/chenweis/hdd/Repo/loongarch64_linux_workspace/linux-6.19.9/vmlinux`

## Step 2: Build and install QEMU

```bash
export CC=gcc
export CXX=g++
cd /home/chenweis/hdd/Repo/loongarch64_linux_workspace/qemu/build
../configure --target-list=loongarch64-softmmu --enable-tcg --enable-slirp --enable-virtfs --disable-werror \
    --prefix=/home/chenweis/hdd/Repo/loongarch64_linux_workspace/qemu_install
ninja -j10
ninja install -j10
```

Expected installed binary:

```text
/home/chenweis/hdd/Repo/loongarch64_linux_workspace/qemu_install/bin/qemu-system-loongarch64
```

The current LoongArch64 example path should be run with single-thread TCG:

```text
-accel tcg,thread=single
```

The repository defaults already reflect that for this example.

## Step 3: Build PRTOS and run the example

```bash
cd /home/chenweis/hdd/Repo/fork_os/prtos-hypervisor
make distclean
cp prtos_config.loongarch64 prtos_config
make defconfig
make

cd user/bail/examples/linux_4vcpu_1partion_loongarch64
make run.loongarch64
```

For the scripted regression check:

```bash
cd /home/chenweis/hdd/Repo/fork_os/prtos-hypervisor
scripts/run_test.sh --arch loongarch64 check-linux_4vcpu_1partion_loongarch64
```

## Expected result

A successful run reaches the Buildroot login prompt:

```text
Welcome to Buildroot
buildroot login: root
Password:
```

After logging in with username `root` and password `1234`, the guest should show
four CPUs online and `htop` should run successfully.

Example session:

```text
# uname -a
Linux buildroot 6.19.9-gc017ea9ffcd3 #2 SMP PREEMPT Sun Mar 22 19:36:34 CST 2026 loongarch64 GNU/Linux
#
# cat /proc/cpuinfo | grep processor
processor       : 0
processor       : 1
processor       : 2
processor       : 3
#
```

## Notes

- The original `S02sysctl` segmentation fault was addressed by moving the guest
    image generation away from the older ad hoc initramfs overlay path and back
    to a Buildroot-generated rootfs with deterministic serial-login configuration.
- The guest kernel embeds the Buildroot rootfs directly; do not point
    `CONFIG_INITRAMFS_SOURCE` at temporary files under `/tmp`.
- LoongArch64 Linux boot under TCG is still substantially slower than native
    hardware, so regression runs need a generous timeout budget.
