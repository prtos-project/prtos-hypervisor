#!/usr/bin/env python3
"""
Test TCP bridge (virtio-console) from System to Guest partition (amd64).

Architecture:
  - System Partition runs virtio_backend which listens on TCP 4321 inside the VM
  - Guest Partition runs virtio_frontend + getty on /dev/hvc0 (PTY via shared memory)
  - From System shell: telnet 127.0.0.1 4321 bridges to Guest /dev/hvc0

Test verifies:
  1. System partition boots and login works
  2. TCP bridge connection from System to Guest
  3. Guest login prompt appears via TCP bridge
  4. Full Guest login (root/1234)
  5. Command execution on Guest shell

Exit: 0=PASS, 1=FAIL
"""
import pexpect
import subprocess
import sys
import os
import time
import grp

BOOT_WAIT = 80  # seconds for Guest to fully boot

os.chdir(os.path.dirname(os.path.abspath(__file__)))

# Build
subprocess.run(["make", "clean"], capture_output=True)
ret = subprocess.run(["make"], capture_output=True)
if ret.returncode != 0:
    print("Build failed:")
    print(ret.stderr.decode(errors='replace')[-500:])
    sys.exit(1)

# KVM access
kvm_ok = 0
if os.access("/dev/kvm", os.W_OK):
    kvm_ok = 1
else:
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
       "-m 1024 -smp 4 -nographic -no-reboot "
       "-cdrom resident_sw.iso "
       "-serial mon:stdio "
       "-nic none "
       f"-boot d{sg_post}")

print("=== Starting QEMU for TCP Bridge Test ===")
child = pexpect.spawn("/bin/bash", ["-c", cmd], encoding='utf-8',
                      timeout=460, codec_errors='replace')
child.logfile = sys.stdout

try:
    # Wait for System Partition login
    idx = child.expect(["buildroot login:", pexpect.TIMEOUT, pexpect.EOF],
                       timeout=240)
    if idx != 0:
        print("\n\nFAIL: System login prompt not reached")
        child.close(force=True)
        sys.exit(1)

    print("\n\n=== System login prompt ===")

    # Login to System Partition
    time.sleep(1)
    child.sendline("root")
    idx = child.expect(["assword", "buildroot login:", pexpect.TIMEOUT],
                       timeout=30)
    if idx != 0:
        print("\n\nFAIL: No password prompt")
        child.close(force=True)
        sys.exit(1)

    time.sleep(0.5)
    child.sendline("1234")
    idx = child.expect([r"[#\$] ", pexpect.TIMEOUT], timeout=30)
    if idx != 0:
        print("\n\nFAIL: System login failed")
        child.close(force=True)
        sys.exit(1)

    print("\n\n=== System login OK ===")

    # Wait for Guest to boot and all frontends to connect
    print("=== Waiting %ds for Guest boot ===" % BOOT_WAIT)
    time.sleep(BOOT_WAIT)

    # Connect to TCP bridge via telnet (with retry)
    print("\n=== Connecting to TCP bridge (telnet 127.0.0.1 4321) ===")
    tcp_connected = False
    for attempt in range(3):
        child.sendline("telnet 127.0.0.1 4321")
        idx = child.expect(["login:", "Connection refused", "Escape character",
                            pexpect.TIMEOUT], timeout=60)
        if idx == 0:
            tcp_connected = True
            break
        elif idx == 1:
            # Connection refused - Guest not ready yet, wait and retry
            print("\n[RETRY] TCP bridge connection refused, waiting 20s (%d/3)..." % (attempt + 1))
            time.sleep(20)
        elif idx == 2:
            # Connected but no login prompt yet - send newline to trigger
            time.sleep(5)
            child.sendline("")
            idx2 = child.expect(["login:", pexpect.TIMEOUT], timeout=30)
            if idx2 == 0:
                tcp_connected = True
                break
            print("\n[RETRY] Connected but no login prompt, retrying (%d/3)..." % (attempt + 1))
            # Exit telnet gracefully
            child.send("\x1d")  # Ctrl-]
            time.sleep(0.5)
            child.sendline("quit")
            time.sleep(2)
        else:
            print("\n[RETRY] Telnet timeout, retrying (%d/3)..." % (attempt + 1))
            time.sleep(10)

    if not tcp_connected:
        print("\n\nFAIL: No login prompt from TCP bridge")
        child.close(force=True)
        sys.exit(1)

    print("\n\n=== Guest login prompt via TCP bridge ===")

    # Login to Guest
    time.sleep(0.5)
    child.sendline("root")
    idx = child.expect(["assword", pexpect.TIMEOUT], timeout=15)
    if idx != 0:
        print("\n\nFAIL: No password prompt from Guest")
        child.close(force=True)
        sys.exit(1)

    time.sleep(0.5)
    child.sendline("1234")
    idx = child.expect([r"[#\$] ", "incorrect", pexpect.TIMEOUT], timeout=15)
    if idx == 1:
        # Retry login
        child.expect(["login:", pexpect.TIMEOUT], timeout=10)
        child.sendline("root")
        child.expect(["assword", pexpect.TIMEOUT], timeout=10)
        child.sendline("1234")
        idx = child.expect([r"[#\$] ", pexpect.TIMEOUT], timeout=15)
        if idx != 0:
            print("\n\nFAIL: Guest login failed on retry")
            child.close(force=True)
            sys.exit(1)
    elif idx != 0:
        print("\n\nFAIL: Guest login failed")
        child.close(force=True)
        sys.exit(1)

    print("\n\n=== Guest login OK ===")

    # Run command on Guest
    time.sleep(0.5)
    child.sendline("echo TCP_BRIDGE_TEST_OK")
    idx = child.expect(["TCP_BRIDGE_TEST_OK", pexpect.TIMEOUT], timeout=10)
    if idx != 0:
        print("\n\nFAIL: Command execution on Guest failed")
        child.close(force=True)
        sys.exit(1)

    print("\n\n=== Guest command execution OK ===")

    # Verify Guest vCPU count
    child.sendline("nproc")
    child.expect([r"[#\$] ", pexpect.TIMEOUT], timeout=10)
    nproc_out = child.before.strip()
    if "2" in nproc_out:
        print("Guest vCPUs: 2 (correct)")
    else:
        print("WARN: Guest nproc = %s" % repr(nproc_out))

    print("\n\n=== ALL TCP BRIDGE TESTS PASSED ===")
    print("  - System boot + login: OK")
    print("  - TCP bridge connection: OK")
    print("  - Guest login prompt: OK")
    print("  - Guest login (root/1234): OK")
    print("  - Guest command execution: OK")

    # Exit telnet
    child.send("\x1d")  # Ctrl-]
    time.sleep(0.5)
    child.sendline("quit")
    time.sleep(1)

    child.sendline("poweroff")
    time.sleep(3)
    child.close(force=True)
    sys.exit(0)

except Exception as e:
    print("\nFAIL: %s" % e)
    try:
        child.close(force=True)
    except Exception:
        pass
    sys.exit(1)
