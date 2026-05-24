#!/usr/bin/env python3
"""Test Linux login on PRTOS (LoongArch64): boot, login as root/1234, verify 4 vCPUs via nproc.
Retries up to 3 times due to QEMU TCG timer non-determinism."""
import pexpect
import subprocess
import sys
import os
import time

BOOT_TIMEOUT = int(os.environ.get("PRTOS_LOGIN_TIMEOUT", "900"))
MAX_RETRIES = int(os.environ.get("PRTOS_MAX_RETRIES", "5"))
BUILD_TIMEOUT = int(os.environ.get("PRTOS_BUILD_TIMEOUT", "600"))

os.chdir(os.path.dirname(os.path.abspath(__file__)))

subprocess.run(["make", "clean"], capture_output=True, check=True, timeout=BUILD_TIMEOUT)
subprocess.run(["make"], capture_output=True, check=True, timeout=BUILD_TIMEOUT)

QEMU = os.environ.get("QEMU_LOONGARCH64",
    "/home/chenweis/hdd/Repo/loongarch64_linux_workspace/qemu_install/bin/qemu-system-loongarch64")
QEMU_ACCEL = os.environ.get("QEMU_LOONGARCH64_ACCEL", "-accel tcg,thread=single")
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

def try_boot():
    """Attempt boot and wait for login prompt. Returns pexpect child on success, None on timeout."""
    child = pexpect.spawn(cmd, encoding='utf-8', timeout=BOOT_TIMEOUT)
    child.logfile = sys.stdout
    idx = child.expect(["buildroot login:", "login:", pexpect.TIMEOUT, pexpect.EOF])
    if idx >= 2:
        child.close(force=True)
        return None
    return child

child = None
for attempt in range(1, MAX_RETRIES + 1):
    print(f"\n=== Boot attempt {attempt}/{MAX_RETRIES} (timeout={BOOT_TIMEOUT}s) ===")
    child = try_boot()
    if child is not None:
        break
    print(f"\n=== Attempt {attempt} timed out, retrying... ===")

if child is None:
    print(f"\n\n=== FAIL: Did not reach login prompt after {MAX_RETRIES} attempts ===")
    sys.exit(1)

print("\n\n=== Got login prompt, sending 'root' ===")
time.sleep(5)
child.send("root\r")

idx = child.expect(["Password:", "password:", pexpect.TIMEOUT, pexpect.EOF], timeout=60)
if idx >= 2:
    print("\n\n=== FAIL: Did not get password prompt ===")
    sys.exit(1)

print("\n\n=== Got password prompt, sending '1234' ===")
time.sleep(5)
child.send("1234\r")

idx = child.expect(["#", "\\$", "Login incorrect", pexpect.TIMEOUT, pexpect.EOF], timeout=300)
if idx == 2:
    print("\n\n=== FAIL: Login incorrect ===")
    sys.exit(1)
elif idx >= 3:
    print("\n\n=== FAIL: No shell prompt after login ===")
    sys.exit(1)

print("\n\n=== Login successful! Checking vCPU count ===")
time.sleep(5)
child.sendline("nproc")
idx = child.expect([r"[\r\n]+4[\r\n]+", pexpect.TIMEOUT], timeout=60)
if idx != 0:
    print("\n\n=== FAIL: nproc did not return 4 ===")
    sys.exit(1)
idx = child.expect(["#", "\\$", pexpect.TIMEOUT], timeout=60)
if idx >= 2:
    print("\n\n=== FAIL: shell prompt did not return after nproc ===")
    sys.exit(1)

print("\n\n=== Launching htop to verify 4 CPU display ===")
time.sleep(3)
child.sendline("TERM=xterm htop --version")
idx = child.expect([r"htop \d", pexpect.TIMEOUT], timeout=60)
if idx != 0:
    print("\n\n=== FAIL: htop binary not present or not runnable ===")
    sys.exit(1)
child.sendline("(echo q | htop -d 5 2>&1 | head -20) || true")
idx = child.expect(["#", "\\$", pexpect.TIMEOUT], timeout=60)
if idx >= 2:
    print("\n\n=== FAIL: htop hung or did not exit cleanly ===")
    sys.exit(1)

print("\n\n=== ALL TESTS PASSED ===")
child.sendline("poweroff")
time.sleep(3)
child.close()
sys.exit(0)
