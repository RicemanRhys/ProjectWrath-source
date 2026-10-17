"""hs84.py MAP SECS OUT -- step 84 (notes 18.87): samples crash_twoc.exe's game thread (a ../step84_game_profile.patch
build with a temporary /MAP) ~1 kHz: suspend, GetThreadContext, read twoc_fp_phase, resume. Phases: 0 guest code
(and anything outside the mirror), 1 recording, 7 mirror translation, 8 waiting in its Swap, 9 the rest of its Swap.
Writes OUT (a readable table) and OUT.json (counts by phase and leaf function, and the callers of system/CRT leaves),
which tab84.py turns into ms a frame with the GPROF CSV. MAP and OUT relative to this folder, or absolute."""
import bisect, collections, ctypes, ctypes.wintypes as W, json, os, re, sys, time

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)
HERE = os.path.dirname(os.path.abspath(__file__))
mp, secs, outp = os.path.join(HERE, sys.argv[1]), float(sys.argv[2]), os.path.join(HERE, sys.argv[3])
PH = {0: "guest", 1: "recording", 7: "mirror", 8: "wait", 9: "swap", -1: "?"}


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
FT = W.FILETIME


def cpu(h):
    a, b, c, d = FT(), FT(), FT(), FT()
    k32.GetThreadTimes(h, ctypes.byref(a), ctypes.byref(b), ctypes.byref(c), ctypes.byref(d))
    return (c.dwHighDateTime << 32 | c.dwLowDateTime) + (d.dwHighDateTime << 32 | d.dwLowDateTime)


out = open(outp, "w")
hs = {t: k32.OpenThread(0x0002 | 0x0008 | 0x0040 | 0x0010, False, t) for t in tids}
c0 = {t: cpu(h) for t, h in hs.items()}
time.sleep(2.0)
c1 = {t: cpu(h) for t, h in hs.items()}
busy = sorted(tids, key=lambda t: c1[t] - c0[t], reverse=True)
print("pid", pid, "threads", len(tids), file=out)
for t in busy[:10]:
    print("thread %6d %5.1f%% of a core" % (t, (c1[t] - c0[t]) / 2e5), file=out)

