#ifndef KHEIRON_H
#define KHEIRON_H

#include <stddef.h>
#include <stdint.h>

uint64_t kheiron_rdtsc(void);
uint64_t kheiron_jitter_sample(void);
int kheiron_fill(void *buf, size_t len);

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

#endif

#endif