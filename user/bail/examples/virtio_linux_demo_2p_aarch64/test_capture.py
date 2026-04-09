#!/usr/bin/env python3
"""Boot QEMU and capture ALL output for 120s to see Guest boot messages."""
import subprocess, time, sys, os, select, fcntl

os.chdir(os.path.dirname(os.path.abspath(__file__)))

cmd = ("qemu-system-aarch64 "
       "-machine virt,gic_version=3 -machine virtualization=true "
       "-cpu cortex-a72 -machine type=virt -m 4096 -smp 3 "
       "-bios ./u-boot/u-boot.bin "
       "-device loader,file=./resident_sw_image,addr=0x40200000,force-raw=on "
       "-nographic -no-reboot")

proc = subprocess.Popen(cmd, shell=True, stdin=subprocess.PIPE,
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
flags = fcntl.fcntl(proc.stdout.fileno(), fcntl.F_GETFL)
fcntl.fcntl(proc.stdout.fileno(), fcntl.F_SETFL, flags | os.O_NONBLOCK)

buf = b""
start = time.time()
print(f"Capturing output for 180s...")

while time.time() - start < 180:
    ready, _, _ = select.select([proc.stdout], [], [], 1)
    if ready:
        try:
            data = proc.stdout.read(8192)
            if data: buf += data
        except: pass
    if proc.poll() is not None:
        break

text = buf.decode("latin-1", errors="replace")
# Write to file for analysis
with open("boot_capture.log", "w") as f:
    f.write(text)

print(f"Captured {len(text)} chars in {time.time()-start:.0f}s")
# Show last 3000 chars
print("=== LAST 3000 CHARS ===")
print(text[-3000:])

proc.kill()
proc.wait()
