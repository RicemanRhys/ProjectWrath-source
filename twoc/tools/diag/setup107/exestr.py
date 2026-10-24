# exestr.py EXE [NEEDLE]: strings in EXE containing NEEDLE (default: the user's home folder), 8-bit and UTF-16.
import collections, os, re, sys
b = open(sys.argv[1], 'rb').read()
needle = (sys.argv[2] if len(sys.argv) > 2 else os.environ['USERPROFILE']).lower()
c = collections.Counter()
for m in re.finditer(rb'[\x20-\x7e]{6,}', b):
    s = m.group().decode('latin-1')
    if needle in s.lower():
        c[s] += 1
for m in re.finditer(rb'(?:[\x20-\x7e]\x00){6,}', b):
    s = m.group().decode('utf-16-le')
    if needle in s.lower():
        c['(UTF-16) ' + s] += 1
for k, v in c.most_common():
    print(v, k[:220])
print(len(c), 'distinct')
