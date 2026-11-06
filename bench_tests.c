#define _POSIX_C_SOURCE 199309L
#define KHEIRON_IMPLEMENTATION
#include "kheiron.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <pthread.h>

#define THREAD_COUNT 8
#define THREAD_BUF_SIZE 65536

typedef struct {
    int thread_id;
    uint8_t buffer[THREAD_BUF_SIZE];
    uint8_t ctx_buffer[THREAD_BUF_SIZE];
    int success;
} thread_data_t;

static void *thread_worker(void *arg) {
    thread_data_t *data = (thread_data_t *)arg;
    data->success = 0;

    if (kheiron_fill(data->buffer, sizeof(data->buffer)) != 0) {
        return NULL;
    }

    kheiron_ctx_t ctx;
    uint8_t seed[32];
    for (size_t i = 0; i < sizeof(seed); i++) {
        seed[i] = (uint8_t)(data->thread_id * 31 + i);
    }

    if (kheiron_ctx_init(&ctx, seed, sizeof(seed)) != 0) {
        return NULL;
    }

    if (kheiron_ctx_fill(&ctx, data->ctx_buffer, sizeof(data->ctx_buffer)) != 0) {
        kheiron_ctx_wipe(&ctx);
        return NULL;
    }

    kheiron_ctx_wipe(&ctx);
    data->success = 1;
    return NULL;
}

