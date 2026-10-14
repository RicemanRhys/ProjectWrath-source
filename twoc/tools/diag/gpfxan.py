"""gpfxan.py: read gpfx.exe's output (float32, bins 0-5 after the GP, a sample a row; notes 18.75).

  gpfxan.py ir OUT.f32 AMP [T0]         impulse response of a --synth impulse run: per output (0 FL, 1 FR,
                                        4 BL, 5 BR) energy gain in dB, first arrival, Schroeder decay
                                        (EDT, T20, T30 as RT60), the octave-band levels
  gpfxan.py noise OUT.f32 RMS           steady-state gain of a --synth noise run (last 5 s), per output
  gpfxan.py wav BINS FROM TO WET.f32 DRY.f32 OUTDIR NAME [XTLK_DB]
                                        listening WAVs from a bin dump stretch: NAME_dry.wav (the GP's
                                        bins 0/1 with the reverb's input zeroed: DRY, a --zero 10 run),
                                        NAME_wet.wav (the GP's bins 0/1: WET), NAME_reverb_x4.wav (WET
                                        minus DRY, the reverb alone, +12 dB), NAME_ours.wav (today's
                                        mixdown: bins 0/1 + the HRTF bins at XTLK_DB, default -2.6);
                                        level lines, and the A-weighted reverb-to-mix ratio in 50 ms windows
The impulse sits at T0 seconds (gpfx --t0, default 0.02: frame 30).
"""
import sys, wave
import numpy as np

FS = 48000
OUTS = {0: 'FL', 1: 'FR', 4: 'BL', 5: 'BR'}


def load(p):
    return np.fromfile(p, np.float32).reshape(-1, 6).astype(np.float64)


def db(x):
    return 10 * np.log10(x) if x > 0 else -199.0


def decay(h):
    """Schroeder backward integral of h^2 (dB), EDT and T20/T30 extrapolated to 60 dB."""
    e = np.cumsum((h ** 2)[::-1])[::-1]
    if e[0] <= 0:
        return None
    edc = 10 * np.log10(e / e[0] + 1e-30)

    def fit(lo, hi):
        i = np.nonzero((edc <= lo) & (edc >= hi))[0]
        if len(i) < 10:
            return float('nan')
        t = i / FS
        k = np.polyfit(t, edc[i], 1)[0]
        return -60.0 / k if k < 0 else float('nan')
    return fit(0, -10), fit(-5, -25), fit(-5, -35)


def ir(path, amp, t0=0.02):
    o = load(path)[int(round(t0 * 1500)) * 32:] / amp
    tot = np.zeros(len(o))
    for c, nm in OUTS.items():
        h = o[:, c]
        e = (h ** 2).sum()
        if e == 0:
            print('  %s: silent' % nm)
            continue
        nz = np.nonzero(np.abs(h) > 1e-7)[0]
        edt, t20, t30 = decay(h)
        early = (h[:int(0.08 * FS) + nz[0]] ** 2).sum()
        print('  %s: gain %6.1f dB  first %5.1f ms  early(80 ms) %5.1f%%  EDT %.2f s  T20 %.2f s  T30 %.2f s  peak %.4f'
              % (nm, db(e), nz[0] / FS * 1e3, 100 * early / e, edt, t20, t30, np.abs(h).max()))
        if c in (0, 1):
            tot += h ** 2
    if tot.sum() > 0:
        H = np.fft.rfft(o[:, 0], 1 << 19)
        H2 = np.fft.rfft(o[:, 1], 1 << 19)
        f = np.fft.rfftfreq(1 << 19, 1 / FS)
        p = (np.abs(H) ** 2 + np.abs(H2) ** 2) / 2
        bands = [125, 250, 500, 1000, 2000, 4000, 8000, 16000]
        s = []
        for b in bands:
            m = (f >= b / np.sqrt(2)) & (f < b * np.sqrt(2))
            s.append('%d:%.1f' % (b, db(p[m].mean())))
        print('  front pair power gain %.1f dB (mean of FL, FR); octave bands (dB re flat 0 dB): %s'
              % (db(tot.sum() / 2), ' '.join(s)))


def noise(path, rms):
    o = load(path)[-5 * FS:]
    for c, nm in OUTS.items():
        g = np.sqrt((o[:, c] ** 2).mean()) / rms
        print('  %s: %.1f dB' % (nm, 20 * np.log10(g) if g > 0 else -199))


def wav_write(path, l, r, scale=1.0):
    x = np.stack([l, r], 1) * scale
    pk = np.abs(x).max()
    x = np.clip(x, -1, 1)
    w = wave.open(path, 'wb')
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(FS)
    w.writeframes((x * 32767).astype('<i2').tobytes())
    w.close()
    return pk


def rms_db(x):
    return 20 * np.log10(np.sqrt((x ** 2).mean()) + 1e-20)


