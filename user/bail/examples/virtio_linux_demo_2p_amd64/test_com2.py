#!/usr/bin/env python3
"""Test COM2 telnet login for Guest partition."""
import subprocess, time, socket, sys, os, signal

DEMO_DIR = os.path.dirname(os.path.abspath(__file__))
ISO = os.path.join(DEMO_DIR, "resident_sw.iso")

cmd = ("sg kvm -c 'qemu-system-x86_64 "
       "-enable-kvm -cpu host,-waitpkg "
       "-m 512 -smp 4 "
       "-no-reboot "
       "-cdrom {iso} "
       "-serial mon:stdio "
       "-serial telnet::4321,server,nowait "
       "-vga std -display none -vnc :1 "
       "-boot d'").format(iso=ISO)

print("[TEST] Starting QEMU...")
proc = subprocess.Popen(cmd, shell=True, stdin=subprocess.PIPE,
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

# Collect COM1 (System) output in background
com1_data = []
def read_com1():
    import threading
    def reader():
        while True:
            ch = proc.stdout.read(1)
            if not ch:
                break
            com1_data.append(ch)
    t = threading.Thread(target=reader, daemon=True)
    t.start()
read_com1()

# Wait for QEMU to start and listen on port 4321
time.sleep(2)

# Try connecting to telnet port 4321 (COM2)
print("[TEST] Connecting to COM2 (telnet localhost:4321)...")
sock = None
for attempt in range(5):
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5)
        sock.connect(("localhost", 4321))
        print(f"[TEST] Connected to port 4321 (attempt {attempt+1})")
        break
    except Exception as e:
        print(f"[TEST] Connect attempt {attempt+1} failed: {e}")
        if sock:
            sock.close()
            sock = None
        time.sleep(2)

if not sock:
    print("[FAIL] Could not connect to port 4321")
    proc.kill()
    sys.exit(1)

# Wait for Guest to boot and getty to start (up to 60s)
print("[TEST] Waiting for Guest to boot and getty login prompt...")
com2_buf = b""
sock.settimeout(2)
deadline = time.time() + 90
got_login = False

while time.time() < deadline:
    try:
        data = sock.recv(1024)
        if data:
            com2_buf += data
            text = com2_buf.decode("latin-1")
            sys.stdout.write(f"\r[COM2] received {len(com2_buf)} bytes: {repr(text[-80:])}")
            sys.stdout.flush()
            if "login:" in text.lower():
                got_login = True
                print(f"\n[TEST] Got login prompt!")
                break
    except socket.timeout:
        # Check if QEMU is still running
        if proc.poll() is not None:
            print("\n[FAIL] QEMU exited")
            break
        # Print COM1 status
        com1_text = b"".join(com1_data).decode("latin-1", errors="replace")
        if "login:" in com1_text.lower():
            print(f"\n[COM1] System login prompt seen ({len(com1_text)} bytes total)")

print()
if got_login:
    print("[PASS] Guest COM2 telnet login prompt received!")
    # Try to login
    print("[TEST] Sending 'root' username...")
    sock.sendall(b"root\n")
    time.sleep(3)
    try:
        data = sock.recv(4096)
        com2_buf += data
        text = com2_buf.decode("latin-1")
        print(f"[COM2] After login: {repr(text[-200:])}")
        if "password" in text.lower() or "#" in text or "$" in text:
            print("[TEST] Password prompt or shell seen!")
    except socket.timeout:
        pass
else:
    print("[FAIL] No login prompt on COM2 within timeout")
    print(f"[COM2] Total received: {len(com2_buf)} bytes")
    if com2_buf:
        print(f"[COM2] Data: {repr(com2_buf[:200])}")
    com1_text = b"".join(com1_data).decode("latin-1", errors="replace")
    # Show last 500 chars of COM1
    print(f"[COM1] Last 500 chars: {repr(com1_text[-500:])}")

# Cleanup  
sock.close()
proc.kill()
proc.wait()
sys.exit(0 if got_login else 1)
