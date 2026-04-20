#!/usr/bin/env python3
"""Test Linux login on PRTOS (LoongArch64): boot, login as root/1234, verify 4 vCPUs via nproc."""
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

QEMU = os.environ.get("QEMU_LOONGARCH64",
    "/home/chenweis/loongarch64_workspace/qemu-install/bin/qemu-system-loongarch64")

cmd = (f"{QEMU} "
       "-machine virt "
       "-cpu max "
       "-smp 4 "
       "-m 2G "
       "-nographic -no-reboot "
       "-kernel resident_sw "
       "-monitor none "
       "-serial stdio")

print("=== Starting QEMU ===")
child = pexpect.spawn(cmd, encoding='utf-8', timeout=TIMEOUT)
child.logfile = sys.stdout

# Wait for login prompt
idx = child.expect(["buildroot login:", "login:", pexpect.TIMEOUT, pexpect.EOF])
if idx >= 2:
    print("\n\n=== FAIL: Did not reach login prompt ===")
    sys.exit(1)

print("\n\n=== Got login prompt, sending 'root' ===")
time.sleep(4)
child.sendline("root")

idx = child.expect(["Password:", "password:", pexpect.TIMEOUT, pexpect.EOF], timeout=LOGIN_TIMEOUT)
if idx >= 2:
    print("\n\n=== FAIL: Did not get password prompt ===")
    sys.exit(1)

print("\n\n=== Got password prompt, sending '1234' ===")
time.sleep(2)
child.sendline("1234")

idx = child.expect(["#", "\\$", "Login incorrect", pexpect.TIMEOUT, pexpect.EOF], timeout=LOGIN_TIMEOUT)
if idx == 2:
    print("\n\n=== FAIL: Login incorrect ===")
    sys.exit(1)
elif idx >= 3:
    print("\n\n=== FAIL: No shell prompt after login ===")
    sys.exit(1)

print("\n\n=== Login successful! Checking vCPU count ===")
time.sleep(2)
child.sendline("nproc")
idx = child.expect(["4", pexpect.TIMEOUT], timeout=10)
if idx != 0:
    print("\n\n=== FAIL: nproc did not return 4 ===")
    sys.exit(1)

print("\n\n=== ALL TESTS PASSED ===")
child.sendline("poweroff")
time.sleep(3)
child.close()
sys.exit(0)
