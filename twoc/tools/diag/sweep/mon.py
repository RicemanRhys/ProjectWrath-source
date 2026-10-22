"""mon.py TAG SECS [EVERY] [ADDR:fmt ...] -- step 100: log Crash's position, lives, wumpa and the frame counter every
EVERY s (default 0.25) to runs/TAG.mon.txt, plus any extra guest values (fmt f = float, I = u32, B = byte)."""
import os, struct, sys, time
sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")
import memsnap
HERE = os.path.dirname(os.path.abspath(__file__))
tag, secs = sys.argv[1], float(sys.argv[2])
every = float(sys.argv[3]) if len(sys.argv) > 3 else 0.25
extra = [(int(a.split("=")[0], 16), a.split("=")[1]) for a in sys.argv[4:]]   # ADDR=fmt
h = memsnap.attach(memsnap.find_pid(os.environ.get('TWOC_EXE', 'crash_twoc.exe')))
out = open(os.path.join(HERE, "runs", tag + ".mon.txt"), "a")
t0 = time.time()
while time.time() - t0 < secs:
    try:
        x, y, z = struct.unpack_from("<3f", memsnap.read(h, memsnap.u32(h, 0x17B0DC) + 0x6C, 12))
        lv, wu = memsnap.read(h, 0x56299C, 2)
        fc = memsnap.u32(h, 0x25A660)
        ex = " ".join("%X=%s" % (a, struct.unpack("<" + f, memsnap.read(h, a, struct.calcsize(f)))[0]) for a, f in extra)
        out.write("%.2f fc %d pos %.2f %.2f %.2f lives %d wumpa %d %s\n" % (time.time(), fc, x, y, z, lv, wu, ex))
        out.flush()
    except Exception as e:
        out.write("%.2f err %s\n" % (time.time(), e))
        break
    time.sleep(every)
