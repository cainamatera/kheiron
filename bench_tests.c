#define _POSIX_C_SOURCE 199309L
#define KHEIRON_IMPLEMENTATION
#include "kheiron.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <math.h>

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

    uint64_t fp_samples = 10000000ULL;
    int double_bounds_ok = 1;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (uint64_t i = 0; i < fp_samples; i++) {
        double d = kheiron_uniform_double();
        if (d < 0.0 || d >= 1.0) {
            double_bounds_ok = 0;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed_double = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    double mdoubles_per_sec = (fp_samples / 1e6) / elapsed_double;
    printf("kheiron_uniform_double: %.2f Mdoubles/sec (elapsed: %.4f s) -> %s\n",
           mdoubles_per_sec, elapsed_double, double_bounds_ok ? "APROVADO [0.0, 1.0)" : "REPROVADO");
    if (!double_bounds_ok) {
        failed = 1;
    }

    int float_bounds_ok = 1;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (uint64_t i = 0; i < fp_samples; i++) {
        float f = kheiron_uniform_float();
        if (f < 0.0f || f >= 1.0f) {
            float_bounds_ok = 0;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed_float = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    double mfloats_per_sec = (fp_samples / 1e6) / elapsed_float;
    printf("kheiron_uniform_float: %.2f Mfloats/sec (elapsed: %.4f s) -> %s\n",
           mfloats_per_sec, elapsed_float, float_bounds_ok ? "APROVADO [0.0f, 1.0f)" : "REPROVADO");
    if (!float_bounds_ok) {
        failed = 1;
    }

    int shuffle_arr[10000];
    for (int i = 0; i < 10000; i++) {
        shuffle_arr[i] = i;
    }

    clock_gettime(CLOCK_MONOTONIC, &start);
    kheiron_shuffle(shuffle_arr, 10000, sizeof(int));
    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed_shuffle = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    double shuffle_rate = 10000.0 / elapsed_shuffle;

    static uint8_t seen[10000];
    int shuffle_ok = 1;
    for (int i = 0; i < 10000; i++) {
        int v = shuffle_arr[i];
        if (v < 0 || v >= 10000 || seen[v]) {
            shuffle_ok = 0;
            break;
        }
        seen[v] = 1;
    }

    printf("kheiron_shuffle: %.2f elem/sec (elapsed: %.6f s) -> %s\n",
           shuffle_rate, elapsed_shuffle, shuffle_ok ? "APROVADO" : "REPROVADO");
    if (!shuffle_ok) {
        failed = 1;
    }

    uint64_t gauss_samples = 1000000ULL;
    double sum = 0.0;
    double sum_sq = 0.0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (uint64_t i = 0; i < gauss_samples; i++) {
        double g = kheiron_gaussian(0.0, 1.0);
        sum += g;
        sum_sq += g * g;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed_gauss = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    double mean = sum / (double)gauss_samples;
    double variance = (sum_sq - (sum * sum) / (double)gauss_samples) / (double)(gauss_samples - 1);
    double mgauss_per_sec = (gauss_samples / 1e6) / elapsed_gauss;

    printf("kheiron_gaussian: %.2f M/sec (elapsed: %.4f s) -> mean: %.4f, variance: %.4f\n",
           mgauss_per_sec, elapsed_gauss, mean, variance);

    return failed ? 1 : 0;
}
