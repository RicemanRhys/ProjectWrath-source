"""tele.py ADDR DELTA [ADDR DELTA ...] -- add DELTA to the guest float at ADDR (hex); step 100."""
import ctypes, os, struct, sys
sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")
import memsnap
pid = memsnap.find_pid(os.environ.get('TWOC_EXE', 'crash_twoc.exe'))
h = memsnap.K32.OpenProcess(0x0438, False, pid)
a = sys.argv[1:]
for i in range(0, len(a), 2):
    d = float(a[i + 1])
    if a[i].startswith('p'):          # pOFF: Crash's object ([0x17B0DC]) + OFF
        va = memsnap.u32(h, 0x17B0DC) + int(a[i][1:], 16)
    else:
        va = int(a[i], 16)
    v = struct.unpack("<f", memsnap.read(h, va, 4))[0]
    got = ctypes.c_size_t()
    memsnap.K32.WriteProcessMemory(h, ctypes.c_void_p(va + memsnap.OFFSET), struct.pack("<f", v + d), 4, ctypes.byref(got))
    print("%08X %.3f -> %.3f" % (va, v, v + d), flush=True)
