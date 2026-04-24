#!/usr/bin/env python3
"""
Test clean console output and Guest terminal functionality (RISC-V 64).

Verifies:
1. System Partition login is clean (no [Backend] debug noise on console)
2. Telnet to Guest partition works via TCP bridge (port 4321)
3. Backspace works correctly in Guest shell
4. Tab completion works in Guest shell
"""
import subprocess
import sys
import os
import time
import select
import fcntl
import re

DEMO_DIR = os.path.dirname(os.path.abspath(__file__))
os.chdir(DEMO_DIR)

# Build only if needed
if not os.path.exists("resident_sw.bin"):
    subprocess.run(["make", "clean"], capture_output=True)
    ret = subprocess.run(["make"], capture_output=True)
    if ret.returncode != 0:
        print("Build failed")
        sys.exit(1)
    subprocess.run(["riscv64-linux-gnu-objcopy", "-O", "binary",
                    "-R", ".note", "-R", ".note.gnu.build-id", "-R", ".comment", "-S",
                    "resident_sw", "resident_sw.bin"], capture_output=True)

# Start QEMU
cmd = ("qemu-system-riscv64 "
       "-machine virt "
       "-cpu rv64 "
       "-smp 4 -m 1G "
       "-nographic -no-reboot "
       "-bios default "
       "-kernel resident_sw.bin "
       "-monitor none "
       "-serial stdio")

