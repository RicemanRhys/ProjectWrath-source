"""Step 110 Part 1 (notes 18.113): how much of each tracked file comes from the game's program or data.

  python gamescan.py REPO [--toolkit=DIR --base=COMMIT] > out.tsv

For each file tracked in REPO/twoc (and, with --toolkit, each file the toolkit's local commits change since BASE):
size, x86 instruction lines (disassembly), runs of 6+ hex bytes (instruction or data bytes), sub_XXXXXXXX names (the
game's functions by address), 0x0XXXXXXX constants in the game's image range (0x00010000-0x00200000, the XBE's code
and data), and SHA-256 hashes of 64 hex digits (the disc manifest's). Counts only: the classification is the notes'.
"""
import os, re, subprocess, sys

repo = os.path.abspath(sys.argv[1])
opts = dict(a[2:].split("=", 1) for a in sys.argv[2:] if a.startswith("--") and "=" in a)
ASM = re.compile(rb"^\s*(?:[0-9A-Fa-f]{6,8}[: ]\s*)?(?:[0-9A-Fa-f]{2} )*\s*(mov|push|pop|call|jmp|j[a-z]{1,3}|lea|cmp|test|xor|sub|add|ret|movzx|fld|fstp|and|or|shl|shr|inc|dec)\s+[^=(]*$", re.M)
HEX = re.compile(rb"(?:\b[0-9A-Fa-f]{2} ){6,}")
SUB = re.compile(rb"\bsub_[0-9A-Fa-f]{8}\b")
ADDR = re.compile(rb"\b0x(000[1-9A-Fa-f][0-9A-Fa-f]{4}|001[0-9A-Fa-f]{5})u?\b")
SHA = re.compile(rb"\b[0-9a-f]{64}\b")


def scan(path, label):
    try:
        d = open(path, "rb").read()
    except OSError:
        return
    print("\t".join([label, str(len(d)), str(len(ASM.findall(d))), str(len(HEX.findall(d))), str(len(SUB.findall(d))),
                     str(len(ADDR.findall(d))), str(len(SHA.findall(d)))]))


print("file\tbytes\tasm_lines\thex_runs\tsub_names\timage_addrs\tsha256")
files = subprocess.run(["git", "-C", repo, "ls-files", "twoc"], capture_output=True, text=True).stdout.splitlines()
for f in files:
    scan(os.path.join(repo, f), f)
if "toolkit" in opts:
    tk = opts["toolkit"]
    ch = subprocess.run(["git", "-C", tk, "diff", "--name-only", opts["base"], "HEAD"], capture_output=True,
                        text=True).stdout.splitlines()
    for f in ch:
        scan(os.path.join(tk, f), "xboxrecomp/" + f)
