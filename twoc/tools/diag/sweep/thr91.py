"""thr91.py -- CPU time of crash_twoc.exe's threads by description (the power log's: "twoc power log"), and the
process's total, with its run time."""
import ctypes, sys
from ctypes import wintypes

k = ctypes.windll.kernel32
TH32CS_SNAPTHREAD, TH32CS_SNAPPROCESS = 4, 2


class THREADENTRY32(ctypes.Structure):
    _fields_ = [("dwSize", wintypes.DWORD), ("cntUsage", wintypes.DWORD), ("th32ThreadID", wintypes.DWORD),
                ("th32OwnerProcessID", wintypes.DWORD), ("tpBasePri", wintypes.LONG), ("tpDeltaPri", wintypes.LONG),
                ("dwFlags", wintypes.DWORD)]


class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [("dwSize", wintypes.DWORD), ("cntUsage", wintypes.DWORD), ("th32ProcessID", wintypes.DWORD),
                ("th32DefaultHeapID", ctypes.c_size_t), ("th32ModuleID", wintypes.DWORD), ("cntThreads", wintypes.DWORD),
                ("th32ParentProcessID", wintypes.DWORD), ("pcPriClassBase", wintypes.LONG), ("dwFlags", wintypes.DWORD),
                ("szExeFile", wintypes.WCHAR * 260)]


def ft(f):
    return (f.dwHighDateTime << 32 | f.dwLowDateTime) / 1e7


snap = k.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
pe = PROCESSENTRY32W(); pe.dwSize = ctypes.sizeof(pe)
pid = None
ok = k.Process32FirstW(snap, ctypes.byref(pe))
while ok:
    if pe.szExeFile.lower() == "crash_twoc.exe":
        pid = pe.th32ProcessID
    ok = k.Process32NextW(snap, ctypes.byref(pe))
k.CloseHandle(snap)
if not pid:
    sys.exit("no crash_twoc.exe")
hp = k.OpenProcess(0x1000, False, pid)
c, e, kt, ut = wintypes.FILETIME(), wintypes.FILETIME(), wintypes.FILETIME(), wintypes.FILETIME()
k.GetProcessTimes(hp, ctypes.byref(c), ctypes.byref(e), ctypes.byref(kt), ctypes.byref(ut))
now = wintypes.FILETIME(); k.GetSystemTimeAsFileTime(ctypes.byref(now))
age = ft(now) - ft(c)
print("process: %.1f s old, CPU %.2f s (%.1f%% of one core)" % (age, ft(kt) + ft(ut), 100 * (ft(kt) + ft(ut)) / age))
snap = k.CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)
te = THREADENTRY32(); te.dwSize = ctypes.sizeof(te)
ok = k.Thread32First(snap, ctypes.byref(te))
k.GetThreadDescription.argtypes = [wintypes.HANDLE, ctypes.POINTER(ctypes.c_wchar_p)]
while ok:
    if te.th32OwnerProcessID == pid:
        ht = k.OpenThread(0x0800 | 0x0040, False, te.th32ThreadID)   # QUERY_LIMITED_INFORMATION | QUERY_INFORMATION
        d = ctypes.c_wchar_p()
        name = ""
        if ht and k.GetThreadDescription(ht, ctypes.byref(d)) >= 0 and d.value:
            name = d.value
        if name == "twoc power log":
            k.GetThreadTimes(ht, ctypes.byref(c), ctypes.byref(e), ctypes.byref(kt), ctypes.byref(ut))
            tage = ft(now) - ft(c)
            cpu = ft(kt) + ft(ut)
            cyc = ctypes.c_ulonglong(); k.QueryThreadCycleTime(ht, ctypes.byref(cyc)); print("  cycles %d" % cyc.value)
            print("thread %d '%s': %.1f s old, CPU %.3f s (kernel %.3f, user %.3f) = %.3f%% of one core, %.2f ms a second"
                  % (te.th32ThreadID, name, tage, cpu, ft(kt), ft(ut), 100 * cpu / tage, 1000 * cpu / tage))
        if ht:
            k.CloseHandle(ht)
    ok = k.Thread32Next(snap, ctypes.byref(te))
