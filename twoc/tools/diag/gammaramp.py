"""gammaramp.py SECS -- read D3D's gamma ramps from a running game every 2 s (step 48, notes 18.51).

D3D keeps two 768-byte ramps (R, G, B planes of 256) at the display object, device + 0x2268,
offset 0x21C and 0x51C, with pending flags at +0x81C/+0x820; the vblank DPC uploads one to
the DAC palette (sub_00105B32). Prints whether each is the identity. Step 48: identity from
boot through the menu and the Arctic Antics demo (the game never sets a ramp). Needs numpy's
venv only for memsnap's import.
"""
import sys
import time
import memsnap

secs = float(sys.argv[1])
t0 = time.time()
while memsnap.find_pid() is None and time.time() - t0 < 30:
    time.sleep(0.5)
h = memsnap.attach()
while time.time() - t0 < secs:
    time.sleep(2)
    dev = memsnap.u32(h, 0x10EBF0)          # [0x10EBF0] = &device (0x10C110)
    if not dev:
        continue
    mp = dev + 0x2268
    out = []
    for k in range(2):
        b = memsnap.read(h, mp + 0x21C + 0x300 * k, 0x300)
        ident = all(b[c * 256 + i] == i for c in range(3) for i in range(256))
        out.append("ramp%d %s (R %s) flag %u" % (k, "identity" if ident else "CUSTOM", list(b[0:256:51]),
                                                memsnap.u32(h, mp + 0x81C + 4 * k)))
    print("%5.1f %s" % (time.time() - t0, "; ".join(out)), flush=True)
