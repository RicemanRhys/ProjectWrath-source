# mkmanifest.py SCAN_TSV OUT_TSV: the repo's disc manifest from isoscan.py's --manifest output (path, size, sha256),
# with a header of the disc's facts. Hashes and sizes only: no game data.
import sys
rows = [l.rstrip('\n').split('\t') for l in open(sys.argv[1], encoding='utf-8') if l.strip()]
rows.sort(key=lambda r: r[0].lower())
xbe = [r for r in rows if r[0].lower() == 'default.xbe'][0]
total = sum(int(r[1]) for r in rows)
hdr = [
    '# The supported disc: Crash Bandicoot: The Wrath of Cortex, Xbox, USA (NTSC-U).',
    '# Every file on it: its path (forward slashes), size in bytes and SHA-256. Hashes and sizes only, no game data.',
    '# Read from the user\'s trimmed XISO image "Crash Bandicoot - The Wrath of Cortex (USA).xiso.iso"',
    '# (975,896,576 bytes, partition at offset 0) by step 107\'s isoscan.py, and checked file by file against the',
    '# game_files the game was brought up with (all 2,267 identical). twoc/CMakeLists.txt turns it into',
    '# build/generated/disc_manifest.h for setup.c (the first-run setup, notes 18.110).',
    '# default.xbe certificate: title id 56550003, title "Crash Bandicoot: tWoC", region 00000001 (North America),',
    '# version 00000001, allowed media 00000002 (DVD-X2).',
    '#! title_id 56550003',
    '#! region 00000001',
    '#! version 00000001',
    '#! title Crash Bandicoot: tWoC',
    '#! xbe_size %s' % xbe[1],
    '#! xbe_sha256 %s' % xbe[2],
    '#! files %d' % len(rows),
    '#! bytes %d' % total,
]
with open(sys.argv[2], 'w', encoding='utf-8', newline='\n') as o:
    o.write('\n'.join(hdr) + '\n')
    for r in rows:
        o.write('\t'.join(r[:3]) + '\n')
print(len(rows), total)
