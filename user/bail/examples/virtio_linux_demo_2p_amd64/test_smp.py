#!/usr/bin/env python3
"""
Test SMP vCPU count for virtio_linux_demo_2p_amd64.

Verifies that the System Partition (Partition 0) boots with 2 vCPUs online.
The Guest Partition uses VGA console (not accessible via serial), so only
the System Partition is verified here.

Architecture:
  Partition 0 (System): 2 vCPU (pCPU 0-1), console=ttyS0 (COM1 -> stdio)
  Partition 1 (Guest):  2 vCPU (pCPU 2-3), console=tty0  (VGA, not tested here)

Exit code: 0 = PASS, 1 = FAIL
"""
import pexpect
import subprocess
import sys
import os
import re
import time

TIMEOUT = 300
LOGIN_TIMEOUT = 30
LOGIN_RETRIES = 3
EXPECTED_VCPUS = 2

os.chdir(os.path.dirname(os.path.abspath(__file__)))

# Build
subprocess.run(["make", "clean"], capture_output=True)
ret = subprocess.run(["make"], capture_output=True)
if ret.returncode != 0:
    print("Build failed:")
    print(ret.stdout.decode(errors='replace'))
    print(ret.stderr.decode(errors='replace'))
    sys.exit(1)

# Determine KVM access method
kvm_ok = 0
if os.access("/dev/kvm", os.W_OK):
    kvm_ok = 1
else:
    import grp
    try:
        kvm_gid = grp.getgrnam("kvm").gr_gid
        if kvm_gid in os.getgroups():
            kvm_ok = 2
    except KeyError:
        pass

if kvm_ok == 0:
    print("SKIP: KVM not accessible")
    sys.exit(0)

sg_pre = "sg kvm -c '" if kvm_ok == 2 else ""
sg_post = "'" if kvm_ok == 2 else ""

cmd = (f"{sg_pre}qemu-system-x86_64 "
       "-enable-kvm -cpu host,-waitpkg "
       "-m 1024 -smp 4 "
       "-nographic -no-reboot "
       "-cdrom resident_sw.iso "
       "-serial mon:stdio "
       f"-boot d{sg_post}")

print("=== Starting QEMU for SMP test ===")
child = pexpect.spawn("/bin/bash", ["-c", cmd], encoding='utf-8',
                      timeout=TIMEOUT, codec_errors='replace')
child.logfile = sys.stdout

try:
    # Wait for System Partition login prompt
    idx = child.expect(["buildroot login:", pexpect.TIMEOUT, pexpect.EOF],
                       timeout=240)
    if idx != 0:
        print("\n\nFAIL: login prompt not reached")
        child.close(force=True)
        sys.exit(1)

    print("\n\n=== Login prompt detected ===")

    # Login with retries
    logged_in = False
    for attempt in range(LOGIN_RETRIES):
        time.sleep(1)
        child.sendline("root")
        idx = child.expect(["assword", "buildroot login:",
                            pexpect.TIMEOUT], timeout=LOGIN_TIMEOUT)
        if idx == 0:
            time.sleep(0.5)
            child.sendline("1234")
            idx2 = child.expect([r"[#\$] ", "Login incorrect",
                                 "buildroot login:", pexpect.TIMEOUT],
                                timeout=LOGIN_TIMEOUT)
            if idx2 == 0:
                logged_in = True
                break
            elif idx2 == 2:
                continue
        elif idx == 1:
            continue
        else:
            break

    if not logged_in:
        print("\n\nFAIL: login failed")
        child.close(force=True)
        sys.exit(1)

    print("\n\n=== Login successful ===")
    time.sleep(0.5)

    # Check CPU count via nproc
    child.sendline("nproc")
    child.expect([r"[#\$] ", pexpect.TIMEOUT], timeout=10)
    nproc_output = child.before.strip()
    nums = re.findall(r'\b(\d+)\b', nproc_output)
    nproc_val = int(nums[-1]) if nums else 0

    # Also check via /proc/cpuinfo for confirmation
    child.sendline("cat /proc/cpuinfo | grep -c ^processor")
    child.expect([r"[#\$] ", pexpect.TIMEOUT], timeout=10)
    cpuinfo_output = child.before.strip()
    nums2 = re.findall(r'\b(\d+)\b', cpuinfo_output)
    cpuinfo_val = int(nums2[-1]) if nums2 else 0

    print(f"\n\n=== System Partition vCPU check ===")
    print(f"  nproc: {nproc_val}")
    print(f"  /proc/cpuinfo processors: {cpuinfo_val}")
    print(f"  Expected: {EXPECTED_VCPUS}")

    if nproc_val != EXPECTED_VCPUS:
        print(f"\nFAIL: nproc returned {nproc_val}, expected {EXPECTED_VCPUS}")
        child.sendline("poweroff")
        time.sleep(3)
        child.close(force=True)
        sys.exit(1)

    if cpuinfo_val != EXPECTED_VCPUS:
        print(f"\nFAIL: /proc/cpuinfo shows {cpuinfo_val} CPUs, "
              f"expected {EXPECTED_VCPUS}")
        child.sendline("poweroff")
        time.sleep(3)
        child.close(force=True)
        sys.exit(1)

    print(f"\n\n=== PASS: All {EXPECTED_VCPUS} vCPUs online "
          f"in System Partition ===")

    child.sendline("poweroff")
    time.sleep(3)
    child.close(force=True)
    sys.exit(0)

except Exception as e:
    print(f"\nFAIL: {e}")
    try:
        child.close(force=True)
    except Exception:
        pass
    sys.exit(1)
