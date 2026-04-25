#!/usr/bin/env python3
"""
Test COM2 telnet login for Guest partition (amd64).

Architecture:
  - QEMU -serial telnet::PORT exposes Guest COM2 (ttyS1) on HOST
  - PRTOS passes through COM2 I/O (0x2F8-0x2FF) to Guest partition
  - Guest runs getty on ttyS1 via S99virtio_guest init script
  - set_serial_poll forces irq=0 for timer-based polling (no IRQ3 routing)

Test verifies:
  1. COM2 telnet connection from HOST
  2. Login prompt (after newline trigger)
  3. Full login (root/1234)
  4. Command execution on Guest shell

Exit: 0=PASS, 1=FAIL
"""
import subprocess, time, socket, sys, os, threading, grp, random

DEMO_DIR = os.path.dirname(os.path.abspath(__file__))
COM2_PORT = random.randint(15000, 19999)
BOOT_WAIT = 100  # seconds for full boot

os.chdir(DEMO_DIR)

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
       "-m 1024 -smp 4 -no-reboot -nographic "
       "-cdrom resident_sw.iso "
       "-serial mon:stdio "
       f"-serial telnet::{COM2_PORT},server,nowait "
       "-nic none "
       f"-boot d{sg_post}")

print("[TEST] Starting QEMU with COM2 on port %d..." % COM2_PORT)
proc = subprocess.Popen(cmd, shell=True, stdin=subprocess.PIPE,
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

# Read COM1 in background
com1_buf = bytearray()
def read_com1():
    while True:
        ch = proc.stdout.read(1)
        if not ch:
            break
        com1_buf.extend(ch)
threading.Thread(target=read_com1, daemon=True).start()

print("[TEST] Waiting %ds for boot..." % BOOT_WAIT)
time.sleep(BOOT_WAIT)
com1_text = com1_buf.decode('latin-1', errors='replace')
print("[TEST] COM1: %d bytes, system login: %s" % (
    len(com1_buf), 'login' in com1_text.lower()))

# Connect to COM2
print("[TEST] Connecting to COM2...")
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.settimeout(10)
try:
    sock.connect(("localhost", COM2_PORT))
except Exception as e:
    print("[FAIL] Cannot connect: %s" % e)
    proc.kill(); proc.wait(); sys.exit(1)

# Drain initial QEMU telnet negotiation
time.sleep(1)
try:
    sock.recv(4096)
except socket.timeout:
    pass

# Send newline to trigger getty re-display login prompt (with retry)
login_found = False
for attempt in range(6):
    sock.sendall(b"\r\n")
    time.sleep(5)
    buf = b""
    sock.settimeout(3)
    try:
        while True:
            d = sock.recv(4096)
            if not d: break
            buf += d
    except socket.timeout:
        pass
    text = buf.decode('latin-1', errors='replace')
    if 'login' in text.lower():
        login_found = True
        break
    # Guest may still be booting, wait and retry
    print("[TEST] COM2 login prompt not ready yet, retrying (%d/6)..." % (attempt + 1))

if not login_found:
    print("[FAIL] No login prompt on COM2")
    print("[COM2] Received: %s" % repr(text[:200]))
    sock.close(); proc.kill(); proc.wait(); sys.exit(1)

print("[TEST] Login prompt received")

# Login
sock.sendall(b"root\r\n")
time.sleep(3)
buf = b""
try:
    while True:
        d = sock.recv(4096)
        if not d: break
        buf += d
except socket.timeout:
    pass
text = buf.decode('latin-1', errors='replace')

if 'assword' not in text.lower():
    print("[FAIL] No password prompt")
    print("[COM2] After username: %s" % repr(text[:200]))
    sock.close(); proc.kill(); proc.wait(); sys.exit(1)

print("[TEST] Password prompt received")
sock.sendall(b"1234\r\n")
time.sleep(5)

buf = b""
try:
    while True:
        d = sock.recv(4096)
        if not d: break
        buf += d
except socket.timeout:
    pass
text = buf.decode('latin-1', errors='replace')

if '#' not in text and '$' not in text:
    print("[FAIL] No shell prompt after login")
    print("[COM2] After password: %s" % repr(text[:200]))
    sock.close(); proc.kill(); proc.wait(); sys.exit(1)

print("[TEST] Shell prompt received")

# Run command
sock.sendall(b"echo COM2_TEST_OK\r\n")
time.sleep(3)
buf = b""
try:
    while True:
        d = sock.recv(4096)
        if not d: break
        buf += d
except socket.timeout:
    pass
text = buf.decode('latin-1', errors='replace')

if 'COM2_TEST_OK' not in text:
    print("[FAIL] Command output not received")
    print("[COM2] After echo: %s" % repr(text[:200]))
    sock.close(); proc.kill(); proc.wait(); sys.exit(1)

print("[PASS] COM2 full login + command execution works!")
print("  - Login prompt: OK")
print("  - Password prompt: OK")
print("  - Shell prompt: OK")
print("  - Command execution: OK")

sock.close()
proc.kill()
proc.wait()
sys.exit(0)
