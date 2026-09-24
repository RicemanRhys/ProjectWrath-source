"""Account guest virtual-memory reservations from a step25_vm_tap.patch run.

Usage: python vmacct.py run.err.log [...]
Sums the [TMPVM] alloc/free lines: new regions, bytes MEM_RELEASEd, live peak, and
the biggest callers by return address.
"""
import re, sys, collections
for path in sys.argv[1:]:
    allocs = {}; live = 0; peak = 0; rel = 0; relbytes = 0; dec = 0
    byret = collections.Counter(); sizes = collections.Counter()
    heapused = None
    for l in open(path, encoding='utf-8', errors='replace'):
        m = re.search(r'\[TMPVM\] alloc ret=(0x\w+) hint=(0x\w+) req=(\d+) type=(0x\w+) -> st=(0x\w+) base=(0x\w+) size=(\d+) blk=(\d+)', l)
        if m:
            ret, hint, req, typ, st, base, size, blk = m.groups()
            if int(st, 16) == 0 and int(hint, 16) == 0:   # new region
                allocs[int(base, 16)] = int(size); live += int(size); peak = max(peak, live)
                byret[(ret, typ)] += int(size); sizes[int(size)] += 1
            continue
        m = re.search(r'\[TMPVM\] free ret=(0x\w+) base=(0x\w+) size=(\d+) type=(0x\w+) blk=(\d+)', l)
        if m:
            ret, base, size, typ, blk = m.groups()
            if int(typ, 16) & 0x8000:
                rel += 1; b = int(base, 16)
                if b in allocs: relbytes += allocs.pop(b); live = sum(allocs.values())
            else: dec += 1
        m = re.search(r'used (\d+)/(\d+)\)', l)
        if m: heapused = int(m.group(1))
    print(f"== {path.split('/')[-1]}: new VM regions {sum(sizes.values())} = {sum(k*v for k,v in sizes.items())/2**20:.1f} MiB; "
          f"MEM_RELEASE {rel} = {relbytes/2**20:.1f} MiB of those; decommits {dec}; still reserved at end {sum(allocs.values())/2**20:.1f} MiB; peak {peak/2**20:.1f} MiB; last heap used {heapused and heapused/2**20:.1f} MiB")
    for (ret, typ), b in byret.most_common(8):
        print(f"   {ret} type {typ}: {b/2**20:.2f} MiB")
    print("   sizes:", ", ".join(f"{n}x{s}" for s, n in sorted(sizes.items(), key=lambda kv: -kv[0]*kv[1])[:10]))
