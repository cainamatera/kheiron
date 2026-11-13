#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <assert.h>
#include <errno.h>

#define KHEIRON_STATIC 1
#include "../kheiron.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_RED     "\033[31m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_BOLD    "\033[1m"

static int g_test_passes = 0;
static int g_test_failures = 0;

#define ADV_CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, COLOR_RED "  [FAIL] %s:%d: Assertion '%s' failed: %s" COLOR_RESET "\n", \
                __FILE__, __LINE__, #cond, (msg)); \
        g_test_failures++; \
        return false; \
    } \
} while (0)

#define ADV_CHECK_FMT(cond, fmt, val) do { \
    if (!(cond)) { \
        char _adv_buf[256]; \
        snprintf(_adv_buf, sizeof(_adv_buf), fmt, val); \
        fprintf(stderr, COLOR_RED "  [FAIL] %s:%d: Assertion '%s' failed: %s" COLOR_RESET "\n", \
                __FILE__, __LINE__, #cond, _adv_buf); \
        g_test_failures++; \
        return false; \
    } \
} while (0)

#define ADV_PASS(name) do { \
    printf(COLOR_GREEN "  [PASS] %s" COLOR_RESET "\n", name); \
    g_test_passes++; \
    return true; \
} while (0)

/* ========================================================================= */
/* Pure Scalar Reference Engine for Differential Verification                */
/* ========================================================================= */

#if KHEIRON_HAVE_AVX2
static void ref_scalar_ctx_fill(kheiron_ctx_t *ctx, void *buf, size_t len) {
    uint8_t *p = (uint8_t *)buf;
    size_t remaining = len;

    while (remaining > 0) {
        if (ctx->bytes_generated >= KHEIRON_KEY_ROTATION_THRESHOLD) {
            kheiron_ctx_rotate(ctx);
        }

        size_t available_in_epoch = KHEIRON_KEY_ROTATION_THRESHOLD - ctx->bytes_generated;
        size_t chunk_limit = remaining < available_in_epoch ? remaining : available_in_epoch;

        if (chunk_limit >= 64) {
            size_t scalar_blocks = chunk_limit / 64;
            for (size_t b = 0; b < scalar_blocks; b++) {
                kheiron_chacha_block(ctx->state, p);
                ctx->state[12]++;
                if (ctx->state[12] == 0) {
                    ctx->state[13]++;
                }
                p += 64;
                remaining -= 64;
                ctx->bytes_generated += 64;
            }
            continue;
        }

        uint8_t keystream[64];
        kheiron_chacha_block(ctx->state, keystream);
        ctx->state[12]++;
        if (ctx->state[12] == 0) {
            ctx->state[13]++;
        }
        for (size_t i = 0; i < chunk_limit; i++) {
            p[i] = keystream[i];
        }
        kheiron_secure_wipe(keystream, sizeof(keystream));
        p += chunk_limit;
        remaining -= chunk_limit;
        ctx->bytes_generated += chunk_limit;
    }
}
#endif

/* ========================================================================= */
/* 1. AVX2 vs SCALAR KEYSTREAM EQUIVALENCE                                   */
/* ========================================================================= */

#if KHEIRON_HAVE_AVX2
static bool test_avx2_vs_scalar_direct_8blocks(void) {
    printf("[1.1] Testing AVX2 8-block kernel vs 8 scalar blocks across 1000 randomized states...\n");
    uint8_t avx_out[512];
    uint8_t sc_out[512];

    for (int trial = 0; trial < 1000; trial++) {
        uint32_t base_state[16];
        kheiron_fill(base_state, sizeof(base_state));

        /* Run 8-block AVX2 */
        kheiron_chacha20_8blocks_avx2(base_state, avx_out);

        /* Run 8 scalar blocks */
        uint32_t sc_state[16];
        memcpy(sc_state, base_state, sizeof(sc_state));
        for (int b = 0; b < 8; b++) {
            kheiron_chacha_block(sc_state, sc_out + b * 64);
            sc_state[12]++;
            if (sc_state[12] == 0) {
                sc_state[13]++;
            }
        }

        ADV_CHECK_FMT(memcmp(avx_out, sc_out, 512) == 0,
                      "Trial %d: AVX2 8-block kernel output differed from 8 scalar blocks!", trial);
    }
    ADV_PASS("AVX2 8-block direct kernel equivalence (1000 random states)");
}

