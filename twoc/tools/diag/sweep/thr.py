"""thr.py MAP SECS OUT -- step 84 (e): every thread of crash_twoc.exe with its CPU over SECS seconds (% of a core)
and its start routine (Win32 start address, named through MAP for the exe's own), busiest first, to OUT."""
import bisect, ctypes, ctypes.wintypes as W, os, re, sys, time

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
ntdll = ctypes.WinDLL("ntdll")
psapi = ctypes.WinDLL("psapi")
HERE = os.path.dirname(os.path.abspath(__file__))
mp, secs, outp = os.path.join(HERE, sys.argv[1]), float(sys.argv[2]), os.path.join(HERE, sys.argv[3])


class PE32(ctypes.Structure):
    _fields_ = [("dwSize", W.DWORD), ("cntUsage", W.DWORD), ("th32ProcessID", W.DWORD), ("th32DefaultHeapID", ctypes.c_void_p),
                ("th32ModuleID", W.DWORD), ("cntThreads", W.DWORD), ("th32ParentProcessID", W.DWORD), ("pcPriClassBase", ctypes.c_long),
                ("dwFlags", W.DWORD), ("szExeFile", ctypes.c_char * 260)]


class TE32(ctypes.Structure):
    _fields_ = [("dwSize", W.DWORD), ("cntUsage", W.DWORD), ("th32ThreadID", W.DWORD), ("th32OwnerProcessID", W.DWORD),
                ("tpBasePri", ctypes.c_long), ("tpDeltaPri", ctypes.c_long), ("dwFlags", W.DWORD)]


class MI(ctypes.Structure):
    _fields_ = [("base", ctypes.c_void_p), ("size", W.DWORD), ("entry", ctypes.c_void_p)]


k32.CreateToolhelp32Snapshot.restype = W.HANDLE
k32.OpenThread.restype = W.HANDLE
k32.OpenProcess.restype = W.HANDLE
snap = k32.CreateToolhelp32Snapshot(6, 0)
pe = PE32(); pe.dwSize = ctypes.sizeof(PE32)
pid = None
ok = k32.Process32First(snap, ctypes.byref(pe))
while ok:
    if pe.szExeFile.lower() == b"crash_twoc.exe":
        pid = pe.th32ProcessID
    ok = k32.Process32Next(snap, ctypes.byref(pe))
te = TE32(); te.dwSize = ctypes.sizeof(TE32)
tids = []
ok = k32.Thread32First(snap, ctypes.byref(te))
while ok:
    if te.th32OwnerProcessID == pid:
        tids.append(te.th32ThreadID)
    ok = k32.Thread32Next(snap, ctypes.byref(te))
hp = k32.OpenProcess(0x0410, False, pid)
mods = (W.HMODULE * 1024)()
need = W.DWORD()
psapi.EnumProcessModulesEx(hp, mods, ctypes.sizeof(mods), ctypes.byref(need), 3)
modl = []
for i in range(need.value // ctypes.sizeof(W.HMODULE)):
    nm = ctypes.create_unicode_buffer(260)
    psapi.GetModuleBaseNameW(hp, ctypes.c_void_p(mods[i]), nm, 260)
    mi = MI()
    psapi.GetModuleInformation(hp, ctypes.c_void_p(mods[i]), ctypes.byref(mi), ctypes.sizeof(mi))
    modl.append((mi.base or 0, mi.size, nm.value))
exe = [m for m in modl if m[2].lower() == "crash_twoc.exe"][0]
syms = []
for l in open(mp, encoding="utf-8", errors="replace"):
    m = re.match(r"\s*0001:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{16})\s", l)
    if m:
        syms.append((int(m.group(2), 16), m.group(1)))
syms.sort()
addrs = [s[0] for s in syms]


def name(a):
    if exe[0] <= a < exe[0] + exe[1]:
        i = bisect.bisect_right(addrs, a - exe[0] + 0x140000000) - 1
        return syms[i][1] if i >= 0 else "?exe"
    for b, s, n in modl:
        if b <= a < b + s:
            return "<%s+%X>" % (n, a - b)
    return "<?>"


FT = W.FILETIME


def cpu(h):
    a, b, c, d = FT(), FT(), FT(), FT()
    k32.GetThreadTimes(h, ctypes.byref(a), ctypes.byref(b), ctypes.byref(c), ctypes.byref(d))
    return (c.dwHighDateTime << 32 | c.dwLowDateTime) + (d.dwHighDateTime << 32 | d.dwLowDateTime)


hs = {t: k32.OpenThread(0x0040 | 0x0800, False, t) for t in tids}   # QUERY_INFORMATION, QUERY_LIMITED
start = {}
for t, h in hs.items():
    sa = ctypes.c_void_p()
    ntdll.NtQueryInformationThread(h, 9, ctypes.byref(sa), ctypes.sizeof(sa), None)
    start[t] = name(sa.value or 0)
c0 = {t: cpu(h) for t, h in hs.items()}
t0 = time.time()
time.sleep(secs)
c1 = {t: cpu(h) for t, h in hs.items()}
dt = time.time() - t0
with open(outp, "w") as f:
    tot = 0
    for t in sorted(tids, key=lambda t: c1[t] - c0[t], reverse=True):
        p = (c1[t] - c0[t]) / (dt * 1e5)
        tot += p
        if p >= 0.5:
            print("thread %6d %6.1f%% of a core  %s" % (t, p, start[t]), file=f)
    print("all threads %.1f%% of a core over %.1f s" % (tot, dt), file=f)
print("written", outp)
