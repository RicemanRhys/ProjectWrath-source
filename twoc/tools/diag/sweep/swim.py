"""swim.py TAG SECS [GRABEVERY] -- step 96: Sea Shell (a side-on swimming level): hold the stick right, weaving
up and down, a spin now and then; lives kept at 9 (0x56299C); Crash's position (0x8F5120 + 0x6C) and the frame
counter logged every 0.5 s to runs/TAG.swim.txt; window grabs TAG_wNN every GRABEVERY s."""
import ctypes, ctypes.wintypes as wt, os, struct, subprocess, sys, threading, time
sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")
import memsnap

K32 = memsnap.K32
K32.WriteProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                   ctypes.POINTER(ctypes.c_size_t)]
tag, secs = sys.argv[1], float(sys.argv[2])
every = float(sys.argv[3]) if len(sys.argv) > 3 else 10.0
HERE = os.path.dirname(os.path.abspath(__file__))
WGC = os.environ.get("WGCGRAB", r"C:\Users\User\AppData\Local\Temp\claude\C--Users-User-Desktop-Desktop-Stuff-Claude-Stuff-Crash-TWOC-decomp\34018395-629f-40f9-9d4d-f55117e9c429\scratchpad\bin\wgcgrab.exe")
pad = os.path.join(HERE, "runs", tag + ".pad.txt")
pid = memsnap.find_pid(os.environ.get("TWOC_EXE", "crash_twoc.exe"))
h = K32.OpenProcess(0x0438, False, pid)
got = ctypes.c_size_t()
log = open(os.path.join(HERE, "runs", tag + ".swim.txt"), "a")
F = 32767
# (seconds, pad line): right, right and up, right, right and down, a spin
cycle = [(2.0, "lx=%d" % F), (1.0, "lx=%d ly=%d" % (F, F)), (1.5, "lx=%d" % F), (1.0, "lx=%d ly=%d" % (F, -F)),
         (0.15, "lx=%d x=255" % F), (0.35, "lx=%d" % F)]


def grab(name):
    threading.Thread(target=lambda: subprocess.run([WGC, "Crash TWOC - D3D bridge (host)",
                                                     os.path.join(HERE, "runs", name), "0"],
                                                    capture_output=True, timeout=60), daemon=True).start()


t0 = time.time()
nlog = ngrab = 0.0
i = 0
seg_end = 0.0
while time.time() - t0 < secs:
    t = time.time() - t0
    if t >= seg_end:
        d, line = cycle[i % len(cycle)]
        open(pad, "w").write(line + "\n")
        seg_end = t + d
        i += 1
    if t >= nlog:
        try:
            K32.WriteProcessMemory(h, ctypes.c_void_p(0x56299C + memsnap.OFFSET), b"\x09", 1, ctypes.byref(got))
            x, y, z = struct.unpack("<3f", memsnap.read(h, 0x8F5120 + 0x6C, 12))
            log.write("%.2f %d %.3f %.3f %.3f\n" % (t, memsnap.u32(h, 0x25A660), x, y, z))
            log.flush()
        except Exception as e:
            log.write("%.2f error %s\n" % (t, e))
        nlog += 0.5
    if t >= ngrab:
        grab("%s_w%02d" % (tag, int(ngrab / every)))
        ngrab += every
    time.sleep(0.02)
open(pad, "w").write("\n")
log.close()
print("swim %s: %.0f s" % (tag, secs))
