"""ticksmon.py EXE_NAME OUT SECONDS: the game's logic ticks per rendered frame, from outside.

Polls the game's frame counter 0x25A660 and its tick count for the frame
[0x1A0A98] (sub_00037540's result: round(QPC delta x 60) less the ticks already
counted, clamped to 1..6; notes 14.8) about every 0.5 ms, and writes one line
per frame-counter change to OUT.ticks.txt: t_ms frame ticks. Waits for the
process to appear. Summarise with ticksum.py.
"""
import ctypes
import ctypes.wintypes as wt
import sys
import time

sys.path.insert(0, __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0])
from perfmon import find_pid, K32, OFFSET, FRAME_VA   # noqa: E402

TICKS_VA = 0x1A0A98


def main():
    name, out, secs = sys.argv[1], sys.argv[2], float(sys.argv[3])
    pid = None
    while pid is None:
        pid = find_pid(name)
        time.sleep(0.05)
    hp = K32.OpenProcess(0x0410, False, pid)
    time.sleep(0.3)
    f = open(out + ".ticks.txt", "w")
    buf = (ctypes.c_uint32 * 1)()
    got = ctypes.c_size_t()
    t0 = time.perf_counter()
    last = None

    def rd(va):
        if not K32.ReadProcessMemory(hp, ctypes.c_void_p(va + OFFSET), buf, 4, ctypes.byref(got)):
            return None
        return buf[0]

    n = 0
    while True:
        t = time.perf_counter() - t0
        if t > secs:
            break
        fr = rd(FRAME_VA)
        if fr is None:
            if find_pid(name) is None:
                break
        elif fr != last:
            f.write("%.3f %d %d\n" % (t * 1000, fr, rd(TICKS_VA) or 0))
            last = fr
            n += 1
            if n % 600 == 0:
                f.flush()
        time.sleep(0.0005)
    f.close()


main()