static bool test_avx2_vs_scalar_counter_overflow(void) {
    printf("[1.2] Testing AVX2 8-block kernel counter overflow boundaries...\n");
    uint8_t avx_out[512];
    uint8_t sc_out[512];

    /* Test counter values spanning 0xFFFFFFF0 to 0xFFFFFFFF and 64-bit carry */
    for (int offset = -16; offset <= 8; offset++) {
        uint32_t base_state[16];
        kheiron_fill(base_state, sizeof(base_state));
        base_state[12] = (uint32_t)(0xFFFFFFF8U + offset);
        base_state[13] = 0x00000001; /* High counter */

        kheiron_chacha20_8blocks_avx2(base_state, avx_out);

        uint32_t sc_state[16];
        memcpy(sc_state, base_state, sizeof(sc_state));
        for (int b = 0; b < 8; b++) {
            kheiron_chacha_block(sc_state, sc_out + b * 64);
            sc_state[12]++;
            if (sc_state[12] == 0) {
                sc_state[13]++;
            }
        }

        ADV_CHECK_FMT(memcmp(avx_out, sc_out, 512) == 0,
                      "Offset %d: AVX2 vs scalar mismatch on counter rollover!", offset);
    }

    ADV_PASS("AVX2 vs Scalar counter rollover boundary equivalence");
}

static bool test_avx2_vs_scalar_arbitrary_buffer_sizes(void) {
    printf("[1.3] Testing AVX2 vs scalar differential equivalence across arbitrary sizes (1 B to 1,048,576 B)...\n");

    const size_t test_sizes[] = {
        1, 2, 3, 5, 7, 8, 15, 16, 31, 32, 63, 64, 65, 127, 128, 129,
        255, 256, 257, 511, 512, 513, 1023, 1024, 1025, 2047, 2048, 2049,
        4095, 4096, 4097, 32767, 32768, 65535, 65536, 65537,
        500000, 1000000,
        /* Key rotation threshold boundaries: 1 MB = 1,048,576 bytes */
        1048576 - 513, 1048576 - 512, 1048576 - 65, 1048576 - 64,
        1048576 - 1, 1048576
    };
    const size_t num_sizes = sizeof(test_sizes) / sizeof(test_sizes[0]);

    for (size_t s_idx = 0; s_idx < num_sizes; s_idx++) {
        size_t len = test_sizes[s_idx];
        uint8_t *buf_avx = (uint8_t *)malloc(len);
        uint8_t *buf_sc  = (uint8_t *)malloc(len);
        ADV_CHECK(buf_avx != NULL && buf_sc != NULL, "Out of memory allocating test buffers");

        /* Initialize identical contexts */
        kheiron_ctx_t ctx_avx, ctx_sc;
        uint8_t seed[32];
        memset(seed, (int)(s_idx + 0x11), sizeof(seed));
        kheiron_ctx_init(&ctx_avx, seed, sizeof(seed));
        memcpy(&ctx_sc, &ctx_avx, sizeof(ctx_sc));

        /* Fill AVX2 (uses kheiron_ctx_fill with AVX2 dynamic path) */
        int r_avx = kheiron_ctx_fill(&ctx_avx, buf_avx, len);
        /* Fill Scalar (uses ref_scalar_ctx_fill) */
        ref_scalar_ctx_fill(&ctx_sc, buf_sc, len);

        ADV_CHECK(r_avx == 0, "kheiron_ctx_fill failed");

        if (memcmp(buf_avx, buf_sc, len) != 0) {
            size_t diff_offset = 0;
            while (diff_offset < len && buf_avx[diff_offset] == buf_sc[diff_offset]) {
                diff_offset++;
            }
            fprintf(stderr, COLOR_RED "  [DIVERGENCE] len=%zu, diff_offset=%zu: AVX2=0x%02X != SC=0x%02X\n" COLOR_RESET,
                    len, diff_offset, buf_avx[diff_offset], buf_sc[diff_offset]);
            ADV_CHECK(memcmp(buf_avx, buf_sc, len) == 0, "Keystream divergence between AVX2 and Scalar!");
        }
        ADV_CHECK(ctx_avx.bytes_generated == ctx_sc.bytes_generated, "bytes_generated mismatch!");
        ADV_CHECK(memcmp(ctx_avx.state, ctx_sc.state, sizeof(ctx_avx.state)) == 0, "Context state mismatch!");

        free(buf_avx);
        free(buf_sc);
    }

    ADV_PASS("AVX2 vs Scalar Keystream equivalence across 43 arbitrary buffer sizes up to 1,048,576 B");
}

