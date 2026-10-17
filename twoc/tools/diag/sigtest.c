/* sigtest.c -- step 84: content_sig (new, d3d_bridge.c's sig_span) against the old byte-wise FNV-1a on random
 * textures and single changes: a byte, an 8-byte word, a short run, at random places, sizes up to and past 256 KB
 * (the sampled form). Counts changes the old one saw and the new one missed, and the reverse; and, for the sampled
 * form, changes outside the samples (both must miss them alike). The functions are copied from d3d_bridge.c. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SIG_P1 0x9E3779B185EBCA87ull
#define SIG_P2 0xC2B2AE3D27D4EB4Full
#define SIG_ROT(x, r) (((x) << (r)) | ((x) >> (64 - (r))))

static uint64_t sig_span(const uint8_t *p, uint32_t n, uint64_t seed)
{
    uint64_t a = seed + SIG_P1 + SIG_P2, b = seed + SIG_P2, c = seed, d = seed - SIG_P1, h, w;
    uint32_t i = 0;

    for (; i + 32 <= n; i += 32) {
        memcpy(&w, p + i, 8);
        a = SIG_ROT(a + w * SIG_P2, 31) * SIG_P1;
        memcpy(&w, p + i + 8, 8);
        b = SIG_ROT(b + w * SIG_P2, 31) * SIG_P1;
        memcpy(&w, p + i + 16, 8);
        c = SIG_ROT(c + w * SIG_P2, 31) * SIG_P1;
        memcpy(&w, p + i + 24, 8);
        d = SIG_ROT(d + w * SIG_P2, 31) * SIG_P1;
    }
    h = SIG_ROT(a, 1) + SIG_ROT(b, 7) + SIG_ROT(c, 12) + SIG_ROT(d, 18) + n;
    for (; i < n; i++)
        h = (h ^ p[i]) * 0x100000001B3ull;
    h ^= h >> 33;
    h *= SIG_P2;
    h ^= h >> 29;
    h *= SIG_P1;
    return h ^ (h >> 32);
}

static uint32_t content_sig(const uint8_t *p, uint32_t n)
{
    uint64_t h;
    uint32_t i;

    if (n <= 256 * 1024) {
        h = sig_span(p, n, 0);
    } else {
        h = sig_span(p, 4096, 0);
        for (i = 0; i < 64; i++)
            h = sig_span(p + (uint64_t)(n - 64) * i / 64, 64, h);
        h = sig_span(p + n - 4096, 4096, h);
    }
    return (uint32_t)h ? (uint32_t)h : 1;
}

static uint32_t content_sig_old(const uint8_t *p, uint32_t n)
{
    uint32_t h = 2166136261u, i;

#define MIX(ptr, len) do { const uint8_t *q_ = (ptr); uint32_t k_; \
        for (k_ = 0; k_ < (len); k_++) h = (h ^ q_[k_]) * 16777619u; } while (0)
    if (n <= 256 * 1024) {
        MIX(p, n);
    } else {
        MIX(p, 4096);
        for (i = 0; i < 64; i++)
            MIX(p + (uint64_t)(n - 64) * i / 64, 64);
        MIX(p + n - 4096, 4096);
    }
#undef MIX
    return h ? h : 1;
}

static uint64_t rs = 88172645463325252ull;
static uint64_t rnd(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 7;
    rs ^= rs << 17;
    return rs;
}

int main(void)
{
    static const uint32_t sizes[] = { 4, 31, 32, 33, 1024, 4096, 65536, 65537, 262144, 262145, 524288, 1398104 };
    unsigned long long trials = 0, old_saw = 0, new_saw = 0, old_only = 0, new_only = 0, same = 0;
    unsigned s, t;
    uint8_t *buf = (uint8_t *)malloc(1398104 + 64);

    for (s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
        uint32_t n = sizes[s], reps = n > 262144 ? 3000 : 20000;
        for (t = 0; t < reps; t++) {
            uint32_t o0, n0, o1, n1, at, len, k, kind = (uint32_t)(rnd() % 3);
            if (t % 64 == 0)
                for (k = 0; k < n; k++)
                    buf[k] = (uint8_t)rnd();
            o0 = content_sig_old(buf, n);
            n0 = content_sig(buf, n);
            at = (uint32_t)(rnd() % n);
            len = kind == 0 ? 1 : kind == 1 ? 8 : 1 + (uint32_t)(rnd() % 64);
            if (at + len > n)
                len = n - at;
            for (k = 0; k < len; k++)
                buf[at + k] ^= (uint8_t)(1 + rnd() % 255);
            o1 = content_sig_old(buf, n);
            n1 = content_sig(buf, n);
            trials++;
            old_saw += o0 != o1;
            new_saw += n0 != n1;
            old_only += o0 != o1 && n0 == n1;
            new_only += o0 == o1 && n0 != n1;
            same += (o0 != o1) == (n0 != n1);
        }
    }
    printf("%llu changes: the old hash saw %llu, the new %llu; old only %llu, new only %llu; agree %llu\n", trials,
           old_saw, new_saw, old_only, new_only, same);
    return old_only != 0;
}
