import os, sys, time, select
dev = sys.argv[1]; lines = open(sys.argv[2]).read().split("\n")
fd = os.open(dev, os.O_RDWR | os.O_NONBLOCK)
t0 = time.time(); buf = b""
def drain(secs):
    global buf
    end = time.time() + secs
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.02)
        if r:
            try: buf += os.read(fd, 4096)
            except BlockingIOError: pass
        while b"\xf7" in buf:
            i = buf.index(b"\xf7"); f, buf = buf[:i+1], buf[i+1:]
            f = bytes(b for b in f if b < 0xF8)
            j = f.find(b"\xf0")
            if j >= 0: print("%7.3f <- %s" % (time.time()-t0, f[j:].hex(" ")), flush=True)
for l in lines:
    l = l.strip()
    if not l: continue
    if l.startswith("#"): print("%7.3f %s" % (time.time()-t0, l), flush=True); continue
    if l.startswith("wait"): drain(float(l.split()[1])); continue
    b = bytes(int(x, 16) for x in l.split())
    n = 0
    while n < len(b):
        try: n += os.write(fd, b[n:])
        except BlockingIOError: time.sleep(0.002)
    print("%7.3f -> %d bytes (written %d)" % (time.time()-t0, len(b), n), flush=True)
    drain(0.4)