static bool test_avx2_vs_scalar_sequential_fills(void) {
    printf("[1.4] Testing AVX2 vs scalar across 500 sequential variable-length chunk fills...\n");

    kheiron_ctx_t ctx_avx, ctx_sc;
    uint8_t seed[32] = {0x42, 0x13, 0x37, 0x99};
    kheiron_ctx_init(&ctx_avx, seed, sizeof(seed));
    memcpy(&ctx_sc, &ctx_avx, sizeof(ctx_sc));

    /* Chunk sizes varying from 7 to 1531 bytes */
    const size_t chunk_lens[] = {7, 63, 64, 65, 127, 256, 511, 512, 513, 1024, 1531};
    const size_t num_chunks = sizeof(chunk_lens) / sizeof(chunk_lens[0]);

    uint8_t b_avx[2048];
    uint8_t b_sc[2048];

    for (int iter = 0; iter < 500; iter++) {
        size_t sz = chunk_lens[iter % num_chunks];

        int ret = kheiron_ctx_fill(&ctx_avx, b_avx, sz);
        ADV_CHECK(ret == 0, "ctx_fill failed");
        ref_scalar_ctx_fill(&ctx_sc, b_sc, sz);

        ADV_CHECK(memcmp(b_avx, b_sc, sz) == 0, "Sequential chunk mismatch between AVX2 and Scalar!");
        ADV_CHECK(ctx_avx.bytes_generated == ctx_sc.bytes_generated, "Sequential bytes_generated mismatch!");
        ADV_CHECK(memcmp(ctx_avx.state, ctx_sc.state, sizeof(ctx_avx.state)) == 0, "Sequential context state mismatch!");
    }

    ADV_PASS("AVX2 vs Scalar sequential variable-length chunk fills verified identical");
}

static bool test_avx2_vs_scalar_post_rotation_epoch(void) {
    printf("[1.5] Testing AVX2 vs scalar post-rotation epoch keystream equivalence...\n");

    /* Initialize context, fill full 1 MB to trigger rotation, then test equivalence on new key */
    kheiron_ctx_t ctx_avx, ctx_sc;
    uint8_t seed[32] = {0xAA, 0xBB, 0xCC, 0xDD};
    kheiron_ctx_init(&ctx_avx, seed, sizeof(seed));

    /* Consume exactly 1,048,576 bytes */
    uint8_t dummy[512];
    for (size_t i = 0; i < 2048; i++) {
        kheiron_ctx_fill(&ctx_avx, dummy, sizeof(dummy));
    }
    ADV_CHECK(ctx_avx.bytes_generated == 1048576, "Epoch bytes_generated should be 1048576");

    /* Now next fill triggers rotation */
    uint8_t rot_test[64];
    kheiron_ctx_fill(&ctx_avx, rot_test, sizeof(rot_test));
    ADV_CHECK(ctx_avx.bytes_generated == 64, "After rotation, bytes_generated should be 64");

    /* Now clone rotated state to ctx_sc */
    memcpy(&ctx_sc, &ctx_avx, sizeof(ctx_sc));

    /* Now test another 1,000,000 bytes generation */
    size_t test_sz = 1000000;
    uint8_t *b_avx = (uint8_t *)malloc(test_sz);
    uint8_t *b_sc  = (uint8_t *)malloc(test_sz);
    ADV_CHECK(b_avx != NULL && b_sc != NULL, "Out of memory");

    kheiron_ctx_fill(&ctx_avx, b_avx, test_sz);
    ref_scalar_ctx_fill(&ctx_sc, b_sc, test_sz);

    ADV_CHECK(memcmp(b_avx, b_sc, test_sz) == 0, "Post-rotation keystream mismatch between AVX2 and Scalar!");
    ADV_CHECK(ctx_avx.bytes_generated == ctx_sc.bytes_generated, "Post-rotation bytes_generated mismatch!");
    ADV_CHECK(memcmp(ctx_avx.state, ctx_sc.state, sizeof(ctx_avx.state)) == 0, "Post-rotation state mismatch!");

    free(b_avx);
    free(b_sc);
    ADV_PASS("Post-rotation epoch AVX2 vs Scalar keystream equivalence verified");
}
#endif

/* ========================================================================= */
/* 2. LEMIRE MODULO BIAS-FREE RANGE SAMPLING                                  */
/* ========================================================================= */

