"""hostsamp.py MAP SECS [OUT] -- step 74: leaf-RIP sampler for crash_twoc.exe's busiest thread (from outside: suspend,
GetThreadContext, resume, ~1 kHz). Names exe addresses with the /MAP, other addresses by module."""
import bisect, collections, ctypes, ctypes.wintypes as W, re, sys, time

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)
mp, secs = sys.argv[1], float(sys.argv[2])

TH32CS_SNAPPROCESS, TH32CS_SNAPTHREAD = 2, 4


class PE32(ctypes.Structure):
    _fields_ = [("dwSize", W.DWORD), ("cntUsage", W.DWORD), ("th32ProcessID", W.DWORD), ("th32DefaultHeapID", ctypes.c_void_p),
                ("th32ModuleID", W.DWORD), ("cntThreads", W.DWORD), ("th32ParentProcessID", W.DWORD), ("pcPriClassBase", ctypes.c_long),
                ("dwFlags", W.DWORD), ("szExeFile", ctypes.c_char * 260)]


class TE32(ctypes.Structure):
    _fields_ = [("dwSize", W.DWORD), ("cntUsage", W.DWORD), ("th32ThreadID", W.DWORD), ("th32OwnerProcessID", W.DWORD),
                ("tpBasePri", ctypes.c_long), ("tpDeltaPri", ctypes.c_long), ("dwFlags", W.DWORD)]


k32.CreateToolhelp32Snapshot.restype = W.HANDLE
k32.OpenThread.restype = W.HANDLE
k32.OpenProcess.restype = W.HANDLE
snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS | TH32CS_SNAPTHREAD, 0)
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
print("pid", pid, "threads", len(tids))

# the busiest thread over 1 s
FT = W.FILETIME


def cpu(h):
    a, b, c, d = FT(), FT(), FT(), FT()
    k32.GetThreadTimes(h, ctypes.byref(a), ctypes.byref(b), ctypes.byref(c), ctypes.byref(d))
    return (c.dwHighDateTime << 32 | c.dwLowDateTime) + (d.dwHighDateTime << 32 | d.dwLowDateTime)


hs = {t: k32.OpenThread(0x0002 | 0x0008 | 0x0040 | 0x0010, False, t) for t in tids}   # suspend, get ctx, query
c0 = {t: cpu(h) for t, h in hs.items()}
time.sleep(1.0)
c1 = {t: cpu(h) for t, h in hs.items()}
busy = sorted(tids, key=lambda t: c1[t] - c0[t], reverse=True)
for t in busy[:4]:
    print("thread", t, "%.0f%%" % ((c1[t] - c0[t]) / 1e5))
tid = busy[0]
h = hs[tid]

# modules
hp = k32.OpenProcess(0x0410, False, pid)
mods = (W.HMODULE * 1024)()
need = W.DWORD()
psapi.EnumProcessModulesEx(hp, mods, ctypes.sizeof(mods), ctypes.byref(need), 3)
modl = []
for i in range(need.value // ctypes.sizeof(W.HMODULE)):
    name = ctypes.create_unicode_buffer(260)
    psapi.GetModuleBaseNameW(hp, ctypes.c_void_p(mods[i]), name, 260)

    class MI(ctypes.Structure):
        _fields_ = [("base", ctypes.c_void_p), ("size", W.DWORD), ("entry", ctypes.c_void_p)]
    mi = MI()
    psapi.GetModuleInformation(hp, ctypes.c_void_p(mods[i]), ctypes.byref(mi), ctypes.sizeof(mi))
    modl.append((mi.base or 0, mi.size, name.value))
modl.sort()
exe = [m for m in modl if m[2].lower() == "crash_twoc.exe"][0]

syms = []
pref = 0x140000000
for l in open(mp, encoding="utf-8", errors="replace"):
    m = re.match(r"\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{16})\s", l)
    if m:
        syms.append((int(m.group(2), 16), m.group(1)))
syms.sort()
addrs = [s[0] for s in syms]


def name(a):
    if exe[0] <= a < exe[0] + exe[1]:
        p = a - exe[0] + pref
        i = bisect.bisect_right(addrs, p) - 1
        return syms[i][1] if i >= 0 else "?exe"
    for b, s, n in modl:
        if b <= a < b + s:
            return "<" + n + ">"
    return "<?>"


CTX = ctypes.c_ubyte * (1232 + 16)
buf = CTX()
off = (-ctypes.addressof(buf)) % 16
ctx_addr = ctypes.addressof(buf) + off
ctypes.c_uint32.from_address(ctx_addr + 0x30).value = 0x100001   # CONTEXT_CONTROL
hist = collections.Counter()
mods_h = collections.Counter()
n = 0
t_end = time.time() + secs
while time.time() < t_end:
    if k32.SuspendThread(h) != 0xFFFFFFFF:
        ctypes.c_uint32.from_address(ctx_addr + 0x30).value = 0x100001
        if k32.GetThreadContext(h, ctypes.c_void_p(ctx_addr)):
            rip = ctypes.c_uint64.from_address(ctx_addr + 0xF8).value
            nm = name(rip)
            hist[nm] += 1
            mods_h[nm if nm.startswith("<") else "<crash_twoc.exe>"] += 1
            n += 1
        k32.ResumeThread(h)
    time.sleep(0.001)
out = open(sys.argv[3], "w") if len(sys.argv) > 3 else sys.stdout
print("samples", n, file=out)
for k, v in mods_h.most_common(12):
    print("%6.2f%%  %s" % (100.0 * v / n, k), file=out)
print("--", file=out)
for k, v in hist.most_common(60):
    print("%6.2f%%  %s" % (100.0 * v / n, k), file=out)
