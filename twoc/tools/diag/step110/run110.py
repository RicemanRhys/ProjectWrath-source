"""Step 110 (notes 18.113): one run of an exe copy for the saves-outside tests.

  python run110.py TAG EXE ROOT SECS [options] [VAR=VAL ...]

  TAG    names the run's files in OUT (default: ./runs): TAG.err.log, TAG.out.log, TAG.pad.txt, TAG.steps.txt
  EXE    the exe copy (crash_twoc.exe: the developer's names; ProjectWrath.exe: the package's)
  ROOT   the working folder for a launcher-style start (game_files inside); ignored with --self
  SECS   the run's length: then WM_CLOSE to its window (exit 0 expected), a hard kill 20 s later if still there

  --self         started as a double-click would: no RECOMP_* or TWOC_* variable but the VAR=VAL given (test aids
                 and RECOMP_DATA_DIR), working folder C:\\, no redirected output (the exe writes its own logs beside
                 itself); TAG.err.log is copied from there at the end
  --menu         RECOMP_DEV_FAST_TITLE=1 (the main menu ~10 s in)
  --kill=REGEX   a hard kill (TerminateProcess) as soon as the err log matches REGEX (after --kill-delay=MS)
  --keys=SPEC    pad presses after the main menu opens: "wait:S", "a", "down*3", "rb", ... (ctl.py's names; see KEYS)
  --out=DIR      where the run's files go
  --keep-env     keep the caller's RECOMP_* variables (launcher style only)

The pad is RECOMP_DEV_INPUT_FILE (TAG.pad.txt): port 0 driven from a one-line text file (notes 18.30). Every run
sets nothing else of its own: RECOMP_DATA_DIR and the like come from VAR=VAL, so a test never touches the user's
%LOCALAPPDATA%\\ProjectWrath unless it says so.
"""
import ctypes, ctypes.wintypes as w, os, re, shutil, subprocess, sys, time

KEYS = {"up": "buttons=0x1", "down": "buttons=0x2", "left": "buttons=0x4", "right": "buttons=0x8",
        "start": "buttons=0x10", "back": "buttons=0x20", "a": "a=255", "b": "b=255", "x": "x=255", "y": "y=255",
        "lt": "lt=255", "rt": "rt=255", "white": "white=255", "black": "black=255"}
u = ctypes.windll.user32


def windows_of(pid):
    res = []
    CB = ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)

    def cb(h, _):
        p = w.DWORD()
        u.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and u.IsWindowVisible(h):
            b = ctypes.create_unicode_buffer(256)
            u.GetWindowTextW(h, b, 256)
            res.append((h, b.value))
        return True
    u.EnumWindows(CB(cb), 0)
    return res


def main():
    a = sys.argv[1:]
    tag, exe, root, secs = a[0], os.path.abspath(a[1]), os.path.abspath(a[2]), float(a[3])
    opts = {}
    env_add = {}
    for x in a[4:]:
        if x.startswith("--"):
            k, _, v = x[2:].partition("=")
            opts[k] = v
        elif "=" in x:
            k, v = x.split("=", 1)
            env_add[k] = v
    out = os.path.abspath(opts.get("out", "runs"))
    os.makedirs(out, exist_ok=True)
    pad = os.path.join(out, tag + ".pad.txt")
    open(pad, "w").write("\n")
    steps = open(os.path.join(out, tag + ".steps.txt"), "w", encoding="utf-8")
    t0 = time.time()

    def step(s):
        steps.write("%7.1f %s\n" % (time.time() - t0, s))
        steps.flush()

    self_start = "self" in opts
    if self_start or "keep-env" not in opts:
        env = {k: v for k, v in os.environ.items() if not k.upper().startswith(("RECOMP_", "TWOC_"))}
    else:
        env = dict(os.environ)
    if not self_start:
        env.update(RECOMP_VBLANK="1", RECOMP_AC97_READY="1", RECOMP_D3D_BRIDGE="1")
    env["RECOMP_DEV_INPUT_FILE"] = pad
    if "menu" in opts:
        env["RECOMP_DEV_FAST_TITLE"] = "1"
    env.update(env_add)
    errp = os.path.join(out, tag + ".err.log")
    if self_start:
        exe_dir = os.path.dirname(exe)
        base = "run_twoc" if os.path.basename(exe).lower() == "crash_twoc.exe" else "ProjectWrath"
        self_err = os.path.join(exe_dir, base + ".err.log")
        p = subprocess.Popen([exe], cwd="C:\\", env=env, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL)
    else:
        self_err = None
        p = subprocess.Popen([exe], cwd=root, env=env, stdout=open(os.path.join(out, tag + ".out.log"), "wb"),
                             stderr=open(errp, "wb"))
    step("start pid %d (%s)" % (p.pid, "self" if self_start else "launcher"))
    live_err = self_err if self_start else errp
    kill_re = re.compile(opts["kill"].encode()) if "kill" in opts else None
    keys = [k for k in opts.get("keys", "").split(",") if k] if "keys" in opts else []
    menu_seen = False
    closed = False
    killed = False
    while p.poll() is None:
        time.sleep(0.2)
        el = time.time() - t0
        try:
            data = open(live_err, "rb").read() if live_err and os.path.exists(live_err) else b""
        except OSError:
            data = b""
        if kill_re and not killed and kill_re.search(data):
            time.sleep(float(opts.get("kill-delay", "0")) / 1000.0)
            subprocess.run(["taskkill", "/F", "/PID", str(p.pid)], capture_output=True)
            step("hard kill (matched %r)" % opts["kill"])
            killed = True
            break
        if not menu_seen and b"MAIN MENU OPEN" in data:
            menu_seen = True
            step("main menu open")
            for k in keys:
                if k.startswith("wait:"):
                    time.sleep(float(k[5:]))
                    step(k)
                    continue
                n = 1
                if "*" in k:
                    k, n = k.split("*")
                    n = int(n)
                for _ in range(n):
                    open(pad, "w").write(KEYS[k] + "\n")
                    time.sleep(0.12)
                    open(pad, "w").write("\n")
                    time.sleep(0.3)
                step("pressed %s x%d" % (k, n))
        if el >= secs and not closed:
            for h, t in windows_of(p.pid):
                u.PostMessageW(h, 0x0010, 0, 0)
                step("WM_CLOSE to %r" % t)
            closed = True
        if closed and el >= secs + 20:
            subprocess.run(["taskkill", "/F", "/PID", str(p.pid)], capture_output=True)
            step("hard kill after the close didn't end it")
            break
    try:
        p.wait(timeout=10)
    except subprocess.TimeoutExpired:
        pass
    step("exit %s" % (p.returncode,))
    if self_start and self_err and os.path.exists(self_err):
        for _ in range(20):
            try:
                shutil.copyfile(self_err, errp)
                break
            except OSError:
                time.sleep(0.25)
    print(tag, "exit", p.returncode, "after %.0f s" % (time.time() - t0))


if __name__ == "__main__":
    main()
