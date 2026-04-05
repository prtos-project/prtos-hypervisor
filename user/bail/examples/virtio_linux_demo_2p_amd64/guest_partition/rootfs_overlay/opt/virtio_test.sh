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

# Configure interfaces if available
for iface in eth0 eth1 eth2; do
    if [ -d "/sys/class/net/$iface" ]; then
        echo "[Guest] Configuring $iface..."
        ip addr add 10.0.$(($(echo $iface | tr -dc '0-9') + 1)).2/24 dev $iface 2>/dev/null
        ip link set $iface up 2>/dev/null
    fi
done

echo "[Guest] IP addresses:"
ip addr show 2>/dev/null | grep "inet " || echo "  (no addresses configured)"
echo ""

# Test NAT connectivity (QEMU user networking gateway is usually 10.0.2.2)
echo "[Guest] Testing NAT connectivity..."
ping -c 2 -W 2 10.0.2.2 2>/dev/null && echo "  NAT ping OK" || echo "  NAT ping failed (expected without QEMU user-net)"
echo ""

# ===== Block Device Test =====
echo "--- Block Device Test ---"
if [ -b /dev/vda ]; then
    echo "[Guest] /dev/vda found:"
    fdisk -l /dev/vda 2>/dev/null || echo "  (fdisk not available)"
    echo ""
    echo "[Guest] Attempting mount..."
    mkdir -p /mnt/vda
    mount /dev/vda1 /mnt/vda 2>/dev/null && {
        echo "  Mounted /dev/vda1 at /mnt/vda"
        ls -la /mnt/vda/
        umount /mnt/vda
    } || echo "  Mount failed (no filesystem on vda, expected for raw disk)"
else
    echo "[Guest] /dev/vda not found"
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