def aweight(x):
    """IEC A-weighting, applied in the frequency domain."""
    X = np.fft.rfft(x)
    f2 = np.fft.rfftfreq(len(x), 1 / FS) ** 2
    ra = (12194 ** 2 * f2 * f2) / ((f2 + 20.6 ** 2) * np.sqrt((f2 + 107.7 ** 2) * (f2 + 737.9 ** 2))
                                   * (f2 + 12194 ** 2) + 1e-30)
    return np.fft.irfft(X * ra * 10 ** (2.0 / 20), len(x))


def wavs(bins, t_from, t_to, wet, dry, outdir, name, xtlk_db=-2.6):
    b = np.fromfile(bins, np.float32).reshape(-1, 8).astype(np.float64)   # 0,1,6,7,8,9,10,11
    b = b[int(t_from * FS):int(t_to * FS)]
    gw = load(wet)[:len(b)]
    gd = load(dry)[:len(b)]
    b = b[:len(gw)]
    gx = 10 ** (xtlk_db / 20)
    c3 = 0.70710678
    ours_l = b[:, 0] + gx * (b[:, 2] + b[:, 4])       # HRTF submixes 6 8 left, 7 9 right
    ours_r = b[:, 1] + gx * (b[:, 3] + b[:, 5])
    gp_l, gp_r = gw[:, 0], gw[:, 1]
    fold_l, fold_r = gw[:, 0] + c3 * gw[:, 4], gw[:, 1] + c3 * gw[:, 5]
    rv_l, rv_r = gw[:, 0] - gd[:, 0], gw[:, 1] - gd[:, 1]
    rv_bl, rv_br = gw[:, 4] - gd[:, 4], gw[:, 5] - gd[:, 5]
    d3 = np.concatenate([b[:, 2], b[:, 3]])
    print('%s: %.1f s; levels (dBFS rms): speaker bins 0/1 %.1f, 3D bins 6/7 %.1f, bin 10 %.1f; '
          'ours %.1f, GP %.1f, GP+fold %.1f; reverb alone front %.1f (rear %.1f): %.1f dB re the 3D dry sound, '
          '%.1f dB re the whole GP mix'
          % (name, len(b) / FS, rms_db(np.concatenate([b[:, 0], b[:, 1]])), rms_db(d3), rms_db(b[:, 6]),
             rms_db(np.concatenate([ours_l, ours_r])), rms_db(np.concatenate([gp_l, gp_r])),
             rms_db(np.concatenate([fold_l, fold_r])), rms_db(np.concatenate([rv_l, rv_r])),
             rms_db(np.concatenate([rv_bl, rv_br])),
             rms_db(np.concatenate([rv_l, rv_r])) - rms_db(d3),
             rms_db(np.concatenate([rv_l, rv_r])) - rms_db(np.concatenate([gp_l, gp_r]))))
    # A-weighted, in 50 ms windows: how close the reverb comes to the whole mix
    wl, wr, ml, mr = aweight(rv_l), aweight(rv_r), aweight(gp_l), aweight(gp_r)
    W = 2400
    k = len(wl) // W
    we = (wl[:k * W] ** 2 + wr[:k * W] ** 2).reshape(k, W).mean(1)
    me = (ml[:k * W] ** 2 + mr[:k * W] ** 2).reshape(k, W).mean(1)
    r = 10 * np.log10(we / (me + 1e-30) + 1e-30)
    print('  A-weighted reverb re the mix: overall %.1f dB; 50 ms windows median %.1f, 90th percentile %.1f, '
          'loudest %.1f dB; windows within 10 dB %.1f%%, within 15 dB %.1f%%'
          % (10 * np.log10(we.mean() / me.mean()), np.median(r), np.percentile(r, 90), r.max(),
             100 * (r > -10).mean(), 100 * (r > -15).mean()))
    out = {}
    for tag, l, r_, s in (('dry', gd[:, 0], gd[:, 1], 1), ('wet', gp_l, gp_r, 1), ('reverb_x4', rv_l, rv_r, 4),
                          ('ours', ours_l, ours_r, 1)):
        l, r = l, r_
        p = '%s/%s_%s.wav' % (outdir, name, tag)
        pk = wav_write(p, l, r, s)
        out[tag] = p
        print('  %s (peak %.3f%s)' % (p, pk, ', clipped' if pk > 1 else ''))
    return out


if __name__ == '__main__':
    a = sys.argv
    if a[1] == 'ir':
        ir(a[2], float(a[3]), float(a[4]) if len(a) > 4 else 0.02)
    elif a[1] == 'noise':
        noise(a[2], float(a[3]))
    elif a[1] == 'wav':
        wavs(a[2], float(a[3]), float(a[4]), a[5], a[6], a[7], a[8], float(a[9]) if len(a) > 9 else -2.6)
