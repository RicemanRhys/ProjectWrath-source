"""aofxc.py -- compile ao.c's shaders with the SDK's fxc (same compiler as D3DCompile) to find errors offline."""
import glob, os, re, subprocess, sys, tempfile

R = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))     # twoc/
OUT = os.path.join(tempfile.gettempdir(), 'twoc_aofxc')
FXC = sorted(glob.glob(r'C:\Program Files (x86)\Windows Kits\10\bin\10.*\x64\fxc.exe'))[-1]
os.makedirs(OUT, exist_ok=True)
src = open(os.path.join(R, 'src', 'ao.c'), encoding='utf-8').read()


def cstring(name):
    m = re.search(r'static const char ' + name + r'\[\] =(.*?);\n', src, re.S)
    body = m.group(1)
    out = []
    for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', body):
        out.append(lit.encode('latin1').decode('unicode_escape'))
    return ''.join(out)


def fxc_patch(s):
    return re.sub(r'(?<![\w.])(\d+(?:\.\d+)?)\.(x{2,4})(?!\w)', r'(\1).\2', s)


open(os.path.join(OUT, 'XeGTAO.h'), 'w').write(open(os.path.join(R, 'third_party', 'xegtao', 'XeGTAO.h')).read())
open(os.path.join(OUT, 'XeGTAO.hlsli'), 'w').write(fxc_patch(open(os.path.join(R, 'third_party', 'xegtao', 'XeGTAO.hlsli')).read()))
open(os.path.join(OUT, 'cs.hlsl'), 'w').write(cstring('g_cs_hlsl'))
open(os.path.join(OUT, 'ps.hlsl'), 'w').write(cstring('g_ps_hlsl'))
jobs = [('cs.hlsl', e, 'cs_5_0', []) for e in ('CSPrefilterDepths16x16', 'CSGTAOHigh', 'CSSSAO', 'CSDenoiseLastPass')]
for ms in '01':
    for half in '01':
        for e in ('prep_ps', 'apply_ps'):
            jobs.append(('ps.hlsl', e, 'ps_5_0', ['/DTWOC_MS=' + ms, '/DTWOC_HALF=' + half]))
jobs.append(('ps.hlsl', 'vs', 'vs_5_0', ['/DTWOC_MS=0', '/DTWOC_HALF=0']))
bad = 0
for f, e, t, d in jobs:
    r = subprocess.run([FXC, '/nologo', '/O3', '/T', t, '/E', e] + d + ['/Fo', os.path.join(OUT, e + '.cso'), f],
                       cwd=OUT, capture_output=True, text=True)
    errs = [l for l in (r.stdout + r.stderr).splitlines() if 'error' in l or 'warning' in l]
    print(e, d, 'ok' if r.returncode == 0 else 'FAILED')
    for l in errs[:8]:
        print('   ', l)
    bad += r.returncode != 0
sys.exit(1 if bad else 0)
