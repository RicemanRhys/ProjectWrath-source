"""rdlv.py OFFSET -- step 99: print the pad level table's byte at 0x19A7F0+OFFSET (60 = room 6 pad 1, cast_bug)."""
import os, sys
sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")
import memsnap
h = memsnap.attach(memsnap.find_pid(os.environ.get('TWOC_EXE', 'crash_twoc.exe')))
print(memsnap.read(h, 0x19A7F0 + int(sys.argv[1]), 1)[0])
