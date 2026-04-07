#!/usr/bin/env python3
"""Test Linux login on PRTOS (amd64): boot, login as root/1234, verify shell works."""
import pexpect
import subprocess
import sys
import os
import time

TIMEOUT = 120
LOGIN_TIMEOUT = 30

os.chdir(os.path.dirname(os.path.abspath(__file__)))

subprocess.run(["make", "clean"], capture_output=True)
ret = subprocess.run(["make"], capture_output=True)
if ret.returncode != 0:
    print("Build failed")
    sys.exit(1)

cmd = ("sg kvm -c 'qemu-system-x86_64 "
    "-enable-kvm -cpu host,-waitpkg "
       "-m 512 -smp 4 "
       "-nographic -no-reboot "
       "-cdrom resident_sw.iso "
       "-serial mon:stdio "
       "-boot d'")

print("=== Starting QEMU ===")
child = pexpect.spawn("/bin/bash", ["-c", cmd], encoding='utf-8', timeout=TIMEOUT)
child.logfile = sys.stdout

# Wait for login prompt
idx = child.expect(["buildroot login:", "login:", pexpect.TIMEOUT, pexpect.EOF])
if idx >= 2:
    print("\n\n=== FAIL: Did not reach login prompt ===")
    sys.exit(1)

print("\n\n=== Got login prompt, sending 'root' ===")
time.sleep(1)
child.sendline("root")

idx = child.expect(["Password:", "password:", pexpect.TIMEOUT, pexpect.EOF], timeout=LOGIN_TIMEOUT)
if idx >= 2:
    print("\n\n=== FAIL: Did not get password prompt ===")
    sys.exit(1)

print("\n\n=== Got password prompt, sending '1234' ===")
time.sleep(1)
child.sendline("1234")

idx = child.expect(["#", "\\$", "Login incorrect", pexpect.TIMEOUT, pexpect.EOF], timeout=LOGIN_TIMEOUT)
if idx == 2:
    print("\n\n=== FAIL: Login incorrect ===")
    sys.exit(1)
elif idx >= 3:
    print("\n\n=== FAIL: No shell prompt after login ===")
    sys.exit(1)

print("\n\n=== Login successful! Running commands ===")
time.sleep(1)
child.sendline("uname -a")
idx = child.expect(["Linux", pexpect.TIMEOUT], timeout=10)
if idx != 0:
    print("\n\n=== FAIL: uname did not return Linux ===")
    sys.exit(1)

child.expect(["#", pexpect.TIMEOUT], timeout=5)
child.sendline("cat /proc/cpuinfo | grep processor | wc -l")
child.expect(["#", pexpect.TIMEOUT], timeout=10)
cpu_output = child.before.strip()
import re
nums = re.findall(r'\b(\d+)\b', cpu_output)
actual_cpus = int(nums[-1]) if nums else 0
expected_cpus = 4
if actual_cpus != expected_cpus:
    print(f"\n\n=== FAIL: expected {expected_cpus} CPUs but got {actual_cpus} ===")
    sys.exit(1)
print(f"\n\n=== All {expected_cpus} vCPUs online ===")

child.sendline("poweroff")
time.sleep(5)
child.close(force=True)
print("\n\n=== PASS ===")
sys.exit(0)

print("\n\n=== ALL TESTS PASSED ===")
child.sendline("poweroff")
time.sleep(3)
child.close()
sys.exit(0)