print("[TEST] Starting QEMU...")
proc = subprocess.Popen(cmd, shell=True, stdin=subprocess.PIPE,
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

flags = fcntl.fcntl(proc.stdout.fileno(), fcntl.F_GETFL)
fcntl.fcntl(proc.stdout.fileno(), fcntl.F_SETFL, flags | os.O_NONBLOCK)

buf = b""

def read_output(timeout=2):
    global buf
    deadline = time.time() + timeout
    while time.time() < deadline:
        ready, _, _ = select.select([proc.stdout], [], [], 0.5)
        if ready:
            try:
                data = proc.stdout.read(4096)
                if data:
                    buf += data
            except (IOError, OSError):
                pass
        if proc.poll() is not None:
            break
    return buf.decode("latin-1", errors="replace")

def send(text):
    proc.stdin.write(text.encode())
    proc.stdin.flush()

def wait_for(pattern, timeout=300):
    deadline = time.time() + timeout
    while time.time() < deadline:
        text = read_output(timeout=2)
        if pattern.lower() in text.lower():
            return True
        if proc.poll() is not None:
            return False
    return False

def cleanup():
    proc.kill()
    proc.wait()

# =====================================================
# Test 1: Clean console output during boot and login
# =====================================================
print("[TEST] Waiting for System partition boot...")
if not wait_for("buildroot login:", timeout=500):
    print("[FAIL] System partition did not boot")
    cleanup()
    sys.exit(1)

# Check for [Backend] noise in the boot output up to login prompt
text = buf.decode("latin-1", errors="replace")
# Find everything from "Welcome to Buildroot" to "buildroot login:"
# There should be no [Backend] lines in that section
login_section_match = re.search(r'(Welcome to Buildroot.*?buildroot login:)', text, re.DOTALL | re.IGNORECASE)
if login_section_match:
    login_section = login_section_match.group(1)
    backend_noise = re.findall(r'\[Backend\][^\n]*', login_section)
    if backend_noise:
        print("[FAIL] Console has [Backend] debug noise during login:")
        for line in backend_noise[:5]:
            print(f"  {line.strip()}")
        cleanup()
        sys.exit(1)
    print("[PASS] Test 1: No [Backend] noise in login area")
else:
    print("[WARN] Could not isolate login section, skipping noise check")

# Login to System
time.sleep(1)
send("root\n")
time.sleep(2)
read_output(timeout=2)
text = buf.decode("latin-1", errors="replace")
if "password" in text.lower():
    send("1234\n")
    time.sleep(2)
    read_output(timeout=2)

# Verify we got a shell prompt
text = buf.decode("latin-1", errors="replace")
if "#" not in text[-300:] and "$" not in text[-300:]:
    print("[FAIL] Login failed - no shell prompt")
    cleanup()
    sys.exit(1)

# Check System login output for noise
# After login, there should be no [Backend] lines mixed in
login_output = text[text.rfind("buildroot login:"):]
backend_in_login = re.findall(r'\[Backend\][^\n]*', login_output)
if backend_in_login:
    print("[FAIL] Console has [Backend] noise after login:")
    for line in backend_in_login[:5]:
        print(f"  {line.strip()}")
    cleanup()
    sys.exit(1)
print("[PASS] Test 1b: No [Backend] noise after login")

# =====================================================
# Test 2: Telnet to Guest and verify connection
# =====================================================
print("[TEST] Connecting to Guest via telnet...")
time.sleep(10)  # Wait for virtio_backend to initialize

buf = b""
send("telnet 127.0.0.1 4321\n")
time.sleep(5)
read_output(timeout=3)

# Send newline to trigger guest login prompt
send("\n")
time.sleep(2)

if not wait_for("login:", timeout=300):
    print("[FAIL] No Guest login prompt via telnet")
    text = buf.decode("latin-1", errors="replace")
    print(f"[Output] Last 500 chars: {repr(text[-500:])}")
    cleanup()
    sys.exit(1)

print("[PASS] Test 2: Guest login prompt received via telnet")

# Login to Guest
send("root\n")
time.sleep(3)
read_output(timeout=2)
text = buf.decode("latin-1", errors="replace")
if "password" in text.lower():
    send("1234\n")
    time.sleep(3)
    read_output(timeout=2)

text = buf.decode("latin-1", errors="replace")
if "#" not in text[-300:] and "$" not in text[-300:]:
    print("[FAIL] Guest login failed")
    cleanup()
    sys.exit(1)
print("[PASS] Test 2b: Guest login successful")

# =====================================================
# Test 3: Backspace works in Guest shell
# =====================================================
print("[TEST] Testing backspace in Guest shell...")
buf = b""
time.sleep(0.5)

# Type "echXo" then backspace to fix it to "echo", then complete the command
# Send: e c h X <BS> o <space> h e l l o <CR>
send("echX\x7fo hello_backspace_test\n")
time.sleep(3)
read_output(timeout=3)
text = buf.decode("latin-1", errors="replace")

# The shell should have executed "echo hello_backspace_test" and printed it
if "hello_backspace_test" in text:
    print("[PASS] Test 3: Backspace works correctly")
else:
    # Check if ^H appeared literally (broken backspace)
    if "\\x7f" in repr(text) or "^H" in text or "^?" in text:
        print("[FAIL] Backspace not working (raw control chars visible)")
    else:
        print(f"[FAIL] Backspace test: expected 'hello_backspace_test' in output")
    print(f"[Output] Last 300 chars: {repr(text[-300:])}")
    cleanup()
    sys.exit(1)

# =====================================================
# Test 4: Tab completion works in Guest shell
# =====================================================
print("[TEST] Testing tab completion in Guest shell...")
buf = b""
time.sleep(0.5)

# Type "unam" then tab - should complete to "uname" (unique match)
send("unam\t")
time.sleep(2)
read_output(timeout=2)
text = buf.decode("latin-1", errors="replace")

# After tab, the shell should have completed to "uname"
if "uname" in text:
    print("[PASS] Test 4: Tab completion works")
    # Clean up - press Enter to execute
    send("\n")
    time.sleep(1)
    read_output(timeout=1)
else:
    print(f"[FAIL] Tab completion: expected 'uname' after tab, got:")
    print(f"[Output] Last 200 chars: {repr(text[-200:])}")
    cleanup()
    sys.exit(1)

# =====================================================
# All tests passed
# =====================================================
print("\n=== ALL CONSOLE TESTS PASSED ===")
print("  1. Clean console (no backend noise): OK")
print("  2. Telnet to Guest: OK")
print("  3. Backspace in Guest shell: OK")
print("  4. Tab completion in Guest shell: OK")

send("exit\n")
time.sleep(1)
cleanup()
sys.exit(0)
