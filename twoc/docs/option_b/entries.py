import d3dlib as L, json, bisect, collections
F = L.load_funcs(); X = L.load_xrefs()
starts = sorted(F)
def owner(a):
    i = bisect.bisect_right(starts, a) - 1
    return starts[i] if i >= 0 else None
def sect(a):
    f = F.get(owner(a)); return f['section'] if f else '?'
ent = collections.defaultdict(list)
for e in X:
    if e['type'] != 'call': continue
    t = int(e['to'], 16); s = int(e['from'], 16)
    if L.D3D_LO <= t < L.D3D_HI and not (L.D3D_LO <= s < L.D3D_HI):
        ent[t].append(s)
print(len(ent), sum(len(v) for v in ent.values()))
out = {}
for t in sorted(ent):
    callers = sorted(set(owner(s) for s in ent[t]))
    secs = collections.Counter(sect(s) for s in ent[t])
    out[hex(t)] = dict(sites=[hex(s) for s in ent[t]], callers=[hex(c) for c in callers], sections=dict(secs))
json.dump(out, open("entries.json", "w"), indent=1)
print(collections.Counter(k for v in out.values() for k in v['sections']))
