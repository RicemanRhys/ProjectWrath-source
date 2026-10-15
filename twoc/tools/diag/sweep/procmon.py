"""procmon.py TAG [PERIOD] -- step 73 endurance: every PERIOD s (60) until the game exits, one line per sample into
runs/TAG.mon.txt: minute, handles, threads, private KiB, working set KiB, GDI/USER objects, and the latest
[HEAPSTAT]/[CONTIGSTAT]/[VMSTAT]/[POOLSTAT]/[KERNEL] summary/[D3DB] host +/swap gaps lines from the log, plus the
level files loaded since the last sample."""
import ctypes, ctypes.wintypes as wt, os, re, sys, time
sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")
import memsnap

HERE = os.path.dirname(os.path.abspath(__file__))
tag = sys.argv[1]
period = float(sys.argv[2]) if len(sys.argv) > 2 else 60
errp = os.path.join(HERE, "runs", tag + ".err.log")
outp = os.path.join(HERE, "runs", tag + ".mon.txt")
K32 = ctypes.WinDLL("kernel32", use_last_error=True)
PSAPI = ctypes.WinDLL("psapi")
U32 = ctypes.WinDLL("user32")


class PMC(ctypes.Structure):
    _fields_ = [("cb", wt.DWORD), ("PageFaultCount", wt.DWORD), ("PeakWorkingSetSize", ctypes.c_size_t),
                ("WorkingSetSize", ctypes.c_size_t), ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPagedPoolUsage", ctypes.c_size_t), ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                ("QuotaNonPagedPoolUsage", ctypes.c_size_t), ("PagefileUsage", ctypes.c_size_t),
                ("PeakPagefileUsage", ctypes.c_size_t), ("PrivateUsage", ctypes.c_size_t)]


class TE32(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ThreadID", wt.DWORD),
                ("th32OwnerProcessID", wt.DWORD), ("tpBasePri", ctypes.c_long), ("tpDeltaPri", ctypes.c_long),
                ("dwFlags", wt.DWORD)]


K32.OpenProcess.restype = wt.HANDLE
K32.CreateToolhelp32Snapshot.restype = wt.HANDLE


def threads(pid):
    snap = K32.CreateToolhelp32Snapshot(4, 0)
    te = TE32()
    te.dwSize = ctypes.sizeof(te)
    n = 0
    ok = K32.Thread32First(wt.HANDLE(snap), ctypes.byref(te))
    while ok:
        if te.th32OwnerProcessID == pid:
            n += 1
        ok = K32.Thread32Next(wt.HANDLE(snap), ctypes.byref(te))
    K32.CloseHandle(wt.HANDLE(snap))
    return n


pid = memsnap.find_pid(os.environ.get("TWOC_EXE", "crash_twoc.exe"))
hp = K32.OpenProcess(0x1000 | 0x0010, False, pid)   # QUERY_LIMITED_INFORMATION | VM_READ
KEYS = ["[HEAPSTAT]", "[CONTIGSTAT]", "[VMSTAT]", "[POOLSTAT]", "[KERNEL] summary", "[D3DB] host +",
        "[D3DB] host swap gaps", "[D3DB] host limiter"]
t0 = time.time()
pos = 0
k = 0
with open(outp, "w") as out:
    while True:
        code = wt.DWORD()
        K32.GetExitCodeProcess(wt.HANDLE(hp), ctypes.byref(code))
        if code.value != 259:
            out.write("exit %d (0x%08X) at %.1f min\n" % (code.value, code.value, (time.time() - t0) / 60))
            break
        hc = wt.DWORD()
        K32.GetProcessHandleCount(wt.HANDLE(hp), ctypes.byref(hc))
        pmc = PMC()
        pmc.cb = ctypes.sizeof(pmc)
        PSAPI.GetProcessMemoryInfo(wt.HANDLE(hp), ctypes.byref(pmc), pmc.cb)
        gdi = U32.GetGuiResources(wt.HANDLE(hp), 0)
        usr = U32.GetGuiResources(wt.HANDLE(hp), 1)
        try:
            with open(errp, "rb") as f:
                f.seek(pos)
                data = f.read()
            pos += len(data)
        except OSError:
            data = b""
        lines = data.decode("latin1").splitlines()
        last = {}
        for l in lines:
            for key in KEYS:
                if key in l:
                    last[key] = l.strip()
        lv = sorted(set(re.findall(r"levels\\[a-z]\\(\w+)\\\w+\.nux", data.decode("latin1"))))
        out.write("== %.2f min: handles %d threads %d private %d KiB ws %d KiB gdi %d user %d levels %s\n"
                  % ((time.time() - t0) / 60, hc.value, threads(pid), pmc.PrivateUsage // 1024,
                     pmc.WorkingSetSize // 1024, gdi, usr, ",".join(lv)))
        for key in KEYS:
            if key in last:
                out.write("   " + last[key] + "\n")
        out.flush()
        k += 1
        time.sleep(max(0, t0 + k * period - time.time()))
