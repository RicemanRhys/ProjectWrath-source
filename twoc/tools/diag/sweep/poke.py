"""poke.py ADDR SIZE VALUE [ADDR SIZE VALUE ...] -- write guest memory (hex addr, size 1/2/4, int value)."""
import ctypes, ctypes.wintypes as wt, os, struct, sys
sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")
import memsnap
K32 = memsnap.K32
K32.WriteProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
pid = memsnap.find_pid(os.environ.get('TWOC_EXE', 'crash_twoc.exe'))
h = K32.OpenProcess(0x0438, False, pid)   # VM_OPERATION|VM_READ|VM_WRITE|QUERY
a = sys.argv[1:]
for i in range(0, len(a), 3):
    va, n, v = int(a[i], 16), int(a[i + 1]), int(a[i + 2], 0)
    b = struct.pack({1: "<B", 2: "<H", 4: "<I"}[n], v & ((1 << (8 * n)) - 1))
    got = ctypes.c_size_t()
    ok = K32.WriteProcessMemory(h, ctypes.c_void_p(va + memsnap.OFFSET), b, n, ctypes.byref(got))
    print("%08X <- %s ok=%d" % (va, b.hex(), ok))
