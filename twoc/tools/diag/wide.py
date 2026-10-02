"""wide.py SECONDS SCHEDULE -- widen the game's camera aspect from outside (16:9 scoping test, step 51, notes 18.54).

The frame's camera block is 0x242660 (fov, aspect, near, far, copied in from the current camera);
sub_000956B0 builds the projection from it (x scale = cot(fov/2) * aspect) and the frustum tangents
0x242350 / 0x2425DC that the culling (sub_00094E20, sub_00095410) reads. A grab taken while ON is
16:9 content squeezed into the 4:3 window: unsqueeze it to look at the edges.

Finds every camera source (fov, 0.75, near, far floats, as camfind.py) in the image and heap, and
rewrites their aspect (and the frame's copy at 0x242664) every ~2 ms: 0.5625 (16:9) while the
schedule says on, 0.75 while off. SCHEDULE is "on:2,off:2" style, repeated. Prints, about every
0.5 s, the state and proj[0] at 0x242398 (2.54 * aspect: 1.905 for 4:3, 1.429 for 16:9).
Rescans for cameras every 2 s (a level load moves the heap camera).
"""
import ctypes, ctypes.wintypes as wt, os, struct, sys, time
import numpy as np
import memsnap

K32 = memsnap.K32
K32.WriteProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                   ctypes.POINTER(ctypes.c_size_t)]
pid = memsnap.find_pid()
h = K32.OpenProcess(0x0438, False, pid)
secs = float(sys.argv[1])
sched = [(p.split(":")[0], float(p.split(":")[1])) for p in sys.argv[2].split(",")]
period = sum(d for _, d in sched)


def wf(va, v):
    got = ctypes.c_size_t()
    K32.WriteProcessMemory(h, ctypes.c_void_p(va + memsnap.OFFSET), struct.pack("<f", v), 4, ctypes.byref(got))


def scan():
    out = []
    for name, lo, hi in memsnap.RANGES[:2]:
        for base in range(lo, hi, 0x100000):
            n = min(0x100000 + 16, hi - base)
            b = memsnap.read(h, base, n)
            if not b:
                continue
            f = np.frombuffer(b[:len(b) // 4 * 4], dtype="<f4")
            with np.errstate(invalid="ignore", over="ignore"):
                a = (f[1:-2] == np.float32(0.75)) | (f[1:-2] == np.float32(0.5625))
                i = np.where(a & (f[:-3] > 0.05) & (f[:-3] < 2.5) & (f[2:-1] > 0.01) & (f[2:-1] < 10) &
                             (f[3:] > 10 * f[2:-1]))[0]
            out += [int(base + 4 * k + 4) for k in i if 4 * k < 0x100000]
    return out


t0 = time.time()
cams, last_scan, last_print = scan(), time.time(), 0
print("cameras (aspect addresses):", " ".join("%08X" % a for a in cams), flush=True)
while time.time() - t0 < secs:
    t = (time.time() - t0) % period
    state = None
    for s, d in sched:
        if t < d:
            state = s
            break
        t -= d
    v = 0.5625 if state == "on" else 0.75
    for a in cams:
        wf(a, v)
    if time.time() - last_scan > 2:
        cams, last_scan = scan(), time.time()
    if time.time() - last_print > 0.5:
        last_print = time.time()
        p0 = struct.unpack("<f", memsnap.read(h, 0x242398, 4))[0]
        print("%.2f %s proj0 %.4f cams %d" % (time.time() - t0, state, p0, len(cams)), flush=True)
    time.sleep(0.002)
