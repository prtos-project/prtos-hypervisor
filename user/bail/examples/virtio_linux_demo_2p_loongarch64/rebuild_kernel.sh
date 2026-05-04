#!/bin/bash
# rebuild_kernel.sh - Re-pack /tmp/rootfs_repack with role-specific overlay
# files and rebuild the LoongArch64 kernel.
#
# Usage:  rebuild_kernel.sh <role> <linux_src_dir>
#   role : "system" | "guest"
#
# Side effects:
#   - Removes any previously staged overlay files from <ROOTFS_SOURCE>
#   - Stages the role-specific overlay files into <ROOTFS_SOURCE>
#   - Forces re-pack of usr/initramfs_data.cpio* in <linux_src_dir>
#   - Runs `make vmlinux` in <linux_src_dir>
#   - Copies <linux_src_dir>/vmlinux{,.unstripped} to ./vmlinux_<role>.{bin,unstripped}
#
# This is invoked from the Makefile to produce two distinct kernels:
# one with the System overlay (virtio_backend, prtos_manager,
# S99virtio_backend) and one with the Guest overlay (virtio_frontend,
# S99virtio_guest, virtio_test.sh, set_serial_poll).

set -e

ROLE="$1"
LINUX_SRC="$2"
ROOTFS_SOURCE="${ROOTFS_SOURCE:-/tmp/rootfs_repack}"
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"

if [[ "$ROLE" != "system" && "$ROLE" != "guest" ]]; then
    echo "[rebuild_kernel.sh] ERROR: role must be 'system' or 'guest', got '$ROLE'" >&2
    exit 1
fi
if [[ ! -d "$LINUX_SRC" ]]; then
    echo "[rebuild_kernel.sh] ERROR: linux_src_dir not a directory: $LINUX_SRC" >&2
    exit 1
fi
if [[ ! -d "$ROOTFS_SOURCE" ]]; then
    echo "[rebuild_kernel.sh] ERROR: ROOTFS_SOURCE not a directory: $ROOTFS_SOURCE" >&2
    exit 1
fi

cd "$DEMO_DIR"

# 1. Strip ALL previously-staged demo overlay files from the rootfs source.
MANAGED_FILES=(
    "$ROOTFS_SOURCE/usr/bin/virtio_backend"
    "$ROOTFS_SOURCE/usr/bin/virtio_frontend"
    "$ROOTFS_SOURCE/usr/bin/prtos_manager"
    "$ROOTFS_SOURCE/etc/init.d/S99virtio_backend"
    "$ROOTFS_SOURCE/etc/init.d/S99virtio_guest"
    "$ROOTFS_SOURCE/opt/virtio_test.sh"
    "$ROOTFS_SOURCE/opt/set_serial_poll"
)
for f in "${MANAGED_FILES[@]}"; do
    rm -f "$f"
done

# 2. Stage role-specific overlay files.
mkdir -p "$ROOTFS_SOURCE/usr/bin" "$ROOTFS_SOURCE/etc/init.d" "$ROOTFS_SOURCE/opt"
if [[ "$ROLE" == "system" ]]; then
    install -m 755 prtos_manager  "$ROOTFS_SOURCE/usr/bin/prtos_manager"
    install -m 755 virtio_backend "$ROOTFS_SOURCE/usr/bin/virtio_backend"
    install -m 755 system_partition/rootfs_overlay/etc/init.d/S99virtio_backend \
                    "$ROOTFS_SOURCE/etc/init.d/S99virtio_backend"
else
    install -m 755 virtio_frontend "$ROOTFS_SOURCE/usr/bin/virtio_frontend"
    install -m 755 set_serial_poll "$ROOTFS_SOURCE/opt/set_serial_poll"
    install -m 755 guest_partition/rootfs_overlay/opt/virtio_test.sh \
                    "$ROOTFS_SOURCE/opt/virtio_test.sh"
    install -m 755 guest_partition/rootfs_overlay/etc/init.d/S99virtio_guest \
                    "$ROOTFS_SOURCE/etc/init.d/S99virtio_guest"
fi
echo "[rebuild_kernel.sh] Staged $ROLE overlay into $ROOTFS_SOURCE"

# 3. Force re-pack of the embedded initramfs and rebuild the kernel.
#    Also swap CONFIG_BUILTIN_DTB_NAME so the System partition gets the
#    UART/earlycon-equipped DTB (prtos-virt) and the Guest partition gets
#    a UART-less DTB (prtos-virt-guest).  Without this swap, the Guest
#    Linux's earlycon writes to 0x1FE001E0 hit a non-mapped MMIO region
#    that the LVZ shim cannot service, hanging the partition before
#    userspace can come up.
cd "$LINUX_SRC"
if [[ "$ROLE" == "system" ]]; then
    DTB_NAME="prtos-virt-system"
else
    DTB_NAME="prtos-virt-guest"
fi
./scripts/config --set-str BUILTIN_DTB_NAME "$DTB_NAME"
rm -f usr/initramfs_data.cpio*
ARCH=loongarch CROSS_COMPILE=loongarch64-linux-gnu- \
    make -j"$(nproc)" olddefconfig >/dev/null
ARCH=loongarch CROSS_COMPILE=loongarch64-linux-gnu- \
    make -j"$(nproc)" vmlinux >/dev/null
echo "[rebuild_kernel.sh] Linux rebuilt with $ROLE overlay (BUILTIN_DTB=$DTB_NAME)"

# 4. Snapshot the kernel artifacts for the demo build.
cd "$DEMO_DIR"
cp -f "$LINUX_SRC/vmlinux.unstripped" "vmlinux_$ROLE.unstripped"
loongarch64-linux-gnu-objcopy -O binary "$LINUX_SRC/vmlinux.unstripped" "vmlinux_$ROLE.bin"
echo "[rebuild_kernel.sh] Wrote vmlinux_$ROLE.unstripped and vmlinux_$ROLE.bin ($(stat -c%s vmlinux_$ROLE.bin) bytes)"

# 5. Restore the Linux source tree to the default DTB (prtos-virt) AND remove
#    the role-specific overlay files we staged.  Other examples
#    (linux_4vcpu_1partion_loongarch64, mix_os_demo_loongarch64) consume
#    $LINUX_SRC/vmlinux.unstripped directly and expect the 4-CPU prtos-virt
#    DTB with the UART and pristine rootfs.  Without this restore, those
#    tests regress.
cd "$LINUX_SRC"
./scripts/config --set-str BUILTIN_DTB_NAME "prtos-virt"
for f in "${MANAGED_FILES[@]}"; do rm -f "$f"; done
rm -f usr/initramfs_data.cpio*
ARCH=loongarch CROSS_COMPILE=loongarch64-linux-gnu- \
    make -j"$(nproc)" olddefconfig >/dev/null
ARCH=loongarch CROSS_COMPILE=loongarch64-linux-gnu- \
    make -j"$(nproc)" vmlinux >/dev/null
echo "[rebuild_kernel.sh] Restored $LINUX_SRC/vmlinux.unstripped to prtos-virt default"
