"""zfmt.py -- step 105: the guest's depth surface (device 0x10C110 + 0x2074): its header, Format word and XDK format
code ((Format >> 8) & 0xFF), Size word, and the render target's."""
import os, sys
sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")
import memsnap
h = memsnap.attach(memsnap.find_pid(os.environ.get('TWOC_EXE', 'crash_twoc.exe')))
dev = memsnap.u32(h, 0x10EBF0)
for name, off in (("depth", 0x2074), ("target", None)):
    if off is None:
        continue
    s = memsnap.u32(h, dev + off)
    if not s:
        print("%s: none" % name)
        continue
    fmt = memsnap.u32(h, s + 0xC)
    print("dev %08X %s surface %08X common %08X data %08X format %08X (code %02X) size %08X" % (
        dev, name, s, memsnap.u32(h, s), memsnap.u32(h, s + 4), fmt, (fmt >> 8) & 0xFF, memsnap.u32(h, s + 0x10)), flush=True)
