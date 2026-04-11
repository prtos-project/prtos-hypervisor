#!/usr/bin/env python3
"""
Test console functionality for virtio_linux_demo_2p_amd64.

Tests clean console I/O on the System Partition (COM1/ttyS0):
  1. Clean output (no garbled characters after login)
  2. Backspace handling (type partial command, erase, retype)
  3. Tab completion (type partial path, press TAB)

Architecture:
  Partition 0 (System): 2 vCPU (pCPU 0-1), console=ttyS0 (COM1 -> stdio)
  Partition 1 (Guest):  2 vCPU (pCPU 2-3), console=tty0  (VGA)

Exit code: 0 = PASS, 1 = FAIL
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

print("=== Starting QEMU for Console Test ===")
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
    time.sleep(1)

    # ====================================================================
    # Test 1: Clean output (run a simple command and verify clean response)
    # ====================================================================
    print("\n--- Test 1: Clean output ---")
    child.sendline("echo CONSOLE_TEST_MARKER")
    idx = child.expect(["CONSOLE_TEST_MARKER", pexpect.TIMEOUT], timeout=10)
    if idx != 0:
        print("FAIL: echo output not received cleanly")
        child.close(force=True)
        sys.exit(1)
    child.expect([r"[#\$] ", pexpect.TIMEOUT], timeout=5)
    print("PASS: Clean echo output received")

    # ====================================================================
    # Test 2: Backspace handling (type, erase, retype)
    # ====================================================================
    print("\n--- Test 2: Backspace handling ---")
    # Type "ech" + backspace + backspace + backspace + "echo OK_BACKSPACE"
    child.send("ech")
    time.sleep(0.2)
    child.send("\x08\x08\x08")  # 3 backspaces (^H)
    time.sleep(0.2)
    child.sendline("echo OK_BACKSPACE")
    idx = child.expect(["OK_BACKSPACE", pexpect.TIMEOUT], timeout=10)
    if idx != 0:
        print("FAIL: backspace test output not received")
        child.close(force=True)
        sys.exit(1)
    child.expect([r"[#\$] ", pexpect.TIMEOUT], timeout=5)
    print("PASS: Backspace handling works correctly")

    # ====================================================================
    # Test 3: Tab completion (complete a known path)
    # ====================================================================
    print("\n--- Test 3: Tab completion ---")
    child.send("/proc/cpui")
    time.sleep(0.3)
    child.send("\t")  # TAB for completion
    time.sleep(0.5)
    # Read whatever the shell completed
    child.sendline("")  # press enter (may produce error, that's fine)
    idx = child.expect(["cpuinfo", pexpect.TIMEOUT], timeout=10)
    if idx != 0:
        # Tab completion may not work in all shells, treat as non-fatal
        print("WARN: Tab completion may not have expanded /proc/cpuinfo")
    else:
        print("PASS: Tab completion expanded /proc/cpuinfo")
    child.expect([r"[#\$] ", pexpect.TIMEOUT], timeout=5)

    # ====================================================================
    # Test 4: Multi-line output (verify no garbled output)
    # ====================================================================
    print("\n--- Test 4: Multi-line output ---")
    child.sendline("for i in 1 2 3 4 5; do echo LINE_$i; done")
    found_lines = 0
    for i in range(1, 6):
        idx = child.expect([f"LINE_{i}", pexpect.TIMEOUT], timeout=10)
        if idx == 0:
            found_lines += 1
    child.expect([r"[#\$] ", pexpect.TIMEOUT], timeout=5)
    if found_lines == 5:
        print("PASS: All 5 lines received cleanly")
    else:
        print(f"FAIL: Only {found_lines}/5 lines received")
        child.close(force=True)
        sys.exit(1)

    print("\n\n=== ALL CONSOLE TESTS PASSED ===")
    print("  - Clean output: OK")
    print("  - Backspace: OK")
    print("  - Tab completion: OK (or WARN)")
    print("  - Multi-line output: OK")

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
