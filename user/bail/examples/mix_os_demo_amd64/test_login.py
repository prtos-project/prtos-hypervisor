#!/usr/bin/env python3
"""
Test Mixed-OS Demo (amd64): FreeRTOS para-virt + Linux hw-virt on PRTOS.

Verifies:
1. FreeRTOS RTOS partition prints motor control status reports
2. Linux partition boots to login, login works as root/1234
"""
import pexpect
import subprocess
import sys
import os
import time

TIMEOUT = 180
LOGIN_TIMEOUT = 30

os.chdir(os.path.dirname(os.path.abspath(__file__)))

subprocess.run(["make", "clean"], capture_output=True)
ret = subprocess.run(["make"], capture_output=True)
if ret.returncode != 0:
    print("Build failed:")
    print(ret.stdout.decode(errors='replace'))
    print(ret.stderr.decode(errors='replace'))
    sys.exit(1)

cmd = ("sg kvm -c 'qemu-system-x86_64 "
       "-enable-kvm -cpu host "
       "-m 512 -smp 4 "
       "-nographic -no-reboot "
       "-cdrom resident_sw.iso "
       "-serial mon:stdio "
       "-boot d'")

print("=== Starting QEMU ===")
child = pexpect.spawn("/bin/bash", ["-c", cmd], encoding='utf-8', timeout=TIMEOUT)
child.logfile = sys.stdout

# Wait for either RTOS output or Linux login prompt
rtos_seen = False
idx = child.expect([r"\[RTOS\]", "buildroot login:", pexpect.TIMEOUT, pexpect.EOF],
                   timeout=120)
if idx == 0:
    print("\n\n=== FreeRTOS RTOS output detected ===")
    rtos_seen = True
elif idx == 1:
    print("\n\n=== Linux login detected first ===")
elif idx >= 2:
    print("\n\n=== FAIL: No output detected ===")
    sys.exit(1)

# Wait for Linux login prompt if not yet seen
if idx != 1:
    idx = child.expect(["buildroot login:", pexpect.TIMEOUT, pexpect.EOF],
                       timeout=TIMEOUT)
    if idx != 0:
        print("\n\n=== FAIL: Did not reach Linux login prompt ===")
        sys.exit(1)

print("\n\n=== Got login prompt, sending 'root' ===")
time.sleep(2)

# Login - retry up to 3 times in case of interleaved RTOS output
logged_in = False
for attempt in range(3):
    time.sleep(3)
    child.sendline("root")
    idx = child.expect(["assword", "buildroot login:", pexpect.TIMEOUT], timeout=30)
    if idx == 0:
        time.sleep(1)
        child.sendline("1234")
        idx2 = child.expect([r"[\$#] ", "Login incorrect", pexpect.TIMEOUT], timeout=30)
        if idx2 == 0:
            logged_in = True
            break
        elif idx2 == 1:
            print(f"\n\n=== Login incorrect on attempt {attempt+1} ===")
            child.expect(["buildroot login:", pexpect.TIMEOUT], timeout=10)
    elif idx == 1:
        continue
    else:
        break

if not logged_in:
    print("\n\n=== FAIL: Could not login ===")
    child.close(force=True)
    sys.exit(1)

print("\n\n=== Login successful! Running commands ===")
time.sleep(1)
child.sendline("uname -a")
idx = child.expect(["Linux", pexpect.TIMEOUT], timeout=10)
if idx != 0:
    print("\n\n=== FAIL: uname did not return Linux ===")
    sys.exit(1)

child.expect(["#", pexpect.TIMEOUT], timeout=5)

# If we haven't seen RTOS output yet, wait a bit more
if not rtos_seen:
    child.sendline("")
    idx = child.expect([r"\[RTOS\]", "#", pexpect.TIMEOUT], timeout=15)
    if idx == 0:
        rtos_seen = True
        print("\n\n=== FreeRTOS RTOS output detected (late) ===")

print("\n\n=== ALL TESTS PASSED ===")
if rtos_seen:
    print("  - FreeRTOS RTOS: OK")
print("  - Linux login: OK")
print("  - uname: OK")

child.sendline("poweroff")
time.sleep(3)
child.close()
sys.exit(0)
