"""cat84.py TAG [GAME_MS] [N] -- step 84: hs84.py's samples of a run by category, as ms a frame and us a draw: the
game's own recompiled code, the bridge's wrappers, the mirror's translation (textures and their signatures, vertex
fetch, the rest of the state, allocation) and the recording (copies, interpolation, the rest). ms a frame = the busy
samples' share times the game side's mean ms (GAME_MS, else the run's own GPROF CSV: sampling slows it a little;
pass a plain run's mean to scale to it). Top functions under each category."""
import collections, csv, json, os, statistics, sys

HERE = os.path.dirname(os.path.abspath(__file__))
tag = sys.argv[1]
N = int(sys.argv[3]) if len(sys.argv) > 3 else 900
rows = list(csv.DictReader(open(os.path.join(HERE, "runs", tag + ".gprof.csv"))))[-(N + 30):-30]
gms = float(sys.argv[2]) if len(sys.argv) > 2 and float(sys.argv[2]) > 0 else statistics.mean(float(r["game"]) for r in rows)
draws = statistics.mean(float(r["rec_draws"]) for r in rows)
h = json.load(open(os.path.join(HERE, "runs", tag + ".hs.txt.json")))
per = {int(p): c for p, c in h["per"].items()}
callers = {(int(p), nm): c for p, nm, c in h["callers"]}
busy = sum(sum(c.values()) for p, c in per.items() if p in (0, 1, 7, 9))

TEX = ("content_sig", "sig_span", "texture_sig", "describe_texture", "d3d8_format_is_compressed", "d3dh_texture_bytes",
       "tex_lookup", "tex_snap")
VTX = ("vs_fetch", "vp_vertices", "vs_fetch_run", "vl_convert", "vl_make", "sprite_vertices", "vp_vertices_ref")
WRAP = ("d3db_enter", "d3db_leave", "d3db_item_drawn", "d3db_item_queued", "d3db_list_flush", "d3db_vertex_data",
        "d3db_panel")
IC = ("ic_", "icam_", "m4_", "q_", "fnv")


def cat(p, nm, caller):
    base = caller if (nm.startswith("<") and caller) else nm
    b = base.lstrip("_")
    if p == 1:
        if b.startswith(IC):
            return "recording: interpolation (ic_*)"
        if nm.startswith("<") or b.startswith(("ra_dup", "ra_keep", "ra_alloc", "rc_push", "rec_draw")):
            return "recording: copies and commands"
        return "recording: the rest"
    if p in (0, 9) and (b.startswith(WRAP) or (nm.startswith("<") and caller and caller.startswith(WRAP))):
        return "bridge wrappers (args, timing)"
    if p in (0, 9):
        return "the game's code (sub_*, runtime, kernel)"
    if b.startswith(TEX):
        return "mirror: textures and signatures"
    if b.startswith(VTX) or (nm.startswith("<") and caller and caller.lstrip("_").startswith(VTX)):
        return "mirror: program vertices (fetch, convert)"
    if "AllocateHeap" in nm or "FreeHeap" in nm:
        return "mirror: heap"
    return "mirror: state, constants, indices, the rest"


tot = collections.Counter()
fn = collections.defaultdict(collections.Counter)
for p, c in per.items():
    if p not in (0, 1, 7, 9):
        continue
    for nm, n in c.items():
        cc = callers.get((p, nm))
        if cc and nm.startswith("<"):
            t = sum(cc.values())
            for cn, k in cc.items():
                k2 = n * k / t
                ct = cat(p, nm, cn)
                tot[ct] += k2
                fn[ct]["%s<-%s" % (nm, cn)] += k2
        else:
            ct = cat(p, nm, None)
            tot[ct] += n
            fn[ct][nm] += n
print("%s: game side %.2f ms a frame, %.0f draws a frame, %d busy samples" % (tag, gms, draws, busy))
for ct, n in tot.most_common():
    ms = gms * n / busy
    print("  %-44s %6.2f ms  %5.2f us a draw  (%4.1f%%)" % (ct, ms, 1000 * ms / draws, 100.0 * n / busy))
    top = ", ".join("%s %.2f" % (f, gms * k / busy) for f, k in fn[ct].most_common(int(os.environ.get("TOPF", "5"))))
    print("      " + top)
