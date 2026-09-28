"""Where, and how well, a loopback capture matches a game source file (notes 18.45).

  python srcmatch.py SRC.wav CAP.wav T0 T1 [--win=S] [--step=S] [--speeds=a,b,...] [--wav=OUT.wav]

SRC is a game .wav: Xbox ADPCM (format 0x69; 36 bytes per channel per 64
samples) or 16-bit PCM. CAP is a 16-bit capture (loopcap.exe). For each window
of CAP between T0 and T1 it prints the source time the window matches best
and the normalised correlation (mono, 1.0 = identical up to gain), once per
playback speed: 1.0 is the source played at its own rate; 48000/44100 (1.088)
is a 44.1 kHz source played unresampled at 48 kHz. --wav writes the decoded
source (for listening). A window matching its predecessor's source time plus
the step is continuous playback.
"""
import struct
import sys
import wave

import numpy as np

STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
        107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
        876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
        5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
        27086, 29794, 32767]
IDX = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]


def decode(path):
    """(samples[n, channels] float32, rate)."""
    b = open(path, "rb").read()
    fmt, ch, rate = struct.unpack_from("<HHI", b, 20)
    p = b.index(b"data") + 8
    n = struct.unpack_from("<I", b, p - 4)[0]
    d = b[p:p + n]
    if fmt == 1:
        return np.frombuffer(d, dtype="<i2").reshape(-1, ch).astype(np.float32) / 32768, rate
    assert fmt == 0x69, "format %#x" % fmt
    blk = 36 * ch
    nb = len(d) // blk
    out = np.zeros((nb * 64, ch), dtype=np.float32)
    for c in range(ch):
        k = 0
        for o in range(0, nb * blk, blk):
            pred, idx = struct.unpack_from("<hB", d, o + 4 * c)
            out[k, c] = pred
            k += 1
            got = 1
            for w in range(8):
                base = o + 4 * ch + 4 * (ch * w + c)
                for byte in d[base:base + 4]:
                    for nib in (byte & 15, byte >> 4):
                        st = STEP[idx]
                        diff = st >> 3
                        if nib & 1: diff += st >> 2
                        if nib & 2: diff += st >> 1
                        if nib & 4: diff += st
                        pred = pred - diff if nib & 8 else pred + diff
                        pred = max(-32768, min(32767, pred))
                        idx = max(0, min(88, idx + IDX[nib]))
                        if got < 64:           # 64 samples a block: the header's and 63 of the 64 nibbles
                            out[k, c] = pred
                            k += 1
                            got += 1
    return out / 32768, rate


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    opt = dict(a[2:].split("=", 1) for a in sys.argv[1:] if a.startswith("--") and "=" in a)
    src_path, cap_path, t0, t1 = args[0], args[1], float(args[2]), float(args[3])
    win = float(opt.get("win", 0.5))
    step = float(opt.get("step", 1.0))
    src, sr = decode(src_path)
    if "wav" in opt:
        w = wave.open(opt["wav"], "wb")
        w.setnchannels(src.shape[1]); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes((np.clip(src, -1, 1) * 32767).astype("<i2").tobytes()); w.close()
    speeds = [float(eval(s)) for s in opt.get("speeds", "1.0,48000/44100").split(",")]
    w = wave.open(cap_path)
    r = w.getframerate()
    cap = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").reshape(-1, w.getnchannels())
    cap = cap.astype(np.float32).mean(1) / 32768
    mono = src.mean(1)
    print("source %s: %.2f s, %d Hz, %d ch; capture %.1f s" % (src_path, len(mono) / sr, sr, src.shape[1], len(cap) / r))
    refs = []
    for k in speeds:
        # capture sample n plays source position n * sr * k / r
        pos = np.arange(0, (len(mono) - 1) * r / (sr * k)) * sr * k / r
        refs.append(np.interp(pos, np.arange(len(mono)), mono).astype(np.float64))
    print("  window   " + "".join("   speed %.3f: src t / corr" % k for k in speeds))
    a = t0
    while a < t1:
        seg = cap[int(a * r):int((a + win) * r)].astype(np.float64)
        if len(seg) < int(win * r) or np.abs(seg).max() < 1e-4:
            print("%7.1f s   silent" % a)
            a += step
            continue
        line = "%7.1f s " % a
        for k, ref in zip(speeds, refs):
            L = len(ref) + len(seg)
            nf = 1 << (L - 1).bit_length()
            c = np.fft.irfft(np.fft.rfft(ref, nf) * np.conj(np.fft.rfft(seg, nf)), nf)[:len(ref) - len(seg) + 1]
            cs = np.concatenate([[0], np.cumsum(ref ** 2)])
            e = np.sqrt(np.maximum(cs[len(seg):len(seg) + len(c)] - cs[:len(c)], 0)) * np.sqrt((seg ** 2).sum()) + 1e-12
            score = np.where(e > 0.01 * e.max(), np.abs(c) / e, 0)
            j = int(np.argmax(score))
            line += "   %24s" % ("%8.3f s / %.3f" % (j * k / r, score[j]))
        print(line)
        a += step


if __name__ == "__main__":
    main()
