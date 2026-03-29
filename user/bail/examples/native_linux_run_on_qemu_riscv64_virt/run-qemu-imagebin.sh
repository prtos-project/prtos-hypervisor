#!/bin/bash

KERNEL_PATH="./linux-6.19.9/arch/riscv/boot/Image.bin"
ROOTFS_PATH="./buildroot/output/images/rootfs.ext2"

echo "Using binary kernel image..."
echo "Kernel format: $(file $KERNEL_PATH)"
echo "Kernel size: $(ls -lh $KERNEL_PATH | awk '{print $5}')"
echo ""

qemu-system-riscv64 \
    -M virt \
    -cpu rv64 \
    -smp 4 \
    -m 1G \
    -bios default \
    -kernel "$KERNEL_PATH" \
    -append "root=/dev/vda rw console=ttyS0 earlycon" \
    -drive file="$ROOTFS_PATH",format=raw,id=hd0 \
    -device virtio-blk-device,drive=hd0 \
    -netdev user,id=net0 \
    -device virtio-net-device,netdev=net0 \
    -nographic
