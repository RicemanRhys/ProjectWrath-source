"""hs2.py MAP SECS OUT -- step 75 (notes 18.78): ../hostsamp.py for a build with ../step75_frame_profile.patch and a
temporary /MAP (MAP and OUT relative to this folder, or absolute). Samples the 4 busiest threads of crash_twoc.exe
(~1 kHz each: suspend, GetThreadContext, read twoc_fp_phase, resume). For each: modules and leaf functions; for the
game thread (the one in guest code sub_* most) also what it was doing (twoc_fp_phase) and, per phase, by module
and function group. Phases: 0 game, 1 recording, 2 ia/ic_begin, 3 replay pass, 4 present copy/after, 5 Present call,
6 pace wait, 7 mirror (d3d_bridge translation)."""
import bisect, collections, ctypes, ctypes.wintypes as W, re, sys, time

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)
import os
HERE = os.path.dirname(os.path.abspath(__file__))
mp, secs, outp = os.path.join(HERE, sys.argv[1]), float(sys.argv[2]), os.path.join(HERE, sys.argv[3])
PH = ["game", "recording", "ia/begin", "replay", "present copy/after", "Present call", "pace wait", "mirror"]


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
samp = busy[:4]

hp = k32.OpenProcess(0x0410, False, pid)
mods = (W.HMODULE * 1024)()
need = W.DWORD()
psapi.EnumProcessModulesEx(hp, mods, ctypes.sizeof(mods), ctypes.byref(need), 3)
modl = []


class MI(ctypes.Structure):
    _fields_ = [("base", ctypes.c_void_p), ("size", W.DWORD), ("entry", ctypes.c_void_p)]


