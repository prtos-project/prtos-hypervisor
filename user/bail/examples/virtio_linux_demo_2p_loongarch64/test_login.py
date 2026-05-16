#!/usr/bin/env python3
"""
Test Virtio Linux Demo (LoongArch64): 2 SMP Linux Partitions.

Architecture:
  Partition 0 (System): Linux + Virtio Backend (2 vCPU, pCPU 0-1, console=NS16550 UART)
  Partition 1 (Guest):  Linux + Virtio Frontend (2 vCPU, pCPU 2-3, console=TCP bridge)
  Shared Memory: 5 regions at 0x98000000+ (net x3, blk, console)

System outputs to NS16550 UART (stdio).

Verifies:
1. System Partition boots to login prompt (NS16550 UART)
2. Login works (root/1234)
3. Kernel is running (uname)
"""
import subprocess
import sys
import os
import time
import select
import fcntl

DEMO_DIR = os.path.dirname(os.path.abspath(__file__))
os.chdir(DEMO_DIR)
TIMEOUT = int(os.environ.get("PRTOS_LOGIN_TIMEOUT", "1200"))

# Build only if needed
if not os.path.exists("resident_sw"):
    subprocess.run(["make", "clean"], capture_output=True)
    ret = subprocess.run(["make"], capture_output=True)
    if ret.returncode != 0:
        print("Build failed:")
        print(ret.stdout.decode(errors='replace'))
        print(ret.stderr.decode(errors='replace'))
        sys.exit(1)

# Start QEMU
QEMU = os.environ.get("QEMU_LOONGARCH64",
    "/home/chenweis/hdd/Repo/loongarch64_linux_workspace/qemu_install/bin/qemu-system-loongarch64")
QEMU_ACCEL = os.environ.get("QEMU_LOONGARCH64_ACCEL", "-accel tcg,thread=multi")
QEMU_EXTRA_ARGS = os.environ.get("QEMU_LOONGARCH64_EXTRA_ARGS", "-nodefaults -nic none")

cmd = (f"{QEMU} "
       f"{QEMU_ACCEL} "
       f"{QEMU_EXTRA_ARGS} "
       "-machine virt "
       "-cpu max "
       "-smp 4 -m 2G "
       "-nographic -no-reboot "
       "-kernel resident_sw "
       "-monitor none "
       "-chardev stdio,id=s0,signal=off "
       "-serial chardev:s0")

print("=== Starting QEMU ===")
proc = subprocess.Popen(cmd, shell=True, stdin=subprocess.PIPE,
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

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

# Wait for login prompt (System outputs to NS16550 UART)
if not wait_for("buildroot login:", timeout=TIMEOUT):
    print("\n=== FAIL: No login prompt detected ===")
    text = buf.decode("latin-1", errors="replace")
    print(f"Last 500 chars: {repr(text[-500:])}")
    proc.kill()
    proc.wait()
    sys.exit(1)

print("\n=== Login prompt detected ===")

# Login
time.sleep(1)
send("root\r")
time.sleep(2)
read_output(timeout=2)
text = buf.decode("latin-1", errors="replace")
if "password" in text.lower():
    send("1234\r")
    time.sleep(2)
    read_output(timeout=2)

text = buf.decode("latin-1", errors="replace")
if "#" not in text[-200:] and "$" not in text[-200:]:
    print("\n=== FAIL: Login failed ===")
    print(f"Last 500 chars: {repr(text[-500:])}")
    proc.kill()
    proc.wait()
    sys.exit(1)

print("=== Login successful ===")

# Verify kernel is running
time.sleep(0.5)
buf = b""
send("uname -a\n")
time.sleep(2)
read_output(timeout=30)
text = buf.decode("latin-1", errors="replace")
if "linux" not in text.lower():
    print("\n=== FAIL: uname did not return Linux ===")
    proc.kill()
    proc.wait()
    sys.exit(1)

print("\n=== ALL TESTS PASSED ===")
print("  - Partition boot: OK")
print("  - Login: OK")
print("  - uname: OK")

send("poweroff\n")
time.sleep(3)
proc.kill()
proc.wait()
sys.exit(0)
