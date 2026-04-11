#!/bin/sh
#
# diag_tcp_bridge.sh - Diagnostic script for virtio-console TCP bridge.
# Run inside System partition to check if the TCP bridge on port 4321 is working.
#
# Usage: /opt/diag_tcp_bridge.sh
#

echo "============================================"
echo "  Virtio Console TCP Bridge Diagnostics"
echo "============================================"
echo ""

# 1. Check if virtio_backend is running
echo "[1] Checking virtio_backend process..."
BACKEND_PID=$(pidof virtio_backend 2>/dev/null)
if [ -n "$BACKEND_PID" ]; then
    echo "    PASS: virtio_backend is running (PID: $BACKEND_PID)"
else
    echo "    FAIL: virtio_backend is NOT running"
    echo "    Fix: /usr/bin/virtio_backend &"
fi
echo ""

# 2. Check TCP port 4321
echo "[2] Checking TCP port 4321..."
if [ -f /proc/net/tcp ]; then
    # Port 4321 = 0x10E1, look for it in /proc/net/tcp
    # Format: local_address (hex IP:hex port)
    PORT_HEX="10E1"
    if grep -qi ":${PORT_HEX} " /proc/net/tcp 2>/dev/null; then
        echo "    PASS: Port 4321 is in /proc/net/tcp"
        grep -i ":${PORT_HEX} " /proc/net/tcp | while read line; do
            state=$(echo "$line" | awk '{print $4}')
            case "$state" in
                0A) echo "    State: LISTEN" ;;
                01) echo "    State: ESTABLISHED" ;;
                *)  echo "    State: $state" ;;
            esac
        done
    else
        echo "    FAIL: Port 4321 is NOT in /proc/net/tcp"
        echo "    Current listening ports:"
        cat /proc/net/tcp | head -5
    fi
else
    echo "    WARN: /proc/net/tcp not available"
fi
echo ""

# 3. Check /dev/mem
echo "[3] Checking /dev/mem access..."
if [ -c /dev/mem ]; then
    echo "    PASS: /dev/mem exists"
    ls -la /dev/mem
else
    echo "    FAIL: /dev/mem does NOT exist"
fi
echo ""

# 4. Check console shared memory magic (0x20500000)
echo "[4] Checking Virtio_Con shared memory (0x20500000)..."
if command -v devmem >/dev/null 2>&1; then
    MAGIC=$(devmem 0x20500000 32 2>/dev/null)
    if [ "$MAGIC" = "0x434F4E53" ]; then
        echo "    PASS: Console SHM magic = $MAGIC (CONS)"
    elif [ -n "$MAGIC" ]; then
        echo "    WARN: Console SHM magic = $MAGIC (expected 0x434F4E53)"
    else
        echo "    FAIL: Cannot read console SHM at 0x20500000"
    fi

    # Check backend_ready field (offset 12 = 0x0C)
    READY=$(devmem 0x2050000C 32 2>/dev/null)
    echo "    backend_ready = $READY (expect 0x00000001)"
else
    echo "    devmem not available, trying dd..."
    MAGIC_HEX=$(dd if=/dev/mem bs=4 count=1 skip=$((0x20500000/4)) 2>/dev/null | od -A n -t x4 | tr -d ' ')
    if [ -n "$MAGIC_HEX" ]; then
        echo "    Console SHM magic (dd) = 0x$MAGIC_HEX"
    else
        echo "    FAIL: Cannot read /dev/mem at offset 0x20500000"
    fi
fi
echo ""

# 5. Check virtio_backend file descriptors
echo "[5] Checking virtio_backend file descriptors..."
if [ -n "$BACKEND_PID" ] && [ -d "/proc/$BACKEND_PID/fd" ]; then
    echo "    Open FDs:"
    ls -la /proc/$BACKEND_PID/fd 2>/dev/null | while read line; do
        echo "    $line"
    done
    echo ""
    echo "    Socket info (/proc/$BACKEND_PID/net/tcp):"
    cat /proc/$BACKEND_PID/net/tcp 2>/dev/null | head -5
else
    echo "    Cannot inspect FDs (process not running or /proc not accessible)"
fi
echo ""

# 6. Quick connectivity test
echo "[6] Testing TCP connection to 127.0.0.1:4321..."
if command -v nc >/dev/null 2>&1; then
    if echo "" | nc -w 2 127.0.0.1 4321 >/dev/null 2>&1; then
        echo "    PASS: Port 4321 is accepting connections"
    else
        echo "    FAIL: Port 4321 refused connection"
    fi
elif command -v telnet >/dev/null 2>&1; then
    echo "    Using telnet (will hang if connected, Ctrl-] to exit)..."
    echo "    Run: telnet 127.0.0.1 4321"
else
    echo "    WARN: Neither nc nor telnet available for connectivity test"
    echo "    Trying bash /dev/tcp..."
    if (echo "" > /dev/tcp/127.0.0.1/4321) 2>/dev/null; then
        echo "    PASS: Port 4321 is accepting connections (bash /dev/tcp)"
    else
        echo "    FAIL: Port 4321 refused connection"
    fi
fi
echo ""

# 7. Check Guest partition status
echo "[7] Checking Guest partition (virtio_frontend status)..."
# Check net0 shared memory magic at 0x20000000
if command -v devmem >/dev/null 2>&1; then
    NET0_MAGIC=$(devmem 0x20000000 32 2>/dev/null)
    NET0_FE_READY=$(devmem 0x20000010 32 2>/dev/null)  # frontend_ready offset
    echo "    Net0 SHM magic = $NET0_MAGIC (expect 0x4E455430)"
    echo "    Net0 frontend_ready = $NET0_FE_READY"
    
    CON_FE_READY=$(devmem 0x20500010 32 2>/dev/null)  # console frontend_ready
    echo "    Console frontend_ready = $CON_FE_READY"
fi
echo ""

echo "============================================"
echo "  Done"
echo "============================================"
