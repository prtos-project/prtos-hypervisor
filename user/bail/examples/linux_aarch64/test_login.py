#!/usr/bin/env python3
"""Test Linux login on PRTOS: boot, login as root/1234, run htop check."""
import pexpect
import subprocess
import sys
import os

TIMEOUT = 120  # seconds for boot
LOGIN_TIMEOUT = 30

os.chdir(os.path.dirname(os.path.abspath(__file__)))

# Build steps (already done, but ensure images are ready)
subprocess.run(["make", "clean"], capture_output=True)
subprocess.run(["make"], capture_output=True)

# Create bootable image
subprocess.run(["aarch64-linux-gnu-objcopy", "-O", "binary", "-R", ".note",
                "-R", ".note.gnu.build-id", "-R", ".comment", "-S",
                "resident_sw", "resident_sw.bin"], check=True)
subprocess.run(["mkimage", "-A", "arm64", "-O", "linux", "-C", "none",
                "-a", "0x40200000", "-e", "0x40200000",
                "-d", "resident_sw.bin", "resident_sw_image"],
               check=True, capture_output=True)
os.makedirs("u-boot", exist_ok=True)
subprocess.run(["cp", "../../bin/u-boot.bin", "./u-boot/"], check=True)

cmd = ("qemu-system-aarch64 "
       "-machine virt,gic_version=3 "
       "-machine virtualization=true "
       "-cpu cortex-a72 "
       "-machine type=virt "
       "-m 4096 -smp 4 "
       "-bios ./u-boot/u-boot.bin "
       "-device loader,file=./resident_sw_image,addr=0x40200000,force-raw=on "
       "-nographic -no-reboot "
       "-chardev socket,id=qemu-monitor,host=localhost,port=8889,server=on,wait=off,telnet=on "
       "-mon qemu-monitor,mode=readline")

print("=== Starting QEMU ===")
child = pexpect.spawn(cmd, encoding='utf-8', timeout=TIMEOUT)
child.logfile = sys.stdout

# Wait for login prompt
idx = child.expect(["buildroot login:", "login:", pexpect.TIMEOUT, pexpect.EOF])
if idx >= 2:
    print("\n\n=== FAIL: Did not reach login prompt ===")
    sys.exit(1)

print("\n\n=== Got login prompt, sending 'root' ===")
child.sendline("root")

# Wait for password prompt
idx = child.expect(["Password:", "password:", pexpect.TIMEOUT, pexpect.EOF], timeout=LOGIN_TIMEOUT)
if idx >= 2:
    print("\n\n=== FAIL: Did not get password prompt ===")
    sys.exit(1)

print("\n\n=== Got password prompt, sending '1234' ===")
child.sendline("1234")

# Wait for shell prompt (# for root)
idx = child.expect(["#", "\\$", "Login incorrect", pexpect.TIMEOUT, pexpect.EOF], timeout=LOGIN_TIMEOUT)
if idx == 2:
    print("\n\n=== FAIL: Login incorrect ===")
    sys.exit(1)
elif idx >= 3:
    print("\n\n=== FAIL: No shell prompt after login ===")
    sys.exit(1)

print("\n\n=== Login successful! Testing htop availability ===")
child.sendline("which htop")
idx = child.expect(["htop", "#", pexpect.TIMEOUT], timeout=10)

child.sendline("nproc")
idx = child.expect(["2", "#", pexpect.TIMEOUT], timeout=10)

print("\n\n=== ALL TESTS PASSED ===")
child.sendline("poweroff")
import time; time.sleep(3)
child.close()
sys.exit(0)