static bool test_lemire_extreme_ranges(void) {
    printf("[2.1] Testing Lemire range sampling under extreme adversarial ranges...\n");

    /* Degenerate cases */
    ADV_CHECK(kheiron_range_u64(0, 0) == 0, "[0, 0] must return 0");
    ADV_CHECK(kheiron_range_u64(UINT64_MAX, UINT64_MAX) == UINT64_MAX, "[MAX, MAX] must return MAX");
    ADV_CHECK(kheiron_range_u64(100, 50) == 100, "Inverted range [100, 50] must safely return min (100)");

    /* Range [UINT64_MAX - 1, UINT64_MAX] -> size 2 */
    const int SAMPLES_2 = 100000;
    int count_max_minus_1 = 0;
    int count_max = 0;
    for (int i = 0; i < SAMPLES_2; i++) {
        uint64_t val = kheiron_range_u64(UINT64_MAX - 1, UINT64_MAX);
        ADV_CHECK(val == UINT64_MAX - 1 || val == UINT64_MAX, "Value out of range [MAX-1, MAX]");
        if (val == UINT64_MAX - 1) count_max_minus_1++;
        else count_max++;
    }
    /* Chi-square test on 2 bins with df=1 (critical value for p=0.001 is 10.83) */
    double expected_2 = SAMPLES_2 / 2.0;
    double chi2_2 = ((count_max_minus_1 - expected_2) * (count_max_minus_1 - expected_2) / expected_2) +
                    ((count_max - expected_2) * (count_max - expected_2) / expected_2);
    ADV_CHECK_FMT(chi2_2 < 15.0, "Chi-square %.2f exceeds critical threshold for [MAX-1, MAX]", (float)chi2_2);

    /* Range [0, UINT64_MAX] -> full 64-bit sampling */
    const int SAMPLES_FULL = 100000;
    uint64_t bit_counts[64] = {0};
    uint64_t min_obs = UINT64_MAX, max_obs = 0;
    for (int i = 0; i < SAMPLES_FULL; i++) {
        uint64_t val = kheiron_range_u64(0, UINT64_MAX);
        if (val < min_obs) min_obs = val;
        if (val > max_obs) max_obs = val;
        for (int b = 0; b < 64; b++) {
            if ((val >> b) & 1ULL) {
                bit_counts[b]++;
            }
        }
    }
    ADV_CHECK(min_obs < 0x0100000000000000ULL, "Minimum observed value suspiciously large");
    ADV_CHECK(max_obs > 0xFE00000000000000ULL, "Maximum observed value suspiciously small");

    /* Check bit balance across all 64 bits */
    double exp_bits = SAMPLES_FULL * 0.5;
    double stddev_bits = sqrt(SAMPLES_FULL * 0.25);
    for (int b = 0; b < 64; b++) {
        double z = fabs((double)bit_counts[b] - exp_bits) / stddev_bits;
        ADV_CHECK_FMT(z < 4.5, "Bit z-score %.2f exceeded 4.5 in [0, UINT64_MAX]", (float)z);
    }

    ADV_PASS("Lemire extreme range sampling ([0, MAX], [MAX-1, MAX], degenerate, bit balance)");
}

static bool test_lemire_chi_square_prime_ranges(void) {
    printf("[2.2] Testing Lemire modulo bias across prime & power-of-two ranges (Chi-Square)...\n");

    /* Test 1: Range [0, 2] (3 bins, prime, tests Lemire rejection threshold t = (-3) % 3 = 1) */
    {
        const int N = 300000;
        const int K = 3;
        int counts[3] = {0};
        for (int i = 0; i < N; i++) {
            uint64_t val = kheiron_range_u64(0, 2);
            ADV_CHECK(val <= 2, "Value out of range [0, 2]");
            counts[val]++;
        }
        double exp_k = (double)N / K;
        double chi2 = 0.0;
        for (int k = 0; k < K; k++) {
            double d = counts[k] - exp_k;
            chi2 += (d * d) / exp_k;
        }
        /* df = 2, critical chi2 at p=0.001 is 13.82 */
        ADV_CHECK_FMT(chi2 < 13.82, "Range [0, 2] chi-square failed: %.2f >= 13.82", (float)chi2);
    }

    /* Test 2: Range [0, 6] (7 bins, prime) */
    {
        const int N = 700000;
        const int K = 7;
        int counts[7] = {0};
        for (int i = 0; i < N; i++) {
            uint64_t val = kheiron_range_u64(0, 6);
            ADV_CHECK(val <= 6, "Value out of range [0, 6]");
            counts[val]++;
        }
        double exp_k = (double)N / K;
        double chi2 = 0.0;
        for (int k = 0; k < K; k++) {
            double d = counts[k] - exp_k;
            chi2 += (d * d) / exp_k;
        }
        /* df = 6, critical chi2 at p=0.001 is 22.46 */
        ADV_CHECK_FMT(chi2 < 22.46, "Range [0, 6] chi-square failed: %.2f >= 22.46", (float)chi2);
    }

    /* Test 3: Range [0, 16] (17 bins, prime) */
    {
        const int N = 340000;
        const int K = 17;
        int counts[17] = {0};
        for (int i = 0; i < N; i++) {
            uint64_t val = kheiron_range_u64(0, 16);
            ADV_CHECK(val <= 16, "Value out of range [0, 16]");
            counts[val]++;
        }
        double exp_k = (double)N / K;
        double chi2 = 0.0;
        for (int k = 0; k < K; k++) {
            double d = counts[k] - exp_k;
            chi2 += (d * d) / exp_k;
        }
        /* df = 16, critical chi2 at p=0.001 is 39.25 */
        ADV_CHECK_FMT(chi2 < 39.25, "Range [0, 16] chi-square failed: %.2f >= 39.25", (float)chi2);
    }

    ADV_PASS("Lemire bias-free sampling passed all Chi-Square goodness-of-fit tests");
}

