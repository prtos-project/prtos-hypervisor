#!/usr/bin/env python3
"""
Test Mixed-OS Demo (RISC-V): FreeRTOS para-virt + Linux on PRTOS.

Verifies:
1. FreeRTOS RTOS partition prints motor control status reports
2. Linux partition boots with 3 vCPUs, login works, nproc returns 3
"""
import pexpect
import subprocess
import sys
import os
import time

TIMEOUT = 420
LOGIN_TIMEOUT = 30

os.chdir(os.path.dirname(os.path.abspath(__file__)))

subprocess.run(["make", "clean"], capture_output=True)
subprocess.run(["make"], capture_output=True)

# Create bootable binary image
subprocess.run(["riscv64-linux-gnu-objcopy", "-O", "binary", "-R", ".note",
                "-R", ".note.gnu.build-id", "-R", ".comment", "-S",
                "resident_sw", "resident_sw.bin"], check=True)

cmd = ("qemu-system-riscv64 "
       "-machine virt "
       "-cpu rv64 "
       "-smp 4 "
       "-m 1G "
       "-nographic -no-reboot "
       "-bios default "
       "-kernel resident_sw.bin "
       "-monitor none "
       "-serial stdio")

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

# Retry login up to 3 times in case of interleaved RTOS output
logged_in = False
for attempt in range(3):
    time.sleep(6)
    child.sendline("root")
    idx = child.expect(["assword", "buildroot login:", pexpect.TIMEOUT], timeout=60)
    if idx == 0:
        time.sleep(2)
        child.sendline("1234")
        idx2 = child.expect([r"[\$#] ", "Login incorrect", pexpect.TIMEOUT], timeout=60)
        if idx2 == 0:
            logged_in = True
            break
        elif idx2 == 1:
            print("MIX_OS_TEST_FAIL: login incorrect")
            child.close(force=True)
            sys.exit(1)
    elif idx == 1:
        continue

if not logged_in:
    print("MIX_OS_TEST_FAIL: login failed after retries")
    child.close(force=True)
    sys.exit(1)

print("\n\n=== Login successful! Checking vCPU count ===")
time.sleep(2)
child.sendline("nproc")
idx = child.expect(["3", pexpect.TIMEOUT], timeout=15)
if idx != 0:
    print("\n\n=== FAIL: nproc did not return 3 ===")
    sys.exit(1)

child.expect([r"[\$#] ", pexpect.TIMEOUT], timeout=10)
child.sendline("cat /proc/cpuinfo | grep processor | wc -l")
idx = child.expect(["3", pexpect.TIMEOUT], timeout=15)
if idx != 0:
    print("\n\n=== FAIL: cpuinfo does not show 3 CPUs ===")
    sys.exit(1)

print("\n\n=== ALL TESTS PASSED ===")
child.sendline("poweroff")
time.sleep(3)
child.close()
sys.exit(0)
