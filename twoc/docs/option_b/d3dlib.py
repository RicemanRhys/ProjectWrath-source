import json, re, os, bisect
ROOT = r"C:/Users/User/Desktop/Desktop Stuff/Claude Stuff/Crash TWOC decomp"
OUT = ROOT + "/xboxrecomp/tools/disasm/output"
D3D_LO, D3D_HI = 0x000FD680, 0x0010F584

def load_funcs():
    fs = json.load(open(OUT + "/functions.json"))
    return {int(f['start'], 16): f for f in fs}

def load_xrefs():
    return json.load(open(OUT + "/xrefs.json"))

_asm_cache = {}
def parse_asm(name):
    if name in _asm_cache: return _asm_cache[name]
    funcs = {}; cur = None
    for line in open(OUT + "/asm/" + name + ".asm", encoding="utf-8", errors="replace"):
        m = re.match(r"^(sub_[0-9A-F]{8}):", line)
        if m:
            cur = int(m.group(1)[4:], 16); funcs[cur] = []; continue
        if line.startswith("; end of function"):
            cur = None; continue
        if cur is not None:
            m = re.match(r"^\s+0x([0-9A-F]{8})\s+([0-9a-f]+)\s+(.*)$", line)
            if m:
                funcs[cur].append((int(m.group(1), 16), m.group(3).rstrip()))
    _asm_cache[name] = funcs
    return funcs

def nv097():
    d = {}
    for line in open(ROOT + "/xboxrecomp/src/nv2a/nv2a_regs.h"):
        m = re.match(r"^#\s{3}define (NV097_\w+)\s+0x([0-9A-Fa-f]+)\s*$", line)
        if m:
            v = int(m.group(2), 16)
            d.setdefault(v, m.group(1))
    return d

NV = None
def method_name(m):
    global NV
    if NV is None: NV = nv097()
    if m in NV: return NV[m]
    # array methods: find nearest lower define
    keys = sorted(NV)
    i = bisect.bisect_right(keys, m) - 1
    if i >= 0: return "%s+0x%X" % (NV[keys[i]], m - keys[i])
    return "?"

def decode_hdr(v):
    """NV2A pushbuffer header: count<<18 | subch<<13 | method; bit30 = non-increasing."""
    if v & 0x3 or v > 0x7FFFFFFF: return None
    count = (v >> 18) & 0x7FF
    sub = (v >> 13) & 7
    meth = v & 0x1FFC
    ni = (v >> 30) & 1
    if count == 0 or count > 128 or sub != 0 or meth < 0x100: return None
    return count, meth, ni
