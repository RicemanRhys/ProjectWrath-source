"""wmp.py LEVELDIR... -- step 100: a level's wumpa fruit (.wmp: count, then x y z floats) and, if it parses, its
crates (.crt), in file order, with the range of each axis: points along the level's path to teleport to."""
import glob, os, struct, sys
R = r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\game_files\Crashdat\levels"
for lv in sys.argv[1:]:
    d = os.path.join(R, lv)
    for f in glob.glob(os.path.join(d, "*.wmp")):
        b = open(f, "rb").read()
        n = struct.unpack_from("<I", b)[0]
        p = [struct.unpack_from("<3f", b, 4 + 12 * i) for i in range(n)]
        print("%s: %d fruit" % (lv, n))
        for i in range(0, n, max(1, n // 12)):
            print("  %3d %8.2f %8.2f %8.2f" % ((i,) + p[i]))
        print("  x %.1f..%.1f y %.1f..%.1f z %.1f..%.1f" % tuple(v for k in range(3) for v in (min(q[k] for q in p), max(q[k] for q in p))))
    for f in glob.glob(os.path.join(d, "*.crt")):
        b = open(f, "rb").read()
        print("  crt %d bytes, head %s" % (len(b), b[:32].hex()))
