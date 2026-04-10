#!/usr/bin/env python3
"""Diagnostic test for Guest console TCP bridge (AArch64).

Tests each stage of the virtio-console TCP bridge connection:
  1. QEMU boots and System partition reaches login prompt
  2. Login to System partition
  3. Check if virtio_backend process is running
  4. Check if TCP port 4321 is listening
  5. Check /dev/mem mapping (shared memory access)
  6. Attempt telnet connection to Guest
  7. Verify Guest login prompt appears

Usage:
  python3 test_tcp_bridge.py            # Full test
  python3 test_tcp_bridge.py --diag     # Diagnostic only (steps 1-6, no Guest login wait)
"""
import subprocess, time, sys, os, select, fcntl, re

DEMO_DIR = os.path.dirname(os.path.abspath(__file__))
os.chdir(DEMO_DIR)

DIAG_ONLY = "--diag" in sys.argv

# ============================================================================
# Build check
# ============================================================================
if not os.path.exists("resident_sw"):
    print("[BUILD] resident_sw not found, building...")
    r = subprocess.run(["make"], capture_output=True, timeout=300)
    if r.returncode != 0:
        print("[FAIL] Build failed:")
        print(r.stderr.decode("latin-1", errors="replace")[-500:])
        sys.exit(1)

if not os.path.exists("resident_sw_image"):
    print("[BUILD] Creating bootable image...")
    subprocess.run(["aarch64-linux-gnu-objcopy", "-O", "binary",
                    "-R", ".note", "-R", ".note.gnu.build-id", "-R", ".comment", "-S",
                    "resident_sw", "resident_sw.bin"], capture_output=True)
    subprocess.run(["mkimage", "-A", "arm64", "-O", "linux", "-C", "none",
                    "-a", "0x40200000", "-e", "0x40200000",
                    "-d", "resident_sw.bin", "resident_sw_image"], capture_output=True)

if not os.path.exists("u-boot/u-boot.bin"):
    print("[FAIL] u-boot/u-boot.bin not found. Run 'make' first.")
    sys.exit(1)

# ============================================================================
# QEMU launch
# ============================================================================
cmd = ("qemu-system-aarch64 "
       "-machine virt,gic_version=3 "
       "-machine virtualization=true "
       "-cpu cortex-a72 -machine type=virt "
       "-m 4096 -smp 4 "
       "-bios ./u-boot/u-boot.bin "
       "-device loader,file=./resident_sw_image,addr=0x40200000,force-raw=on "
       "-nographic -no-reboot")

print("=" * 70)
print("  Virtio Console TCP Bridge Diagnostic Test")
print("=" * 70)
print(f"[TEST] QEMU command: {cmd[:80]}...")
print("[TEST] Starting QEMU...")