for i in range(need.value // ctypes.sizeof(W.HMODULE)):
    name = ctypes.create_unicode_buffer(260)
    psapi.GetModuleBaseNameW(hp, ctypes.c_void_p(mods[i]), name, 260)
    mi = MI()
    psapi.GetModuleInformation(hp, ctypes.c_void_p(mods[i]), ctypes.byref(mi), ctypes.sizeof(mi))
    modl.append((mi.base or 0, mi.size, name.value))
modl.sort()
exe = [m for m in modl if m[2].lower() == "crash_twoc.exe"][0]
syms = []
pref = 0x140000000
phase_addr = None
for l in open(mp, encoding="utf-8", errors="replace"):
    m = re.match(r"\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{16})\s", l)
    if m:
        syms.append((int(m.group(2), 16), m.group(1)))
        if m.group(1) in ("twoc_fp_phase", "?twoc_fp_phase@@3HC"):
            phase_addr = int(m.group(2), 16) - pref + exe[0]
syms.sort()
addrs = [s[0] for s in syms]
print("twoc_fp_phase at", hex(phase_addr) if phase_addr else None, file=out)


def name(a):
    if exe[0] <= a < exe[0] + exe[1]:
        p = a - exe[0] + pref
        i = bisect.bisect_right(addrs, p) - 1
        return syms[i][1] if i >= 0 else "?exe"
    for b, s, n in modl:
        if b <= a < b + s:
            return "<" + n + ">"
    return "<?>"


def group(nm):
    if nm.startswith("<"):
        return nm
    n = nm.lstrip("_")
    if n.startswith("sub_"):
        return "guest code (sub_*)"
    if n.startswith(("d3d8_", "dev_", "up_ring", "ib_ring", "ff_", "cache_lookup", "get_cached_layout", "update_",
                     "hash_", "tex_", "surf_", "vb_", "ib_", "compile_shader", "nv2a_", "map_primitive", "convert_fan")) \
            and not n.startswith(("tex_get", "tex_upload", "tex_release")):
        return "xbox_d3d8"
    if n.startswith(("ic_", "aff_", "q_", "m4_", "m3_", "bone_", "uf_", "ig_", "cmp_")):
        return "interpolation (ic_*, maths)"
    if n.startswith(("draw_exec", "replay", "rec_", "ra_", "rc_", "clear_exec", "copy_exec", "bind_target", "vs_bind",
                     "vs_slot", "apply_ff", "tex_get", "tex_upload", "rescale", "target_", "map_", "rt_", "ms_",
                     "present", "pace", "d3dh_", "blit", "frame_tex", "flip", "ia_", "fp_")):
        return "d3d_host"
    if n.startswith(("mirror_", "host_draw", "d3db_", "translate", "xl_", "vp_", "fetch", "expand", "cpu_light",
                     "texel", "describe", "tex_sig", "sig", "fnv")):
        return "d3d_bridge"
    if n.startswith(("memcpy", "memset", "memcmp", "memmove")):
        return "CRT mem*"
    return "other exe: " + nm


CTX = ctypes.c_ubyte * (1232 + 16)
buf = CTX()
ctx_addr = ctypes.addressof(buf) + (-ctypes.addressof(buf)) % 16
ph = ctypes.c_int()
got = ctypes.c_size_t()
per = {t: collections.Counter() for t in samp}
perph = collections.defaultdict(collections.Counter)
perphf = collections.defaultdict(collections.Counter)
phases = collections.Counter()
callers = collections.defaultdict(collections.Counter)
n = collections.Counter()
t_end = time.time() + secs
while time.time() < t_end:
    for t in samp:
        h = hs[t]
        if k32.SuspendThread(h) == 0xFFFFFFFF:
            continue
        ctypes.c_uint32.from_address(ctx_addr + 0x30).value = 0x100001
        okc = k32.GetThreadContext(h, ctypes.c_void_p(ctx_addr))
        p = -1
        if phase_addr and k32.ReadProcessMemory(hp, ctypes.c_void_p(phase_addr), ctypes.byref(ph), 4, ctypes.byref(got)):
            p = ph.value
        k32.ResumeThread(h)
        if not okc:
            continue
        rip = ctypes.c_uint64.from_address(ctx_addr + 0xF8).value
        nm = name(rip)
        if nm in ("<VCRUNTIME140.dll>", "<ucrtbase.dll>", "<d3d11.dll>", "<nvwgf2umx.dll>", "<ntdll.dll>"):
            rsp = ctypes.c_uint64.from_address(ctx_addr + 0x98).value
            ra = ctypes.c_uint64()
            if k32.ReadProcessMemory(hp, ctypes.c_void_p(rsp), ctypes.byref(ra), 8, ctypes.byref(got)):
                cn = name(ra.value)
                if not cn.startswith("<"):
                    callers[(t, p, nm)][cn] += 1
        per[t][nm] += 1
        n[t] += 1
        per[t]["@phase%d" % p] += 1
        perph[(t, p)][group(nm)] += 1
        perphf[(t, p)][nm] += 1
    time.sleep(0.001)
game = max(samp, key=lambda t: sum(v for k, v in per[t].items() if k.startswith("sub_")) / max(1, n[t]))
for t in samp:
    tot = n[t]
    if not tot:
        continue
    g = collections.Counter()
    for k, v in per[t].items():
        if not k.startswith("@"):
            g[group(k)] += v
    print("\n== thread %d%s: %d samples" % (t, " (the game thread)" if t == game else "", tot), file=out)
    for k, v in g.most_common(14):
        print("%6.2f%%  %s" % (100.0 * v / tot, k), file=out)
    print("-- leaf functions", file=out)
    for k, v in [x for x in per[t].most_common(60) if not x[0].startswith("@")][:25]:
        print("%6.2f%%  %s" % (100.0 * v / tot, k), file=out)
    if t == game:
        print("-- by phase (twoc_fp_phase)", file=out)
        for p in range(-1, 8):
            v = per[t].get("@phase%d" % p, 0)
            if not v:
                continue
            print("%6.2f%%  %s" % (100.0 * v / tot, PH[p] if p >= 0 else "?"), file=out)
            for k2, v2 in perph[(t, p)].most_common(8):
                print("          %6.2f%% of it  %s" % (100.0 * v2 / v, k2), file=out)
            for k2, v2 in perphf[(t, p)].most_common(10):
                print("               %6.2f%%  %s" % (100.0 * v2 / v, k2), file=out)
print('-- callers of system leaves ([rsp] is an exe address), game thread', file=out)
for (t, p, nm), c in sorted(callers.items(), key=lambda x: -sum(x[1].values())):
    if t != game or sum(c.values()) < 20: continue
    print('%s in %s: %d with an exe [rsp]: %s' % (nm, PH[p] if p >= 0 else '?', sum(c.values()), ', '.join('%s %d' % kv for kv in c.most_common(6))), file=out)
out.close()
print("written", outp)
