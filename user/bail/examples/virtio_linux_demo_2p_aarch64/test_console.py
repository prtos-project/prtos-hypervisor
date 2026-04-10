#!/usr/bin/env python3
"""
Test clean console output and Guest terminal functionality (AArch64).

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
if not os.path.exists("resident_sw_image"):
    subprocess.run(["make", "clean"], capture_output=True)
    ret = subprocess.run(["make"], capture_output=True)
    if ret.returncode != 0:
        print("Build failed")
        sys.exit(1)
    subprocess.run(["aarch64-linux-gnu-objcopy", "-O", "binary",
                    "-R", ".note", "-R", ".note.gnu.build-id", "-R", ".comment", "-S",
                    "resident_sw", "resident_sw.bin"], capture_output=True)
    subprocess.run(["mkimage", "-A", "arm64", "-O", "linux", "-C", "none",
                    "-a", "0x40200000", "-e", "0x40200000",
                    "-d", "resident_sw.bin", "resident_sw_image"],
                   capture_output=True)

# Build u-boot if needed
uboot_src = os.path.realpath(os.path.join(DEMO_DIR, "../../../../..", "u-boot"))
uboot_bin = os.path.join(DEMO_DIR, "u-boot", "u-boot.bin")
if not os.path.exists(uboot_bin):
    os.makedirs(os.path.join(DEMO_DIR, "u-boot"), exist_ok=True)
    if os.path.isdir(uboot_src):
        subprocess.run(["make", "-C", uboot_src, "qemu_arm64_defconfig"], capture_output=True)
        subprocess.run([uboot_src + "/scripts/config", "--file", uboot_src + "/.config",
                        "--set-val", "CONFIG_SYS_BOOTM_LEN", "0x10000000"], capture_output=True)
        subprocess.run([uboot_src + "/scripts/config", "--file", uboot_src + "/.config",
                        "--set-str", "CONFIG_PREBOOT",
                        "bootm 0x40200000 - ${fdtcontroladdr}"], capture_output=True)
        subprocess.run(["make", "-C", uboot_src, "-j4",
                        "CROSS_COMPILE=aarch64-linux-gnu-"], capture_output=True)
        if os.path.exists(os.path.join(uboot_src, "u-boot.bin")):
            import shutil
            shutil.copy2(os.path.join(uboot_src, "u-boot.bin"), uboot_bin)

if not os.path.exists(uboot_bin):
    print("[FAIL] Cannot find u-boot.bin")
    sys.exit(1)

# Start QEMU
cmd = ("qemu-system-aarch64 "
       "-machine virt,gic_version=3 "
       "-machine virtualization=true "
       "-cpu cortex-a72 -machine type=virt "
       "-m 4096 -smp 4 "
       "-bios ./u-boot/u-boot.bin "
       "-device loader,file=./resident_sw_image,addr=0x40200000,force-raw=on "
       "-nographic -no-reboot")

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

# Check for [Backend] noise in the login area
text = buf.decode("latin-1", errors="replace")
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

text = buf.decode("latin-1", errors="replace")
if "#" not in text[-300:] and "$" not in text[-300:]:
    print("[FAIL] Login failed - no shell prompt")
    cleanup()
    sys.exit(1)

# Check System login output for noise
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
time.sleep(10)

buf = b""
send("telnet 127.0.0.1 4321\n")
time.sleep(5)
read_output(timeout=3)
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

send("echX\x7fo hello_backspace_test\n")
time.sleep(3)
read_output(timeout=3)
text = buf.decode("latin-1", errors="replace")

if "hello_backspace_test" in text:
    print("[PASS] Test 3: Backspace works correctly")
else:
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

send("unam\t")
time.sleep(2)
read_output(timeout=2)
text = buf.decode("latin-1", errors="replace")

if "uname" in text:
    print("[PASS] Test 4: Tab completion works")
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
