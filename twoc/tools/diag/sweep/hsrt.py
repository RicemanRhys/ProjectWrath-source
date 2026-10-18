"""hsrt.py MAP SECS OUT -- step 86: every thread's CPU over 3 s (named by its description or start routine), then
"twoc render thread" sampled ~1 kHz for SECS: the leaf by module (the exe's by function through MAP), and for each
leaf outside the exe the first exe function on the stack (which of our calls led into the runtime or the driver).
Writes OUT (a readable table) and OUT.json. MAP and OUT relative to this folder, or absolute."""
import bisect, collections, ctypes, ctypes.wintypes as W, json, os, re, sys, time

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)
ntdll = ctypes.WinDLL("ntdll")
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
modl.sort()
exe = [m for m in modl if m[2].lower() == "crash_twoc.exe"][0]
syms = []
pref = 0x140000000
for l in open(mp, encoding="utf-8", errors="replace"):
    m = re.match(r"\s*([0-9a-fA-F]{4}):[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{16})\s", l)
    if m and m.group(1) == "0001":
        syms.append((int(m.group(3), 16), m.group(2)))
syms.sort()
addrs = [s[0] for s in syms]
code_end = syms[-1][0] + 0x10000


def name(a):
    if exe[0] <= a < exe[0] + exe[1]:
        p = a - exe[0] + pref
        if p >= code_end:
            return "?exe-data"
        i = bisect.bisect_right(addrs, p) - 1
        return syms[i][1] if i >= 0 else "?exe"
    for b, s, n in modl:
        if b <= a < b + s:
            return "<" + n + ">"
    return "<?>"


FT = W.FILETIME


def cpu(h):
    a, b, c, d = FT(), FT(), FT(), FT()
    k32.GetThreadTimes(h, ctypes.byref(a), ctypes.byref(b), ctypes.byref(c), ctypes.byref(d))
    return (c.dwHighDateTime << 32 | c.dwLowDateTime) + (d.dwHighDateTime << 32 | d.dwLowDateTime)


k32.GetThreadDescription.argtypes = [W.HANDLE, ctypes.POINTER(ctypes.c_wchar_p)]
hs = {t: k32.OpenThread(0x0002 | 0x0008 | 0x0040 | 0x0010 | 0x0800, False, t) for t in tids}
label = {}
for t, h in hs.items():
    d = ctypes.c_wchar_p()
    desc = ""
    if k32.GetThreadDescription(h, ctypes.byref(d)) >= 0 and d.value:
        desc = d.value
    sa = ctypes.c_void_p()
    ntdll.NtQueryInformationThread(h, 9, ctypes.byref(sa), ctypes.sizeof(sa), None)
    label[t] = (desc + " " if desc else "") + name(sa.value or 0)
c0 = {t: cpu(h) for t, h in hs.items()}
t0 = time.time()
time.sleep(3.0)
c1 = {t: cpu(h) for t, h in hs.items()}
dt = time.time() - t0
out = open(outp, "w")
print("pid", pid, "threads", len(tids), file=out)
tot = 0.0
thr = []
for t in sorted(tids, key=lambda t: c1[t] - c0[t], reverse=True):
    p = (c1[t] - c0[t]) / (dt * 1e5)
    tot += p
    thr.append([t, p, label[t]])
    if p >= 0.5:
        print("thread %6d %6.1f%% of a core  %s" % (t, p, label[t]), file=out)
print("all threads %.1f%% of a core over %.1f s" % (tot, dt), file=out)
rt = [t for t in tids if "twoc render thread" in label[t] and "watchdog" not in label[t]]
if not rt:
    print("no render thread", file=out)
    out.close()
    sys.exit(0)
h = hs[rt[0]]

CTX = ctypes.c_ubyte * (1232 + 16)
buf = CTX()
ctx_addr = ctypes.addressof(buf) + (-ctypes.addressof(buf)) % 16
got = ctypes.c_size_t()


def sample(h):
    if k32.SuspendThread(h) == 0xFFFFFFFF:
        return None
    ctypes.c_uint32.from_address(ctx_addr + 0x30).value = 0x100001
    okc = k32.GetThreadContext(h, ctypes.c_void_p(ctx_addr))
    rip = ctypes.c_uint64.from_address(ctx_addr + 0xF8).value
    rsp = ctypes.c_uint64.from_address(ctx_addr + 0x98).value
    stk = (ctypes.c_uint64 * 512)()
    nst = 0
    if k32.ReadProcessMemory(hp, ctypes.c_void_p(rsp), ctypes.byref(stk), 8 * 512, ctypes.byref(got)):
        nst = got.value // 8
    k32.ResumeThread(h)
    if not okc:
        return None
    return rip, [stk[i] for i in range(nst)]


leaf = collections.Counter()
mod = collections.Counter()
callers = collections.defaultdict(collections.Counter)
n = 0
t_end = time.time() + secs
while time.time() < t_end:
    r = sample(h)
    if r:
        rip, stk = r
        nm = name(rip)
        leaf[nm] += 1
        mod[nm if nm.startswith("<") else "crash_twoc.exe"] += 1
        n += 1
        if nm.startswith("<"):
            for v in stk:
                if exe[0] <= v < exe[0] + exe[1]:
                    cn = name(v)
                    if not cn.startswith("?"):
                        callers[nm][cn] += 1
                        break
    time.sleep(0.001)
print("\nrender thread %d: %d samples" % (rt[0], n), file=out)
print("\n-- by module", file=out)
for k, v in mod.most_common(20):
    print("%6.2f%%  %s" % (100.0 * v / max(1, n), k), file=out)
print("\n-- leaves", file=out)
for k, v in leaf.most_common(50):
    print("%6.2f%%  %s" % (100.0 * v / max(1, n), k), file=out)
print("\n-- leaves outside the exe: the first exe function on the stack", file=out)
for k, c in sorted(callers.items(), key=lambda x: -sum(x[1].values())):
    if sum(c.values()) < 10:
        continue
    print("%s %d: %s" % (k, sum(c.values()), ", ".join("%s %d" % kv for kv in c.most_common(10))), file=out)
out.close()
json.dump({"samples": n, "threads": thr, "leaf": dict(leaf), "mod": dict(mod),
           "callers": {k: dict(c) for k, c in callers.items()}}, open(outp + ".json", "w"))
print("written", outp)
