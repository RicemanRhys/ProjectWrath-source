"""wavlevel.py WAV...: each file's RMS and peak in dBFS (32-bit float or 16-bit PCM), and the loudest second."""
import sys, wave, struct
import numpy as np
for f in sys.argv[1:]:
    raw = open(f, 'rb').read()
    # find fmt and data chunks
    i = 12
    fmt = None
    data = None
    while i + 8 <= len(raw):
        cid = raw[i:i + 4]
        n = struct.unpack('<I', raw[i + 4:i + 8])[0]
        if cid == b'fmt ':
            fmt = struct.unpack('<HHIIHH', raw[i + 8:i + 24])
        elif cid == b'data':
            data = raw[i + 8:i + 8 + n]
        i += 8 + n + (n & 1)
    tag, ch, rate, _, _, bits = fmt
    if bits == 32 and tag in (3, 0xFFFE):
        x = np.frombuffer(data, dtype='<f4')
    else:
        x = np.frombuffer(data, dtype='<i2').astype(np.float32) / 32768.0
    x = x[:len(x) // ch * ch].reshape(-1, ch)
    m = np.sqrt(np.mean(x.astype(np.float64) ** 2)) if len(x) else 0
    pk = np.max(np.abs(x)) if len(x) else 0
    sec = [np.sqrt(np.mean(x[k:k + rate].astype(np.float64) ** 2)) for k in range(0, len(x), rate)]
    db = lambda v: 20 * np.log10(v) if v > 0 else -999
    print('%s: %.1f s, RMS %.1f dBFS, peak %.1f dBFS, loudest second %.1f dBFS' % (f, len(x) / rate, db(m), db(pk),
          db(max(sec)) if sec else -999))
