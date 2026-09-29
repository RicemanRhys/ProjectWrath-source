"""perfmon.py EXE_NAME OUT_PREFIX SECONDS [--map=crash_twoc.map] [--topmost]

Attach to a running crash_twoc copy by process name and, from outside:
  - read the game's frame counter 0x25A660 about every millisecond, logging each
    change with its QPC time (OUT.frames.txt: t_ms counter);
  - once a second, every thread's CPU time and start address (OUT.cpu.txt);
  - with --topmost, keep the bridge window "Crash TWOC - D3D bridge (host)" on
    top without activating it, so it is visible (and composed) all run.
Waits for the process to appear.
"""
import ctypes
import ctypes.wintypes as wt
import sys
import time

K32 = ctypes.WinDLL("kernel32", use_last_error=True)
NT = ctypes.WinDLL("ntdll")
U32 = ctypes.WinDLL("user32", use_last_error=True)
OFFSET = 0x10000
FRAME_VA = 0x25A660


class PE32(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ProcessID", wt.DWORD),
                ("th32DefaultHeapID", ctypes.c_void_p), ("th32ModuleID", wt.DWORD), ("cntThreads", wt.DWORD),
                ("th32ParentProcessID", wt.DWORD), ("pcPriClassBase", ctypes.c_long), ("dwFlags", wt.DWORD),
                ("szExeFile", ctypes.c_char * 260)]


class TE32(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ThreadID", wt.DWORD),
                ("th32OwnerProcessID", wt.DWORD), ("tpBasePri", ctypes.c_long), ("tpDeltaPri", ctypes.c_long),
                ("dwFlags", wt.DWORD)]


K32.CreateToolhelp32Snapshot.restype = wt.HANDLE
K32.OpenProcess.restype = wt.HANDLE
K32.OpenThread.restype = wt.HANDLE
K32.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                  ctypes.POINTER(ctypes.c_size_t)]
U32.FindWindowW.restype = wt.HWND


def find_pid(name):
    snap = K32.CreateToolhelp32Snapshot(2, 0)
    pe = PE32()
    pe.dwSize = ctypes.sizeof(pe)
    ok = K32.Process32First(snap, ctypes.byref(pe))
    pid = None
    while ok:
        if pe.szExeFile.decode(errors="replace").lower() == name.lower():
            pid = pe.th32ProcessID
        ok = K32.Process32Next(snap, ctypes.byref(pe))
    K32.CloseHandle(snap)
    return pid


def threads(pid):
    snap = K32.CreateToolhelp32Snapshot(4, 0)
    te = TE32()
    te.dwSize = ctypes.sizeof(te)
    ok = K32.Thread32First(snap, ctypes.byref(te))
    out = []
    while ok:
        if te.th32OwnerProcessID == pid:
            out.append(te.th32ThreadID)
        ok = K32.Thread32Next(snap, ctypes.byref(te))
    K32.CloseHandle(snap)
    return out


thread_info = {}


def thread_cpu(tid):
    h = thread_info.get(tid)
    if h is None:
        h = K32.OpenThread(0x0040 | 0x0800, False, tid)   # QUERY_INFORMATION | QUERY_LIMITED
        start = ctypes.c_void_p()
        if h:
            NT.NtQueryInformationThread(wt.HANDLE(h), 9, ctypes.byref(start), ctypes.sizeof(start), None)
        thread_info[tid] = h = (h, start.value or 0)
    th, start = h
    if not th:
        return None, start
    c, e, k, u = wt.FILETIME(), wt.FILETIME(), wt.FILETIME(), wt.FILETIME()
    if not K32.GetThreadTimes(wt.HANDLE(th), ctypes.byref(c), ctypes.byref(e), ctypes.byref(k), ctypes.byref(u)):
        return None, start
    f = lambda t: (t.dwHighDateTime << 32 | t.dwLowDateTime) / 1e4   # ms
    return f(k) + f(u), start


def main():
    name, out, secs = sys.argv[1], sys.argv[2], float(sys.argv[3])
    topmost = "--topmost" in sys.argv
    frames_only = "--frames" in sys.argv
    cpu_only = "--cpu" in sys.argv
    pid = None
    while pid is None:
        pid = find_pid(name)
        time.sleep(0.05)
    hp = K32.OpenProcess(0x0410, False, pid)
    mods = (ctypes.c_void_p * 1)()
    need = wt.DWORD()
    time.sleep(0.3)
    ctypes.WinDLL("psapi").EnumProcessModules(wt.HANDLE(hp), mods, ctypes.sizeof(mods), ctypes.byref(need))
    base = None
    ff = open(out + (".cpu-frames.txt" if cpu_only else ".frames.txt"), "w")
    fc = open(out + (".frames-cpu.txt" if frames_only else ".cpu.txt"), "w")
    fc.write("base %x\n" % (mods[0] or 0))
    t0 = time.perf_counter()
    if frames_only:
        open(out + ".t0", "w").write("%.3f" % time.time())
    last_cpu = {}
    next_cpu = 0.0
    last = None
    buf = ctypes.c_uint32()
    got = ctypes.c_size_t()
    hwnd = None
    while True:
        t = time.perf_counter() - t0
        if t > secs:
            break
        if not K32.ReadProcessMemory(hp, ctypes.c_void_p(FRAME_VA + OFFSET), ctypes.byref(buf), 4, ctypes.byref(got)):
            if find_pid(name) is None:
                break
        elif buf.value != last:
            ff.write("%.3f %d\n" % (t * 1000, buf.value))
            last = buf.value
        if t >= next_cpu and not frames_only:
            next_cpu += 1.0
            line = []
            for tid in threads(pid):
                cpu, start = thread_cpu(tid)
                if cpu is None:
                    continue
                d = cpu - last_cpu.get(tid, cpu)
                last_cpu[tid] = cpu
                line.append("%d:%x:%.1f" % (tid, start, d))
            fc.write("%.1f %s\n" % (t, " ".join(line)))
            fc.flush()
            ff.flush()
            mn = next((x[11:] for x in sys.argv if x.startswith("--minimize=")), None)
            if mn:
                a_, b_ = map(float, mn.split("-"))
                h2 = U32.FindWindowW(None, "Crash TWOC - D3D bridge (host)")
                if h2 and a_ <= t < a_ + 1:
                    U32.ShowWindow(h2, 6)   # SW_MINIMIZE
                if h2 and b_ <= t < b_ + 1:
                    U32.ShowWindow(h2, 9)   # SW_RESTORE
            if topmost and not (mn and a_ <= t < b_ + 1):
                hwnd = U32.FindWindowW(None, "Crash TWOC - D3D bridge (host)")
                if hwnd:
                    # HWND_TOPMOST, no move/size/activate
                    U32.SetWindowPos(hwnd, wt.HWND(-1), 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0010)
        time.sleep(0.05 if cpu_only else 0.0005)
    ff.close()
    fc.close()


if __name__ == "__main__":
    main()
