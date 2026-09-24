"""Frame rate from fpsrun samples (game frame counter 0x25A660).
python fps.py TAG [TAG ...]
Per run: menu time, intro-load gap, cutscene start, fps in 10 s wall buckets
from cutscene start, average cutscene fps, and wall time to reach cutscene
frame N (content-aligned if the cutscene advances per frame)."""
import json, sys

def load(tag):
    return [json.loads(l) for l in open(f"{tag}.samples.jsonl") if '"frame": null' not in l]

def analyse(tag):
    r = [x for x in load(tag) if x.get("frame") is not None]
    menu = next((x["t"] for x in r if x.get("menu")), None)
    # the intro load: the longest stretch after the menu with no new frame
    best = (0, None, None)
    i = 0
    while i < len(r) - 1:
        if menu is None or r[i]["t"] < menu: i += 1; continue
        j = i
        while j + 1 < len(r) and r[j + 1]["frame"] == r[i]["frame"]: j += 1
        if r[j]["t"] - r[i]["t"] > best[0]: best = (r[j]["t"] - r[i]["t"], i, j)
        i = j + 1
    gap, gi, gj = best
    start = r[gj]["t"] if gj is not None else None
    f0 = r[gj]["frame"] if gj is not None else None
    end = r[-1]
    out = {"tag": tag, "menu_s": menu, "load_gap_s": round(gap, 1), "cutscene_start_s": start}
    if start is not None:
        out["cutscene_fps"] = round((end["frame"] - f0) / (end["t"] - start), 2)
        out["cutscene_frames"] = end["frame"] - f0
        buckets = []
        k = gj
        t_next = start + 10
        for x in r[gj:]:
            if x["t"] >= t_next:
                buckets.append(round((x["frame"] - r[k]["frame"]) / (x["t"] - r[k]["t"]), 1))
                k = r.index(x); t_next = x["t"] + 10
        out["fps_10s"] = buckets
        reach = {}
        for n in (250, 500, 1000, 1500, 2000, 3000):
            hit = next((x["t"] - start for x in r[gj:] if x["frame"] - f0 >= n), None)
            reach[n] = round(hit, 1) if hit is not None else None
        out["s_to_frame"] = reach
    # before the menu: the title animation
    pre = [x for x in r if menu is None or x["t"] <= menu]
    if len(pre) > 2:
        out["title_fps"] = round((pre[-1]["frame"] - pre[0]["frame"]) / (pre[-1]["t"] - pre[0]["t"]), 2)
    return out

for tag in sys.argv[1:]:
    print(json.dumps(analyse(tag)))
