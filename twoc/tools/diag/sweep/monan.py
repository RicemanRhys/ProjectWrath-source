import re,sys
rows=[]; cur=None
for l in open(sys.argv[1]):
    m=re.match(r"== ([\d.]+) min: handles (\d+) threads (\d+) private (\d+) KiB ws (\d+) KiB gdi (\d+) user (\d+) levels (.*)",l)
    if m:
        cur=dict(t=float(m[1]),h=int(m[2]),th=int(m[3]),pv=int(m[4])//1024,ws=int(m[5])//1024,gdi=int(m[6]),usr=int(m[7]),lv=m[8].strip()); rows.append(cur); continue
    if cur is None: continue
    if "[HEAPSTAT]" in l:
        m=re.search(r"used (\d+)/(\d+) KiB.*?live (\d+) KiB",l); cur['heap']=int(m[1]); cur['heapl']=int(m[3])
    if "[CONTIGSTAT]" in l:
        m=re.search(r"high-water (\d+) KiB; live (\d+) KiB, free for reuse (\d+)",l); cur['chw']=int(m[1]); cur['cl']=int(m[2])
    if "[VMSTAT]" in l:
        m=re.search(r"live (\d+) KiB in.*largest (\d+) KiB.*heap live less zone free (\d+)",l); cur['vml']=int(m[1]); cur['vmlarge']=int(m[2]); cur['real']=int(m[3])
    if "[POOLSTAT]" in l:
        m=re.search(r"live (\d+) KiB in (\d+) blocks",l); cur['pool']=int(m[1])
    if "[KERNEL] summary" in l:
        m=re.search(r"(\d+) total calls",l); cur['kc']=int(m[1])
    if "host +" in l:
        m=re.search(r"guest swaps.*?, ([\d.]+)/s\)",l)
        if m: cur['fps']=float(m[1])
    if "swap gaps" in l:
        m=re.search(r"over 25 ms (\d+), over 50 ms (\d+), over 100 ms (\d+), longest ([\d.]+)",l); cur['g25']=int(m[1]); cur['gmax']=float(m[4])
print("min  handles thr privMB wsMB gdi usr heapKiB realKiB contigLive contigHW vmLargest poolKiB kcalls(M) fps gap25 gapmax levels")
for r in rows:
    print("%4.0f %6d %4d %6d %5d %3d %3d %7s %7s %7s %7s %7s %5s %8s %5s %4s %6s %s"%(r['t'],r['h'],r['th'],r['pv'],r['ws'],r['gdi'],r['usr'],r.get('heap','-'),r.get('real','-'),r.get('cl','-'),r.get('chw','-'),r.get('vmlarge','-'),r.get('pool','-'),"%.1f"%(r['kc']/1e6) if 'kc' in r else '-',r.get('fps','-'),r.get('g25','-'),r.get('gmax','-'),r['lv']))
