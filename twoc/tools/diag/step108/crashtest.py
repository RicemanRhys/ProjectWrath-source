"""crashtest.py [ADDR] -- step 108: makes the running crash_twoc.exe crash on purpose, to see its crash handler work:
a thread started in it at ADDR (default 0x10, nothing mapped there), so the CPU faults on its first instruction
and the exe's vectored handler logs its [CRASH] block before the process ends (0xC0000005)."""
import ctypes, ctypes.wintypes as w, subprocess, sys

addr = int(sys.argv[1], 0) if len(sys.argv) > 1 else 0x10
out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq crash_twoc.exe", "/FO", "CSV", "/NH"], capture_output=True, text=True).stdout
pid = int(out.strip().splitlines()[0].split(",")[1].strip('"'))
k = ctypes.windll.kernel32
k.OpenProcess.restype = w.HANDLE
k.CreateRemoteThread.restype = w.HANDLE
k.CreateRemoteThread.argtypes = [w.HANDLE, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_void_p, w.DWORD, ctypes.c_void_p]
h = k.OpenProcess(0x1F0FFF, False, pid)
t = k.CreateRemoteThread(h, None, 0, ctypes.c_void_p(addr), None, 0, None)
print("pid %d: thread at 0x%X started: %s" % (pid, addr, bool(t)))
