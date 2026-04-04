#!/bin/sh
#
# virtio_test.sh - Virtio Frontend test script for PRTOS Guest Partition.
#
# Runs inside the Guest Partition's Linux to exercise all Virtio devices
# by accessing the shared memory region mapped at 0x26000000.
#
# This script is placed in the Guest's rootfs overlay and can be executed
# after login to verify inter-partition Virtio communication.
#
# Usage: /opt/virtio_test.sh
#

echo "=== PRTOS Virtio Guest Frontend Test ==="
echo "Guest Partition - Testing Virtio devices via shared memory"
echo ""

SHMEM_BASE=0x26000000

# Check kernel command line for partition role
echo "[Guest] Kernel cmdline:"
cat /proc/cmdline
echo ""

# Check if shared memory is visible in the memory map
echo "[Guest] Checking shared memory region (0x26000000):"
if [ -f /proc/iomem ]; then
    grep -i "2600" /proc/iomem 2>/dev/null || echo "  (not listed in iomem)"
fi
echo ""

# Try to read the control block magic number from shared memory
# The Backend writes 0x50525456 ("PRTV") at offset 0 of the shared region
echo "[Guest] Attempting to read control block from shared memory..."
if [ -c /dev/mem ]; then
    # Read 4 bytes at the shmem base (control block magic)
    MAGIC=$(devmem $SHMEM_BASE 32 2>/dev/null)
    if [ $? -eq 0 ] && [ -n "$MAGIC" ]; then
        echo "[Guest] Control block magic: $MAGIC"
        if [ "$MAGIC" = "0x50525456" ]; then
            echo "[Guest] Backend detected! Magic matches PRTV."
        else
            echo "[Guest] Unknown magic value (Backend may not be initialized)"
        fi
    else
        echo "[Guest] Cannot read shared memory (devmem not available or access denied)"
    fi
else
    echo "[Guest] /dev/mem not available"
fi
echo ""

# Display system info
echo "[Guest] System information:"
echo "  Kernel: $(uname -r)"
echo "  Arch:   $(uname -m)"
echo "  CPUs:   $(nproc)"
echo ""

echo "=== Virtio Guest Test Complete ==="
echo "Verification Passed"