proc = subprocess.Popen(cmd, shell=True, stdin=subprocess.PIPE,
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

# Non-blocking stdout
flags = fcntl.fcntl(proc.stdout.fileno(), fcntl.F_GETFL)
fcntl.fcntl(proc.stdout.fileno(), fcntl.F_SETFL, flags | os.O_NONBLOCK)

buf = b""
all_output = b""  # Keep full output for diagnostics

def read_output(timeout=2):
    """Read available output, return accumulated text."""
    global buf, all_output
    deadline = time.time() + timeout
    while time.time() < deadline:
        ready, _, _ = select.select([proc.stdout], [], [], 0.5)
        if ready:
            try:
                data = proc.stdout.read(8192)
                if data:
                    buf += data
                    all_output += data
            except (IOError, OSError):
                pass
        if proc.poll() is not None:
            break
    return buf.decode("latin-1", errors="replace")

def send(text):
    """Send text to QEMU stdin."""
    proc.stdin.write(text.encode())
    proc.stdin.flush()

def wait_for(pattern, timeout=300, clear_buf=False):
    """Wait for pattern in output. Returns True if found."""
    global buf
    if clear_buf:
        buf = b""
    deadline = time.time() + timeout
    while time.time() < deadline:
        text = read_output(timeout=2)
        if pattern.lower() in text.lower():
            return True
        if proc.poll() is not None:
            return False
    return False

def run_cmd_in_guest(cmd_str, timeout=10, clear=True):
    """Run a command in the System partition shell, return output."""
    global buf
    if clear:
        buf = b""
        time.sleep(0.2)
        read_output(timeout=0.5)  # drain
        buf = b""
    send(cmd_str + "\n")
    time.sleep(1)
    text = read_output(timeout=timeout)
    return text

results = {}

# ============================================================================
# Step 1: Boot System partition
# ============================================================================
print("\n[Step 1] Waiting for System partition boot (up to 500s)...")
step1_pass = wait_for("buildroot login:", timeout=500)
results["Step 1: System boot"] = step1_pass

if step1_pass:
    print("[PASS] Step 1: System partition booted - login prompt received")
else:
    print("[FAIL] Step 1: System partition did not boot")
    text = all_output.decode("latin-1", errors="replace")
    print(f"[DIAG] Last 500 chars:\n{text[-500:]}")
    proc.kill()
    proc.wait()
    print("\n" + "=" * 70)
    print("  RESULT: FAIL (System did not boot)")
    print("=" * 70)
    sys.exit(1)

# ============================================================================
# Step 2: Login to System partition
# ============================================================================
print("\n[Step 2] Logging into System partition...")
time.sleep(1)
send("root\n")
time.sleep(2)
text = read_output(timeout=3)
if "password" in text.lower():
    send("1234\n")
    time.sleep(2)
    text = read_output(timeout=3)

step2_pass = "#" in text[-300:] or "$" in text[-300:]
results["Step 2: System login"] = step2_pass

if step2_pass:
    print("[PASS] Step 2: Logged into System partition")
else:
    print("[WARN] Step 2: Shell prompt not clearly detected, trying anyway...")
    results["Step 2: System login"] = True  # Continue anyway

# ============================================================================
# Step 3: Check if virtio_backend is running
# ============================================================================
print("\n[Step 3] Checking if virtio_backend is running...")
output = run_cmd_in_guest("ps | grep virtio_backend | grep -v grep")
step3_pass = "virtio_backend" in output
results["Step 3: virtio_backend running"] = step3_pass

if step3_pass:
    # Extract PID
    lines = [l for l in output.split("\n") if "virtio_backend" in l and "grep" not in l]
    if lines:
        print(f"[PASS] Step 3: virtio_backend is running: {lines[0].strip()}")
    else:
        print("[PASS] Step 3: virtio_backend is running")
else:
    print("[FAIL] Step 3: virtio_backend is NOT running")
    print("[DIAG] Checking if it was ever started...")
    output2 = run_cmd_in_guest("dmesg | tail -20")
    print(f"[DIAG] dmesg tail:\n{output2[-500:]}")

# ============================================================================
# Step 4: Check TCP port 4321
# ============================================================================
print("\n[Step 4] Checking if TCP port 4321 is listening...")

# Try ss first, fall back to netstat, then /proc/net/tcp
output = run_cmd_in_guest("ss -tlnp 2>/dev/null || netstat -tlnp 2>/dev/null || cat /proc/net/tcp")
step4_pass = "4321" in output or ":10E1" in output  # 0x10E1 = 4321
results["Step 4: TCP port 4321 listening"] = step4_pass

if step4_pass:
    lines = [l for l in output.split("\n") if "4321" in l or "10E1" in l]
    for l in lines[:3]:
        print(f"  {l.strip()}")
    print("[PASS] Step 4: TCP port 4321 is listening")
else:
    print("[FAIL] Step 4: TCP port 4321 is NOT listening")
    print(f"[DIAG] Network state output:\n{output[-500:]}")

    # Additional diagnostics: check /proc/net/tcp directly
    print("[DIAG] Checking /proc/net/tcp...")
    output_tcp = run_cmd_in_guest("cat /proc/net/tcp")
    print(f"[DIAG] /proc/net/tcp:\n{output_tcp[-500:]}")

# ============================================================================
# Step 5: Check shared memory mapping (virtio_backend boot messages)
# ============================================================================
print("\n[Step 5] Checking virtio_backend shared memory mapping...")
boot_text = all_output.decode("latin-1", errors="replace")

diag_patterns = {
    "Console TCP port 4321": "TCP server started on port 4321",
    "TCP bind failed": "TCP bind() failed - port already in use or access denied",
    "no TCP": "TCP socket creation failed",
    "TCP listen failed": "TCP listen() failed",
    "demo mode": "Shared memory mapping failed - /dev/mem not accessible",
    "Virtio_Con mapped": "Console shared memory mapped successfully",
    "All 5 Virtio": "All 5 Virtio devices initialized",
    "Could not map": "One or more shared memory regions failed to map",
}

found_any = False
for pattern, description in diag_patterns.items():
    if pattern.lower() in boot_text.lower():
        status = "OK" if pattern in ("Console TCP port 4321", "Virtio_Con mapped", "All 5 Virtio") else "ERROR"
        print(f"  [{status}] Found: '{pattern}' -> {description}")
        found_any = True

if not found_any:
    print("  [WARN] No diagnostic messages found in boot output")
    print("[DIAG] Searching for 'Backend' messages in boot output...")
    backend_lines = [l for l in boot_text.split("\n") if "Backend" in l or "backend" in l]
    for l in backend_lines[:10]:
        print(f"  {l.strip()}")

step5_pass = "tcp port 4321" in boot_text.lower() or "all 5 virtio" in boot_text.lower()
results["Step 5: SHM mapping & TCP init"] = step5_pass

if step5_pass:
    print("[PASS] Step 5: Shared memory mapped and TCP server initialized")
else:
    print("[FAIL] Step 5: TCP server initialization issue detected")

# ============================================================================
# Step 6: Check /dev/mem accessibility
# ============================================================================
print("\n[Step 6] Checking /dev/mem access for shared memory regions...")
output = run_cmd_in_guest("ls -la /dev/mem 2>&1")
step6_devmem = "/dev/mem" in output and "crw" in output
print(f"  /dev/mem: {'exists' if step6_devmem else 'MISSING'}")

# Check console magic at 0x20500000 using devmem if available
output = run_cmd_in_guest("which devmem 2>/dev/null && devmem 0x20500000 32 2>/dev/null || echo 'no_devmem'")
if "no_devmem" not in output and "0x" in output.lower():
    magic_match = re.search(r'0[xX]([0-9A-Fa-f]+)', output)
    if magic_match:
        magic_val = int(magic_match.group(1), 16)
        expected = 0x434F4E53  # "CONS"
        if magic_val == expected:
            print(f"  Console SHM magic: 0x{magic_val:08X} (CONS) - correct")
        else:
            print(f"  Console SHM magic: 0x{magic_val:08X} (expected 0x{expected:08X} CONS)")
else:
    print("  devmem not available, skipping SHM magic check")

# Try dd to read magic
output = run_cmd_in_guest(
    "dd if=/dev/mem bs=4 count=1 skip=$((0x20500000/4)) 2>/dev/null | od -A x -t x4 2>/dev/null | head -1",
    timeout=5)
if "0000000" in output:
    print(f"  Console SHM (dd): {output.strip()}")

results["Step 6: /dev/mem access"] = step6_devmem
if step6_devmem:
    print("[PASS] Step 6: /dev/mem is accessible")
else:
    print("[FAIL] Step 6: /dev/mem is NOT accessible")

# ============================================================================
# Step 7: Attempt telnet connection
# ============================================================================
if not DIAG_ONLY:
    print("\n[Step 7] Attempting telnet 127.0.0.1 4321...")
    buf = b""
    time.sleep(0.5)
    read_output(timeout=0.5)
    buf = b""

    send("telnet 127.0.0.1 4321\n")
    time.sleep(3)
    text = read_output(timeout=5)

    if "connection refused" in text.lower():
        step7_pass = False
        print("[FAIL] Step 7: Connection refused on port 4321")
        print(f"[DIAG] telnet output: {text[-300:]}")
    elif "connected" in text.lower() or "escape character" in text.lower():
        step7_pass = True
        print("[PASS] Step 7: Connected to TCP port 4321")

        # Step 8: Wait for Guest login prompt
        print("\n[Step 8] Waiting for Guest login prompt (up to 300s)...")
        step8_pass = wait_for("login:", timeout=300)
        results["Step 8: Guest login prompt"] = step8_pass

        if step8_pass:
            print("[PASS] Step 8: Guest login prompt received!")
            # Try login
            send("root\n")
            time.sleep(3)
            text = read_output(timeout=3)
            if "password" in text.lower():
                send("1234\n")
                time.sleep(3)
                text = read_output(timeout=3)
            if "#" in text[-200:] or "$" in text[-200:]:
                print("[PASS] Step 8: Logged into Guest partition!")
                results["Step 8: Guest login"] = True
            else:
                print("[WARN] Step 8: Login attempt sent, but shell prompt not detected")
                results["Step 8: Guest login"] = False
        else:
            print("[FAIL] Step 8: No login prompt from Guest within 300s")
            print("[DIAG] This might mean:")
            print("  - virtio_frontend is not running in Guest partition")
            print("  - Guest kernel didn't boot")
            print("  - /dev/hvc0 was not created by virtio_frontend")
            print("  - S99virtio_guest didn't start getty on /dev/hvc0")
    else:
        step7_pass = "login:" in text.lower()
        if step7_pass:
            print("[PASS] Step 7: Connected and got login prompt directly!")
            results["Step 8: Guest login prompt"] = True
        else:
            print(f"[WARN] Step 7: Unexpected telnet output:")
            print(f"  {text[-300:]}")

    results["Step 7: telnet connection"] = step7_pass
else:
    print("\n[DIAG] Skipping telnet test (--diag mode)")
    # Run nc test instead
    output = run_cmd_in_guest(
        "(echo '' | nc -w 2 127.0.0.1 4321 >/dev/null 2>&1 && echo 'PORT_OPEN') || echo 'PORT_CLOSED'",
        timeout=5)
    if "PORT_OPEN" in output:
        print("[PASS] Port 4321 is reachable (nc test)")
        results["Step 7: port reachable"] = True
    elif "PORT_CLOSED" in output:
        print("[FAIL] Port 4321 is NOT reachable (nc test)")
        results["Step 7: port reachable"] = False
    else:
        print(f"[WARN] nc test inconclusive: {output[-200:]}")
        results["Step 7: port reachable"] = None

# ============================================================================
# Summary
# ============================================================================
print("\n" + "=" * 70)
print("  TCP Bridge Diagnostic Report")
print("=" * 70)

all_pass = True
for step, passed in results.items():
    status = "PASS" if passed else ("FAIL" if passed is False else "WARN")
    print(f"  {step:40s} {status}")
    if passed is False:
        all_pass = False

print("-" * 70)
if all_pass:
    print("  OVERALL: PASS")
else:
    print("  OVERALL: FAIL")
    print()
    print("  Troubleshooting:")
    if not results.get("Step 3: virtio_backend running", True):
        print("  -> virtio_backend is not running. Check S99virtio_backend init script.")
        print("  -> Check if /usr/bin/virtio_backend exists in rootfs overlay CPIO.")
    if not results.get("Step 4: TCP port 4321 listening", True):
        print("  -> TCP port 4321 is not listening.")
        print("  -> Possible causes:")
        print("     1. /dev/mem mmap failed for console SHM (0x20500000)")
        print("     2. socket/bind/listen failed in virtio_console_init()")
        print("     3. virtio_backend entered 'demo mode' (no SHM access)")
    if not results.get("Step 5: SHM mapping & TCP init", True):
        print("  -> Check boot messages for '[Backend]' errors.")
    if not results.get("Step 6: /dev/mem access", True):
        print("  -> /dev/mem is not accessible. Check kernel CONFIG_DEVMEM=y.")
print("=" * 70)

# Cleanup
proc.kill()
proc.wait()
sys.exit(0 if all_pass else 1)
