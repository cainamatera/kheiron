#if !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#define KHEIRON_IMPLEMENTATION
#include "kheiron.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <pthread.h>
#if !defined(_WIN32) && !defined(_WIN64)
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif

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

static int simulate_static_linkage(void) {
    uint8_t s_buf[32];
    if (kheiron_selftest() != 0) {
        return -1;
    }
    if (kheiron_fill(s_buf, sizeof(s_buf)) != 0) {
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    int failed = 0;
    int is_strict = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--strict") == 0) {
            is_strict = 1;
        }
    }

    int selftest_res = kheiron_selftest();
    printf("kheiron_selftest: %s\n", selftest_res == 0 ? "APROVADO" : "REPROVADO");
    if (selftest_res != 0) {
        failed = 1;
    }

    int static_link_res = simulate_static_linkage();
    printf("KHEIRON_STATIC linkage simulation: %s\n", static_link_res == 0 ? "APROVADO" : "REPROVADO");
    if (static_link_res != 0) {
        failed = 1;
    }

#if !defined(_WIN32) && !defined(_WIN64)
    int fork_ok = 0;
    int fds[2];
    if (pipe(fds) == 0) {
        pid_t pid = fork();
        if (pid == 0) {
            close(fds[0]);
            uint8_t child_bytes[64];
            kheiron_fill(child_bytes, sizeof(child_bytes));
            ssize_t w = write(fds[1], child_bytes, sizeof(child_bytes));
            close(fds[1]);
            (void)w;
            exit(0);
        } else if (pid > 0) {
            close(fds[1]);
            uint8_t parent_bytes[64];
            uint8_t child_bytes[64];
            kheiron_fill(parent_bytes, sizeof(parent_bytes));
            ssize_t r = read(fds[0], child_bytes, sizeof(child_bytes));
            close(fds[0]);
            int status = 0;
            waitpid(pid, &status, 0);
            if (r == (ssize_t)sizeof(child_bytes) && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                if (kheiron_consttime_cmp(parent_bytes, child_bytes, sizeof(parent_bytes)) != 0) {
                    fork_ok = 1;
                }
            }
        } else {
            close(fds[0]);
            close(fds[1]);
        }
    }
    printf("Fork Safety: %s\n", fork_ok ? "APROVADO" : "REPROVADO");
    if (!fork_ok) {
        failed = 1;
    }
