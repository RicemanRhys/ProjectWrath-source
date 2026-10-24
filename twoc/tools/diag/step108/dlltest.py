"""dlltest.py EXEDIR [V2DLL] -- step 108: how the exe treats xemu_apu.dll. Each case: a folder (with spaces and an
"&") holding a copy of EXEDIR's crash_twoc.exe and the case's xemu_apu.dll (or none), the exe started with the
launcher's switches from another folder, the message box read and OK clicked (boxclick.py), the exit code and the
log's [APU-DLL] lines printed. Cases: good (the real DLL), missing, planted (no DLL beside the exe, but a good one in
the current folder and in a folder on PATH: must not be used), text (a text file named xemu_apu.dll), foreign
(Windows' version.dll renamed), v2 (a build offering interface version 2, if given)."""
import os, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
PY = sys.executable
exe_dir = os.path.abspath(sys.argv[1])
v2 = sys.argv[2] if len(sys.argv) > 2 else None
base = os.path.join(HERE, "dt", "dll test & co")
good = os.path.join(exe_dir, "xemu_apu.dll")
cases = [("good", good), ("missing", None), ("planted", None), ("text", "TEXT"),
         ("foreign", r"C:\Windows\System32\version.dll")]
if v2:
    cases.append(("v2", v2))
for name, dll in cases:
    d = os.path.join(base, name)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    shutil.copy2(os.path.join(exe_dir, "crash_twoc.exe"), d)
    if dll == "TEXT":
        open(os.path.join(d, "xemu_apu.dll"), "w").write("not a DLL\n")
    elif dll:
        shutil.copy(dll, os.path.join(d, "xemu_apu.dll"))
    cwd = os.path.join(base, "elsewhere")
    os.makedirs(cwd, exist_ok=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith("RECOMP_")}
    env.update(RECOMP_VBLANK="1", RECOMP_AC97_READY="1", RECOMP_D3D_BRIDGE="1", RECOMP_WATCHDOG_SECS="8")
    if name == "planted":
        evil = os.path.join(base, "on path")
        os.makedirs(evil, exist_ok=True)
        shutil.copy(good, os.path.join(evil, "xemu_apu.dll"))
        shutil.copy(good, os.path.join(cwd, "xemu_apu.dll"))
        env["PATH"] = evil + os.pathsep + env["PATH"]
    else:
        try:
            os.remove(os.path.join(cwd, "xemu_apu.dll"))
        except OSError:
            pass
    err = os.path.join(d, "err.log")
    p = subprocess.Popen([os.path.join(d, "crash_twoc.exe")], cwd=cwd, env=env,
                         stdout=subprocess.DEVNULL, stderr=open(err, "wb"))
    b = subprocess.run([PY, os.path.join(HERE, "boxclick.py"), "12", "OK"], capture_output=True, text=True)
    try:
        code = p.wait(30)
    except subprocess.TimeoutExpired:
        p.kill(); code = "killed"
    lines = [l for l in open(err, encoding="utf-8", errors="replace") if "[APU-DLL]" in l or "[BOOT] emulated" in l]
    print("=== %s: exit %s" % (name, code))
    box = b.stdout.strip()
    print("  box: " + (box.replace("\n", "\n       ") if box else "(none)"))
    for l in lines:
        print("  log: " + l.rstrip())
