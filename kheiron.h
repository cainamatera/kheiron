#ifndef KHEIRON_H
#define KHEIRON_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

uint64_t kheiron_rdtsc(void);
uint64_t kheiron_jitter_sample(void);
void kheiron_secure_wipe(void *v, size_t n);
int kheiron_fill(void *buf, size_t len);
int kheiron_string(char *dest, size_t len, const char *charset);
uint64_t kheiron_range_u64(uint64_t min, uint64_t max);
double kheiron_uniform_double(void);

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

static inline uint64_t kheiron_mix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

int kheiron_fill(void *buf, size_t len) {
    if (!buf || len == 0) {
        return -1;
    }

    uint64_t state = kheiron_jitter_sample();
    uint8_t *p = (uint8_t *)buf;

    for (size_t i = 0; i < len; i++) {
        uint64_t delta = kheiron_rdtsc();
        state = kheiron_mix64(state ^ delta);
        p[i] = (uint8_t)(state & 0xFFULL);
    }

    kheiron_secure_wipe(&state, sizeof(state));

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