hp = k32.OpenProcess(0x0410, False, pid)
mods = (W.HMODULE * 1024)()
need = W.DWORD()
psapi.EnumProcessModulesEx(hp, mods, ctypes.sizeof(mods), ctypes.byref(need), 3)
modl = []
import pefile
exports = {}                            # module name -> (sorted RVAs, names)
for i in range(need.value // ctypes.sizeof(W.HMODULE)):
    name = ctypes.create_unicode_buffer(260)
    psapi.GetModuleBaseNameW(hp, ctypes.c_void_p(mods[i]), name, 260)
    path = ctypes.create_unicode_buffer(520)
    psapi.GetModuleFileNameExW(hp, ctypes.c_void_p(mods[i]), path, 520)
    mi = MI()
    psapi.GetModuleInformation(hp, ctypes.c_void_p(mods[i]), ctypes.byref(mi), ctypes.sizeof(mi))
    modl.append((mi.base or 0, mi.size, name.value))
    if name.value.lower() in ("ntdll.dll", "kernelbase.dll", "kernel32.dll", "vcruntime140.dll", "ucrtbase.dll"):
        try:
            pe_ = pefile.PE(path.value, fast_load=True)
            pe_.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"]])
            ex = sorted((s.address, (s.name or b"?").decode()) for s in pe_.DIRECTORY_ENTRY_EXPORT.symbols)
            exports[name.value] = ([e[0] for e in ex], [e[1] for e in ex])
        except Exception as e_:
            print("exports of", name.value, "unread:", e_, file=out)
modl.sort()
exe = [m for m in modl if m[2].lower() == "crash_twoc.exe"][0]
syms = []
pref = 0x140000000
phase_addr = None
for l in open(mp, encoding="utf-8", errors="replace"):
    m = re.match(r"\s*([0-9a-fA-F]{4}):[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{16})\s", l)
    if m:
        if m.group(1) == "0001":        # code only: a data symbol never names a return address
            syms.append((int(m.group(3), 16), m.group(2)))
        if m.group(2) in ("twoc_fp_phase", "?twoc_fp_phase@@3HC"):
            phase_addr = int(m.group(3), 16) - pref + exe[0]
syms.sort()
addrs = [s[0] for s in syms]
code_end = syms[-1][0] + 0x10000
print("twoc_fp_phase at", hex(phase_addr) if phase_addr else None, file=out)


def name(a):
    if exe[0] <= a < exe[0] + exe[1]:
        p = a - exe[0] + pref
        if p >= code_end:
            return "?exe-data"
        i = bisect.bisect_right(addrs, p) - 1
        return syms[i][1] if i >= 0 else "?exe"
    for b, s, n in modl:
        if b <= a < b + s:
            if n in exports:
                rv, nm_ = exports[n]
                i = bisect.bisect_right(rv, a - b) - 1
                if i >= 0:
                    return "<%s!%s>" % (n.split(".")[0], nm_[i])
            return "<" + n + ">"
    return "<?>"


# The game thread: the busiest thread whose samples are mostly guest code (sub_*), found in a first second.
CTX = ctypes.c_ubyte * (1232 + 16)
buf = CTX()
ctx_addr = ctypes.addressof(buf) + (-ctypes.addressof(buf)) % 16
ph = ctypes.c_int()
got = ctypes.c_size_t()


def sample(h):
    if k32.SuspendThread(h) == 0xFFFFFFFF:
        return None
    ctypes.c_uint32.from_address(ctx_addr + 0x30).value = 0x100001
    okc = k32.GetThreadContext(h, ctypes.c_void_p(ctx_addr))
    p = -1
    if phase_addr and k32.ReadProcessMemory(hp, ctypes.c_void_p(phase_addr), ctypes.byref(ph), 4, ctypes.byref(got)):
        p = ph.value
    rip = ctypes.c_uint64.from_address(ctx_addr + 0xF8).value
    rsp = ctypes.c_uint64.from_address(ctx_addr + 0x98).value
    stk = (ctypes.c_uint64 * 64)()
    nst = 0
    if k32.ReadProcessMemory(hp, ctypes.c_void_p(rsp), ctypes.byref(stk), 8 * 64, ctypes.byref(got)):
        nst = got.value // 8
    k32.ResumeThread(h)
    if not okc:
        return None
    return p, rip, [stk[i] for i in range(nst)]


guest_share = collections.Counter()
t1 = time.time() + 1.0
while time.time() < t1:
    for t in busy[:4]:
        r = sample(hs[t])
        if r and name(r[1]).startswith("sub_"):
            guest_share[t] += 1
    time.sleep(0.002)
game = guest_share.most_common(1)[0][0] if guest_share else busy[0]
print("game thread", game, file=out)

per = collections.defaultdict(collections.Counter)      # phase -> leaf -> n
callers = collections.defaultdict(collections.Counter)  # (phase, leaf) -> first exe frame on the stack
n = 0
t_end = time.time() + secs
while time.time() < t_end:
    r = sample(hs[game])
    if r:
        p, rip, stk = r
        nm = name(rip)
        per[p][nm] += 1
        n += 1
        if nm.startswith("<") or nm.lstrip("_").startswith(("memcpy", "memset", "memcmp", "memmove")):
            for v in stk:
                cn = name(v)
                if not cn.startswith(("<", "?")) and not cn.lstrip("_").startswith(("memcpy", "memset", "memcmp", "memmove")) \
                        and exe[0] <= v < exe[0] + exe[1]:
                    callers[(p, nm)][cn] += 1
                    break
    time.sleep(0.001)
print("samples", n, file=out)
for p in sorted(per):
    tot = sum(per[p].values())
    print("\n== phase %d %s: %d samples (%.1f%%)" % (p, PH.get(p, "?"), tot, 100.0 * tot / max(1, n)), file=out)
    for k, v in per[p].most_common(40):
        print("%6.2f%%  %s" % (100.0 * v / tot, k), file=out)
print("\n-- callers of system and CRT leaves (the first exe frame on the stack)", file=out)
for (p, nm), c in sorted(callers.items(), key=lambda x: -sum(x[1].values())):
    if sum(c.values()) < 10:
        continue
    print("%s in %s: %d: %s" % (nm, PH.get(p, "?"), sum(c.values()), ", ".join("%s %d" % kv for kv in c.most_common(8))),
          file=out)
out.close()
json.dump({"samples": n, "per": {str(p): dict(c) for p, c in per.items()},
           "callers": [[p, nm, dict(c)] for (p, nm), c in callers.items()]}, open(outp + ".json", "w"))
print("written", outp)
