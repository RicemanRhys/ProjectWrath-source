"""camev.py ERRLOG [N] -- step 77: the TMP camera log (camlog.py build, TMP_CAMLOG=1). Each present's drawn camera
position (an interpolated pass: 'at'; a plain pass: that frame's recorded 'b'), its t, and the distance from the
present before; the game frame's own step |b - a|; and whether the drawn point lies at t between a and b."""
import re, sys

lines = open(sys.argv[1], 'rb').read().decode('latin1').splitlines()
n = int(sys.argv[2]) if len(sys.argv) > 2 else 40
pres = []
last_b = None
last_a = None
for l in lines:
    m = re.search(r"\[TMP\] cam t ([\d.]+) a (\S+) (\S+) (\S+) b (\S+) (\S+) (\S+) at (\S+) (\S+) (\S+)", l)
    if m:
        v = [float(x) for x in m.groups()]
        t, a, b, at = v[0], v[1:4], v[4:7], v[7:10]
        lin = [a[i] + (b[i] - a[i]) * t for i in range(3)]
        err = max(abs(lin[i] - at[i]) for i in range(3))
        pres.append((t, at, a, b, err))
        last_a, last_b = a, b
    elif "[TMP] cam plain" in l and last_b:
        pres.append((1.0, last_b, last_a, last_b, 0.0))


def d(p, q):
    return sum((p[i] - q[i]) ** 2 for i in range(3)) ** 0.5


rows = []
for i in range(1, len(pres)):
    t, at, a, b, err = pres[i]
    rows.append((t, d(at, pres[i - 1][1]), d(a, b), err))
mid = len(rows) // 2
for r in rows[mid: mid + n]:
    print("t %.4f  moved %.4f since the last present; this game frame's step %.4f; off the line %.2g" % r)
import statistics
mv = [r[1] for r in rows if r[2] > 0]
print("presents %d, worst off-the-line %.2g; moved per present mean %.4f sd %.4f; game frame steps mean %.4f sd %.4f"
      % (len(rows), max(r[3] for r in rows), statistics.mean(mv), statistics.pstdev(mv),
         statistics.mean(r[2] for r in rows), statistics.pstdev([r[2] for r in rows])))
