#ifndef KHEIRON_H
#define KHEIRON_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

uint64_t kheiron_rdtsc(void);
uint64_t kheiron_jitter_sample(void);
int kheiron_fill(void *buf, size_t len);
int kheiron_string(char *dest, size_t len, const char *charset);

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

    state = 0;
    __asm__ __volatile__("" : "+r"(state));

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

#endif

#endif