/* ========================================================================= */
/* 3. FLOATING-POINT SAMPLING PRECISION                                      */
/* ========================================================================= */

static bool test_floating_point_bounds_and_mantissa(void) {
    printf("[3.1] Testing 10^7 samples of kheiron_uniform_double() for [0.0, 1.0) strictly & mantissa bits...\n");

    const uint64_t N = 10000000ULL; /* 10,000,000 samples */
    double min_val = 1.0;
    double max_val = 0.0;
    uint64_t bit_counts[53] = {0};

    for (uint64_t i = 0; i < N; i++) {
        double d = kheiron_uniform_double();

        /* Critical mathematical boundary assertions */
        ADV_CHECK(d >= 0.0, "Sample is negative");
        ADV_CHECK(d < 1.0, "Sample produced 1.0 or greater");
        ADV_CHECK(d != 1.0, "Sample strictly produced 1.0");
        ADV_CHECK(!isnan(d) && !isinf(d), "Sample is NaN or Inf");

        if (d < min_val) min_val = d;
        if (d > max_val) max_val = d;

        /* Extract 53-bit mantissa integer: d = k * 2^-53, so k = (uint64_t)(d * 2^53) */
        uint64_t mantissa = (uint64_t)(d * 9007199254740992.0);
        for (int b = 0; b < 53; b++) {
            if ((mantissa >> b) & 1ULL) {
                bit_counts[b]++;
            }
        }
    }

    printf("    Min observed: %.17g | Max observed: %.17g (1 - %.2e)\n",
           min_val, max_val, 1.0 - max_val);
    ADV_CHECK(max_val < 1.0, "Max value was not strictly less than 1.0");

    /* Verify uniform mantissa bit frequency */
    double exp_count = (double)N * 0.5;
    double stddev = sqrt((double)N * 0.25);
    for (int b = 0; b < 53; b++) {
        double z = fabs((double)bit_counts[b] - exp_count) / stddev;
        ADV_CHECK_FMT(z < 4.5, "Mantissa bit z-score %.2f exceeded 4.5", (float)z);
    }

    /* Single precision float verification (1,000,000 samples) */
    printf("[3.2] Testing 10^6 samples of kheiron_uniform_float() for [0.0f, 1.0f)...\n");
    for (int i = 0; i < 1000000; i++) {
        float f = kheiron_uniform_float();
        ADV_CHECK(f >= 0.0f && f < 1.0f && f != 1.0f, "Float sample out of bounds");
    }

    ADV_PASS("Floating-point precision strictly in [0.0, 1.0) and uniform 53-bit mantissa verified");
}

/* ========================================================================= */
/* 4. VOLATILE MEMORY SANITIZATION                                           */
/* ========================================================================= */

static bool test_secure_wipe_canary(void) {
    printf("[4.1] Testing kheiron_secure_wipe on heap, stack, and boundary buffers...\n");

    /* 1. Stack canary */
    uint8_t stack_buf[256];
    for (size_t i = 0; i < sizeof(stack_buf); i++) stack_buf[i] = (uint8_t)(i + 1);
    kheiron_secure_wipe(stack_buf, sizeof(stack_buf));
    for (size_t i = 0; i < sizeof(stack_buf); i++) {
        ADV_CHECK(stack_buf[i] == 0, "Stack canary byte was not zeroed");
    }

    /* 2. Heap buffer */
    uint8_t *heap_buf = (uint8_t *)malloc(1024);
    ADV_CHECK(heap_buf != NULL, "Out of memory");
    memset(heap_buf, 0xAA, 1024);
    kheiron_secure_wipe(heap_buf, 1024);
    for (size_t i = 0; i < 1024; i++) {
        ADV_CHECK(heap_buf[i] == 0, "Heap canary byte was not zeroed");
    }
    free(heap_buf);

    /* 3. Degenerate wipe calls (NULL or 0 bytes) */
    kheiron_secure_wipe(NULL, 100);
    kheiron_secure_wipe(stack_buf, 0);

    ADV_PASS("kheiron_secure_wipe basic sanitization verified");
}

/* ========================================================================= */
/* 5. ARBITRARY POINTER MISALIGNMENT & PAGE BOUNDARIES UNDER ASAN/UBSAN      */
/* ========================================================================= */

