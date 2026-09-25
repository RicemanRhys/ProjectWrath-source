"""Point every caller of a wrapped function at the wrapper (notes 18.32).

  python twoc/tools/wrap_callers.py [GEN_DIR] [MANUAL_C]

recomp_manual.c wraps a lifted function by declaring `extern void sub_X_gen(void);`
and defining `void sub_X(void)`. tools.recomp (--exclude-manual) then emits the
lifted body as sub_X_gen, but it does that by renaming the function in its
database, so every reference is renamed with it: direct calls, tail jumps and the
dispatch table all name sub_X_gen, and nothing but recomp_manual.c ever reaches
sub_X. This pass renames those references back to sub_X. Only the body's own
definition line keeps the _gen name. lift.ps1 runs it after every lift.

It fails (exit 1) if a wrapped function has no body or more than one, so a
changed lifter output shows up here instead of as a silently bypassed wrapper.
"""
import glob
import os
import re
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
sys.path.insert(0, os.path.join(REPO, "xboxrecomp"))
from tools.recomp.manual_scan import scan  # noqa: E402

gen_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, "twoc", "src", "recomp", "gen")
manual = sys.argv[2] if len(sys.argv) > 2 else os.path.join(REPO, "twoc", "src", "recomp_manual.c")

_skip, wrap, _ref = scan(manual)
ref_re = re.compile(r"\bsub_([0-9A-Fa-f]{8})_gen\b")
bodies = {a: 0 for a in wrap}
renamed = {a: 0 for a in wrap}

for path in sorted(glob.glob(os.path.join(gen_dir, "*.c")) + glob.glob(os.path.join(gen_dir, "*.h"))):
    with open(path, encoding="utf-8", newline="") as f:
        lines = f.read().splitlines(keepends=True)
    changed = False
    for i, line in enumerate(lines):
        if "_gen" not in line:
            continue
        m = ref_re.search(line)
        if not m or int(m.group(1), 16) not in wrap:
            continue
        addr = int(m.group(1), 16)
        if path.endswith(".c") and re.match(r"^void sub_%08X_gen\(void\)\s*(\{|$)" % addr, line):
            bodies[addr] += 1                     # the lifted body keeps its _gen name
            continue
        if line.lstrip().startswith("*"):
            continue                              # the body's header comment
        new = ref_re.sub(lambda k: ("sub_%s" % k.group(1)) if int(k.group(1), 16) in wrap else k.group(0), line)
        renamed[addr] += 1
        lines[i] = new
        changed = True
    if changed:
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.write("".join(lines))

bad = [a for a in sorted(wrap) if bodies[a] != 1]
for a in sorted(wrap):
    print("wrap_callers: sub_%08X: %d reference(s) now call the wrapper, body sub_%08X_gen x%d"
          % (a, renamed[a], a, bodies[a]))
if bad:
    print("wrap_callers: ERROR: no single lifted body for " + ", ".join("sub_%08X" % a for a in bad),
          file=sys.stderr)
    sys.exit(1)
