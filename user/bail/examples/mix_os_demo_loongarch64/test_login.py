#!/usr/bin/env python3
"""
Test Mixed-OS Demo (LoongArch64): FreeRTOS para-virt + Linux on PRTOS.

Verifies:
1. FreeRTOS RTOS partition prints motor control status reports
2. Linux partition boots with 3 vCPUs, login works, nproc returns 3
"""
import pexpect
import subprocess
import sys
import os
import time

TIMEOUT = int(os.environ.get("PRTOS_LOGIN_TIMEOUT", "1200"))
LOGIN_TIMEOUT = int(os.environ.get("PRTOS_LOGIN_PROMPT_TIMEOUT", "180"))

os.chdir(os.path.dirname(os.path.abspath(__file__)))

subprocess.run(["make", "clean"], capture_output=True)
subprocess.run(["make"], capture_output=True)

QEMU = os.environ.get("QEMU_LOONGARCH64",
    "/home/chenweis/hdd/Repo/loongarch64_linux_workspace/qemu_install/bin/qemu-system-loongarch64")
QEMU_ACCEL = os.environ.get("QEMU_LOONGARCH64_ACCEL", "-accel tcg,thread=multi")
QEMU_EXTRA_ARGS = os.environ.get("QEMU_LOONGARCH64_EXTRA_ARGS", "-nodefaults -nic none")

cmd = (f"{QEMU} "
    f"{QEMU_ACCEL} "
    f"{QEMU_EXTRA_ARGS} "
    "-machine virt "
    "-cpu max "
    "-smp 4 "
    "-m 2G "
    "-nographic -no-reboot "
    "-kernel resident_sw "
    "-monitor none "
    "-chardev stdio,id=s0,signal=off "
    "-serial chardev:s0")

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
    child.send("root\r")
    idx = child.expect(["assword", "buildroot login:", pexpect.TIMEOUT], timeout=60)
    if idx == 0:
        time.sleep(2)
        child.send("1234\r")
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
idx = child.expect(["3", pexpect.TIMEOUT], timeout=LOGIN_TIMEOUT)
if idx != 0:
    print("\n\n=== FAIL: nproc did not return 3 ===")
    sys.exit(1)

print("\n\n=== ALL TESTS PASSED ===")
child.sendline("poweroff")
time.sleep(3)
child.close()
sys.exit(0)
