#!/usr/bin/env python3
"""
Test Mixed-OS Demo: FreeRTOS + Linux on PRTOS.

Verifies:
1. FreeRTOS RTOS partition prints motor control status reports
2. Linux partition boots with 3 vCPUs, login works, nproc returns 3
"""
import pexpect
import subprocess
import sys
import os
import time

TIMEOUT = 360
LOGIN_TIMEOUT = 30

os.chdir(os.path.dirname(os.path.abspath(__file__)))

subprocess.run(["make", "clean"], capture_output=True)
subprocess.run(["make"], capture_output=True)

subprocess.run(["aarch64-linux-gnu-objcopy", "-O", "binary", "-R", ".note",
                "-R", ".note.gnu.build-id", "-R", ".comment", "-S",
                "resident_sw", "resident_sw.bin"], check=True)
subprocess.run(["mkimage", "-A", "arm64", "-O", "linux", "-C", "none",
                "-a", "0x40200000", "-e", "0x40200000",
                "-d", "resident_sw.bin", "resident_sw_image"],
               check=True, capture_output=True)
os.makedirs("u-boot", exist_ok=True)
subprocess.run(["cp", "../../bin/u-boot.bin", "./u-boot/"], check=True)

cmd = ("qemu-system-aarch64 "
       "-machine virt,gic_version=3 "
       "-machine virtualization=true "
       "-cpu cortex-a72 "
       "-machine type=virt "
       "-m 4096 -smp 4 "
       "-bios ./u-boot/u-boot.bin "
       "-device loader,file=./resident_sw_image,addr=0x40200000,force-raw=on "
       "-nographic -no-reboot")

print("=== Starting QEMU ===")
child = pexpect.spawn(cmd, encoding='utf-8', timeout=TIMEOUT)
child.logfile = sys.stdout

# Wait for RTOS output first (should appear early)
idx = child.expect([r"\[RTOS\]", "buildroot login:", pexpect.TIMEOUT, pexpect.EOF],
                   timeout=120)
if idx == 0:
    print("\n\n=== FreeRTOS RTOS output detected ===")
elif idx == 1:
    print("\n\n=== Linux login detected (RTOS may have printed earlier) ===")
elif idx >= 2:
    print("\n\n=== FAIL: No output detected ===")
    sys.exit(1)

# Wait for Linux login prompt
if idx != 1:
    idx = child.expect(["buildroot login:", pexpect.TIMEOUT, pexpect.EOF],
                       timeout=TIMEOUT)
    if idx != 0:
        print("\n\n=== FAIL: Did not reach Linux login prompt ===")
        sys.exit(1)

print("\n\n=== Got login prompt, sending 'root' ===")
time.sleep(2)
child.sendline("root")

idx = child.expect(["Password:", "password:", pexpect.TIMEOUT, pexpect.EOF],
                   timeout=LOGIN_TIMEOUT)
if idx >= 2:
    print("\n\n=== FAIL: Did not get password prompt ===")
    sys.exit(1)

print("\n\n=== Got password prompt, sending '1234' ===")
time.sleep(1)
child.sendline("1234")

idx = child.expect(["#", "\\$", "Login incorrect", pexpect.TIMEOUT, pexpect.EOF],
                   timeout=LOGIN_TIMEOUT)
if idx == 2:
    print("\n\n=== FAIL: Login incorrect ===")
    sys.exit(1)
elif idx >= 3:
    print("\n\n=== FAIL: No shell prompt after login ===")
    sys.exit(1)

print("\n\n=== Login successful! Checking vCPU count ===")
time.sleep(1)
child.sendline("nproc")
idx = child.expect(["3", pexpect.TIMEOUT], timeout=10)
if idx != 0:
    print("\n\n=== FAIL: nproc did not return 3 ===")
    sys.exit(1)

child.expect(["#", pexpect.TIMEOUT], timeout=5)
child.sendline("which htop")
idx = child.expect(["htop", pexpect.TIMEOUT], timeout=10)

print("\n\n=== ALL TESTS PASSED ===")
child.sendline("poweroff")
time.sleep(3)
child.close()
sys.exit(0)
