"""Drive port 0 through RECOMP_DEV_INPUT_FILE and find guest floats that move
only while the stick is held (Crash's position candidates).

  python probe.py INPUT_FILE OUT_PREFIX [hold_s] [stick]   stick: up|down|left|right

Phases: neutral idle (snap A, then A2 1 s later), stick held (snap B, then C),
released (snap D, then E). A candidate dword is a finite float that is equal
in A/A2 (still when idle), differs B vs A2 and C vs B (moves while held), and
is equal in D/E (still again). Candidates are grouped into runs of 3 adjacent
floats (x, y, z). Writes OUT_PREFIX.json with the ranked list and the frame
counter at each snap.
"""
import json, sys, time
import numpy as np
import memsnap

STICKS = {"none": "", "up": "ly=32767", "down": "ly=-32767", "left": "lx=-32767", "right": "lx=32767"}

def put(path, line):
    with open(path, "w") as f:
        f.write(line + "\n")

def main():
    inp, out = sys.argv[1], sys.argv[2]
    hold = float(sys.argv[3]) if len(sys.argv) > 3 else 1.5
    stick = STICKS[sys.argv[4] if len(sys.argv) > 4 else "up"]
    h = memsnap.attach()
    frames, snaps = {}, {}
    def take(tag):
        frames[tag] = memsnap.u32(h, 0x25A660)
        snaps[tag] = memsnap.snap(h)
        print(tag, "frame", frames[tag], flush=True)
    put(inp, ""); time.sleep(2.0); take("A"); time.sleep(1.0); take("A2")
    put(inp, stick); time.sleep(hold); take("B"); time.sleep(hold); take("C")
    put(inp, ""); time.sleep(2.0); take("D"); time.sleep(1.0); take("E")

    res = []
    for name in snaps["A"]:
        base = snaps["A"][name][0]
        n = min(len(snaps[t][name][1]) for t in snaps)
        g = {t: snaps[t][name][1][:n] for t in snaps}
        f = {t: g[t].view(np.float32) for t in g}
        ok = (g["A"] == g["A2"]) & (g["B"] != g["A2"]) & (g["C"] != g["B"]) & (g["D"] == g["E"]) & (g["C"] != g["A"])
        for t in f:
            ok &= np.isfinite(f[t]) & (np.abs(f[t]) < 1e5) & (np.abs(f[t]) > 1e-3)
        idx = np.nonzero(ok)[0]
        for i in idx:
            res.append({"va": hex(base + 4 * int(i)),
                        "A": float(f["A"][i]), "B": float(f["B"][i]), "C": float(f["C"][i]),
                        "D": float(f["D"][i]), "moved": float(f["D"][i] - f["A"][i])})
    # triplets: three candidates at consecutive addresses
    vas = {int(r["va"], 16) for r in res}
    trip = sorted(v for v in vas if v + 4 in vas or v + 8 in vas or v - 4 in vas)
    json.dump({"frames": frames, "stick": stick, "count": len(res), "candidates": res[:400],
               "clustered": [hex(v) for v in trip][:400]}, open(out + ".json", "w"), indent=1)
    np.savez_compressed(out + ".npz", **{f"{t}_{n}": snaps[t][n][1] for t in snaps for n in snaps[t]})
    print("candidates", len(res), "clustered", len(trip))

if __name__ == "__main__":
    main()
