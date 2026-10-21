"""act.py TAG STEP... -- timed actions on a launch.py run. STEP: pad:LINE (pad line, '_' for space), wait:S,
rec:N (INTERP_REC frames), sub:N (sub-frame dumps), dl:N (draw log swaps), grab:NAME"""
import os, sys, time, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
tag = sys.argv[1]
run = os.path.join(HERE, "runs", tag)
pad = os.path.join(HERE, "runs", tag + ".pad.txt")
WGC = os.environ.get("WGCGRAB", r"C:\Users\User\AppData\Local\Temp\claude\C--Users-User-Desktop-Desktop-Stuff-Claude-Stuff-Crash-TWOC-decomp\34018395-629f-40f9-9d4d-f55117e9c429\scratchpad\bin\wgcgrab.exe")  # wgcgrab.exe (../wgcgrab_build.bat)
# step 99: ACT_KEEP_LIVES=1 keeps the lives at 9 (0x56299C) every 0.5 s while the steps run (a boss's cutscene grabs
# leave Crash standing; play.py keeps them itself)
if os.environ.get("ACT_KEEP_LIVES") == "1":
    import ctypes, threading
    sys.path.insert(0, os.path.dirname(HERE))
    import memsnap
    hw = memsnap.K32.OpenProcess(0x0438, False, memsnap.find_pid(os.environ.get("TWOC_EXE", "crash_twoc.exe")))
    def keep():
        got = ctypes.c_size_t()
        while True:
            memsnap.K32.WriteProcessMemory(hw, ctypes.c_void_p(0x56299C + memsnap.OFFSET), b"\x09", 1, ctypes.byref(got))
            time.sleep(0.5)
    threading.Thread(target=keep, daemon=True).start()
for s in sys.argv[2:]:
    k, v = s.split(":", 1)
    if k == "pad": open(pad, "w").write(v.replace("_", " ") + "\n")
    elif k == "wait": time.sleep(float(v))
    elif k == "rec": open(os.path.join(run, "rec.bin.go"), "w").write(v)
    elif k == "sub": open(os.path.join(run, "sub.trig"), "w").write(v)
    elif k == "dl": open(os.path.join(run, "dl.trig"), "w").write(v)
    elif k == "burst":
        nm, n, g = v.split(":"); subprocess.run([WGC, "Crash TWOC - D3D bridge (host)", os.path.join(HERE, "runs", tag + "_" + nm)] + [str(round(i * float(g), 2)) for i in range(int(n))], capture_output=True)
    elif k == "tele": subprocess.run([sys.executable, os.path.join(HERE, "tele.py")] + v.split(":"))
    elif k == "x": subprocess.run([sys.executable, os.path.join(HERE, v.split(":")[0])] + v.split(":")[1:])
    elif k == "poke":subprocess.run([sys.executable, os.path.join(HERE, "poke.py")] + v.split(":"))
    elif k == "grab":subprocess.run([WGC, "Crash TWOC - D3D bridge (host)", os.path.join(HERE, "runs", tag + "_" + v), "0"], capture_output=True)
    print("%.2f %s" % (time.time(), s), flush=True)
