"""progs.py INTERP.BIN SWAP -- step 106: every vertex program of the frame (by key) with its draw count, and the last
lines of each program's disassembly (how it ends: the XDK's screen-space epilogue)."""
import sys
sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")
import interp_recio as rio
import vscheck as vc
seen = {}
for fr in rio.read(sys.argv[1]):
    if fr["swap"] != int(sys.argv[2]) or not fr["full"]:
        continue
    for c in fr["cmds"]:
        if c["kind"] == 0 and c["full"]["n_insns"]:
            k = c["vkey"]
            if k not in seen:
                seen[k] = [0, c["full"]["code"]]
            seen[k][0] += 1
for k, (n, code) in sorted(seen.items(), key=lambda x: -x[1][0]):
    lines = [vc.disasm(vc.decode(code[i:i + 4])) for i in range(0, len(code), 4)]
    print("program %08X: %d draws, %d instructions; last 4:" % (k, n, len(lines)))
    for l in lines[-4:]:
        print("    " + l)
