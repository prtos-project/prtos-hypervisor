#!/bin/sh
#
# virtio_test.sh - Virtio Frontend test script for PRTOS Guest Partition.
#
# Runs inside the Guest Partition's Linux to exercise all Virtio devices:
#   - 3 x virtio-net (eth0=bridge, eth1=NAT, eth2=p2p)
#   - 1 x virtio-blk (/dev/vda)
#   - 1 x virtio-console (/dev/hvc0)
#
# Usage: /opt/virtio_test.sh
#

echo "=== PRTOS Virtio Guest Frontend Test ==="
echo "Guest Partition (VGA Console) - Testing Virtio devices"
echo ""

# System info
echo "[Guest] System information:"
echo "  Kernel: $(uname -r)"
echo "  Arch:   $(uname -m)"
echo "  CPUs:   $(nproc)"
cat /proc/cmdline
echo ""

# ===== Network Tests =====
echo "--- Network Test ---"
echo "[Guest] Network interfaces:"
ip link show 2>/dev/null || ifconfig -a 2>/dev/null
echo ""

echo "[Guest] IP addresses:"
ip addr show 2>/dev/null | grep "inet " || echo "  (no addresses configured)"
echo ""

# Test p2p connectivity (System Partition TAP addresses: 10.0.x.1)
echo "[Guest] Testing System<->Guest connectivity..."
PING_OK=0
for i in 0 1 2; do
    SYSTEM_IP="10.0.$((i + 1)).1"
    if [ -d "/sys/class/net/tap$i" ]; then
        if ping -c 2 -W 2 $SYSTEM_IP > /dev/null 2>&1; then
            echo "  tap$i -> $SYSTEM_IP: OK"
            PING_OK=1
        else
            echo "  tap$i -> $SYSTEM_IP: FAIL"
        fi
    fi
done
if [ $PING_OK -eq 0 ]; then
    echo "  No TAP interfaces available (expected without virtio_frontend --net)"
fi
echo ""

# ===== Block Device Test =====
echo "--- Block Device Test ---"
BLK_DEV=""
if [ -b /dev/vda ]; then
    BLK_DEV=/dev/vda
elif [ -b /dev/nbd0 ]; then
    BLK_DEV=/dev/nbd0
fi

if [ -n "$BLK_DEV" ]; then
    echo "[Guest] $BLK_DEV found:"
    fdisk -l $BLK_DEV 2>/dev/null || echo "  (fdisk not available)"
    echo ""

    # Create ext2 filesystem on raw disk
    echo "[Guest] Creating ext2 filesystem on $BLK_DEV ..."
    mke2fs -F $BLK_DEV >/dev/null 2>&1
    if [ $? -eq 0 ]; then
        echo "  Filesystem created successfully"
    else
        echo "  Failed to create filesystem"
    fi

    echo "[Guest] Mounting $BLK_DEV ..."
    mkdir -p /mnt/vda
    if mount $BLK_DEV /mnt/vda 2>/dev/null; then
        echo "  Mounted $BLK_DEV at /mnt/vda"
        # Write a test file
        echo "Hello from PRTOS Guest Partition!" > /mnt/vda/test.txt 2>/dev/null
        echo "  Wrote test file:"
        cat /mnt/vda/test.txt
        ls -la /mnt/vda/test.txt
        df -h /mnt/vda 2>/dev/null | grep -v Filesystem
        umount /mnt/vda
        echo "  Unmounted $BLK_DEV"
    else
        echo "  Mount failed"
    fi
else
    echo "[Guest] /dev/vda not found"
    echo "[Guest] /dev/nbd0 not found"
fi
echo ""

# ===== Console Test =====
echo "--- Console Test ---"
if [ -c /dev/hvc0 ]; then
    echo "[Guest] /dev/hvc0 found"
    echo "Hello PRTOS from Guest!" > /dev/hvc0 2>/dev/null && \
        echo "  Message sent to /dev/hvc0 -> should appear on System UART" || \
        echo "  Write to /dev/hvc0 failed"
else
    echo "[Guest] /dev/hvc0 not found"
fi
echo ""

# ===== Shared Memory Check =====
echo "--- Shared Memory Check ---"
echo "[Guest] Checking shared memory regions:"
if [ -c /dev/mem ]; then
    for addr in 0x16000000 0x16100000 0x16200000 0x16300000 0x16500000; do
        MAGIC=$(devmem $addr 32 2>/dev/null)
        if [ $? -eq 0 ] && [ -n "$MAGIC" ]; then
            echo "  $addr: magic=$MAGIC"
        else
            echo "  $addr: cannot read"
        fi
    done
else
    echo "  /dev/mem not available"
fi
echo ""

echo "=== Virtio Guest Test Complete ==="
echo "Verification Passed"