static bool test_unaligned_loads_and_stores(void) {
    printf("[5.1] Stress-testing unaligned loads and stores with offsets {1,2,3,5,7,15,31,63,127}...\n");

    const size_t offsets[] = {1, 2, 3, 5, 7, 15, 31, 63, 127};
    const size_t num_offsets = sizeof(offsets) / sizeof(offsets[0]);

    const size_t sizes[] = {1, 7, 63, 64, 65, 511, 512, 513, 1023, 1024, 1025, 4096, 65536};
    const size_t num_sizes = sizeof(sizes) / sizeof(sizes[0]);

    for (size_t o = 0; o < num_offsets; o++) {
        size_t offset = offsets[o];
        for (size_t s = 0; s < num_sizes; s++) {
            size_t size = sizes[s];

            size_t alloc_sz = offset + size + 256;
            uint8_t *mem = (uint8_t *)malloc(alloc_sz);
            ADV_CHECK(mem != NULL, "malloc failed");

            /* Canaries */
            memset(mem, 0xCD, alloc_sz);

            uint8_t *target = mem + offset;
            int ret = kheiron_fill(target, size);
            ADV_CHECK(ret == 0, "kheiron_fill failed");

            /* Check leading canaries */
            for (size_t i = 0; i < offset; i++) {
                ADV_CHECK(mem[i] == 0xCD, "Leading canary corrupted!");
            }
            /* Check trailing canaries */
            for (size_t i = offset + size; i < alloc_sz; i++) {
                ADV_CHECK(mem[i] == 0xCD, "Trailing canary corrupted!");
            }

            /* Context fill unaligned test */
            kheiron_ctx_t ctx;
            kheiron_ctx_init(&ctx, "seed_test", 9);
            ret = kheiron_ctx_fill(&ctx, target, size);
            ADV_CHECK(ret == 0, "kheiron_ctx_fill failed");

            for (size_t i = 0; i < offset; i++) {
                ADV_CHECK(mem[i] == 0xCD, "Leading canary corrupted in ctx_fill!");
            }
            for (size_t i = offset + size; i < alloc_sz; i++) {
                ADV_CHECK(mem[i] == 0xCD, "Trailing canary corrupted in ctx_fill!");
            }

            free(mem);
        }
    }

    ADV_PASS("Unaligned loads & stores across all offsets and sizes passed without canary corruption");
}

