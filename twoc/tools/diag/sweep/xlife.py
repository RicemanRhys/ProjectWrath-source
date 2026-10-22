"""xlife.py TAG SECS TRIGFILE TEXT [BURST N GAP] -- step 100 item 4: poll the wumpa count (0x56299D) and the lives
(0x56299C) every 5 ms; at the first change of lives (an extra life) write TEXT into TRIGFILE (runs/TAG/...) and,
with BURST, grab N window grabs GAP s apart. Prints what it saw."""
import os, subprocess, sys, time
sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")
import memsnap
HERE = os.path.dirname(os.path.abspath(__file__))
tag, secs, trig, text = sys.argv[1], float(sys.argv[2]), sys.argv[3], sys.argv[4]
h = memsnap.attach(memsnap.find_pid(os.environ.get('TWOC_EXE', 'crash_twoc.exe')))
l0, w0 = memsnap.read(h, 0x56299C, 2)
print("start lives %d wumpa %d" % (l0, w0), flush=True)
t0 = time.time()
while time.time() - t0 < secs:
    b = memsnap.read(h, 0x56299C, 2)
    if not b:
        time.sleep(0.005)
        continue
    l, w = b
    if w != w0:
        print("%.3f wumpa %d" % (time.time() - t0, w), flush=True)
        w0 = w
    if l != l0:
        print("%.3f lives %d -> %d" % (time.time() - t0, l0, l), flush=True)
        open(os.path.join(HERE, "runs", tag, trig), "w").write(text)
        if len(sys.argv) > 5:
            n, g = int(sys.argv[6]), float(sys.argv[7])
            subprocess.run([os.environ["WGCGRAB"], "Crash TWOC - D3D bridge (host)", os.path.join(HERE, "runs", tag + "_" + sys.argv[5])]
                           + [str(round(i * g, 2)) for i in range(n)], capture_output=True)
        break
    time.sleep(0.005)
print("done", flush=True)
