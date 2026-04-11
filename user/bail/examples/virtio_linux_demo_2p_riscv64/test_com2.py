#!/usr/bin/env python3
"""Test Guest console access via TCP bridge through System partition (RISC-V 64).

On RISC-V 64, the Guest partition has no direct UART. Instead:
  - virtio_backend (System) listens on TCP port 4321 inside the System partition
  - Guest /dev/hvc0 is bridged via shared memory to the TCP socket
  - To access Guest: log into System, then 'telnet 127.0.0.1 4321'

This test:
  1. Boots QEMU in nographic mode
  2. Waits for System partition login prompt
  3. Logs into System partition
  4. Runs 'telnet 127.0.0.1 4321' from System shell
  5. Checks for Guest login prompt on the telnet connection
"""
import subprocess, time, sys, os, select

DEMO_DIR = os.path.dirname(os.path.abspath(__file__))
os.chdir(DEMO_DIR)

# Ensure image is built
if not os.path.exists("resident_sw.bin"):
    print("[TEST] Building...")
    subprocess.run(["make", "clean"], capture_output=True)
    subprocess.run(["make"], capture_output=True)
    subprocess.run(["riscv64-linux-gnu-objcopy", "-O", "binary",
                    "-R", ".note", "-R", ".note.gnu.build-id", "-R", ".comment", "-S",
                    "resident_sw", "resident_sw.bin"], capture_output=True)

cmd = ("qemu-system-riscv64 "
       "-machine virt "
       "-cpu rv64 "
       "-smp 4 -m 1G "
       "-nographic -no-reboot "
       "-bios default "
       "-kernel resident_sw.bin "
       "-monitor none "
       "-serial stdio")

print("[TEST] Starting QEMU...")
proc = subprocess.Popen(cmd, shell=True, stdin=subprocess.PIPE,
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

import fcntl
flags = fcntl.fcntl(proc.stdout.fileno(), fcntl.F_GETFL)
fcntl.fcntl(proc.stdout.fileno(), fcntl.F_SETFL, flags | os.O_NONBLOCK)

buf = b""

def read_output(timeout=2):
    global buf
    deadline = time.time() + timeout
    while time.time() < deadline:
        ready, _, _ = select.select([proc.stdout], [], [], 0.5)
        if ready:
            try:
                data = proc.stdout.read(4096)
                if data:
                    buf += data
            except (IOError, OSError):
                pass
        if proc.poll() is not None:
            break
    return buf.decode("latin-1", errors="replace")

def send(text):
    proc.stdin.write(text.encode())
    proc.stdin.flush()

def wait_for(pattern, timeout=300):
    deadline = time.time() + timeout
    while time.time() < deadline:
        text = read_output(timeout=2)
        if pattern.lower() in text.lower():
            return True
        if proc.poll() is not None:
            return False
    return False

# Step 1: Wait for System partition boot
print("[TEST] Waiting for System partition boot (up to 500s)...")
if not wait_for("buildroot login:", timeout=500):
    print("[FAIL] System partition did not boot")
    text = buf.decode("latin-1", errors="replace")
    print(f"[System] Last 500 chars: {repr(text[-500:])}")
    proc.kill()
    proc.wait()
    sys.exit(1)

print("[PASS] System partition booted")

# Step 2: Login to System
print("[TEST] Logging into System partition...")
time.sleep(1)
send("root\n")
time.sleep(2)
read_output(timeout=2)
if "password" in buf.decode("latin-1", errors="replace").lower():
    send("1234\n")
    time.sleep(2)
    read_output(timeout=2)

# Check for shell prompt
text = buf.decode("latin-1", errors="replace")
if "#" in text[-200:] or "$" in text[-200:]:
    print("[PASS] Logged into System partition")
else:
    print("[WARN] Shell prompt not detected, continuing anyway...")

# Step 3: Wait for virtio_backend to start (TCP port 4321 ready)
print("[TEST] Waiting for virtio_backend TCP server...")
time.sleep(10)  # Give the backend time to initialize

# Step 4: Connect to Guest via telnet from System shell
print("[TEST] Running 'telnet 127.0.0.1 4321' from System shell...")
buf = b""  # Clear buffer
send("telnet 127.0.0.1 4321\n")

# Wait for telnet connection, then send newline to trigger getty re-prompt
time.sleep(5)
read_output(timeout=3)
send("\n")  # Trigger getty to re-display login prompt
time.sleep(2)

# Step 5: Wait for Guest login prompt
print("[TEST] Waiting for Guest login prompt (up to 300s)...")
if wait_for("login:", timeout=300):
    print("[PASS] Guest login prompt received via TCP bridge!")
    # Try to login
    send("root\n")
    time.sleep(3)
    read_output(timeout=2)
    text = buf.decode("latin-1", errors="replace")
    if "password" in text.lower():
        send("1234\n")
        time.sleep(3)
        read_output(timeout=2)
    text = buf.decode("latin-1", errors="replace")
    if "#" in text[-200:] or "$" in text[-200:]:
        print("[PASS] Logged into Guest partition!")
    result = True
else:
    print("[FAIL] No Guest login prompt via TCP bridge")
    text = buf.decode("latin-1", errors="replace")
    print(f"[Output] Last 500 chars: {repr(text[-500:])}")
    result = False

# Cleanup
proc.kill()
proc.wait()
sys.exit(0 if result else 1)