int main(int argc, char *argv[]) {
    int failed = 0;
    int is_strict = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--strict") == 0) {
            is_strict = 1;
        }
    }

    uint64_t n_samples = is_strict ? 10000000ULL : 100000ULL;
    double expected = (double)n_samples / 100.0;
    double chi2_threshold = 135.807;

    static uint64_t bins[100];
    for (int i = 0; i < 100; i++) {
        bins[i] = 0;
    }

    for (uint64_t i = 0; i < n_samples; i++) {
        uint64_t val = kheiron_range_u64(0, 99);
        bins[val]++;
    }

    double chi2 = 0.0;
    for (int i = 0; i < 100; i++) {
        double diff = (double)bins[i] - expected;
        chi2 += (diff * diff) / expected;
    }

    printf("Chi-Square: %.4f (samples: %llu, threshold: %.3f) -> ",
           chi2, (unsigned long long)n_samples, chi2_threshold);
    if (chi2 <= chi2_threshold) {
        printf("APROVADO\n");
    } else {
        printf("REPROVADO\n");
        failed = 1;
    }

    uint8_t user_entropy[32] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
        0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
        0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20
    };
    int reseed_res = kheiron_reseed(user_entropy, sizeof(user_entropy));
    printf("kheiron_reseed: %s\n", reseed_res == 0 ? "APROVADO" : "REPROVADO");
    if (reseed_res != 0) {
        failed = 1;
    }

    size_t rotate_test_size = 2 * 1024 * 1024;
    uint8_t *rotate_buf = (uint8_t *)malloc(rotate_test_size);
    if (!rotate_buf) {
        return 1;
    }
    int fill_rotate_res = kheiron_fill(rotate_buf, rotate_test_size);
    printf("kheiron_fill (>1MB rotation): %s\n", fill_rotate_res == 0 ? "APROVADO" : "REPROVADO");
    if (fill_rotate_res != 0) {
        failed = 1;
    }
    free(rotate_buf);

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
    printf("kheiron_fill (10MB): %.2f MB/s (elapsed: %.4f s)\n", mb_per_sec, elapsed_fill);
    free(buf);

    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    char str_buf[33];
    uint64_t n_strings = is_strict ? 100000ULL : 10000ULL;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (uint64_t i = 0; i < n_strings; i++) {
        kheiron_string(str_buf, 32, charset);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed_str = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    double kstr_per_sec = (n_strings / 1000.0) / elapsed_str;
    printf("kheiron_string (%llu): %.2f kstrings/sec (elapsed: %.4f s)\n",
           (unsigned long long)n_strings, kstr_per_sec, elapsed_str);

    uint64_t fp_samples = is_strict ? 10000000ULL : 100000ULL;
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
    printf("kheiron_uniform_double (%llu): %.2f Mdoubles/sec (elapsed: %.4f s) -> %s\n",
           (unsigned long long)fp_samples, mdoubles_per_sec, elapsed_double,
           double_bounds_ok ? "APROVADO [0.0, 1.0)" : "REPROVADO");
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
    printf("kheiron_uniform_float (%llu): %.2f Mfloats/sec (elapsed: %.4f s) -> %s\n",
           (unsigned long long)fp_samples, mfloats_per_sec, elapsed_float,
           float_bounds_ok ? "APROVADO [0.0f, 1.0f)" : "REPROVADO");
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

    uint64_t gauss_samples = is_strict ? 1000000ULL : 50000ULL;
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

    printf("kheiron_gaussian (%llu): %.2f M/sec (elapsed: %.4f s) -> mean: %.4f, variance: %.4f\n",
           (unsigned long long)gauss_samples, mgauss_per_sec, elapsed_gauss, mean, variance);

    pthread_t threads[THREAD_COUNT];
    thread_data_t thread_data[THREAD_COUNT];

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < THREAD_COUNT; i++) {
        thread_data[i].thread_id = i;
        pthread_create(&threads[i], NULL, thread_worker, &thread_data[i]);
    }

    int threads_ok = 1;
    for (int i = 0; i < THREAD_COUNT; i++) {
        pthread_join(threads[i], NULL);
        if (!thread_data[i].success) {
            threads_ok = 0;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    for (int i = 0; i < THREAD_COUNT; i++) {
        int all_zeros = 1;
        for (size_t b = 0; b < sizeof(thread_data[i].buffer); b++) {
            if (thread_data[i].buffer[b] != 0) {
                all_zeros = 0;
                break;
            }
        }
        if (all_zeros) {
            threads_ok = 0;
        }
        for (int j = i + 1; j < THREAD_COUNT; j++) {
            if (memcmp(thread_data[i].buffer, thread_data[j].buffer, sizeof(thread_data[i].buffer)) == 0) {
                threads_ok = 0;
            }
            if (memcmp(thread_data[i].ctx_buffer, thread_data[j].ctx_buffer, sizeof(thread_data[i].ctx_buffer)) == 0) {
                threads_ok = 0;
            }
        }
    }

    double elapsed_threads = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("kheiron_threads (%d threads): %s (elapsed: %.6f s)\n",
           THREAD_COUNT, threads_ok ? "APROVADO" : "REPROVADO", elapsed_threads);
    if (!threads_ok) {
        failed = 1;
    }

    size_t nist_bits = is_strict ? 10000000ULL : 1000000ULL;
    size_t nist_bytes = nist_bits / 8;
    uint8_t *nist_buf = (uint8_t *)malloc(nist_bytes);
    if (!nist_buf) {
        return 1;
    }

    clock_gettime(CLOCK_MONOTONIC, &start);
    kheiron_fill(nist_buf, nist_bytes);

    int64_t sn = 0;
    size_t total_ones = 0;
    for (size_t i = 0; i < nist_bytes; i++) {
        uint8_t b = nist_buf[i];
        for (int bit = 0; bit < 8; bit++) {
            if ((b >> bit) & 1) {
                sn++;
                total_ones++;
            } else {
                sn--;
            }
        }
    }

    double sobs = fabs((double)sn) / sqrt((double)nist_bits);
    double p_val_monobit = erfc(sobs / sqrt(2.0));
    int monobit_ok = (p_val_monobit >= 0.01);

    double pi = (double)total_ones / (double)nist_bits;
    int runs_ok = 1;
    double p_val_runs = 0.0;

    if (fabs(pi - 0.5) >= (2.0 / sqrt((double)nist_bits))) {
        runs_ok = 0;
    } else {
        size_t vn = 1;
        uint8_t prev_bit = nist_buf[0] & 1;
        for (size_t i = 0; i < nist_bytes; i++) {
            uint8_t b = nist_buf[i];
            int start_bit = (i == 0) ? 1 : 0;
            for (int bit = start_bit; bit < 8; bit++) {
                uint8_t curr_bit = (b >> bit) & 1;
                if (curr_bit != prev_bit) {
                    vn++;
                    prev_bit = curr_bit;
                }
            }
        }

        double num = fabs((double)vn - 2.0 * (double)nist_bits * pi * (1.0 - pi));
        double den = 2.0 * sqrt(2.0 * (double)nist_bits) * pi * (1.0 - pi);
        p_val_runs = erfc(num / den);
        runs_ok = (p_val_runs >= 0.01);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed_nist = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("NIST Monobit (%zu bits): p-val = %.6f -> %s (elapsed: %.4f s)\n",
           nist_bits, p_val_monobit, monobit_ok ? "APROVADO" : "REPROVADO", elapsed_nist);
    printf("NIST Runs (%zu bits): p-val = %.6f, pi = %.6f -> %s\n",
           nist_bits, p_val_runs, pi, runs_ok ? "APROVADO" : "REPROVADO");

    if (!monobit_ok || !runs_ok) {
        failed = 1;
    }

    free(nist_buf);

    return failed ? 1 : 0;
}
