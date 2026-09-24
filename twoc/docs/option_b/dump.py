import d3dlib as L, json, re, sys
A = L.parse_asm("D3D")
E = json.load(open("entries.json"))
F = L.load_funcs()
def ann(txt):
    notes = []
    for h in re.findall(r"0x[0-9a-f]+", txt):
        v = int(h, 16)
        d = L.decode_hdr(v)
        if d and ("mov" in txt and ("dword ptr [e" in txt or "], " in txt)):
            c, m, ni = d
            notes.append("HDR n=%d %s%s" % (c, L.method_name(m), " NI" if ni else ""))
    return ("   ;; " + "; ".join(notes)) if notes else ""
def dump(a, fh, maxlines=400):
    lines = A.get(a)
    if lines is None:
        fh.write("  (not a function start in D3D.asm)\n"); return
    for i,(va, t) in enumerate(lines):
        if i >= maxlines: fh.write("  ...\n"); break
        fh.write("  %08X  %s%s\n" % (va, t, ann(t)))
with open(sys.argv[1] if len(sys.argv)>1 else "d3d99_asm.txt", "w") as fh:
    for k, v in E.items():
        a = int(k, 16); f = F.get(a, {})
        fh.write("\n==== %s  sites=%d size=%s callers=%s  calls=%s\n" % (k, len(v['sites']), f.get('size'), ",".join(v['callers'][:8]), ",".join(f.get('calls_to', [])[:12])))
        dump(a, fh)
