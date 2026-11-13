#define _GNU_SOURCE 1
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <assert.h>
#include <errno.h>

#ifndef KHEIRON_STATIC
#define KHEIRON_STATIC 1
#endif
#include "../kheiron.h"

#define COLOR_RESET  "\033[0m"
#define COLOR_GREEN  "\033[32m"
#define COLOR_RED    "\033[31m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_CYAN   "\033[36m"
#define COLOR_BOLD   "\033[1m"

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define CHK_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, COLOR_RED "  [FAIL] %s:%d: %s\n" COLOR_RESET, __FILE__, __LINE__, (msg)); \
        return -1; \
    } \
} while (0)

#define CHK_ASSERT_FMT(cond, fmt, ...) do { \
    if (!(cond)) { \
        fprintf(stderr, COLOR_RED "  [FAIL] %s:%d: " fmt "\n" COLOR_RESET, __FILE__, __LINE__, __VA_ARGS__); \
        return -1; \
    } \
} while (0)

#define RUN_TEST(fn) do { \
    g_tests_run++; \
    printf(COLOR_CYAN "RUNNING: %s..." COLOR_RESET "\n", #fn); \
    struct timespec ts0, ts1; \
    clock_gettime(CLOCK_MONOTONIC, &ts0); \
    int res = fn(); \
    clock_gettime(CLOCK_MONOTONIC, &ts1); \
    double ms = (double)(ts1.tv_sec - ts0.tv_sec) * 1000.0 + (double)(ts1.tv_nsec - ts0.tv_nsec) / 1000000.0; \
    if (res == 0) { \
        printf(COLOR_GREEN "  [PASS] %-45s (%7.2f ms)" COLOR_RESET "\n", #fn, ms); \
        g_tests_passed++; \
    } else { \
        printf(COLOR_RED "  [FAIL] %-45s (%7.2f ms)" COLOR_RESET "\n", #fn, ms); \
        g_tests_failed++; \
    } \
} while (0)

/* ========================================================================= */
/* 1. STATISTICAL STRESS TESTS                                               */
/* ========================================================================= */

/* Test 1.1: High-Volume Monte Carlo Pi Estimation (10 Million samples) */
static int test_stat_monte_carlo_pi_10m(void) {
    const uint64_t N = 10000000ULL;
    uint64_t inside_double = 0;
    uint64_t quadrant_counts[4] = {0}; /* Q1(+,+), Q2(-,+), Q3(-,-), Q4(+,-) */

    /* 10 concentric annular rings of equal area on the unit disk */
    const int RINGS = 10;
    uint64_t ring_counts[10] = {0};

    for (uint64_t i = 0; i < N; i++) {
        double u1 = kheiron_uniform_double();
        double u2 = kheiron_uniform_double();
        double x = 2.0 * u1 - 1.0;
        double y = 2.0 * u2 - 1.0;
        double r2 = x * x + y * y;

        if (r2 <= 1.0) {
            inside_double++;
            /* Quadrant classification */
            int q = 0;
            if (x >= 0.0 && y >= 0.0) q = 0;
            else if (x < 0.0 && y >= 0.0) q = 1;
            else if (x < 0.0 && y < 0.0) q = 2;
            else q = 3;
            quadrant_counts[q]++;

            int ring = (int)(r2 * (double)RINGS);
            if (ring >= RINGS) ring = RINGS - 1;
            ring_counts[ring]++;
        }
    }

    double pi_est = 4.0 * (double)inside_double / (double)N;
    double pi_err = fabs(pi_est - 3.14159265358979323846);
    /* 99.999% confidence threshold for 10M points is ~0.0020 */
    CHK_ASSERT_FMT(pi_err < 0.0025, "Pi estimate error too high: %f (est: %f)", pi_err, pi_est);

    /* 4-Quadrant Symmetry Chi-Squared Test */
    double expected_q = (double)inside_double / 4.0;
    double chi2_q = 0.0;
    for (int q = 0; q < 4; q++) {
        double d = (double)quadrant_counts[q] - expected_q;
        chi2_q += (d * d) / expected_q;
    }
    /* df=3, critical value at p=0.001 is 16.27 */
    CHK_ASSERT_FMT(chi2_q < 16.27, "Quadrant chi2 failed: %f (expected < 16.27)", chi2_q);

    /* Concentric Ring Uniformity Chi-Squared Test */
    double expected_ring = (double)inside_double / (double)RINGS;
    double chi2_ring = 0.0;
    for (int r = 0; r < RINGS; r++) {
        double d = (double)ring_counts[r] - expected_ring;
        chi2_ring += (d * d) / expected_ring;
    }
    /* df=9, critical value at p=0.001 is 27.88 */
    CHK_ASSERT_FMT(chi2_ring < 27.88, "Concentric ring chi2 failed: %f (expected < 27.88)", chi2_ring);

    return 0;
}

/* Test 1.2: Float vs Double Monte Carlo Pi Consistency */
static int test_stat_monte_carlo_float_vs_double(void) {
    const uint64_t N = 5000000ULL;
    uint64_t in_flt = 0;
    uint64_t in_dbl = 0;

    for (uint64_t i = 0; i < N; i++) {
        float fx = 2.0f * kheiron_uniform_float() - 1.0f;
        float fy = 2.0f * kheiron_uniform_float() - 1.0f;
        if (fx * fx + fy * fy <= 1.0f) in_flt++;

        double dx = 2.0 * kheiron_uniform_double() - 1.0;
        double dy = 2.0 * kheiron_uniform_double() - 1.0;
        if (dx * dx + dy * dy <= 1.0) in_dbl++;
    }

    double pi_flt = 4.0 * (double)in_flt / (double)N;
    double pi_dbl = 4.0 * (double)in_dbl / (double)N;
    double err_flt = fabs(pi_flt - 3.141592653589793);
    double err_dbl = fabs(pi_dbl - 3.141592653589793);

    CHK_ASSERT_FMT(err_flt < 0.0035, "Float Pi err too large: %f", err_flt);
    CHK_ASSERT_FMT(err_dbl < 0.0035, "Double Pi err too large: %f", err_dbl);
    CHK_ASSERT_FMT(fabs(pi_flt - pi_dbl) < 0.0050, "Discrepancy between float and double: %f", fabs(pi_flt - pi_dbl));
    return 0;
}

/* Test 1.3: Fisher-Yates Shuffling at 1,000,000 Elements (8 MB Buffer) */
static int test_stat_fisher_yates_1m_elements(void) {
    const size_t N = 1000000;
    uint64_t *arr = (uint64_t *)malloc(N * sizeof(uint64_t));
    CHK_ASSERT(arr != NULL, "Out of memory allocating 1M uint64 array");

    uint64_t expected_sum = 0;
    uint64_t expected_xor = 0;
    for (size_t i = 0; i < N; i++) {
        arr[i] = i;
        expected_sum += i;
        expected_xor ^= i;
    }

    /* Shuffle array (8MB buffer will traverse 8 key rotations) */
    CHK_ASSERT(kheiron_shuffle(arr, N, sizeof(uint64_t)) == 0, "kheiron_shuffle returned non-zero");

    uint64_t actual_sum = 0;
    uint64_t actual_xor = 0;
    size_t fixed_points = 0;
    double displacement_sum = 0.0;

    for (size_t i = 0; i < N; i++) {
        uint64_t v = arr[i];
        CHK_ASSERT_FMT(v < N, "Value out of bounds: %llu >= %zu", (unsigned long long)v, N);
        actual_sum += v;
        actual_xor ^= v;
        if (v == i) fixed_points++;
        displacement_sum += fabs((double)v - (double)i);
    }

    /* 1. Conservation of Elements */
    CHK_ASSERT_FMT(actual_sum == expected_sum, "Permutation sum mismatch: actual %llu != expected %llu",
                   (unsigned long long)actual_sum, (unsigned long long)expected_sum);
    CHK_ASSERT_FMT(actual_xor == expected_xor, "Permutation XOR mismatch: actual %llu != expected %llu",
                   (unsigned long long)actual_xor, (unsigned long long)expected_xor);

    /* 2. Fixed Points Count (Poisson distribution with mean=1, variance=1) */
    CHK_ASSERT_FMT(fixed_points < 15, "Abnormal number of fixed points in 1M shuffle: %zu", fixed_points);

    /* 3. Average Displacement */
    /* Theoretical mean displacement = (N^2 - 1) / (3N) ~ N / 3 = 333333.33 */
    double avg_disp = displacement_sum / (double)N;
    double disp_err = fabs(avg_disp - 333333.33);
    CHK_ASSERT_FMT(disp_err < 2000.0, "Average displacement %f deviates from 333333.33 by %f", avg_disp, disp_err);

    free(arr);
    return 0;
}

/* Test 1.4: Fisher-Yates 5-Element Permutation Chi-Squared Uniformity (600,000 trials) */
static int test_stat_fisher_yates_5elem_chi2(void) {
    const uint64_t TRIALS = 600000ULL;
    const int NUM_PERMS = 120; /* 5! = 120 */
    uint64_t counts[120] = {0};
    uint8_t deck[5];

    for (uint64_t t = 0; t < TRIALS; t++) {
        deck[0] = 0; deck[1] = 1; deck[2] = 2; deck[3] = 3; deck[4] = 4;
        CHK_ASSERT(kheiron_shuffle(deck, 5, sizeof(uint8_t)) == 0, "kheiron_shuffle failed");

        /* Compute Lehmer code / permutation index */
        int idx = 0;
        int factor = 24;
        for (int i = 0; i < 4; i++) {
            int smaller = 0;
            for (int j = i + 1; j < 5; j++) {
                if (deck[j] < deck[i]) smaller++;
            }
            idx += smaller * factor;
            factor /= (4 - i);
        }
        CHK_ASSERT_FMT(idx >= 0 && idx < NUM_PERMS, "Invalid perm index: %d", idx);
        counts[idx]++;
    }

    double expected = (double)TRIALS / (double)NUM_PERMS; /* 5000 per bin */
    double chi2 = 0.0;
    for (int p = 0; p < NUM_PERMS; p++) {
        double d = (double)counts[p] - expected;
        chi2 += (d * d) / expected;
    }
    /* df=119, critical value at p=0.0001 is 185.0 */
    CHK_ASSERT_FMT(chi2 < 185.0, "5! Permutation Chi2 failed: %f (expected < 185.0)", chi2);
    return 0;
}

/* Test 1.5: Chi-Squared Uniformity on 10 MB Byte Stream Across 10 Rotations */
static int test_stat_chisquare_10mb_bytes(void) {
    const size_t TOTAL_BYTES = 10 * 1024 * 1024; /* 10 MB */
    uint64_t counts[256] = {0};
    uint8_t buf[4096];
    size_t done = 0;

    while (done < TOTAL_BYTES) {
        size_t chunk = (TOTAL_BYTES - done) < sizeof(buf) ? (TOTAL_BYTES - done) : sizeof(buf);
        CHK_ASSERT(kheiron_fill(buf, chunk) == 0, "kheiron_fill failed");
        for (size_t i = 0; i < chunk; i++) {
            counts[buf[i]]++;
        }
        done += chunk;
    }

    double expected = (double)TOTAL_BYTES / 256.0; /* 40960.0 */
    double chi2 = 0.0;
    for (int i = 0; i < 256; i++) {
        double d = (double)counts[i] - expected;
        chi2 += (d * d) / expected;
    }
    /* df=255. Mean=255, StdDev = sqrt(2*255) = 22.58. 3.5-sigma interval: [175, 335] */
    CHK_ASSERT_FMT(chi2 >= 175.0 && chi2 <= 335.0, "Byte stream Chi2 out of 3.5-sigma bounds: %f", chi2);
    return 0;
}

/* Test 1.6: Chi-Squared Uniformity on 16-bit Words (65,536 Bins, 8M samples) */
static int test_stat_chisquare_16bit_words(void) {
    const size_t NUM_WORDS = 8 * 1024 * 1024; /* 8M uint16 words = 16 MB */
    uint32_t *counts = (uint32_t *)calloc(65536, sizeof(uint32_t));
    CHK_ASSERT(counts != NULL, "calloc failed for 65536 word bins");

    uint16_t buf[2048];
    size_t words_done = 0;

    while (words_done < NUM_WORDS) {
        size_t cur = (NUM_WORDS - words_done) < 2048 ? (NUM_WORDS - words_done) : 2048;
        CHK_ASSERT(kheiron_fill(buf, cur * sizeof(uint16_t)) == 0, "kheiron_fill failed");
        for (size_t i = 0; i < cur; i++) {
            counts[buf[i]]++;
        }
        words_done += cur;
    }

    double expected = (double)NUM_WORDS / 65536.0; /* 128.0 */
    double chi2 = 0.0;
    for (int i = 0; i < 65536; i++) {
        double d = (double)counts[i] - expected;
        chi2 += (d * d) / expected;
    }
    free(counts);

    /* df=65535, Mean=65535, StdDev = sqrt(2 * 65535) ~ 362.0. 4-sigma range: [64087, 66983] */
    double z = (chi2 - 65535.0) / 362.0;
    CHK_ASSERT_FMT(fabs(z) < 4.0, "16-bit word Chi2 z-score failed: %f (chi2: %f)", z, chi2);
    return 0;
}

/* Test 1.7: Lemire Modulo Bias-Free Sampling Stress (Prime & Power-of-Two Moduli) */
static int test_stat_lemire_range_modulo_stress(void) {
    /* Test A: Prime Modulus M = 97 (970,000 samples) */
    {
        const int M = 97;
        const uint64_t SAMPLES = 970000ULL;
        uint64_t bin[97] = {0};
        for (uint64_t i = 0; i < SAMPLES; i++) {
            uint64_t v = kheiron_range_u64(0, M - 1);
            CHK_ASSERT_FMT(v < (uint64_t)M, "Value out of range: %llu", (unsigned long long)v);
            bin[v]++;
        }
        double expected = (double)SAMPLES / (double)M;
        double chi2 = 0.0;
        for (int i = 0; i < M; i++) {
            double d = (double)bin[i] - expected;
            chi2 += (d * d) / expected;
        }
        /* df=96, critical value at p=0.001 is 144.3 */
        CHK_ASSERT_FMT(chi2 < 145.0, "Lemire prime M=97 Chi2 failed: %f", chi2);
    }

    /* Test B: Non-zero offset range [1000, 1009] (1,000,000 samples) */
    {
        const uint64_t SAMPLES = 1000000ULL;
        uint64_t bin[10] = {0};
        for (uint64_t i = 0; i < SAMPLES; i++) {
            uint64_t v = kheiron_range_u64(1000, 1009);
            CHK_ASSERT_FMT(v >= 1000 && v <= 1009, "Value out of range: %llu", (unsigned long long)v);
            bin[v - 1000]++;
        }
        double expected = (double)SAMPLES / 10.0;
        double chi2 = 0.0;
        for (int i = 0; i < 10; i++) {
            double d = (double)bin[i] - expected;
            chi2 += (d * d) / expected;
        }
        /* df=9, critical value at p=0.001 is 27.88 */
        CHK_ASSERT_FMT(chi2 < 28.0, "Lemire offset range Chi2 failed: %f", chi2);
    }

    /* Test C: Full 64-bit bounds [0, UINT64_MAX] */
    {
        uint64_t hi_bit_count = 0;
        uint64_t lo_bit_count = 0;
        const int RUNS = 100000;
        for (int i = 0; i < RUNS; i++) {
            uint64_t v = kheiron_range_u64(0, UINT64_MAX);
            if (v & (1ULL << 63)) hi_bit_count++;
            if (v & 1ULL) lo_bit_count++;
        }
        double p_hi = (double)hi_bit_count / (double)RUNS;
        double p_lo = (double)lo_bit_count / (double)RUNS;
        CHK_ASSERT_FMT(fabs(p_hi - 0.5) < 0.015, "Full range hi bit bias: %f", p_hi);
        CHK_ASSERT_FMT(fabs(p_lo - 0.5) < 0.015, "Full range lo bit bias: %f", p_lo);
    }

    return 0;
}

/* Normal CDF helper for Kolmogorov-Smirnov test */
static double standard_normal_cdf(double x) {
    return 0.5 * erfc(-x * 0.70710678118654752440);
}

static int compare_doubles(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

/* Test 1.8: Gaussian Mean, Variance, Higher Moments and KS Goodness-of-Fit */
static int test_stat_gaussian_moments_and_ks(void) {
    const int N = 1000000;
    double sum = 0.0;
    double sum_sq = 0.0;
    double sum_cube = 0.0;
    double sum_four = 0.0;

    for (int i = 0; i < N; i++) {
        double g = kheiron_gaussian(0.0, 1.0);
        sum += g;
        sum_sq += g * g;
        sum_cube += g * g * g;
        sum_four += g * g * g * g;
    }

    double mean = sum / (double)N;
    double var = (sum_sq / (double)N) - (mean * mean);
    double stddev = sqrt(var);
    double m3 = (sum_cube / (double)N) - 3.0 * mean * var - (mean * mean * mean);
    double skewness = m3 / (stddev * stddev * stddev);
    double m4 = sum_four / (double)N;
    double kurtosis = (m4 / (var * var)) - 3.0;

    /* 4-sigma checks for 1M samples */
    CHK_ASSERT_FMT(fabs(mean) < 0.005, "Gaussian mean out of bounds: %f", mean);
    CHK_ASSERT_FMT(fabs(var - 1.0) < 0.007, "Gaussian variance out of bounds: %f", var);
    CHK_ASSERT_FMT(fabs(skewness) < 0.020, "Gaussian skewness out of bounds: %f", skewness);
    CHK_ASSERT_FMT(fabs(kurtosis) < 0.035, "Gaussian excess kurtosis out of bounds: %f", kurtosis);

    /* Kolmogorov-Smirnov Test on a 20,000-sample subsample */
    const int KS_N = 20000;
    double *ks_samples = (double *)malloc(KS_N * sizeof(double));
    CHK_ASSERT(ks_samples != NULL, "malloc failed for KS test");

    for (int i = 0; i < KS_N; i++) {
        ks_samples[i] = kheiron_gaussian(0.0, 1.0);
    }
    qsort(ks_samples, KS_N, sizeof(double), compare_doubles);

    double max_d = 0.0;
    for (int i = 0; i < KS_N; i++) {
        double f_emp = (double)(i + 1) / (double)KS_N;
        double f_theo = standard_normal_cdf(ks_samples[i]);
        double d = fabs(f_emp - f_theo);
        if (d > max_d) max_d = d;
    }
    free(ks_samples);

    /* KS critical value for alpha=0.01 with N=20000 is 1.63 / sqrt(20000) ~ 0.0115 */
    CHK_ASSERT_FMT(max_d < 0.0120, "Kolmogorov-Smirnov test failed: D = %f (threshold: 0.0120)", max_d);
    return 0;
}

/* ========================================================================= */
/* 2. BOUNDARY STRESS TESTS                                                  */
/* ========================================================================= */

/* Test 2.1: Exhaustive Alignment Sweep 1..63 with Canary Envelopes */
static int test_bound_alignment_canary_sweep(void) {
    const size_t test_lens[] = {
        0, 1, 2, 3, 7, 8, 15, 16, 31, 32, 63, 64, 65,
        127, 128, 255, 256, 511, 512, 513, 1023, 1024, 1025
    };
    const size_t num_lens = sizeof(test_lens) / sizeof(test_lens[0]);

    const size_t CANARY_SIZE = 64;
    const uint8_t PRE_CANARY = 0x5A;
    const uint8_t POST_CANARY = 0xA5;

    size_t total_alloc = 1025 + 63 + 2 * CANARY_SIZE + 64;
    uint8_t *raw = (uint8_t *)malloc(total_alloc);
    CHK_ASSERT(raw != NULL, "malloc failed");

    uint8_t *base = (uint8_t *)(((uintptr_t)raw + 63) & ~(uintptr_t)63);

    for (size_t offset = 0; offset <= 63; offset++) {
        for (size_t l_idx = 0; l_idx < num_lens; l_idx++) {
            size_t len = test_lens[l_idx];

            uint8_t *pre = base + offset;
            uint8_t *target = pre + CANARY_SIZE;
            uint8_t *post = target + len;

            memset(pre, PRE_CANARY, CANARY_SIZE);
            if (len > 0) memset(target, 0x00, len);
            memset(post, POST_CANARY, CANARY_SIZE);

            int r = kheiron_fill(target, len);
            CHK_ASSERT_FMT(r == 0, "kheiron_fill failed for offset %zu, len %zu", offset, len);

            for (size_t i = 0; i < CANARY_SIZE; i++) {
                CHK_ASSERT_FMT(pre[i] == PRE_CANARY,
                               "Pre-canary corrupted at idx %zu (offset %zu, len %zu): 0x%02X != 0x%02X",
                               i, offset, len, pre[i], PRE_CANARY);
            }

            for (size_t i = 0; i < CANARY_SIZE; i++) {
                CHK_ASSERT_FMT(post[i] == POST_CANARY,
                               "Post-canary corrupted at idx %zu (offset %zu, len %zu): 0x%02X != 0x%02X",
                               i, offset, len, post[i], POST_CANARY);
            }
        }
    }

    free(raw);
    return 0;
}

/* Test 2.2: Zero-Length Requests and Defensive Contract Robustness */
static int test_bound_zero_length_and_nulls(void) {
    uint8_t buf[64];
    memset(buf, 0xAA, sizeof(buf));

    /* 1. Zero length with valid buffer must succeed and not modify memory */
    CHK_ASSERT(kheiron_fill(buf, 0) == 0, "kheiron_fill(buf, 0) returned non-zero");
    for (size_t i = 0; i < sizeof(buf); i++) {
        CHK_ASSERT_FMT(buf[i] == 0xAA, "kheiron_fill(buf, 0) modified byte %zu", i);
    }

    /* 2. NULL buffer with length 0 and non-zero */
    CHK_ASSERT(kheiron_fill(NULL, 0) == -1, "kheiron_fill(NULL, 0) must return -1");
    CHK_ASSERT(kheiron_fill(NULL, 10) == -1, "kheiron_fill(NULL, 10) must return -1");

    /* 3. Contextual API NULL & zero handling */
    kheiron_ctx_t ctx;
    CHK_ASSERT(kheiron_ctx_init(&ctx, NULL, 0) == 0, "kheiron_ctx_init(NULL, 0) should succeed");
    CHK_ASSERT(kheiron_ctx_fill(&ctx, buf, 0) == 0, "kheiron_ctx_fill(ctx, buf, 0) should succeed");
    for (size_t i = 0; i < sizeof(buf); i++) {
        CHK_ASSERT_FMT(buf[i] == 0xAA, "kheiron_ctx_fill(ctx, buf, 0) modified byte %zu", i);
    }
    CHK_ASSERT(kheiron_ctx_fill(&ctx, NULL, 0) == -1, "kheiron_ctx_fill(ctx, NULL, 0) must return -1");
    CHK_ASSERT(kheiron_ctx_fill(NULL, buf, 10) == -1, "kheiron_ctx_fill(NULL, buf, 10) must return -1");
    kheiron_ctx_wipe(&ctx);

    /* 4. Secure Wipe NULL and zero length safety */
    kheiron_secure_wipe(NULL, 0);
    kheiron_secure_wipe(NULL, 128);
    kheiron_secure_wipe(buf, 0);
    for (size_t i = 0; i < sizeof(buf); i++) {
        CHK_ASSERT_FMT(buf[i] == 0xAA, "kheiron_secure_wipe(buf, 0) modified byte %zu", i);
    }

    /* 5. String NULL & zero length */
    char str[16] = {0};
    CHK_ASSERT(kheiron_string(NULL, 10, "abc") == -1, "kheiron_string NULL dest");
    CHK_ASSERT(kheiron_string(str, 10, NULL) == -1, "kheiron_string NULL charset");
    CHK_ASSERT(kheiron_string(str, 10, "") == -1, "kheiron_string empty charset");

    /* 6. Shuffle bounds */
    int arr[5] = {1, 2, 3, 4, 5};
    CHK_ASSERT(kheiron_shuffle(NULL, 5, sizeof(int)) == -1, "kheiron_shuffle NULL base");
    CHK_ASSERT(kheiron_shuffle(arr, 0, sizeof(int)) == -1, "kheiron_shuffle 0 nmemb");
    CHK_ASSERT(kheiron_shuffle(arr, 1, sizeof(int)) == -1, "kheiron_shuffle 1 nmemb");
    CHK_ASSERT(kheiron_shuffle(arr, 5, 0) == -1, "kheiron_shuffle 0 size");

    /* 7. Range bounds */
    CHK_ASSERT(kheiron_range_u64(42, 42) == 42, "kheiron_range_u64 min==max must return min");
    CHK_ASSERT(kheiron_range_u64(100, 50) == 100, "kheiron_range_u64 min > max must return min");

    /* 8. Gaussian bounds */
    CHK_ASSERT(kheiron_gaussian(5.5, 0.0) == 5.5, "kheiron_gaussian stddev=0 must return mean");
    CHK_ASSERT(kheiron_gaussian(-3.2, -1.0) == -3.2, "kheiron_gaussian stddev<0 must return mean");

    return 0;
}

/* Test 2.3: 1-Byte Writes with Hardware Guard Page Boundary (mmap + PROT_NONE) */
static int test_bound_guard_page_1byte_overrun(void) {
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) page_size = 4096;

    /* Allocate 2 contiguous memory pages */
    uint8_t *two_pages = (uint8_t *)mmap(NULL, page_size * 2, PROT_READ | PROT_WRITE,
                                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHK_ASSERT(two_pages != MAP_FAILED, "mmap failed for guard page test");

    /* Page 2 becomes a HARD GUARD PAGE (PROT_NONE) */
    int mres = mprotect(two_pages + page_size, page_size, PROT_NONE);
    CHK_ASSERT(mres == 0, "mprotect failed to set PROT_NONE guard page");

    /* Run tests writing right up to the guard page boundary */
    const size_t test_sizes[] = {1, 2, 3, 4, 7, 8, 15, 16, 31, 32, 63, 64, 128, 256, 511, 512};
    size_t num_tests = sizeof(test_sizes) / sizeof(test_sizes[0]);

    for (size_t i = 0; i < num_tests; i++) {
        size_t len = test_sizes[i];
        uint8_t *target = two_pages + page_size - len;

        int r = kheiron_fill(target, len);
        CHK_ASSERT_FMT(r == 0, "kheiron_fill failed at guard boundary for len %zu", len);

        kheiron_ctx_t ctx;
        CHK_ASSERT(kheiron_ctx_init(&ctx, NULL, 0) == 0, "ctx_init failed");
        r = kheiron_ctx_fill(&ctx, target, len);
        CHK_ASSERT_FMT(r == 0, "kheiron_ctx_fill failed at guard boundary for len %zu", len);
        kheiron_ctx_wipe(&ctx);
    }

    mprotect(two_pages + page_size, page_size, PROT_READ | PROT_WRITE);
    munmap(two_pages, page_size * 2);
    return 0;
}

/* Test 2.4: 1 MB Rotation Threshold Boundary Transitions */
static int test_bound_1mb_rotation_threshold_transitions(void) {
    const size_t ROT_THRESHOLD = 1048576; /* 1 MB */

    /* Case A: Step-by-step 1-byte transition across 1 MB */
    {
        kheiron_ctx_t ctx;
        CHK_ASSERT(kheiron_ctx_init(&ctx, NULL, 0) == 0, "ctx_init failed");

        /* 1. Advance to exactly 1 MB - 1 byte */
        uint8_t *big = (uint8_t *)malloc(ROT_THRESHOLD - 1);
        CHK_ASSERT(big != NULL, "malloc failed");
        CHK_ASSERT(kheiron_ctx_fill(&ctx, big, ROT_THRESHOLD - 1) == 0, "fill failed");
        free(big);
        CHK_ASSERT(ctx.bytes_generated == ROT_THRESHOLD - 1, "Bytes generated mismatch before boundary");

        /* 2. Write 1 byte to hit exactly 1 MB threshold */
        uint8_t b1 = 0;
        CHK_ASSERT(kheiron_ctx_fill(&ctx, &b1, 1) == 0, "fill 1 byte failed");
        CHK_ASSERT(ctx.bytes_generated == ROT_THRESHOLD, "Must equal exact 1MB threshold");

        /* 3. Next write triggers key rotation and resets epoch */
        uint8_t b2 = 0;
        CHK_ASSERT(kheiron_ctx_fill(&ctx, &b2, 1) == 0, "fill after boundary failed");
        CHK_ASSERT(ctx.bytes_generated == 1, "Epoch bytes_generated should be 1 after rotation");

        kheiron_ctx_wipe(&ctx);
    }

    /* Case B: Spanning request crossing 1 MB boundary */
    {
        kheiron_ctx_t ctx;
        CHK_ASSERT(kheiron_ctx_init(&ctx, NULL, 0) == 0, "ctx_init failed");

        size_t initial = ROT_THRESHOLD - 100;
        uint8_t *big = (uint8_t *)malloc(initial);
        CHK_ASSERT(big != NULL, "malloc failed");
        CHK_ASSERT(kheiron_ctx_fill(&ctx, big, initial) == 0, "fill failed");
        free(big);
        CHK_ASSERT(ctx.bytes_generated == initial, "Initial bytes generated mismatch");

        uint8_t span_buf[250 + 128];
        uint8_t *pre = span_buf;
        uint8_t *target = span_buf + 64;
        uint8_t *post = target + 250;
        memset(pre, 0x33, 64);
        memset(post, 0x77, 64);

        CHK_ASSERT(kheiron_ctx_fill(&ctx, target, 250) == 0, "spanning fill failed");
        CHK_ASSERT_FMT(ctx.bytes_generated == 150, "Post-rotation byte count must be 150, got %zu", ctx.bytes_generated);

        for (int i = 0; i < 64; i++) {
            CHK_ASSERT(pre[i] == 0x33, "Pre canary corrupted during spanning rotation");
            CHK_ASSERT(post[i] == 0x77, "Post canary corrupted during spanning rotation");
        }
        kheiron_ctx_wipe(&ctx);
    }

    /* Case C: Unaligned AVX2 chunk crossing 1 MB boundary */
    {
        kheiron_ctx_t ctx;
        CHK_ASSERT(kheiron_ctx_init(&ctx, NULL, 0) == 0, "ctx_init failed");

        size_t initial = ROT_THRESHOLD - 300;
        uint8_t *big = (uint8_t *)malloc(initial);
        CHK_ASSERT(big != NULL, "malloc failed");
        CHK_ASSERT(kheiron_ctx_fill(&ctx, big, initial) == 0, "fill failed");
        free(big);

        uint8_t raw[1024 + 128 + 64];
        uint8_t *base = (uint8_t *)(((uintptr_t)raw + 63) & ~(uintptr_t)63);
        uint8_t *tgt = base + 37;
        memset(base, 0x11, 37);

        CHK_ASSERT(kheiron_ctx_fill(&ctx, tgt, 1024) == 0, "unaligned spanning fill failed");
        for (int i = 0; i < 37; i++) {
            CHK_ASSERT(base[i] == 0x11, "Unaligned canary corrupted");
        }
        CHK_ASSERT_FMT(ctx.bytes_generated == 724, "Bytes generated in new epoch mismatch: %zu != 724", ctx.bytes_generated);
        kheiron_ctx_wipe(&ctx);
    }

    /* Case D: Forward Secrecy Keystream Non-Correlation Across Rotation */
    {
        kheiron_ctx_t ctx;
        CHK_ASSERT(kheiron_ctx_init(&ctx, NULL, 0) == 0, "ctx_init failed");

        uint8_t *big = (uint8_t *)malloc(ROT_THRESHOLD - 64);
        CHK_ASSERT(big != NULL, "malloc failed");
        CHK_ASSERT(kheiron_ctx_fill(&ctx, big, ROT_THRESHOLD - 64) == 0, "fill failed");
        free(big);

        uint8_t pre_rot[64];
        CHK_ASSERT(kheiron_ctx_fill(&ctx, pre_rot, 64) == 0, "fill pre-rotation block failed");
        CHK_ASSERT(ctx.bytes_generated == ROT_THRESHOLD, "Should hit exact rotation threshold");

        uint8_t post_rot[64];
        CHK_ASSERT(kheiron_ctx_fill(&ctx, post_rot, 64) == 0, "fill post-rotation block failed");
        CHK_ASSERT(ctx.bytes_generated == 64, "Should be in new epoch");

        CHK_ASSERT(memcmp(pre_rot, post_rot, 64) != 0, "Pre- and post-rotation blocks are identical!");

        int matches = 0;
        for (int i = 0; i < 64; i++) {
            if (pre_rot[i] == post_rot[i]) matches++;
        }
        CHK_ASSERT_FMT(matches < 6, "Suspicious matching byte count across rotation: %d", matches);

        kheiron_ctx_wipe(&ctx);
    }

    return 0;
}

/* ========================================================================= */
/* MAIN RUNNER                                                               */
/* ========================================================================= */

int main(void) {
    printf("%s================================================================================%s\n", COLOR_CYAN, COLOR_RESET);
    printf("%sCHALLENGER M2.1: EMPIRICAL STATISTICAL & BOUNDARY STRESS HARNESS%s\n", COLOR_BOLD, COLOR_RESET);
    printf("Target: kheiron.h | Branch: kheiron-hydrogen | Architecture: Linux x86_64\n");
    printf("%s================================================================================%s\n\n", COLOR_CYAN, COLOR_RESET);

    if (kheiron_init() != 0) {
        fprintf(stderr, COLOR_RED "FATAL: kheiron_init() failed during test harness setup\n" COLOR_RESET);
        return 1;
    }

    printf("%s--- [CATEGORY 1: STATISTICAL STRESS TESTS] ---%s\n", COLOR_BOLD, COLOR_RESET);
    RUN_TEST(test_stat_monte_carlo_pi_10m);
    RUN_TEST(test_stat_monte_carlo_float_vs_double);
    RUN_TEST(test_stat_fisher_yates_1m_elements);
    RUN_TEST(test_stat_fisher_yates_5elem_chi2);
    RUN_TEST(test_stat_chisquare_10mb_bytes);
    RUN_TEST(test_stat_chisquare_16bit_words);
    RUN_TEST(test_stat_lemire_range_modulo_stress);
    RUN_TEST(test_stat_gaussian_moments_and_ks);

    printf("\n%s--- [CATEGORY 2: BOUNDARY STRESS TESTS] ---%s\n", COLOR_BOLD, COLOR_RESET);
    RUN_TEST(test_bound_alignment_canary_sweep);
    RUN_TEST(test_bound_zero_length_and_nulls);
    RUN_TEST(test_bound_guard_page_1byte_overrun);
    RUN_TEST(test_bound_1mb_rotation_threshold_transitions);

    printf("\n%s================================================================================%s\n", COLOR_CYAN, COLOR_RESET);
    printf("ADVERSARIAL STRESS SUMMARY:\n");
    printf("  Total Stress Suites Executed : %d\n", g_tests_run);
    printf("  Passed                       : %s%d%s\n", g_tests_passed > 0 ? COLOR_GREEN : "", g_tests_passed, COLOR_RESET);
    printf("  Failed                       : %s%d%s\n", g_tests_failed > 0 ? COLOR_RED : "", g_tests_failed, COLOR_RESET);
    printf("%s================================================================================%s\n", COLOR_CYAN, COLOR_RESET);

    if (g_tests_failed == 0) {
        printf(COLOR_GREEN "VERDICT: ALL ADVERSARIAL STRESS TESTS PASSED (100%% EMPIRICAL SUCCESS)" COLOR_RESET "\n");
        return 0;
    } else {
        printf(COLOR_RED "VERDICT: ADVERSARIAL STRESS SUITE DETECTED %d FAILURES" COLOR_RESET "\n", g_tests_failed);
        return 1;
    }
}
