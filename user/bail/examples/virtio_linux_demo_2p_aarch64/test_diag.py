#!/usr/bin/env python3
"""Diagnostic: boot QEMU, login to System, check shared memory state."""
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
def read_output(timeout=2):
    global buf
    deadline = time.time() + timeout
    while time.time() < deadline:
        ready, _, _ = select.select([proc.stdout], [], [], 0.5)
        if ready:
            try:
                data = proc.stdout.read(4096)
                if data: buf += data
            except: pass
    return buf.decode("latin-1", errors="replace")

def send(text):
    proc.stdin.write(text.encode()); proc.stdin.flush()

def wait_for(pattern, timeout=300):
    deadline = time.time() + timeout
    while time.time() < deadline:
        text = read_output(timeout=2)
        if pattern.lower() in text.lower(): return True
        if proc.poll() is not None: return False
    return False

# Boot
print("[DIAG] Booting QEMU...")
if not wait_for("buildroot login:", timeout=500):
    text = buf.decode("latin-1", errors="replace")
    print(f"[DIAG] System didn't boot. Last 1000: {repr(text[-1000:])}")
    proc.kill(); proc.wait(); sys.exit(1)

print("[DIAG] System booted. Logging in...")
time.sleep(1); send("root\n"); time.sleep(2); read_output(2)
send("1234\n"); time.sleep(3); read_output(2)

# Wait for backend to start
print("[DIAG] Waiting 30s for backend to start...")
time.sleep(30); read_output(5)

# Check backend state
print("[DIAG] Checking shared memory state...")
buf = b""

# Read Console SHM header: magic, backend_ready, frontend_ready
send("echo '=== DIAG START ==='\n")
time.sleep(1)
# Use devmem to read Console SHM at 0x20500000
# Offset 0: magic (4 bytes), Offset 4: backend_ready, Offset 8: frontend_ready
send("xxd -l 64 /dev/mem 2>&1 | head -4\n")
time.sleep(2)

# Check that devmem works
send("dd if=/dev/mem bs=1 skip=$((0x20500000)) count=16 2>/dev/null | xxd\n")
time.sleep(3)

# Check what processes are running on this partition
send("ps aux | grep -E 'virtio|prtos' 2>&1\n")
time.sleep(2)

# Check TCP port
send("netstat -tlnp 2>/dev/null || ss -tlnp\n")
time.sleep(2)

# Wait 60s more for Guest to boot and set frontend_ready
print("[DIAG] Waiting 60s more for Guest to initialize...")
time.sleep(60)

# Re-check shared memory
send("echo '=== AFTER 60s WAIT ==='\n")
send("dd if=/dev/mem bs=1 skip=$((0x20500000)) count=16 2>/dev/null | xxd\n")
time.sleep(3)

# Check all output
read_output(5)
text = buf.decode("latin-1", errors="replace")
print("[DIAG] Full diagnostic output:")
print(text)

send("poweroff\n")
time.sleep(5)
proc.kill(); proc.wait()
