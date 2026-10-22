"""setpos.py X Y Z [REPEAT_S] -- step 100: put Crash at X Y Z (his object [0x17B0DC] + 0x6C), written again every
5 ms for REPEAT_S seconds (default 1) so the game's own step doesn't undo it while the camera catches up."""
import ctypes, os, struct, sys, time
sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")
import memsnap
x, y, z = map(float, sys.argv[1:4])
rep = float(sys.argv[4]) if len(sys.argv) > 4 else 1.0
h = memsnap.K32.OpenProcess(0x0438, False, memsnap.find_pid(os.environ.get('TWOC_EXE', 'crash_twoc.exe')))
va = memsnap.u32(h, 0x17B0DC) + 0x6C
got = ctypes.c_size_t()
t0 = time.time()
while time.time() - t0 < rep:
    memsnap.K32.WriteProcessMemory(h, ctypes.c_void_p(va + memsnap.OFFSET), struct.pack("<3f", x, y, z), 12, ctypes.byref(got))
    time.sleep(0.005)
print("setpos %08X %.2f %.2f %.2f" % (va, x, y, z), flush=True)
