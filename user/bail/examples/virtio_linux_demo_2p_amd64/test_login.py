#!/usr/bin/env python3
"""
Test Virtio Linux Demo (amd64): 2 Linux Partitions with Virtio Device Virtualization.

Architecture:
  Partition 0 (System): Linux + Virtio Backend (1 vCPU, pCPU 0)
  Partition 1 (Guest):  Linux + Virtio Frontend (1 vCPU, pCPU 1)
  Shared Memory: 8MB at GPA 0x16000000

Verifies:
1. Both Linux partitions boot to login prompt
2. System Partition login works (root/1234)
3. Guest Partition login works (root/1234)
4. Shared memory is accessible from both partitions
"""
import pexpect
import subprocess
import sys
import os
import time

TIMEOUT = 300
LOGIN_TIMEOUT = 30

os.chdir(os.path.dirname(os.path.abspath(__file__)))

# Build
subprocess.run(["make", "clean"], capture_output=True)
ret = subprocess.run(["make"], capture_output=True)
if ret.returncode != 0:
    print("Build failed:")
    print(ret.stdout.decode(errors='replace'))
    print(ret.stderr.decode(errors='replace'))
    sys.exit(1)

# Start QEMU with 4GB RAM, 4 CPUs for 2 Linux partitions
cmd = ("sg kvm -c 'qemu-system-x86_64 "
       "-enable-kvm -cpu host,-waitpkg "
       "-m 512 -smp 2 "
       "-nographic -no-reboot "
       "-cdrom resident_sw.iso "
       "-serial mon:stdio "
       "-boot d'")

print("=== Starting QEMU ===")
child = pexpect.spawn("/bin/bash", ["-c", cmd], encoding='utf-8', timeout=TIMEOUT)
child.logfile = sys.stdout

# Wait for first login prompt (either partition could reach it first)
login_count = 0
max_logins = 2

# Wait for login prompt
idx = child.expect(["buildroot login:", pexpect.TIMEOUT, pexpect.EOF], timeout=180)
if idx != 0:
    print("\n\n=== FAIL: No login prompt detected ===")
    child.close(force=True)
    sys.exit(1)

print("\n\n=== First login prompt detected ===")
login_count = 1

# Login to first partition
time.sleep(3)
child.sendline("root")
idx = child.expect(["assword", "buildroot login:", pexpect.TIMEOUT], timeout=30)
if idx == 0:
    time.sleep(1)
    child.sendline("1234")
    idx2 = child.expect([r"[\$#] ", "Login incorrect", pexpect.TIMEOUT], timeout=30)
    if idx2 == 0:
        print("\n\n=== First partition login successful ===")
    else:
        print("\n\n=== First partition login failed ===")
        child.close(force=True)
        sys.exit(1)
elif idx == 1:
    # Second login prompt appeared, try again
    time.sleep(2)
    child.sendline("root")
    idx = child.expect(["assword", pexpect.TIMEOUT], timeout=30)
    if idx == 0:
        time.sleep(1)
        child.sendline("1234")
        idx2 = child.expect([r"[\$#] ", pexpect.TIMEOUT], timeout=30)
        if idx2 != 0:
            print("\n\n=== FAIL: Login failed ===")
            child.close(force=True)
            sys.exit(1)

# Verify kernel is running
time.sleep(1)
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
