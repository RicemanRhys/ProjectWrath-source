"""Launch crash_twoc.exe like run_twoc.ps1 does, with stderr to a file, NUL or a
pipe, and sample guest memory from outside the process (ReadProcessMemory), so a
frame rate can be measured even when stderr is thrown away.

  python fpsrun.py TAG --stderr file|nul|pipe [--fast] [--auto] [--seconds N]
                       [--stop-at-menu] [--discover]

Writes TAG.samples.jsonl (one line per 0.5 s: USER DMA_PUT/GET, the D3D fence,
the vblank count, and the frame counters: the game's 0x25A660, incremented once
per main-loop pass by sub_0009A670, and D3D's 0x10EBD4, which moves with it),
TAG.err.log (file/pipe modes) and TAG.out.log, in --outdir (default: here).
--discover also dumps guest 0x100000..0x946000 every 5 s into TAG.disc.bin
(large; see discover.py). The menu time comes from the banner on stdout.
Guest VA v lives at host v + OFFSET (main.c prints "Offset: 0x10000").
fps.py summarises the samples. Env vars match run_twoc.ps1.
"""
import argparse, ctypes, ctypes.wintypes as wt, json, os, struct, subprocess, sys, threading, time

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
EXE = os.path.join(REPO, r"twoc\build\Release\crash_twoc.exe")
OFFSET = 0x10000
K32 = ctypes.WinDLL("kernel32", use_last_error=True)
K32.OpenProcess.restype = wt.HANDLE
K32.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                  ctypes.POINTER(ctypes.c_size_t)]

def rpm(h, va, n):
    buf = ctypes.create_string_buffer(n)
    got = ctypes.c_size_t()
    if not K32.ReadProcessMemory(h, ctypes.c_void_p(va + OFFSET), buf, n, ctypes.byref(got)):
        return None
    return buf.raw[:got.value]

def u32(h, va):
    b = rpm(h, va, 4)
    return struct.unpack("<I", b)[0] if b else None

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tag")
    ap.add_argument("--stderr", choices=["file", "nul", "pipe"], default="file")
    ap.add_argument("--fast", action="store_true")
    ap.add_argument("--auto", action="store_true")
    ap.add_argument("--seconds", type=int, default=180)
    ap.add_argument("--stop-at-menu", action="store_true")
    ap.add_argument("--discover", action="store_true")
    ap.add_argument("--outdir", default=".")
    a = ap.parse_args()
    out = lambda ext: os.path.join(a.outdir, f"{a.tag}.{ext}")

    env = dict(os.environ)
    for k in ["RECOMP_DEV_FAST_TITLE", "RECOMP_DEV_AUTOSTART", "RECOMP_FB_WINDOW", "RECOMP_FB_DUMP",
              "RECOMP_PB_EXEC_VERBOSE", "RECOMP_NV2A_TRACE", "RECOMP_PB_SCAN"]:
        env.pop(k, None)
    env.update(RECOMP_VBLANK="1", RECOMP_PB_EXEC="1", RECOMP_AC97_READY="1",
               RECOMP_WATCHDOG_SECS=str(a.seconds))
    if a.fast: env["RECOMP_DEV_FAST_TITLE"] = "1"
    if a.auto: env["RECOMP_DEV_AUTOSTART"] = "1"

    fout = open(out("out.log"), "wb")
    if a.stderr == "file":
        ferr = open(out("err.log"), "wb"); errarg = ferr
    elif a.stderr == "nul":
        ferr = None; errarg = subprocess.DEVNULL
    else:
        ferr = open(out("err.log"), "wb", buffering=1 << 20); errarg = subprocess.PIPE

    t0 = time.perf_counter()
    p = subprocess.Popen([EXE], cwd=REPO, env=env, stdout=fout, stderr=errarg)
    if a.stderr == "pipe":
        def pump():
            while True:
                b = p.stderr.read1(1 << 16)
                if not b: break
                ferr.write(b)
        threading.Thread(target=pump, daemon=True).start()

    h = K32.OpenProcess(0x0010 | 0x0400, False, p.pid)  # VM_READ | QUERY_INFORMATION
    samples = open(out("samples.jsonl"), "w")
    disc = open(out("disc.bin"), "wb") if a.discover else None
    menu_t = None
    while p.poll() is None:
        time.sleep(0.5)
        t = time.perf_counter() - t0
        dev = u32(h, 0x0010EBF0) or 0
        rec = {"t": round(t, 3), "put": u32(h, 0xFD800040), "get": u32(h, 0xFD800044),
               "fence": u32(h, dev + 0x30) if dev else None, "vbl": u32(h, 0x001BABB0), "dev": dev,
               "frame": u32(h, 0x0025A660), "d3dframe": u32(h, 0x0010EBD4)}
        if menu_t is None:
            try:
                with open(out("out.log"), "rb") as f:
                    if b"MAIN MENU OPEN" in f.read():
                        menu_t = t; rec["menu"] = True
            except OSError:
                pass
        samples.write(json.dumps(rec) + "\n"); samples.flush()
        if disc is not None:
            if int(t * 2) % 10: continue
            g = rpm(h, 0x00100000, 0x946000 - 0x00100000) or b""
            d = rpm(h, dev, 0x1000) if dev else b""
            d = d or b""
            disc.write(struct.pack("<dII", t, len(g), len(d)) + g + d)
        if a.stop_at_menu and menu_t is not None and t > menu_t + 3:
            p.kill(); break
    p.wait()
    total = time.perf_counter() - t0
    print(json.dumps({"tag": a.tag, "exit": p.returncode, "wall_s": round(total, 1),
                      "menu_s": round(menu_t, 1) if menu_t else None}))

if __name__ == "__main__":
    main()
