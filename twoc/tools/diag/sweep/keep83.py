"""keep83.py seed|check DIR -- step 83's retention test.

seed: adds fake backups (two a day, 30 days back from today, at 00:10 and 01:20 so today's come before the real
ones), a folder that isn't a backup ('notes'), a loose file, and a backup folder with an extra file in it.
check: lists what is left against the rule: the newest 10, and the newest of each of the last 14 days (today and
13 before); the extra-file folder stays whatever the rule says (its folder can't be removed).
"""
import datetime, os, re, sys

cmd, d = sys.argv[1], sys.argv[2]
pat = re.compile(r'^\d{4}-\d\d-\d\d_\d\d-\d\d-\d\d(_\d+)?$')
today = datetime.date.today()
EXTRA = None
if cmd == 'seed':
    for back in range(30):
        day = today - datetime.timedelta(days=back)
        for hm in ('00-10-00', '01-20-00'):
            p = os.path.join(d, '%s_%s' % (day.isoformat(), hm))
            os.makedirs(p, exist_ok=True)
            open(os.path.join(p, 'crash.sav'), 'wb').write(b'x' * 100)
            open(os.path.join(p, 'backup.txt'), 'w').write('fake\nwhy: seeded by keep83.py\n')
    os.makedirs(os.path.join(d, 'notes'), exist_ok=True)
    open(os.path.join(d, 'notes', 'mine.txt'), 'w').write('not a backup')
    open(os.path.join(d, 'loose.txt'), 'w').write('a loose file')
    extra = os.path.join(d, '%s_00-10-00' % (today - datetime.timedelta(days=25)).isoformat())
    open(os.path.join(extra, 'keep.me'), 'w').write('a file the backup code never wrote')
    print('seeded', len([n for n in os.listdir(d) if pat.match(n)]), 'backups')
    sys.exit()

names = sorted([n for n in os.listdir(d) if pat.match(n)], reverse=True)
print('left:', len(names))
# what the rule keeps, from the names that were there before (left + those deleted can't be known here), so
# recompute from the seed's set plus the real ones still present
seen_days = set()
expect = set()
allnames = sorted(set(names) | set(sys.argv[3:]), reverse=True)
for i, n in enumerate(allnames):
    day = datetime.date.fromisoformat(n[:10])
    keep = i < 10
    if n[:10] not in seen_days:
        seen_days.add(n[:10])
        if (today - day).days < 14:
            keep = True
    if keep:
        expect.add(n)
extra = '%s_00-10-00' % (today - datetime.timedelta(days=25)).isoformat()
expect.add(extra)
print('expected:', len(expect))
print('missing (should be kept):', sorted(expect - set(names)))
print('extra (should be gone):', sorted(set(names) - expect))
print('notes kept:', os.path.exists(os.path.join(d, 'notes', 'mine.txt')), 'loose kept:',
      os.path.exists(os.path.join(d, 'loose.txt')), 'keep.me kept:', os.path.exists(os.path.join(d, extra, 'keep.me')))
