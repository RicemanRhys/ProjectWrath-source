"""Snapshot guest memory of a running crash_twoc.exe from outside the process.

Guest VA v lives at host v + 0x10000 (main.c: "Offset: 0x10000"), the same
mapping fpsrun.py uses. Reads the XBE image/BSS, the guest heap and the
contiguous arena (0x80000000..0x84000000, pushbuffers and surfaces).

  import memsnap; h = memsnap.attach(); s = memsnap.snap(h)   # dict name -> (base_va, bytes)
"""
import ctypes, ctypes.wintypes as wt, struct, subprocess
import numpy as np

OFFSET = 0x10000
RANGES = [("image", 0x00010000, 0x00946000), ("heap", 0x00F80000, 0x04000000), ("contig", 0x80000000, 0x84000000)]
K32 = ctypes.WinDLL("kernel32", use_last_error=True)
K32.OpenProcess.restype = wt.HANDLE
K32.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                  ctypes.POINTER(ctypes.c_size_t)]

def find_pid(name="crash_twoc.exe"):
    out = subprocess.run(["tasklist", "/FI", f"IMAGENAME eq {name}", "/FO", "CSV", "/NH"],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = [p.strip('"') for p in line.split('","')]
        if parts and parts[0].lower() == name:
            return int(parts[1])
    return None

def attach(pid=None):
    pid = pid or find_pid()
    if not pid:
        raise RuntimeError("crash_twoc.exe not running")
    return K32.OpenProcess(0x0410, False, pid)   # QUERY_INFORMATION | VM_READ

def read(h, va, n):
    buf = ctypes.create_string_buffer(n)
    got = ctypes.c_size_t()
    ok = K32.ReadProcessMemory(h, ctypes.c_void_p(va + OFFSET), buf, n, ctypes.byref(got))
    return buf.raw[:got.value] if ok else None

def u32(h, va):
    b = read(h, va, 4)
    return struct.unpack("<I", b)[0] if b else None

def snap(h):
    out = {}
    for name, lo, hi in RANGES:
        chunks, va = [], lo
        while va < hi:
            n = min(0x100000, hi - va)
            b = read(h, va, n)
            if b is None or len(b) < n:
                break
            chunks.append(b)
            va += n
        out[name] = (lo, np.frombuffer(b"".join(chunks), dtype=np.uint32).copy())
    return out