#endif

    uint64_t n_samples = is_strict ? 10000000ULL : 100000ULL;
    double expected = (double)n_samples / 100.0;
    double chi2_threshold = 135.807;

    double chi2 = 0.0;
    int chi2_pass = 0;
    for (int attempt = 0; attempt < 3; attempt++) {
        static uint64_t bins[100];
        for (int i = 0; i < 100; i++) {
            bins[i] = 0;
        }

        for (uint64_t i = 0; i < n_samples; i++) {
            uint64_t val = kheiron_range_u64(0, 99);
            bins[val]++;
        }

        chi2 = 0.0;
        for (int i = 0; i < 100; i++) {
            double diff = (double)bins[i] - expected;
            chi2 += (diff * diff) / expected;
        }

        if (chi2 <= chi2_threshold) {
            chi2_pass = 1;
            break;
        }
    }

    printf("Chi-Square: %.4f (samples: %llu, threshold: %.3f) -> ",
           chi2, (unsigned long long)n_samples, chi2_threshold);
    if (chi2_pass) {
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

    uint32_t rfc_state[16] = {
        0x61707865, 0x3320646e, 0x79622d32, 0x6b206574,
        0x03020100, 0x07060504, 0x0b0a0908, 0x0f0e0d0c,
        0x13121110, 0x17161514, 0x1b1a1918, 0x1f1e1d1c,
        0x00000001, 0x09000000, 0x4a000000, 0x00000000
    };
    uint8_t rfc_out[64];
    kheiron_chacha_block(rfc_state, rfc_out);

    const uint8_t rfc_expected[64] = {
        0x10, 0xf1, 0xe7, 0xe4, 0xd1, 0x3b, 0x59, 0x15,
        0x50, 0x0f, 0xdd, 0x1f, 0xa3, 0x20, 0x71, 0xc4,
        0xc7, 0xd1, 0xf4, 0xc7, 0x33, 0xc0, 0x68, 0x03,
        0x04, 0x22, 0xaa, 0x9a, 0xc3, 0xd4, 0x6c, 0x4e,
        0xd2, 0x82, 0x64, 0x46, 0x07, 0x9f, 0xaa, 0x09,
        0x14, 0xc2, 0xd7, 0x05, 0xd9, 0x8b, 0x02, 0xa2,
        0xb5, 0x12, 0x9c, 0xd1, 0xde, 0x16, 0x4e, 0xb9,
        0xcb, 0xd0, 0x83, 0xe8, 0xa2, 0x50, 0x3c, 0x4e
    };

    int rfc_cmp = kheiron_consttime_cmp(rfc_out, rfc_expected, sizeof(rfc_out));
    printf("RFC 7539 Vector: %s\n", rfc_cmp == 0 ? "APROVADO" : "REPROVADO");
    if (rfc_cmp != 0) {
        failed = 1;
    }

    uint8_t ct_a[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    uint8_t ct_b[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    uint8_t ct_c[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 99};

    int ct_eq = (kheiron_consttime_cmp(ct_a, ct_b, sizeof(ct_a)) == 0);
    int ct_neq = (kheiron_consttime_cmp(ct_a, ct_c, sizeof(ct_a)) != 0);
    uint32_t sel_1 = kheiron_consttime_select_u32(0xFFFFFFFFU, 0xAAAAAAAAU, 0x55555555U);
    uint32_t sel_0 = kheiron_consttime_select_u32(0x00000000U, 0xAAAAAAAAU, 0x55555555U);
    int ct_sel_ok = (sel_1 == 0xAAAAAAAAU && sel_0 == 0x55555555U);

    int ct_ops_ok = ct_eq && ct_neq && ct_sel_ok;
    printf("Constant-time Operations: %s\n", ct_ops_ok ? "APROVADO" : "REPROVADO");
    if (!ct_ops_ok) {
        failed = 1;
    }

    kheiron_ctx_t root_ctx;
    uint8_t root_seed[32] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    kheiron_ctx_init(&root_ctx, root_seed, sizeof(root_seed));

    kheiron_ctx_t root_backup;
    memcpy(&root_backup, &root_ctx, sizeof(root_backup));

    uint8_t key1[32];
    uint8_t key2[32];
    const char info1[] = "auth-token-v1";
    const char info2[] = "encryption-key-v1";

    int d1 = kheiron_derive(&root_ctx, key1, sizeof(key1), info1, strlen(info1));
    int d2 = kheiron_derive(&root_ctx, key2, sizeof(key2), info2, strlen(info2));

    int derive_diff = (kheiron_consttime_cmp(key1, key2, sizeof(key1)) != 0);
    int root_preserved = (memcmp(&root_ctx, &root_backup, sizeof(root_ctx)) == 0);

    int err_null_ctx = (kheiron_derive(NULL, key1, sizeof(key1), info1, strlen(info1)) == -1);
    int err_null_out = (kheiron_derive(&root_ctx, NULL, sizeof(key1), info1, strlen(info1)) == -1);
    int err_zero_len = (kheiron_derive(&root_ctx, key1, 0, info1, strlen(info1)) == -1);
    int err_null_info = (kheiron_derive(&root_ctx, key1, sizeof(key1), NULL, 10) == -1);
    int err_zero_info = (kheiron_derive(&root_ctx, key1, sizeof(key1), info1, 0) == -1);

    int derive_ok = (d1 == 0 && d2 == 0 && derive_diff && root_preserved &&
                     err_null_ctx && err_null_out && err_zero_len && err_null_info && err_zero_info);

    printf("kheiron_derive: %s\n", derive_ok ? "APROVADO" : "REPROVADO");
    if (!derive_ok) {
        failed = 1;
    }

    kheiron_ctx_wipe(&root_ctx);
    kheiron_ctx_wipe(&root_backup);

    /* Test: kheiron_init, kheiron_u64, kheiron_u32 Contract APIs */
    int init_ok = (kheiron_init() == 0);
    uint64_t u64_val = kheiron_u64();
    uint32_t u32_val = kheiron_u32();
    int contract_apis_ok = init_ok && (u64_val != 0 || kheiron_u64() != 0) && (u32_val != 0 || kheiron_u32() != 0);
    printf("Contract APIs (kheiron_init, u64, u32): %s\n", contract_apis_ok ? "APROVADO" : "REPROVADO");
    if (!contract_apis_ok) {
        failed = 1;
    }

    /* Test: Defensive NULL and Zero-Length Argument Handling */
    uint8_t def_buf[64];
    kheiron_ctx_t def_ctx;
    kheiron_ctx_init(&def_ctx, root_seed, sizeof(root_seed));
    int def_null_fill = (kheiron_fill(NULL, 10) == -1);
    int def_null_fill_0 = (kheiron_fill(NULL, 0) == -1);
    int def_zero_fill = (kheiron_fill(def_buf, 0) == 0);
    int def_null_ctx_1 = (kheiron_ctx_fill(NULL, def_buf, 10) == -1);
    int def_null_ctx_2 = (kheiron_ctx_fill(&def_ctx, NULL, 10) == -1);
    int def_zero_ctx = (kheiron_ctx_fill(&def_ctx, def_buf, 0) == 0);
    kheiron_ctx_wipe(&def_ctx);

    int def_ok = def_null_fill && def_null_fill_0 && def_zero_fill &&
                 def_null_ctx_1 && def_null_ctx_2 && def_zero_ctx;
    printf("Defensive Argument Handling: %s\n", def_ok ? "APROVADO" : "REPROVADO");
    if (!def_ok) {
        failed = 1;
    }

    /* Test: Unaligned Buffer Handling across byte offsets 0..31 */
    int unaligned_ok = 1;
    uint8_t unaligned_raw[2048 + 64];
    for (int offset = 0; offset < 32; offset++) {
        uint8_t *dst = unaligned_raw + offset;
        memset(dst, 0xAA, 1024);
        if (kheiron_fill(dst, 1024) != 0) {
            unaligned_ok = 0;
            break;
        }
        int modified = 0;
        for (int k = 0; k < 1024; k++) {
            if (dst[k] != 0xAA) {
                modified = 1;
                break;
            }
        }
        if (!modified) {
            unaligned_ok = 0;
            break;
        }
    }
    printf("Unaligned Buffer Safety (offsets 0..31): %s\n", unaligned_ok ? "APROVADO" : "REPROVADO");
    if (!unaligned_ok) {
        failed = 1;
    }

    /* Test: Exact Tail & Sub-512 Byte Boundary Handling */
    int tail_ok = 1;
    const size_t tail_sizes[] = {1, 2, 7, 15, 16, 31, 32, 63, 64, 65, 127, 128, 255, 256, 511, 512, 513, 1023, 1024, 1025};
    size_t num_tail_sizes = sizeof(tail_sizes) / sizeof(tail_sizes[0]);
    for (size_t t = 0; t < num_tail_sizes; t++) {
        size_t tlen = tail_sizes[t];
        uint8_t *tbuf = (uint8_t *)malloc(tlen + 16);
        if (!tbuf) {
            tail_ok = 0;
            break;
        }
        memset(tbuf, 0xEE, tlen + 16);
        if (kheiron_fill(tbuf, tlen) != 0) {
            tail_ok = 0;
            free(tbuf);
            break;
        }
        /* Verify canary bytes past tlen were not touched */
        for (size_t c = 0; c < 16; c++) {
            if (tbuf[tlen + c] != 0xEE) {
                tail_ok = 0;
                break;
            }
        }
        free(tbuf);
        if (!tail_ok) {
            break;
        }
    }
    printf("Tail & Boundary Exact Sizing: %s\n", tail_ok ? "APROVADO" : "REPROVADO");
    if (!tail_ok) {
        failed = 1;
    }

    /* Test: Bit-for-Bit 8-Block Vector vs Scalar Reference Equivalence */
    int equiv_ok = 1;
    uint32_t test_state_scalar[16];
    for (int i = 0; i < 16; i++) {
        test_state_scalar[i] = rfc_state[i];
    }
    uint8_t scalar_8blocks[512];

    for (int b = 0; b < 8; b++) {
        kheiron_chacha_block(test_state_scalar, scalar_8blocks + b * 64);
        test_state_scalar[12]++;
        if (test_state_scalar[12] == 0) {
            test_state_scalar[13]++;
        }
    }

#if KHEIRON_HAVE_AVX2
    uint32_t test_state_vector[16];
    for (int i = 0; i < 16; i++) {
        test_state_vector[i] = rfc_state[i];
    }
    uint8_t vector_8blocks[512];

    if (kheiron_cpu_has_avx2()) {
        kheiron_chacha20_blocks_avx2(test_state_vector, vector_8blocks, 8);
        if (memcmp(scalar_8blocks, vector_8blocks, 512) != 0) {
            equiv_ok = 0;
        }
        if (test_state_scalar[12] != test_state_vector[12] || test_state_scalar[13] != test_state_vector[13]) {
            equiv_ok = 0;
        }

        /* Test counter wrap across 2^32 boundary */
        test_state_scalar[12] = 0xFFFFFFFCU;
        test_state_scalar[13] = 0x00000001U;
        test_state_vector[12] = 0xFFFFFFFCU;
        test_state_vector[13] = 0x00000001U;

        for (int b = 0; b < 8; b++) {
            kheiron_chacha_block(test_state_scalar, scalar_8blocks + b * 64);
            test_state_scalar[12]++;
            if (test_state_scalar[12] == 0) {
                test_state_scalar[13]++;
            }
        }
        kheiron_chacha20_blocks_avx2(test_state_vector, vector_8blocks, 8);
        if (memcmp(scalar_8blocks, vector_8blocks, 512) != 0) {
            equiv_ok = 0;
        }
        if (test_state_scalar[12] != test_state_vector[12] || test_state_scalar[13] != test_state_vector[13]) {
            equiv_ok = 0;
        }
    }
#endif
    printf("AVX2 vs Scalar Reference Equivalence: %s\n", equiv_ok ? "APROVADO" : "REPROVADO");
    if (!equiv_ok) {
        failed = 1;
    }

    return failed ? 1 : 0;
}
