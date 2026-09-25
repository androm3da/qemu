#!/usr/bin/env python3
"""Run shell commands on the DSP over the inetd shell: dsh.py 'cmd1' 'cmd2' ..."""
import socket, sys, time
s = socket.create_connection(("192.168.1.4", 2323), timeout=60)
s.settimeout(120)
def run(cmd):
    s.sendall(("(" + cmd + ") 2>&1; echo __END_$?__\n").encode())
    buf = b""
    while b"__END_" not in buf or not buf.rstrip().endswith(b"__"):
        d = s.recv(4096)
        if not d: break
        buf += d
    out = buf.decode(errors="replace")
    return out
time.sleep(1)
s.sendall(b"\n"); time.sleep(0.5)
try: s.recv(4096)
except Exception: pass
for c in sys.argv[1:]:
    print("$ " + c); print(run(c).rsplit("__END_", 1)[0].rstrip())
