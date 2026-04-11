#!/usr/bin/env python3
"""
Test Virtio Linux Demo (AArch64): 2 SMP Linux Partitions.

Architecture:
  Partition 0 (System): Linux + Virtio Backend (2 vCPU, pCPU 0-1, console=PL011 UART)
  Partition 1 (Guest):  Linux + Virtio Frontend (2 vCPU, pCPU 2-3, console=TCP bridge)
  Shared Memory: 5 regions at 0x20000000+ (net x3, blk, console)

System outputs to PL011 UART (stdio).

Verifies:
1. System Partition boots to login prompt (PL011 UART)
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

# Build only if needed
if not os.path.exists("resident_sw_image"):
    subprocess.run(["make", "clean"], capture_output=True)
    ret = subprocess.run(["make"], capture_output=True)
    if ret.returncode != 0:
        print("Build failed:")
        print(ret.stdout.decode(errors='replace'))
        print(ret.stderr.decode(errors='replace'))
        sys.exit(1)
    subprocess.run(["aarch64-linux-gnu-objcopy", "-O", "binary",
                    "-R", ".note", "-R", ".note.gnu.build-id", "-R", ".comment", "-S",
                    "resident_sw", "resident_sw.bin"], capture_output=True)
    subprocess.run(["mkimage", "-A", "arm64", "-O", "linux", "-C", "none",
                    "-a", "0x40200000", "-e", "0x40200000",
                    "-d", "resident_sw.bin", "resident_sw_image"], capture_output=True)

# Build U-Boot if not present
if not os.path.exists("u-boot/u-boot.bin"):
    ret = subprocess.run(["make", "u-boot/u-boot.bin"], capture_output=True)
    if ret.returncode != 0 or not os.path.exists("u-boot/u-boot.bin"):
        print("U-Boot build failed.")
        sys.exit(1)

# Start QEMU
cmd = ("qemu-system-aarch64 "
       "-machine virt,gic_version=3 "
       "-machine virtualization=true "
       "-cpu cortex-a72 -machine type=virt "
       "-m 4096 -smp 4 "
       "-bios ./u-boot/u-boot.bin "
       "-device loader,file=./resident_sw_image,addr=0x40200000,force-raw=on "
       "-nographic -no-reboot")

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

# Wait for login prompt (System outputs to PL011 UART)
if not wait_for("buildroot login:", timeout=500):
    print("\n=== FAIL: No login prompt detected ===")
    text = buf.decode("latin-1", errors="replace")
    print(f"Last 500 chars: {repr(text[-500:])}")
    proc.kill()
    proc.wait()
    sys.exit(1)

print("\n=== Login prompt detected ===")

# Login
time.sleep(1)
send("root\n")
time.sleep(2)
read_output(timeout=2)
text = buf.decode("latin-1", errors="replace")
if "password" in text.lower():
    send("1234\n")
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
read_output(timeout=3)
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
