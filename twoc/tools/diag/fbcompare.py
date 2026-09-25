"""Compare the bridge's host frames with the executor's (notes 18.33, Option B S2).

  python fbcompare.py DUMPDIR [--tol=8] [--diff=OUTDIR] [--max-dt=S]
  python fbcompare.py A.bmp B.bmp [--tol=8] [--diff=OUT.bmp]

With a directory (what run_twoc.ps1 -DumpDir or drive.py --dump fills), each
host dump (hostNNN.bmp, from twoc/src/d3d_host.c) is paired with the executor
dump (shotNNN.bmp) closest to it in time. The two are written on different
threads at different points in a frame (the executor mid push-buffer, the
host at the guest Swap), so pairing by file time is the best available; pairs
more than --max-dt seconds apart (default 2) are listed but not compared.

For each pair: the share of pixels within +/-tol on all three channels (S3's
bar is 99% within 8), the share identical, the mean absolute difference per
channel, and the largest difference. --diff writes an image per pair: black
where a pixel is within tolerance, the host pixel where it isn't.

Reads 24- and 32-bit uncompressed BMPs, bottom-up or top-down. No dependencies.
"""
import os
import struct
import sys


def read_bmp(path):
    data = open(path, "rb").read()
    if data[:2] != b"BM":
        raise ValueError("%s: not a BMP" % path)
    off = struct.unpack_from("<I", data, 10)[0]
    w, h = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0]
    comp = struct.unpack_from("<I", data, 30)[0]
    if bpp not in (24, 32) or comp not in (0, 3):
        raise ValueError("%s: %d bpp, compression %d not supported" % (path, bpp, comp))
    top_down = h < 0
    h = abs(h)
    bpp_b = bpp // 8
    stride = (w * bpp_b + 3) & ~3
    rows = []
    for y in range(h):
        src_y = y if top_down else h - 1 - y
        r = data[off + src_y * stride: off + src_y * stride + w * bpp_b]
        if bpp_b == 4:
            r = bytes(b for i, b in enumerate(r) if i % 4 != 3)
        rows.append(r)                       # BGR, top row first
    return w, h, b"".join(rows)


def write_bmp(path, w, h, bgr):
    stride = (w * 3 + 3) & ~3
    pad = b"\0" * (stride - w * 3)
    with open(path, "wb") as f:
        f.write(b"BM" + struct.pack("<IHHI", 54 + stride * h, 0, 0, 54))
        f.write(struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, stride * h, 2835, 2835, 0, 0))
        for y in range(h - 1, -1, -1):
            f.write(bgr[y * w * 3:(y + 1) * w * 3] + pad)


def compare(a_path, b_path, tol, diff_path=None):
    wa, ha, a = read_bmp(a_path)
    wb, hb, b = read_bmp(b_path)
    if (wa, ha) != (wb, hb):
        return {"size": "%dx%d vs %dx%d" % (wa, ha, wb, hb)}
    n = wa * ha
    within = same = 0
    sums = [0, 0, 0]
    worst = 0
    out = bytearray(len(a)) if diff_path else None
    for i in range(0, len(a), 3):
        d0 = abs(a[i] - b[i])
        d1 = abs(a[i + 1] - b[i + 1])
        d2 = abs(a[i + 2] - b[i + 2])
        m = max(d0, d1, d2)
        sums[0] += d0
        sums[1] += d1
        sums[2] += d2
        if m > worst:
            worst = m
        if m <= tol:
            within += 1
            if m == 0:
                same += 1
        elif out is not None:
            out[i:i + 3] = a[i:i + 3]
    if diff_path:
        write_bmp(diff_path, wa, ha, bytes(out))
    return {"size": "%dx%d" % (wa, ha), "within": 100.0 * within / n, "same": 100.0 * same / n,
            "mean": [s / n for s in reversed(sums)], "max": worst}   # mean as R, G, B


def fmt(r):
    if "within" not in r:
        return "size mismatch: " + r["size"]
    return "%s  within %.2f%%  identical %.2f%%  mean |d| R %.2f G %.2f B %.2f  max %d" % (
        r["size"], r["within"], r["same"], r["mean"][0], r["mean"][1], r["mean"][2], r["max"])


def main(argv):
    opts = dict(a[2:].split("=", 1) for a in argv if a.startswith("--") and "=" in a)
    args = [a for a in argv if not a.startswith("--")]
    tol = int(opts.get("tol", "8"))
    max_dt = float(opts.get("max-dt", "2"))
    diff = opts.get("diff")

    if len(args) == 2:
        print(fmt(compare(args[0], args[1], tol, diff)))
        return 0
    if len(args) != 1 or not os.path.isdir(args[0]):
        print(__doc__)
        return 2
    d = args[0]
    files = sorted(os.listdir(d))
    hosts = [os.path.join(d, f) for f in files if f.startswith("host") and f.endswith(".bmp")]
    shots = [os.path.join(d, f) for f in files if f.startswith("shot") and f.endswith(".bmp")]
    print("%s: %d host dumps, %d executor dumps, tolerance %d" % (d, len(hosts), len(shots), tol))
    if not hosts or not shots:
        return 1
    if diff:
        os.makedirs(diff, exist_ok=True)
    st = {p: os.path.getmtime(p) for p in shots}
    t0 = min(min(st.values()), min(os.path.getmtime(p) for p in hosts))
    compared = passed = 0
    for h in hosts:
        th = os.path.getmtime(h)
        s = min(shots, key=lambda p: abs(st[p] - th))
        dt = st[s] - th
        head = "%s @%6.1f s  vs  %s (%+.2f s)" % (os.path.basename(h), th - t0, os.path.basename(s), dt)
        if abs(dt) > max_dt:
            print(head + "  not compared: more than %.1f s apart" % max_dt)
            continue
        dp = os.path.join(diff, "diff_" + os.path.basename(h)) if diff else None
        r = compare(h, s, tol, dp)
        compared += 1
        passed += r.get("within", 0) >= 99.0
        print(head + "  " + fmt(r))
    print("%d pairs compared, %d with at least 99%% of pixels within %d" % (compared, passed, tol))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
