#ifndef KHEIRON_H
#define KHEIRON_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

uint64_t kheiron_rdtsc(void);
uint64_t kheiron_jitter_sample(void);
int kheiron_fill(void *buf, size_t len);
int kheiron_string(char *dest, size_t len, const char *charset);
uint64_t kheiron_range_u64(uint64_t min, uint64_t max);
double kheiron_uniform_double(void);
void kheiron_secure_wipe(void *v, size_t n);

#ifdef KHEIRON_IMPLEMENTATION

uint64_t kheiron_rdtsc(void) {
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

uint64_t kheiron_jitter_sample(void) {
    uint64_t res = 0;
    for (int i = 0; i < 64; i++) {
        uint64_t t0 = kheiron_rdtsc();
        for (volatile int j = 0; j < 8; j++) {
            __asm__ __volatile__("" ::: "memory");
        }
        uint64_t t1 = kheiron_rdtsc();
        uint64_t bit = (t1 - t0) & 1ULL;
        res = (res << 1) | bit;
    }
    return res;
}

void kheiron_secure_wipe(void *v, size_t n) {
    if (!v || n == 0) {
        return;
    }
    volatile uint8_t *p = (volatile uint8_t *)v;
    while (n--) {
        *p++ = 0;
    }
    __asm__ __volatile__("" : : "r"(p) : "memory");
}

static inline uint32_t kheiron_rotl32(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

static inline void kheiron_chacha_qr(uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    *a += *b; *d ^= *a; *d = kheiron_rotl32(*d, 16);
    *c += *d; *b ^= *c; *b = kheiron_rotl32(*b, 12);
    *a += *b; *d ^= *a; *d = kheiron_rotl32(*d, 8);
    *c += *d; *b ^= *c; *b = kheiron_rotl32(*b, 7);
}

static void kheiron_chacha_block(const uint32_t in[16], uint8_t out[64]) {
    uint32_t x[16];
    for (int i = 0; i < 16; i++) {
        x[i] = in[i];
    }

    for (int i = 0; i < 10; i++) {
        kheiron_chacha_qr(&x[0], &x[4], &x[8], &x[12]);
        kheiron_chacha_qr(&x[1], &x[5], &x[9], &x[13]);
        kheiron_chacha_qr(&x[2], &x[6], &x[10], &x[14]);
        kheiron_chacha_qr(&x[3], &x[7], &x[11], &x[15]);

        kheiron_chacha_qr(&x[0], &x[5], &x[10], &x[15]);
        kheiron_chacha_qr(&x[1], &x[6], &x[11], &x[12]);
        kheiron_chacha_qr(&x[2], &x[7], &x[8], &x[13]);
        kheiron_chacha_qr(&x[3], &x[4], &x[9], &x[14]);
    }

    for (int i = 0; i < 16; i++) {
        uint32_t val = in[i] + x[i];
        out[i * 4 + 0] = (uint8_t)(val & 0xFF);
        out[i * 4 + 1] = (uint8_t)((val >> 8) & 0xFF);
        out[i * 4 + 2] = (uint8_t)((val >> 16) & 0xFF);
        out[i * 4 + 3] = (uint8_t)((val >> 24) & 0xFF);
    }

    kheiron_secure_wipe(x, sizeof(x));
}

int kheiron_fill(void *buf, size_t len) {
    if (!buf || len == 0) {
        return -1;
    }

    uint32_t ctx[16];
    ctx[0] = 0x61707865;
    ctx[1] = 0x3320646e;
    ctx[2] = 0x79622d32;
    ctx[3] = 0x6b206574;

    for (int i = 0; i < 4; i++) {
        uint64_t sample = kheiron_jitter_sample();
        ctx[4 + i * 2] = (uint32_t)(sample & 0xFFFFFFFF);
        ctx[4 + i * 2 + 1] = (uint32_t)(sample >> 32);
    }

    uint64_t t1 = kheiron_rdtsc();
    uint64_t t2 = kheiron_rdtsc();
    ctx[12] = (uint32_t)(t1 & 0xFFFFFFFF);
    ctx[13] = (uint32_t)(t1 >> 32);
    ctx[14] = (uint32_t)(t2 & 0xFFFFFFFF);
    ctx[15] = (uint32_t)(t2 >> 32);

    uint8_t *p = (uint8_t *)buf;
    size_t remaining = len;
    uint8_t keystream[64];

    while (remaining > 0) {
        kheiron_chacha_block(ctx, keystream);

        ctx[12]++;
        if (ctx[12] == 0) {
            ctx[13]++;
        }

        size_t chunk = remaining < 64 ? remaining : 64;
        for (size_t i = 0; i < chunk; i++) {
            p[i] = keystream[i];
        }

        p += chunk;
        remaining -= chunk;
    }

    kheiron_secure_wipe(keystream, sizeof(keystream));
    kheiron_secure_wipe(ctx, sizeof(ctx));

    return 0;
}

int kheiron_string(char *dest, size_t len, const char *charset) {
    if (!dest || len == 0 || !charset) {
        return -1;
    }

    size_t c_len = strlen(charset);
    if (c_len == 0 || c_len > 256) {
        return -1;
    }

    size_t limit = 256 - (256 % c_len);

    for (size_t i = 0; i < len; i++) {
        uint8_t byte;
        do {
            if (kheiron_fill(&byte, 1) != 0) {
                return -1;
            }
        } while ((size_t)byte >= limit);

        dest[i] = charset[byte % c_len];
    }

    dest[len] = '\0';
    return 0;
}

uint64_t kheiron_range_u64(uint64_t min, uint64_t max) {
    if (min >= max) {
        return min;
    }

    uint64_t range = max - min + 1ULL;
    if (range == 0) {
        uint64_t val;
        kheiron_fill(&val, sizeof(val));
        return val;
    }

    uint64_t limit = UINT64_MAX - (UINT64_MAX % range);
    uint64_t val;
    do {
        kheiron_fill(&val, sizeof(val));
    } while (val >= limit);

    return min + (val % range);
}

double kheiron_uniform_double(void) {
    uint64_t val;
    kheiron_fill(&val, sizeof(val));
    return (double)(val >> 11) / (double)(1ULL << 53);
}

#endif

#endif