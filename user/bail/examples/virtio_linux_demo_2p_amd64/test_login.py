#!/usr/bin/env python3
"""
Test Virtio Linux Demo (amd64): 2 SMP Linux Partitions with Dual Console.

Architecture:
  Partition 0 (System): Linux + Virtio Backend (2 vCPU, pCPU 0-1, console=ttyS0 -> COM1)
  Partition 1 (Guest):  Linux + Virtio Frontend (2 vCPU, pCPU 2-3, console=tty0 -> VGA)
  Shared Memory: 5 regions at 0x16000000+ (net x3, blk, console)

System outputs to COM1 (stdio). Guest outputs to VGA (not visible in nographic mode).

Verifies:
1. System Partition boots to login prompt (COM1)
2. Login works (root/1234)
3. Kernel is running (uname)
"""
import pexpect
import subprocess
import sys
import os
import time

TIMEOUT = 300
LOGIN_TIMEOUT = 30
LOGIN_RETRIES = 3

os.chdir(os.path.dirname(os.path.abspath(__file__)))

# Build
subprocess.run(["make", "clean"], capture_output=True)
ret = subprocess.run(["make"], capture_output=True)
if ret.returncode != 0:
    print("Build failed:")
    print(ret.stdout.decode(errors='replace'))
    print(ret.stderr.decode(errors='replace'))
    sys.exit(1)

# Start QEMU (4 pCPUs, 512MB, COM1=stdio for System, Guest uses VGA)
cmd = ("sg kvm -c 'qemu-system-x86_64 "
       "-enable-kvm -cpu host,-waitpkg "
       "-m 512 -smp 4 "
       "-nographic -no-reboot "
       "-cdrom resident_sw.iso "
       "-serial mon:stdio "
       "-serial null "
       "-boot d'")

print("=== Starting QEMU ===")
child = pexpect.spawn("/bin/bash", ["-c", cmd], encoding='utf-8', timeout=TIMEOUT)
child.logfile = sys.stdout

# Wait for login prompt (only System outputs to serial; Guest uses VGA)
idx = child.expect(["buildroot login:", pexpect.TIMEOUT, pexpect.EOF], timeout=180)
if idx != 0:
    print("\n\n=== FAIL: No login prompt detected ===")
    child.close(force=True)
    sys.exit(1)

print("\n\n=== Login prompt detected ===")

# Login with retries (hypervisor output may interleave with input)
logged_in = False
for attempt in range(LOGIN_RETRIES):
    time.sleep(1)
    child.sendline("root")
    idx = child.expect(["assword", "buildroot login:", pexpect.TIMEOUT], timeout=LOGIN_TIMEOUT)
    if idx == 0:
        time.sleep(0.5)
        child.sendline("1234")
        idx2 = child.expect([r"[#\$] ", "Login incorrect", "buildroot login:", pexpect.TIMEOUT],
                            timeout=LOGIN_TIMEOUT)
        if idx2 == 0:
            logged_in = True
            break
        elif idx2 == 2:
            # Got another login prompt, retry
            continue
    elif idx == 1:
        # Another login prompt appeared (noise interrupted), retry
        continue
    else:
        break

if not logged_in:
    print("\n\n=== FAIL: Login failed after retries ===")
    child.close(force=True)
    sys.exit(1)

print("\n\n=== Login successful ===")

# Verify kernel is running
time.sleep(0.5)
child.sendline("uname -a")
idx = child.expect(["Linux", pexpect.TIMEOUT], timeout=10)
if idx != 0:
    print("\n\n=== FAIL: uname did not return Linux ===")
    child.close(force=True)
    sys.exit(1)

child.expect(["#", pexpect.TIMEOUT], timeout=5)

# Check for shared memory region visibility
child.sendline("cat /proc/iomem | head -30")
child.expect(["#", pexpect.TIMEOUT], timeout=10)

print("\n\n=== ALL TESTS PASSED ===")
print("  - Partition boot: OK")
print("  - Login: OK")
print("  - uname: OK")

child.sendline("poweroff")
time.sleep(3)
child.close()
sys.exit(0)
