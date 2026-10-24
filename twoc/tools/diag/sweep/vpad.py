"""vpad.py FILE LINE... -- write SDL virtual pad lines with waits: 'wait:S' sleeps, anything else is written."""
import sys, time
f = sys.argv[1]
for a in sys.argv[2:]:
    if a.startswith("wait="):
        time.sleep(float(a[5:]))
    else:
        open(f, "w").write(a.replace("_", " ") + "\n")
