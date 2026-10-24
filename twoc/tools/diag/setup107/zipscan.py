# zipscan.py ZIP: what a Collect logs zip holds, and whether this PC's user name, home folder or PC name is left
# in any of its files (as 8-bit text or UTF-16).
import os, re, sys, zipfile
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
z = zipfile.ZipFile(sys.argv[1])
home, user, pc = os.environ['USERPROFILE'], os.environ['USERNAME'], os.environ['COMPUTERNAME']
needles = [('home', re.compile(re.escape(home), re.I)), ('home/', re.compile(re.escape(home.replace('\\', '/')), re.I)),
           ('\\Users\\name', re.compile(r'(?i)[\\/]Users[\\/]' + re.escape(user) + r'(?=[\\/"\' ]|$)')),
           ('pc', re.compile(r'(?i)(?<![A-Za-z0-9_-])' + re.escape(pc) + r'(?![A-Za-z0-9_-])'))]
left = 0
for i in z.infolist():
    b = z.read(i)
    hits = []
    for t in (b.decode('utf-8', 'replace'), b.decode('utf-16-le', 'replace')):
        for name, rx in needles:
            m = rx.search(t)
            if m:
                hits.append('%s at "...%s..."' % (name, t[max(0, m.start() - 30):m.end() + 10].replace('\n', ' ')))
    print('%-50s %8d bytes %s' % (i.filename, i.file_size, '; '.join(hits) if hits else 'clean'))
    left += len(hits)
print('scrub.txt:')
print(z.read('scrub.txt').decode('utf-8-sig', 'replace'))
print('TRACES LEFT: %d' % left)