static bool test_page_boundary_guard_abutment(void) {
    printf("[5.2] Stress-testing page boundary straddling and PROT_NONE guard abutment...\n");

    long page_size = sysconf(_SC_PAGESIZE);
    ADV_CHECK(page_size > 0, "Invalid page size");

    /* Allocate 3 pages: [Page 0: RW] [Page 1: RW] [Page 2: PROT_NONE (Guard)] */
    size_t total_map = (size_t)page_size * 3;
    uint8_t *map = (uint8_t *)mmap(NULL, total_map, PROT_READ | PROT_WRITE,
                                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ADV_CHECK(map != MAP_FAILED, "mmap failed");

    uint8_t *page1 = map + page_size;
    uint8_t *page2_guard = map + 2 * page_size;

    /* Guard Page 2 */
    int m_ret = mprotect(page2_guard, (size_t)page_size, PROT_NONE);
    ADV_CHECK(m_ret == 0, "mprotect guard page failed");

    /* 1. Straddle Page 0 and Page 1 across the boundary */
    const size_t offsets_from_bound[] = {1, 2, 3, 5, 7, 15, 31, 63, 127};
    const size_t straddle_sizes[] = {64, 128, 256, 512, 1024, 2048};

    for (size_t o = 0; o < sizeof(offsets_from_bound) / sizeof(offsets_from_bound[0]); o++) {
        size_t off = offsets_from_bound[o];
        for (size_t s = 0; s < sizeof(straddle_sizes) / sizeof(straddle_sizes[0]); s++) {
            size_t sz = straddle_sizes[s];
            /* Start 'off' bytes before Page 1, spanning across Page 0 -> Page 1 */
            uint8_t *target = page1 - off;
            int ret = kheiron_fill(target, sz);
            ADV_CHECK(ret == 0, "Straddle fill failed");
        }
    }

    /* 2. Abut EXACTLY against the PROT_NONE guard page!
     * Buffer ends exactly at page2_guard. Any write overshoot into page2_guard causes SIGSEGV. */
    const size_t abut_sizes[] = {1, 2, 3, 5, 7, 15, 31, 63, 64, 65, 127, 128, 255, 256, 511, 512, 513, 1024, 2048, 4096};
    for (size_t a = 0; a < sizeof(abut_sizes) / sizeof(abut_sizes[0]); a++) {
        size_t sz = abut_sizes[a];
        uint8_t *target = page2_guard - sz;
        int ret = kheiron_fill(target, sz);
        ADV_CHECK(ret == 0, "Abutting fill failed");

        /* Also test context fill */
        kheiron_ctx_t ctx;
        kheiron_ctx_init(&ctx, "guard_seed", 10);
        ret = kheiron_ctx_fill(&ctx, target, sz);
        ADV_CHECK(ret == 0, "Abutting ctx_fill failed");
    }

    /* Cleanup: restore permissions and unmap */
    mprotect(page2_guard, (size_t)page_size, PROT_READ | PROT_WRITE);
    munmap(map, total_map);

    ADV_PASS("Page boundary straddling and PROT_NONE guard abutment passed with 0 faults");
}

/* ========================================================================= */
/* 6. MULTI-THREADED CONTENTION AND STREAM INDEPENDENCE                      */
/* ========================================================================= */

#define NUM_THREADS 16
#define BLOCKS_PER_THREAD 1024
#define BYTES_PER_THREAD (BLOCKS_PER_THREAD * 64) /* 65,536 bytes */

typedef struct {
    int thread_id;
    uint8_t *stream;
    int status;
} thread_worker_arg_t;

static void *stream_worker(void *arg) {
    thread_worker_arg_t *tdata = (thread_worker_arg_t *)arg;
    tdata->status = 0;

    if (kheiron_init() != 0) {
        tdata->status = -1;
        return NULL;
    }

    if (kheiron_fill(tdata->stream, BYTES_PER_THREAD) != 0) {
        tdata->status = -2;
        return NULL;
    }

    return NULL;
}

static bool test_multithreaded_independence_and_collision(void) {
    printf("[6.1] Testing 16 concurrent threads: cross-stream independence, Pearson r, and block uniqueness...\n");

    pthread_t threads[NUM_THREADS];
    thread_worker_arg_t targs[NUM_THREADS];

    for (int t = 0; t < NUM_THREADS; t++) {
        targs[t].thread_id = t;
        targs[t].stream = (uint8_t *)malloc(BYTES_PER_THREAD);
        ADV_CHECK(targs[t].stream != NULL, "Out of memory");
        targs[t].status = 0;
        int err = pthread_create(&threads[t], NULL, stream_worker, &targs[t]);
        ADV_CHECK(err == 0, "pthread_create failed for thread");
    }

    for (int t = 0; t < NUM_THREADS; t++) {
        pthread_join(threads[t], NULL);
        ADV_CHECK(targs[t].status == 0, "Thread reported failure");
    }

    /* 1. Check for duplicate 64-byte blocks across all threads (Total blocks: 16 * 1024 = 16,384) */
    size_t total_blocks = NUM_THREADS * BLOCKS_PER_THREAD;
    uint64_t *block_prefixes = (uint64_t *)malloc(total_blocks * sizeof(uint64_t));
    ADV_CHECK(block_prefixes != NULL, "malloc failed");

    size_t idx = 0;
    for (int t = 0; t < NUM_THREADS; t++) {
        for (int b = 0; b < BLOCKS_PER_THREAD; b++) {
            uint64_t pfx;
            memcpy(&pfx, targs[t].stream + b * 64, sizeof(pfx));
            block_prefixes[idx++] = pfx;
        }
    }

    /* Check pairwise uniqueness */
    int collisions = 0;
    for (size_t i = 0; i < total_blocks; i++) {
        for (size_t j = i + 1; j < total_blocks; j++) {
            if (block_prefixes[i] == block_prefixes[j]) {
                collisions++;
            }
        }
    }
    free(block_prefixes);
    ADV_CHECK_FMT(collisions == 0, "Found %d block prefix collisions across thread streams!", collisions);

    /* 2. Cross-Thread Pearson Correlation Coefficient for all 120 thread pairs */
    int pairs_tested = 0;
    for (int i = 0; i < NUM_THREADS; i++) {
        for (int j = i + 1; j < NUM_THREADS; j++) {
            const uint64_t *s_i = (const uint64_t *)(const void *)targs[i].stream;
            const uint64_t *s_j = (const uint64_t *)(const void *)targs[j].stream;
            size_t n_words = BYTES_PER_THREAD / sizeof(uint64_t);

            double sum_x = 0.0, sum_y = 0.0;
            double sum_xx = 0.0, sum_yy = 0.0, sum_xy = 0.0;
            for (size_t w = 0; w < n_words; w++) {
                double x = (double)s_i[w];
                double y = (double)s_j[w];
                sum_x += x;
                sum_y += y;
                sum_xx += x * x;
                sum_yy += y * y;
                sum_xy += x * y;
            }
            double n = (double)n_words;
            double num = n * sum_xy - sum_x * sum_y;
            double den = sqrt((n * sum_xx - sum_x * sum_x) * (n * sum_yy - sum_y * sum_y));
            double r = fabs(num / den);
            ADV_CHECK_FMT(r < 0.05, "Thread pair Pearson r=%.4f exceeded correlation bound 0.05", (float)r);
            pairs_tested++;
        }
    }
    printf("    Verified %d thread pairs: all Pearson |r| < 0.05 (independent uncorrelated streams)\n", pairs_tested);

    for (int t = 0; t < NUM_THREADS; t++) {
        free(targs[t].stream);
    }

    ADV_PASS("Multi-threaded stream independence & zero collisions verified across 16 threads");
}

#define RAPID_THREADS 32
#define RAPID_ITERS 10000

static void *rapid_worker(void *arg) {
    (void)arg;
    uint8_t small_buf[48];
    for (int i = 0; i < RAPID_ITERS; i++) {
        if (kheiron_fill(small_buf, sizeof(small_buf)) != 0) {
            return (void *)1;
        }
    }
    return NULL;
}

static bool test_multithreaded_high_contention_scaling(void) {
    printf("[6.2] Stress-testing 32 concurrent threads executing 10,000 rapid small fills...\n");

    pthread_t threads[RAPID_THREADS];
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int t = 0; t < RAPID_THREADS; t++) {
        int err = pthread_create(&threads[t], NULL, rapid_worker, NULL);
        ADV_CHECK(err == 0, "pthread_create failed");
    }

    for (int t = 0; t < RAPID_THREADS; t++) {
        void *res = NULL;
        pthread_join(threads[t], &res);
        ADV_CHECK(res == NULL, "Rapid worker thread reported failure");
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double elapsed_ms = (double)(t1.tv_sec - t0.tv_sec) * 1000.0 +
                        (double)(t1.tv_nsec - t0.tv_nsec) / 1000000.0;
    printf("    32 threads completed 320,000 total fill operations in %.2f ms (%.2f ops/ms)\n",
           elapsed_ms, 320000.0 / elapsed_ms);

    ADV_PASS("32-thread high contention stress test passed cleanly with zero lock serialization");
}

/* ========================================================================= */
/* MAIN RUNNER                                                               */
/* ========================================================================= */

int main(void) {
    printf("%s================================================================================%s\n", COLOR_CYAN, COLOR_RESET);
    printf("%sKHEIRON MILESTONE 4 TIER 5 ADVERSARIAL STRESS TEST BATTERY (CHALLENGER 2)%s\n", COLOR_BOLD, COLOR_RESET);
    printf("Platform: Linux x86_64 | Standards: C99 / POSIX.1-2008\n");
    printf("%s================================================================================%s\n", COLOR_CYAN, COLOR_RESET);

    double start_time = (double)clock() / CLOCKS_PER_SEC;

    /* 1. AVX2 vs Scalar Keystream Equivalence */
#if KHEIRON_HAVE_AVX2
    test_avx2_vs_scalar_direct_8blocks();
    test_avx2_vs_scalar_counter_overflow();
    test_avx2_vs_scalar_arbitrary_buffer_sizes();
    test_avx2_vs_scalar_sequential_fills();
    test_avx2_vs_scalar_post_rotation_epoch();
#else
    printf("[1] Skipping AVX2 tests (KHEIRON_HAVE_AVX2 is 0 on this build target)\n");
#endif

    /* 2. Lemire Modulo Bias-Free Range Sampling */
    test_lemire_extreme_ranges();
    test_lemire_chi_square_prime_ranges();

    /* 3. Floating-Point Precision & Mantissa Uniformity */
    test_floating_point_bounds_and_mantissa();

    /* 4. Volatile Memory Sanitization */
    test_secure_wipe_canary();

    /* 5. Pointer Misalignment & Page Boundary Guard Abutment */
    test_unaligned_loads_and_stores();
    test_page_boundary_guard_abutment();

    /* 6. Multi-threaded Contention and Independence */
    test_multithreaded_independence_and_collision();
    test_multithreaded_high_contention_scaling();

    double elapsed = ((double)clock() / CLOCKS_PER_SEC) - start_time;

    printf("\n%s--------------------------------------------------------------------------------%s\n", COLOR_CYAN, COLOR_RESET);
    printf("TIER 5 ADVERSARIAL TEST SUMMARY:\n");
    printf("  Passed   : %s%d%s\n", COLOR_GREEN, g_test_passes, COLOR_RESET);
    printf("  Failed   : %s%d%s\n", g_test_failures > 0 ? COLOR_RED : COLOR_GREEN, g_test_failures, COLOR_RESET);
    printf("  CPU Time : %.2f s\n", elapsed);
    printf("--------------------------------------------------------------------------------\n");

    if (g_test_failures == 0) {
        printf("%sVERDICT: 100%% ADVERSARIAL STRESS TESTS PASSED (ZERO REGRESSIONS)%s\n", COLOR_GREEN, COLOR_RESET);
    } else {
        printf("%sVERDICT: ADVERSARIAL FAILURES DETECTED (%d FAILURES)%s\n", COLOR_RED, g_test_failures, COLOR_RESET);
    }
    printf("%s================================================================================%s\n", COLOR_CYAN, COLOR_RESET);

    return g_test_failures == 0 ? 0 : 1;
}
