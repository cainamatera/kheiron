#define _POSIX_C_SOURCE 199309L
#define KHEIRON_IMPLEMENTATION
#include "kheiron.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

int main(void) {
    int failed = 0;

    static uint64_t bins[100];
    uint64_t n_samples = 10000000ULL;
    double expected = 100000.0;

    for (uint64_t i = 0; i < n_samples; i++) {
        uint64_t val = kheiron_range_u64(0, 99);
        bins[val]++;
    }

    double chi2 = 0.0;
    for (int i = 0; i < 100; i++) {
        double diff = (double)bins[i] - expected;
        chi2 += (diff * diff) / expected;
    }

    printf("Chi-Square: %.4f (threshold: 135.807) -> ", chi2);
    if (chi2 <= 135.807) {
        printf("APROVADO\n");
    } else {
        printf("REPROVADO\n");
        failed = 1;
    }

    size_t buf_size = 10 * 1024 * 1024;
    uint8_t *buf = (uint8_t *)malloc(buf_size);
    if (!buf) {
        return 1;
    }

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    kheiron_fill(buf, buf_size);
    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed_fill = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    double mb_per_sec = 10.0 / elapsed_fill;
    printf("kheiron_fill: %.2f MB/s (elapsed: %.4f s)\n", mb_per_sec, elapsed_fill);
    free(buf);

    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    char str_buf[33];
    uint64_t n_strings = 100000ULL;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (uint64_t i = 0; i < n_strings; i++) {
        kheiron_string(str_buf, 32, charset);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed_str = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    double kstr_per_sec = (n_strings / 1000.0) / elapsed_str;
    printf("kheiron_string: %.2f kstrings/sec (elapsed: %.4f s)\n", kstr_per_sec, elapsed_str);

    return failed ? 1 : 0;
}
