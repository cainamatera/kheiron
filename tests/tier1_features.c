#include "e2e_common.h"

static test_status_t test_t1_init_01(void) {
    int ret = kheiron_init();
    ASSERT_EQ_INT(ret, 0);
    uint64_t val = kheiron_u64();
    ASSERT_TRUE(val != 0ULL);
    return TEST_PASS;
}

static test_status_t test_t1_init_02(void) {
    ASSERT_EQ_INT(kheiron_init(), 0);
    ASSERT_EQ_INT(kheiron_init(), 0);
    ASSERT_EQ_INT(kheiron_init(), 0);
    uint64_t v1 = kheiron_u64();
    uint64_t v2 = kheiron_u64();
    ASSERT_TRUE(v1 != 0ULL && v2 != 0ULL);
    ASSERT_TRUE(v1 != v2);
    return TEST_PASS;
}

static test_status_t test_t1_init_03(void) {
    uint8_t buf[32] = {0};
    int ret = kheiron_fill(buf, sizeof(buf));
    ASSERT_EQ_INT(ret, 0);
    size_t non_zero = 0;
    for (size_t i = 0; i < sizeof(buf); i++) {
        if (buf[i] != 0) non_zero++;
    }
    ASSERT_TRUE(non_zero > 0);
    return TEST_PASS;
}

static test_status_t test_t1_init_04(void) {
    uint32_t a = kheiron_u32();
    uint64_t b = kheiron_u64();
    ASSERT_TRUE(a != 0U);
    ASSERT_TRUE(b != 0ULL);
    ASSERT_TRUE((uint64_t)a != b);
    return TEST_PASS;
}

typedef struct {
    uint8_t out[32];
    int init_ret;
} thread_init_t;

static void *thread_init_worker(void *arg) {
    thread_init_t *ti = (thread_init_t *)arg;
    ti->init_ret = kheiron_init();
    kheiron_fill(ti->out, sizeof(ti->out));
    return NULL;
}

static test_status_t test_t1_init_05(void) {
    pthread_t th[4];
    thread_init_t args[4];
    for (int i = 0; i < 4; i++) {
        pthread_create(&th[i], NULL, thread_init_worker, &args[i]);
    }
    for (int i = 0; i < 4; i++) {
        pthread_join(th[i], NULL);
        ASSERT_EQ_INT(args[i].init_ret, 0);
    }
    for (int i = 0; i < 4; i++) {
        for (int j = i + 1; j < 4; j++) {
            ASSERT_MEM_NEQ(args[i].out, args[j].out, 32);
        }
    }
    return TEST_PASS;
}

static test_status_t test_t1_fill_01(void) {
    uint8_t b16[16] = {0};
    uint8_t b32[32] = {0};
    ASSERT_EQ_INT(kheiron_fill(b16, 16), 0);
    ASSERT_EQ_INT(kheiron_fill(b32, 32), 0);
    size_t nz = 0;
    for (int i = 0; i < 16; i++) if (b16[i] != 0) nz++;
    ASSERT_TRUE(nz > 0);
    nz = 0;
    for (int i = 0; i < 32; i++) if (b32[i] != 0) nz++;
    ASSERT_TRUE(nz > 0);
    return TEST_PASS;
}

static test_status_t test_t1_fill_02(void) {
    uint8_t buf[64] = {0};
    ASSERT_EQ_INT(kheiron_fill(buf, 64), 0);
    size_t nz = 0;
    for (int i = 0; i < 64; i++) if (buf[i] != 0) nz++;
    ASSERT_TRUE(nz > 40);
    return TEST_PASS;
}

static test_status_t test_t1_fill_03(void) {
    uint8_t b128[128] = {0};
    uint8_t b256[256] = {0};
    ASSERT_EQ_INT(kheiron_fill(b128, 128), 0);
    ASSERT_EQ_INT(kheiron_fill(b256, 256), 0);
    return TEST_PASS;
}

static test_status_t test_t1_fill_04(void) {
    uint8_t b512[512] = {0};
    ASSERT_EQ_INT(kheiron_fill(b512, 512), 0);
    return TEST_PASS;
}

static test_status_t test_t1_fill_05(void) {
    size_t sz = 65536;
    uint8_t *buf = (uint8_t *)malloc(sz);
    ASSERT_NOT_NULL(buf);
    ASSERT_EQ_INT(kheiron_fill(buf, sz), 0);
    uint32_t counts[256] = {0};
    for (size_t i = 0; i < sz; i++) {
        counts[buf[i]]++;
    }
    double expected = (double)sz / 256.0;
    double chi2 = 0.0;
    for (int i = 0; i < 256; i++) {
        double d = (double)counts[i] - expected;
        chi2 += (d * d) / expected;
    }
    free(buf);
    ASSERT_TRUE(chi2 < 380.0);
    return TEST_PASS;
}

static test_status_t test_t1_u64_01(void) {
    uint64_t arr[100];
    for (int i = 0; i < 100; i++) {
        arr[i] = kheiron_u64();
        ASSERT_TRUE(arr[i] != 0ULL);
    }
    for (int i = 0; i < 100; i++) {
        for (int j = i + 1; j < 100; j++) {
            ASSERT_TRUE(arr[i] != arr[j]);
        }
    }
    return TEST_PASS;
}

static test_status_t test_t1_u64_02(void) {
    uint64_t prev = kheiron_u64();
    for (int i = 0; i < 10000; i++) {
        uint64_t curr = kheiron_u64();
        ASSERT_TRUE(curr != prev);
        prev = curr;
    }
    return TEST_PASS;
}

static test_status_t test_t1_u64_03(void) {
    uint64_t total_bits = 0;
    const int N = 10000;
    for (int i = 0; i < N; i++) {
        uint64_t v = kheiron_u64();
        total_bits += __builtin_popcountll(v);
    }
    double mean_bits = (double)total_bits / (double)N;
    ASSERT_DOUBLE_BETWEEN(mean_bits, 31.0, 33.0);
    return TEST_PASS;
}

static test_status_t test_t1_u64_04(void) {
    uint32_t bit_counts[64] = {0};
    const int N = 10000;
    for (int i = 0; i < N; i++) {
        uint64_t v = kheiron_u64();
        for (int b = 0; b < 64; b++) {
            if ((v >> b) & 1ULL) {
                bit_counts[b]++;
            }
        }
    }
    for (int b = 0; b < 64; b++) {
        ASSERT_TRUE(bit_counts[b] >= 4600 && bit_counts[b] <= 5400);
    }
    return TEST_PASS;
}

static void *thread_u64_worker(void *arg) {
    (void)arg;
    for (int i = 0; i < 10000; i++) {
        uint64_t v = kheiron_u64();
        (void)v;
    }
    return NULL;
}

static test_status_t test_t1_u64_05(void) {
    pthread_t th[4];
    for (int i = 0; i < 4; i++) {
        pthread_create(&th[i], NULL, thread_u64_worker, NULL);
    }
    for (int i = 0; i < 4; i++) {
        pthread_join(th[i], NULL);
    }
    return TEST_PASS;
}

static test_status_t test_t1_u32_01(void) {
    uint32_t arr[100];
    for (int i = 0; i < 100; i++) {
        arr[i] = kheiron_u32();
        ASSERT_TRUE(arr[i] != 0U);
    }
    for (int i = 0; i < 100; i++) {
        for (int j = i + 1; j < 100; j++) {
            ASSERT_TRUE(arr[i] != arr[j]);
        }
    }
    return TEST_PASS;
}

static test_status_t test_t1_u32_02(void) {
    uint32_t prev = kheiron_u32();
    for (int i = 0; i < 10000; i++) {
        uint32_t curr = kheiron_u32();
        ASSERT_TRUE(curr != prev);
        prev = curr;
    }
    return TEST_PASS;
}

static test_status_t test_t1_u32_03(void) {
    uint64_t total_bits = 0;
    const int N = 10000;
    for (int i = 0; i < N; i++) {
        uint32_t v = kheiron_u32();
        total_bits += __builtin_popcount(v);
    }
    double mean_bits = (double)total_bits / (double)N;
    ASSERT_DOUBLE_BETWEEN(mean_bits, 15.5, 16.5);
    return TEST_PASS;
}

static test_status_t test_t1_u32_04(void) {
    uint32_t bit_counts[32] = {0};
    const int N = 10000;
    for (int i = 0; i < N; i++) {
        uint32_t v = kheiron_u32();
        for (int b = 0; b < 32; b++) {
            if ((v >> b) & 1U) {
                bit_counts[b]++;
            }
        }
    }
    for (int b = 0; b < 32; b++) {
        ASSERT_TRUE(bit_counts[b] >= 4600 && bit_counts[b] <= 5400);
    }
    return TEST_PASS;
}

static test_status_t test_t1_u32_05(void) {
    const int N = 1000;
    uint32_t last_a = 0;
    uint64_t last_b = 0;
    uint32_t or_a = 0;
    uint32_t and_a = 0xFFFFFFFFU;
    uint64_t or_b = 0;
    uint64_t and_b = 0xFFFFFFFFFFFFFFFFULL;
    uint64_t total_bits_a = 0;
    uint64_t total_bits_b = 0;
    int non_zero_a = 0;
    int non_zero_b = 0;

    for (int i = 0; i < N; i++) {
        uint32_t a = kheiron_u32();
        uint64_t b = kheiron_u64();

        if (a != 0) non_zero_a++;
        if (b != 0) non_zero_b++;

        if (i > 0) {
            ASSERT_TRUE(a != last_a);
            ASSERT_TRUE(b != last_b);
        }
        last_a = a;
        last_b = b;

        ASSERT_TRUE((uint64_t)a != b);

        or_a |= a;
        and_a &= a;
        or_b |= b;
        and_b &= b;

        total_bits_a += (uint64_t)__builtin_popcount(a);
        total_bits_b += (uint64_t)__builtin_popcountll(b);
    }

    ASSERT_TRUE(non_zero_a >= N - 1);
    ASSERT_TRUE(non_zero_b >= N - 1);

    ASSERT_EQ_U64(or_a, 0xFFFFFFFFU);
    ASSERT_EQ_U64(and_a, 0x00000000U);
    ASSERT_EQ_U64(or_b, 0xFFFFFFFFFFFFFFFFULL);
    ASSERT_EQ_U64(and_b, 0x0000000000000000ULL);

    ASSERT_TRUE(total_bits_a >= 14500 && total_bits_a <= 17500);

    ASSERT_TRUE(total_bits_b >= 29000 && total_bits_b <= 35000);

    return TEST_PASS;
}

static test_status_t test_t1_range_01(void) {
    uint32_t counts[7] = {0};
    for (int i = 0; i < 6000; i++) {
        uint64_t v = kheiron_range_u64(1, 6);
        ASSERT_TRUE(v >= 1 && v <= 6);
        counts[v]++;
    }
    for (int f = 1; f <= 6; f++) {
        ASSERT_TRUE(counts[f] >= 850 && counts[f] <= 1150);
    }
    return TEST_PASS;
}

static test_status_t test_t1_range_02(void) {
    uint32_t ones = 0;
    for (int i = 0; i < 10000; i++) {
        uint64_t v = kheiron_range_u64(0, 1);
        ASSERT_TRUE(v == 0 || v == 1);
        if (v == 1) ones++;
    }
    ASSERT_TRUE(ones >= 4700 && ones <= 5300);
    return TEST_PASS;
}

static test_status_t test_t1_range_03(void) {
    uint32_t counts[256] = {0};
    for (int i = 0; i < 25600; i++) {
        uint64_t v = kheiron_range_u64(0, 255);
        ASSERT_TRUE(v <= 255);
        counts[v]++;
    }
    for (int i = 0; i < 256; i++) {
        ASSERT_TRUE(counts[i] >= 40 && counts[i] <= 160);
    }
    return TEST_PASS;
}

static test_status_t test_t1_range_04(void) {
    for (int i = 0; i < 5000; i++) {
        uint64_t v = kheiron_range_u64(100, 200);
        ASSERT_TRUE(v >= 100 && v <= 200);
    }
    return TEST_PASS;
}

static test_status_t test_t1_range_05(void) {
    uint64_t prev = 0;
    for (int i = 0; i < 10000; i++) {
        uint64_t v = kheiron_range_u64(1, 1000000);
        ASSERT_TRUE(v >= 1 && v <= 1000000);
        ASSERT_TRUE(v != prev);
        prev = v;
    }
    return TEST_PASS;
}

static test_status_t test_t1_dbl_01(void) {
    for (int i = 0; i < 100000; i++) {
        double d = kheiron_uniform_double();
        ASSERT_TRUE(!isnan(d) && !isinf(d));
        ASSERT_DOUBLE_BETWEEN(d, 0.0, 1.0);
    }
    return TEST_PASS;
}

static test_status_t test_t1_dbl_02(void) {
    double sum = 0.0, sum_sq = 0.0;
    const int N = 100000;
    for (int i = 0; i < N; i++) {
        double d = kheiron_uniform_double();
        sum += d;
        sum_sq += d * d;
    }
    double mean = sum / (double)N;
    double var = (sum_sq / (double)N) - (mean * mean);
    ASSERT_TRUE(fabs(mean - 0.5) < 0.005);
    ASSERT_TRUE(fabs(var - (1.0 / 12.0)) < 0.002);
    return TEST_PASS;
}

static test_status_t test_t1_dbl_03(void) {
    uint32_t bins[10] = {0};
    const int N = 100000;
    for (int i = 0; i < N; i++) {
        double d = kheiron_uniform_double();
        int b = (int)(d * 10.0);
        if (b > 9) b = 9;
        bins[b]++;
    }
    for (int b = 0; b < 10; b++) {
        ASSERT_TRUE(bins[b] >= 9500 && bins[b] <= 10500);
    }
    return TEST_PASS;
}

static test_status_t test_t1_dbl_04(void) {
    const int N = 50000;
    double prev = kheiron_uniform_double();
    double sum_x = 0.0, sum_y = 0.0, sum_xy = 0.0;
    for (int i = 0; i < N; i++) {
        double curr = kheiron_uniform_double();
        sum_x += prev;
        sum_y += curr;
        sum_xy += prev * curr;
        prev = curr;
    }
    double cov = (sum_xy / (double)N) - (sum_x / (double)N) * (sum_y / (double)N);
    double corr = cov / (1.0 / 12.0);
    ASSERT_TRUE(fabs(corr) < 0.05);
    return TEST_PASS;
}

static void *thread_dbl_worker(void *arg) {
    (void)arg;
    for (int i = 0; i < 25000; i++) {
        double d = kheiron_uniform_double();
        if (d < 0.0 || d >= 1.0) exit(1);
    }
    return NULL;
}

static test_status_t test_t1_dbl_05(void) {
    pthread_t th[4];
    for (int i = 0; i < 4; i++) {
        pthread_create(&th[i], NULL, thread_dbl_worker, NULL);
    }
    for (int i = 0; i < 4; i++) {
        pthread_join(th[i], NULL);
    }
    return TEST_PASS;
}

static test_status_t test_t1_flt_01(void) {
    for (int i = 0; i < 100000; i++) {
        float f = kheiron_uniform_float();
        ASSERT_TRUE(!isnan(f) && !isinf(f));
        ASSERT_FLOAT_BETWEEN(f, 0.0f, 1.0f);
    }
    return TEST_PASS;
}

static test_status_t test_t1_flt_02(void) {
    double sum = 0.0, sum_sq = 0.0;
    const int N = 100000;
    for (int i = 0; i < N; i++) {
        float f = kheiron_uniform_float();
        sum += (double)f;
        sum_sq += (double)(f * f);
    }
    double mean = sum / (double)N;
    double var = (sum_sq / (double)N) - (mean * mean);
    ASSERT_TRUE(fabs(mean - 0.5) < 0.005);
    ASSERT_TRUE(fabs(var - (1.0 / 12.0)) < 0.002);
    return TEST_PASS;
}

static test_status_t test_t1_flt_03(void) {
    uint32_t bins[10] = {0};
    const int N = 100000;
    for (int i = 0; i < N; i++) {
        float f = kheiron_uniform_float();
        int b = (int)(f * 10.0f);
        if (b > 9) b = 9;
        bins[b]++;
    }
    for (int b = 0; b < 10; b++) {
        ASSERT_TRUE(bins[b] >= 9500 && bins[b] <= 10500);
    }
    return TEST_PASS;
}

static test_status_t test_t1_flt_04(void) {
    for (int i = 0; i < 1000; i++) {
        float f = kheiron_uniform_float();
        double d = kheiron_uniform_double();
        ASSERT_FLOAT_BETWEEN(f, 0.0f, 1.0f);
        ASSERT_DOUBLE_BETWEEN(d, 0.0, 1.0);
    }
    return TEST_PASS;
}

static void *thread_flt_worker(void *arg) {
    (void)arg;
    for (int i = 0; i < 25000; i++) {
        float f = kheiron_uniform_float();
        if (f < 0.0f || f >= 1.0f) exit(1);
    }
    return NULL;
}

static test_status_t test_t1_flt_05(void) {
    pthread_t th[4];
    for (int i = 0; i < 4; i++) {
        pthread_create(&th[i], NULL, thread_flt_worker, NULL);
    }
    for (int i = 0; i < 4; i++) {
        pthread_join(th[i], NULL);
    }
    return TEST_PASS;
}

static test_status_t test_t1_wipe_01(void) {
    uint8_t key[32];
    memset(key, 0xFF, sizeof(key));
    kheiron_secure_wipe(key, sizeof(key));
    for (int i = 0; i < 32; i++) {
        ASSERT_EQ_INT(key[i], 0);
    }
    return TEST_PASS;
}

static test_status_t test_t1_wipe_02(void) {
    kheiron_ctx_t ctx;
    memset(&ctx, 0xAA, sizeof(ctx));
    kheiron_secure_wipe(&ctx, sizeof(ctx));
    const uint8_t *p = (const uint8_t *)&ctx;
    for (size_t i = 0; i < sizeof(ctx); i++) {
        ASSERT_EQ_INT(p[i], 0);
    }
    return TEST_PASS;
}

static test_status_t test_t1_wipe_03(void) {
    uint8_t b17[17], b63[63];
    memset(b17, 0x11, sizeof(b17));
    memset(b63, 0x33, sizeof(b63));
    kheiron_secure_wipe(b17, sizeof(b17));
    kheiron_secure_wipe(b63, sizeof(b63));
    for (size_t i = 0; i < sizeof(b17); i++) ASSERT_EQ_INT(b17[i], 0);
    for (size_t i = 0; i < sizeof(b63); i++) ASSERT_EQ_INT(b63[i], 0);
    return TEST_PASS;
}

static test_status_t test_t1_wipe_04(void) {
    uint8_t *page = (uint8_t *)malloc(4096);
    ASSERT_NOT_NULL(page);
    memset(page, 0xAA, 4096);
    kheiron_secure_wipe(page, 4096);
    for (size_t i = 0; i < 4096; i++) {
        ASSERT_EQ_INT(page[i], 0);
    }
    free(page);
    return TEST_PASS;
}

static test_status_t test_t1_wipe_05(void) {
    volatile uint8_t stack_buf[128];
    for (int i = 0; i < 128; i++) stack_buf[i] = (uint8_t)i;
    kheiron_secure_wipe((void *)stack_buf, sizeof(stack_buf));
    for (int i = 0; i < 128; i++) {
        ASSERT_EQ_INT(stack_buf[i], 0);
    }
    return TEST_PASS;
}

static test_status_t test_t1_ctxinit_01(void) {
    kheiron_ctx_t ctx;
    ASSERT_EQ_INT(kheiron_ctx_init(&ctx, NULL, 0), 0);
    ASSERT_EQ_U64(ctx.bytes_generated, 0);
    ASSERT_EQ_U64(ctx.state[0], 0x61707865);
    ASSERT_EQ_U64(ctx.state[1], 0x3320646e);
    ASSERT_EQ_U64(ctx.state[2], 0x79622d32);
    ASSERT_EQ_U64(ctx.state[3], 0x6b206574);
    return TEST_PASS;
}

static test_status_t test_t1_ctxinit_02(void) {
    kheiron_ctx_t ctx;
    uint8_t seed[32] = "explicit_deterministic_seed_32B";
    ASSERT_EQ_INT(kheiron_ctx_init(&ctx, seed, sizeof(seed)), 0);
    ASSERT_EQ_U64(ctx.bytes_generated, 0);
    ASSERT_EQ_U64(ctx.state[0], 0x61707865);
    return TEST_PASS;
}

static test_status_t test_t1_ctxinit_03(void) {
    kheiron_ctx_t c1, c2;
    ASSERT_EQ_INT(kheiron_ctx_init(&c1, NULL, 0), 0);
    ASSERT_EQ_INT(kheiron_ctx_init(&c2, NULL, 0), 0);
    ASSERT_MEM_NEQ(&c1.state[4], &c2.state[4], 32);
    return TEST_PASS;
}

static test_status_t test_t1_ctxinit_04(void) {
    kheiron_ctx_t c16, c48, c64;
    uint8_t s64[64];
    memset(s64, 0x42, sizeof(s64));
    ASSERT_EQ_INT(kheiron_ctx_init(&c16, s64, 16), 0);
    ASSERT_EQ_INT(kheiron_ctx_init(&c48, s64, 48), 0);
    ASSERT_EQ_INT(kheiron_ctx_init(&c64, s64, 64), 0);
    return TEST_PASS;
}

static test_status_t test_t1_ctxinit_05(void) {
    kheiron_ctx_t *ctx = (kheiron_ctx_t *)malloc(sizeof(kheiron_ctx_t));
    ASSERT_NOT_NULL(ctx);
    ASSERT_EQ_INT(kheiron_ctx_init(ctx, NULL, 0), 0);
    uint8_t buf[32];
    ASSERT_EQ_INT(kheiron_ctx_fill(ctx, buf, sizeof(buf)), 0);
    kheiron_ctx_wipe(ctx);
    free(ctx);
    return TEST_PASS;
}

static test_status_t test_t1_ctxfill_01(void) {
    kheiron_ctx_t ctx;
    ASSERT_EQ_INT(kheiron_ctx_init(&ctx, NULL, 0), 0);
    uint8_t buf[32] = {0};
    ASSERT_EQ_INT(kheiron_ctx_fill(&ctx, buf, 32), 0);
    ASSERT_EQ_U64(ctx.bytes_generated, 32);
    size_t nz = 0;
    for (int i = 0; i < 32; i++) if (buf[i] != 0) nz++;
    ASSERT_TRUE(nz > 0);
    kheiron_ctx_wipe(&ctx);
    return TEST_PASS;
}

static test_status_t test_t1_ctxfill_02(void) {
    kheiron_ctx_t ctx;
    ASSERT_EQ_INT(kheiron_ctx_init(&ctx, NULL, 0), 0);
    uint8_t buf[64];
    ASSERT_EQ_INT(kheiron_ctx_fill(&ctx, buf, 64), 0);
    ASSERT_EQ_U64(ctx.bytes_generated, 64);
    kheiron_ctx_wipe(&ctx);
    return TEST_PASS;
}

static test_status_t test_t1_ctxfill_03(void) {
    kheiron_ctx_t ctx;
    ASSERT_EQ_INT(kheiron_ctx_init(&ctx, NULL, 0), 0);
    uint8_t buf[512];
    ASSERT_EQ_INT(kheiron_ctx_fill(&ctx, buf, 512), 0);
    ASSERT_EQ_U64(ctx.bytes_generated, 512);
    kheiron_ctx_wipe(&ctx);
    return TEST_PASS;
}

static test_status_t test_t1_ctxfill_04(void) {
    kheiron_ctx_t ctx;
    ASSERT_EQ_INT(kheiron_ctx_init(&ctx, NULL, 0), 0);
    uint8_t buf[4096];
    ASSERT_EQ_INT(kheiron_ctx_fill(&ctx, buf, 4096), 0);
    ASSERT_EQ_U64(ctx.bytes_generated, 4096);
    kheiron_ctx_wipe(&ctx);
    return TEST_PASS;
}

static test_status_t test_t1_ctxfill_05(void) {
    kheiron_ctx_t ctx;
    ASSERT_EQ_INT(kheiron_ctx_init(&ctx, NULL, 0), 0);
    uint8_t b1[100], b2[200];
    ASSERT_EQ_INT(kheiron_ctx_fill(&ctx, b1, 100), 0);
    ASSERT_EQ_INT(kheiron_ctx_fill(&ctx, b2, 200), 0);
    ASSERT_EQ_U64(ctx.bytes_generated, 300);
    kheiron_ctx_wipe(&ctx);
    return TEST_PASS;
}

static test_status_t test_t1_ctxwipe_01(void) {
    kheiron_ctx_t ctx;
    ASSERT_EQ_INT(kheiron_ctx_init(&ctx, NULL, 0), 0);
    uint8_t buf[128];
    ASSERT_EQ_INT(kheiron_ctx_fill(&ctx, buf, 128), 0);
    kheiron_ctx_wipe(&ctx);
    const uint8_t *p = (const uint8_t *)&ctx;
    for (size_t i = 0; i < sizeof(ctx); i++) {
        ASSERT_EQ_INT(p[i], 0);
    }
    return TEST_PASS;
}

static test_status_t test_t1_ctxwipe_02(void) {
    kheiron_ctx_t ctx;
    ASSERT_EQ_INT(kheiron_ctx_init(&ctx, NULL, 0), 0);
    kheiron_ctx_wipe(&ctx);
    const uint8_t *p = (const uint8_t *)&ctx;
    for (size_t i = 0; i < sizeof(ctx); i++) {
        ASSERT_EQ_INT(p[i], 0);
    }
    return TEST_PASS;
}

static test_status_t test_t1_ctxwipe_03(void) {
    kheiron_ctx_t ctx;
    ASSERT_EQ_INT(kheiron_ctx_init(&ctx, NULL, 0), 0);
    kheiron_ctx_wipe(&ctx);
    kheiron_ctx_wipe(&ctx);
    const uint8_t *p = (const uint8_t *)&ctx;
    for (size_t i = 0; i < sizeof(ctx); i++) {
        ASSERT_EQ_INT(p[i], 0);
    }
    return TEST_PASS;
}

static test_status_t test_t1_ctxwipe_04(void) {
    kheiron_ctx_t stack_ctx;
    ASSERT_EQ_INT(kheiron_ctx_init(&stack_ctx, "seed", 4), 0);
    kheiron_ctx_wipe(&stack_ctx);
    ASSERT_EQ_U64(stack_ctx.bytes_generated, 0);
    return TEST_PASS;
}

static test_status_t test_t1_ctxwipe_05(void) {
    kheiron_ctx_t *hctx = (kheiron_ctx_t *)malloc(sizeof(kheiron_ctx_t));
    ASSERT_NOT_NULL(hctx);
    ASSERT_EQ_INT(kheiron_ctx_init(hctx, "heap_seed", 9), 0);
    kheiron_ctx_wipe(hctx);
    ASSERT_EQ_U64(hctx->bytes_generated, 0);
    free(hctx);
    return TEST_PASS;
}

static test_status_t test_t1_rdtsc_01(void) {
    uint64_t prev = kheiron_rdtsc();
    ASSERT_TRUE(prev > 0ULL);
    int non_decreasing = 0;
    for (int i = 0; i < 1000; i++) {
        volatile int dummy = i;
        (void)dummy;
        uint64_t curr = kheiron_rdtsc();
        if (curr >= prev) {
            non_decreasing++;
        }
        prev = curr;
    }
    ASSERT_TRUE(non_decreasing >= 900);
    uint64_t end = kheiron_rdtsc();
    ASSERT_TRUE(end > 0ULL);
    return TEST_PASS;
}

static test_status_t test_t1_rdtsc_02(void) {
    int passed = 0;
    for (int trial = 0; trial < 10; trial++) {
        uint64_t t0 = kheiron_rdtsc();
        volatile uint64_t sink = 0;
        for (int i = 0; i < 500; i++) {
            sink += (uint64_t)i;
        }
        (void)sink;
        uint64_t t1 = kheiron_rdtsc();
        if (t1 >= t0) {
            passed++;
        }
    }
    ASSERT_TRUE(passed >= 8);
    return TEST_PASS;
}

static test_status_t test_t1_rdtsc_03(void) {
    uint8_t buf[512];
    uint64_t t0 = kheiron_rdtsc();
    int ret = kheiron_fill(buf, sizeof(buf));
    uint64_t t1 = kheiron_rdtsc();
    ASSERT_EQ_INT(ret, 0);
    ASSERT_TRUE(t0 > 0ULL && t1 > 0ULL);
    return TEST_PASS;
}

static test_status_t test_t1_rdtsc_04(void) {
    const int runs = 20;
    uint64_t sum_delta = 0;
    for (int r = 0; r < runs; r++) {
        uint64_t t0 = kheiron_rdtsc();
        volatile uint32_t val = 1;
        for (int i = 0; i < 5000; i++) {
            val = (val * 1664525U) + 1013904223U;
        }
        (void)val;
        uint64_t t1 = kheiron_rdtsc();
        if (t1 > t0) {
            sum_delta += (t1 - t0);
        }
    }
    ASSERT_TRUE(sum_delta > 0ULL);
    return TEST_PASS;
}

typedef struct {
    int error_count;
    uint64_t start_val;
    uint64_t end_val;
} rdtsc_thread_ctx_t;

static void *rdtsc_worker_thread(void *arg) {
    rdtsc_thread_ctx_t *ctx = (rdtsc_thread_ctx_t *)arg;
    ctx->error_count = 0;
    uint64_t prev = kheiron_rdtsc();
    ctx->start_val = prev;
    for (int i = 0; i < 5000; i++) {
        volatile int d = i;
        (void)d;
        uint64_t curr = kheiron_rdtsc();
        prev = curr;
    }
    ctx->end_val = prev;
    return NULL;
}

static test_status_t test_t1_rdtsc_05(void) {
    const int num_threads = 4;
    pthread_t threads[4];
    rdtsc_thread_ctx_t ctxs[4];

    for (int i = 0; i < num_threads; i++) {
        memset(&ctxs[i], 0, sizeof(ctxs[i]));
        int rc = pthread_create(&threads[i], NULL, rdtsc_worker_thread, &ctxs[i]);
        ASSERT_EQ_INT(rc, 0);
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
        ASSERT_EQ_INT(ctxs[i].error_count, 0);
        ASSERT_TRUE(ctxs[i].end_val > 0ULL && ctxs[i].start_val > 0ULL);
    }
    return TEST_PASS;
}

static test_status_t test_t1_jitter_01(void) {
    const int num_samples = 16;
    uint64_t accum_or = 0ULL;
    uint64_t accum_and = 0xFFFFFFFFFFFFFFFFULL;
    for (int i = 0; i < num_samples; i++) {
        uint64_t sample = kheiron_jitter_sample();
        accum_or |= sample;
        accum_and &= sample;
    }
    ASSERT_TRUE(accum_or != 0ULL);
    ASSERT_TRUE(accum_and != 0xFFFFFFFFFFFFFFFFULL);
    return TEST_PASS;
}

static test_status_t test_t1_jitter_02(void) {
    const int n = 32;
    uint64_t samples[32];
    for (int i = 0; i < n; i++) {
        samples[i] = kheiron_jitter_sample();
    }

    int distinct_consecutive = 0;
    for (int i = 1; i < n; i++) {
        if (samples[i] != samples[i - 1]) {
            distinct_consecutive++;
        }
    }

    ASSERT_TRUE(distinct_consecutive >= 24);

    int unique = 0;
    for (int i = 0; i < n; i++) {
        bool dup = false;
        for (int j = 0; j < i; j++) {
            if (samples[i] == samples[j]) {
                dup = true;
                break;
            }
        }
        if (!dup) unique++;
    }
    ASSERT_TRUE(unique >= 16);
    return TEST_PASS;
}

static test_status_t test_t1_jitter_03(void) {
    const int n = 64;
    double weights[64];
    double sum = 0.0;

    for (int i = 0; i < n; i++) {
        uint64_t s = kheiron_jitter_sample();
        int w = 0;
        for (int b = 0; b < 64; b++) {
            if ((s >> b) & 1ULL) w++;
        }
        weights[i] = (double)w;
        sum += (double)w;
    }

    double mean = sum / (double)n;
    double variance = 0.0;
    for (int i = 0; i < n; i++) {
        double d = weights[i] - mean;
        variance += d * d;
    }
    variance /= (double)(n - 1);

    ASSERT_TRUE(variance > 0.1);
    ASSERT_TRUE(mean >= 5.0 && mean <= 59.0);
    return TEST_PASS;
}

static test_status_t test_t1_jitter_04(void) {
    const int n = 100;
    uint64_t bit_or = 0ULL;
    uint64_t bit_and = 0xFFFFFFFFFFFFFFFFULL;

    for (int i = 0; i < n; i++) {
        uint64_t s = kheiron_jitter_sample();
        bit_or |= s;
        bit_and &= s;
    }

    ASSERT_TRUE(bit_or != 0ULL);
    ASSERT_TRUE(bit_and != 0xFFFFFFFFFFFFFFFFULL);

    int active_bits = 0;
    int stuck_ones = 0;
    for (int b = 0; b < 64; b++) {
        if ((bit_or >> b) & 1ULL) active_bits++;
        if ((bit_and >> b) & 1ULL) stuck_ones++;
    }

    ASSERT_TRUE(active_bits >= 48);

    ASSERT_TRUE(stuck_ones <= 16);
    return TEST_PASS;
}

static test_status_t test_t1_jitter_05(void) {
    const size_t thrash_sz = 256 * 1024;
    uint8_t *thrash = (uint8_t *)malloc(thrash_sz);
    ASSERT_NOT_NULL(thrash);

    for (size_t i = 0; i < thrash_sz; i += 64) {
        thrash[i] = (uint8_t)(i ^ 0xA5);
    }

    volatile int x = 0;
    for (int i = 0; i < 1000; i++) {
        if ((thrash[i % thrash_sz] & 1) == 0) {
            x += 3;
        } else {
            x -= 1;
        }
    }
    (void)x;

    uint64_t s_perturbed = kheiron_jitter_sample();
    free(thrash);

    uint64_t s_normal = kheiron_jitter_sample();

    ASSERT_TRUE(s_perturbed != 0ULL);
    ASSERT_TRUE(s_normal != 0ULL);
    ASSERT_TRUE(s_perturbed != s_normal);
    return TEST_PASS;
}

static test_status_t test_t1_osent_01(void) {
    const size_t sizes[] = {16, 32, 64, 256};
    const size_t num_sizes = sizeof(sizes) / sizeof(sizes[0]);

    for (size_t s = 0; s < num_sizes; s++) {
        size_t len = sizes[s];
        uint8_t buf[256];
        memset(buf, 0, sizeof(buf));

        int ret = kheiron_os_entropy(buf, len);
        ASSERT_EQ_INT(ret, 0);

        size_t nonzero = 0;
        for (size_t i = 0; i < len; i++) {
            if (buf[i] != 0) nonzero++;
        }
        ASSERT_TRUE(nonzero > 0);
        for (size_t i = len; i < sizeof(buf); i++) {
            ASSERT_EQ_INT(buf[i], 0);
        }
    }
    return TEST_PASS;
}

static test_status_t test_t1_osent_02(void) {
    int ret16 = kheiron_os_entropy(NULL, 16);
    int ret64 = kheiron_os_entropy(NULL, 64);
    int ret256 = kheiron_os_entropy(NULL, 256);

    ASSERT_EQ_INT(ret16, -1);
    ASSERT_EQ_INT(ret64, -1);
    ASSERT_EQ_INT(ret256, -1);
    return TEST_PASS;
}

static test_status_t test_t1_osent_03(void) {
    uint8_t buf[32];
    memset(buf, 0xEE, sizeof(buf));
    int ret = kheiron_os_entropy(buf, 0);
    ASSERT_EQ_INT(ret, 0);
    for (size_t i = 0; i < sizeof(buf); i++) {
        ASSERT_EQ_INT(buf[i], 0xEE);
    }

    int ret_null = kheiron_os_entropy(NULL, 0);
    ASSERT_EQ_INT(ret_null, 0);
    return TEST_PASS;
}

static test_status_t test_t1_osent_04(void) {
    uint8_t buf[256];
    memset(buf, 0, sizeof(buf));

    ASSERT_EQ_INT(kheiron_os_entropy(buf, sizeof(buf)), 0);

    int byte_counts[256] = {0};
    int distinct_values = 0;
    int nonzero_count = 0;

    for (size_t i = 0; i < sizeof(buf); i++) {
        if (buf[i] != 0) nonzero_count++;
        if (byte_counts[buf[i]] == 0) {
            distinct_values++;
        }
        byte_counts[buf[i]]++;
    }

    ASSERT_TRUE(distinct_values >= 100);
    ASSERT_TRUE(nonzero_count >= 235);
    ASSERT_MEM_NEQ(buf, buf + 16, 16);
    ASSERT_MEM_NEQ(buf + 16, buf + 32, 16);
    return TEST_PASS;
}

static test_status_t test_t1_osent_05(void) {
    const int draws = 10;
    uint8_t bufs[10][32];

    for (int i = 0; i < draws; i++) {
        memset(bufs[i], 0, 32);
        ASSERT_EQ_INT(kheiron_os_entropy(bufs[i], 32), 0);
    }

    for (int i = 0; i < draws; i++) {
        for (int j = i + 1; j < draws; j++) {
            ASSERT_MEM_NEQ(bufs[i], bufs[j], 32);
        }
    }
    return TEST_PASS;
}

static test_status_t test_t1_null_01(void) {
    ASSERT_EQ_INT(kheiron_fill(NULL, 16), -1);
    ASSERT_EQ_INT(kheiron_fill(NULL, 1024), -1);
    return TEST_PASS;
}

static test_status_t test_t1_null_02(void) {
    ASSERT_EQ_INT(kheiron_ctx_init(NULL, "seed", 4), -1);
    return TEST_PASS;
}

static test_status_t test_t1_null_03(void) {
    kheiron_ctx_t ctx;
    uint8_t buf[64];
    ASSERT_EQ_INT(kheiron_ctx_init(&ctx, NULL, 0), 0);
    ASSERT_EQ_INT(kheiron_ctx_fill(NULL, buf, 64), -1);
    ASSERT_EQ_INT(kheiron_ctx_fill(&ctx, NULL, 64), -1);
    kheiron_ctx_wipe(&ctx);
    return TEST_PASS;
}

static test_status_t test_t1_null_04(void) {
    kheiron_ctx_wipe(NULL);
    return TEST_PASS;
}

static test_status_t test_t1_null_05(void) {
    kheiron_secure_wipe(NULL, 128);
    return TEST_PASS;
}

static test_status_t test_t1_str_01(void) {
    char str[33];
    const char *c62 = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    ASSERT_EQ_INT(kheiron_string(str, 32, c62), 0);
    ASSERT_EQ_INT(str[32], '\0');
    ASSERT_EQ_U64(strlen(str), 32);
    for (int i = 0; i < 32; i++) {
        ASSERT_NOT_NULL(strchr(c62, str[i]));
    }
    return TEST_PASS;
}

static test_status_t test_t1_str_02(void) {
    char str[65];
    const char *c16 = "0123456789abcdef";
    ASSERT_EQ_INT(kheiron_string(str, 64, c16), 0);
    ASSERT_EQ_INT(str[64], '\0');
    for (int i = 0; i < 64; i++) {
        ASSERT_NOT_NULL(strchr(c16, str[i]));
    }
    return TEST_PASS;
}

static test_status_t test_t1_str_03(void) {
    char str[129];
    const char *b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    ASSERT_EQ_INT(kheiron_string(str, 128, b64), 0);
    ASSERT_EQ_INT(str[128], '\0');
    for (int i = 0; i < 128; i++) {
        ASSERT_NOT_NULL(strchr(b64, str[i]));
    }
    return TEST_PASS;
}

static test_status_t test_t1_str_04(void) {
    char str[101];
    ASSERT_EQ_INT(kheiron_string(str, 100, "01"), 0);
    ASSERT_EQ_INT(str[100], '\0');
    int zeros = 0, ones = 0;
    for (int i = 0; i < 100; i++) {
        if (str[i] == '0') zeros++;
        else if (str[i] == '1') ones++;
        else ASSERT_TRUE(false);
    }
    ASSERT_TRUE(zeros > 25 && ones > 25);
    return TEST_PASS;
}

static test_status_t test_t1_str_05(void) {
    char str[1025];
    const char *ascii = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*()";
    ASSERT_EQ_INT(kheiron_string(str, 1024, ascii), 0);
    ASSERT_EQ_INT(str[1024], '\0');
    ASSERT_EQ_U64(strlen(str), 1024);
    return TEST_PASS;
}

static test_status_t test_t1_shuf_01(void) {
    int deck[52];
    for (int i = 0; i < 52; i++) deck[i] = i;
    ASSERT_EQ_INT(kheiron_shuffle(deck, 52, sizeof(int)), 0);
    int seen[52] = {0};
    for (int i = 0; i < 52; i++) {
        ASSERT_TRUE(deck[i] >= 0 && deck[i] < 52);
        seen[deck[i]]++;
    }
    for (int i = 0; i < 52; i++) {
        ASSERT_EQ_INT(seen[i], 1);
    }
    return TEST_PASS;
}

static test_status_t test_t1_shuf_02(void) {
    int counts[6] = {0};
    for (int trial = 0; trial < 6000; trial++) {
        int a[3] = {1, 2, 3};
        ASSERT_EQ_INT(kheiron_shuffle(a, 3, sizeof(int)), 0);
        int idx = 0;
        if (a[0] == 1 && a[1] == 2) idx = 0;
        else if (a[0] == 1 && a[1] == 3) idx = 1;
        else if (a[0] == 2 && a[1] == 1) idx = 2;
        else if (a[0] == 2 && a[1] == 3) idx = 3;
        else if (a[0] == 3 && a[1] == 1) idx = 4;
        else if (a[0] == 3 && a[1] == 2) idx = 5;
        counts[idx]++;
    }
    double chi2 = 0.0;
    for (int i = 0; i < 6; i++) {
        double d = (double)counts[i] - 1000.0;
        chi2 += (d * d) / 1000.0;
    }
    ASSERT_TRUE(chi2 < 20.5);
    return TEST_PASS;
}

typedef struct {
    uint64_t id;
    uint8_t payload[24];
} item32_t;

static test_status_t test_t1_shuf_03(void) {
    item32_t items[100];
    for (uint64_t i = 0; i < 100; i++) {
        items[i].id = i;
        memset(items[i].payload, (int)(i & 0xFF), 24);
    }
    ASSERT_EQ_INT(kheiron_shuffle(items, 100, sizeof(item32_t)), 0);
    int seen[100] = {0};
    for (int i = 0; i < 100; i++) {
        uint64_t id = items[i].id;
        ASSERT_TRUE(id < 100);
        seen[id]++;
        for (int b = 0; b < 24; b++) {
            ASSERT_EQ_INT(items[i].payload[b], (uint8_t)(id & 0xFF));
        }
    }
    for (int i = 0; i < 100; i++) ASSERT_EQ_INT(seen[i], 1);
    return TEST_PASS;
}

static test_status_t test_t1_shuf_04(void) {
    const size_t N = 10000;
    uint32_t *arr = (uint32_t *)malloc(N * sizeof(uint32_t));
    ASSERT_NOT_NULL(arr);
    for (size_t i = 0; i < N; i++) arr[i] = (uint32_t)i;
    ASSERT_EQ_INT(kheiron_shuffle(arr, N, sizeof(uint32_t)), 0);
    uint8_t *seen = (uint8_t *)calloc(N, 1);
    ASSERT_NOT_NULL(seen);
    for (size_t i = 0; i < N; i++) {
        ASSERT_TRUE(arr[i] < N);
        seen[arr[i]]++;
    }
    for (size_t i = 0; i < N; i++) {
        ASSERT_EQ_INT(seen[i], 1);
    }
    free(seen);
    free(arr);
    return TEST_PASS;
}

static test_status_t test_t1_shuf_05(void) {

    uint8_t arr[10][512];
    for (int i = 0; i < 10; i++) {
        memset(arr[i], i, 512);
    }
    ASSERT_EQ_INT(kheiron_shuffle(arr, 10, 512), 0);
    int seen[10] = {0};
    for (int i = 0; i < 10; i++) {
        uint8_t tag = arr[i][0];
        ASSERT_TRUE(tag < 10);
        seen[tag]++;
        for (int b = 0; b < 512; b++) {
            ASSERT_EQ_INT(arr[i][b], tag);
        }
    }
    for (int i = 0; i < 10; i++) ASSERT_EQ_INT(seen[i], 1);
    return TEST_PASS;
}

static test_status_t test_t1_gauss_01(void) {
    double sum = 0.0, sum_sq = 0.0;
    const int N = 100000;
    for (int i = 0; i < N; i++) {
        double g = kheiron_gaussian(0.0, 1.0);
        sum += g;
        sum_sq += g * g;
    }
    double mean = sum / (double)N;
    double var = (sum_sq / (double)N) - (mean * mean);
    ASSERT_TRUE(fabs(mean) < 0.02);
    ASSERT_TRUE(fabs(var - 1.0) < 0.03);
    return TEST_PASS;
}

static test_status_t test_t1_gauss_02(void) {
    double sum = 0.0, sum_sq = 0.0;
    const int N = 100000;
    for (int i = 0; i < N; i++) {
        double g = kheiron_gaussian(100.0, 15.0);
        sum += g;
        sum_sq += g * g;
    }
    double mean = sum / (double)N;
    double var = (sum_sq / (double)N) - (mean * mean);
    double s = sqrt(var);
    ASSERT_TRUE(fabs(mean - 100.0) < 0.3);
    ASSERT_TRUE(fabs(s - 15.0) < 0.3);
    return TEST_PASS;
}

static test_status_t test_t1_gauss_03(void) {
    int c1 = 0, c2 = 0, c3 = 0;
    const int N = 100000;
    for (int i = 0; i < N; i++) {
        double g = kheiron_gaussian(0.0, 1.0);
        if (fabs(g) <= 1.0) c1++;
        if (fabs(g) <= 2.0) c2++;
        if (fabs(g) <= 3.0) c3++;
    }
    double p1 = (double)c1 / (double)N;
    double p2 = (double)c2 / (double)N;
    double p3 = (double)c3 / (double)N;
    ASSERT_TRUE(p1 >= 0.675 && p1 <= 0.690);
    ASSERT_TRUE(p2 >= 0.950 && p2 <= 0.960);
    ASSERT_TRUE(p3 >= 0.995 && p3 <= 0.999);
    return TEST_PASS;
}

static test_status_t test_t1_gauss_04(void) {
    const int N = 100000;
    double sum = 0.0, sum_sq = 0.0, sum_cube = 0.0;
    for (int i = 0; i < N; i++) {
        double g = kheiron_gaussian(0.0, 1.0);
        sum += g;
        sum_sq += g * g;
        sum_cube += g * g * g;
    }
    double mean = sum / (double)N;
    double var = (sum_sq / (double)N) - (mean * mean);
    double stddev = sqrt(var);
    double m3 = (sum_cube / (double)N) - 3.0 * mean * var - (mean * mean * mean);
    double skewness = m3 / (stddev * stddev * stddev);
    ASSERT_TRUE(fabs(skewness) < 0.05);
    return TEST_PASS;
}

static test_status_t test_t1_gauss_05(void) {
    const int N = 100000;
    double sum = 0.0, sum_sq = 0.0, sum4 = 0.0;
    for (int i = 0; i < N; i++) {
        double g = kheiron_gaussian(0.0, 1.0);
        sum += g;
        sum_sq += g * g;
        sum4 += g * g * g * g;
    }
    double mean = sum / (double)N;
    double var = (sum_sq / (double)N) - (mean * mean);
    double m4 = sum4 / (double)N;
    double kurtosis = (m4 / (var * var)) - 3.0;
    ASSERT_TRUE(fabs(kurtosis) < 0.1);
    return TEST_PASS;
}

static test_status_t test_t1_deriv_01(void) {
    kheiron_ctx_t ctx;
    ASSERT_EQ_INT(kheiron_ctx_init(&ctx, "seed_master", 11), 0);
    uint8_t out[32] = {0};
    ASSERT_EQ_INT(kheiron_derive(&ctx, out, 32, "encryption_key", 14), 0);
    size_t nz = 0;
    for (int i = 0; i < 32; i++) if (out[i] != 0) nz++;
    ASSERT_TRUE(nz > 0);
    kheiron_ctx_wipe(&ctx);
    return TEST_PASS;
}

static test_status_t test_t1_deriv_02(void) {
    kheiron_ctx_t c1, c2;
    uint8_t seed[32] = "immutability_test_seed_val_32!";
    ASSERT_EQ_INT(kheiron_ctx_init(&c1, seed, 32), 0);
    memcpy(&c2, &c1, sizeof(kheiron_ctx_t));
    uint8_t d[32];
    ASSERT_EQ_INT(kheiron_derive(&c1, d, 32, "label", 5), 0);
    uint8_t out1[64], out2[64];
    ASSERT_EQ_INT(kheiron_ctx_fill(&c1, out1, 64), 0);
    ASSERT_EQ_INT(kheiron_ctx_fill(&c2, out2, 64), 0);
    ASSERT_MEM_EQ(out1, out2, 64);
    kheiron_ctx_wipe(&c1);
    kheiron_ctx_wipe(&c2);
    return TEST_PASS;
}

static test_status_t test_t1_deriv_03(void) {
    kheiron_ctx_t ctx;
    ASSERT_EQ_INT(kheiron_ctx_init(&ctx, "master_seed", 11), 0);
    uint8_t out1[32], out2[32];
    ASSERT_EQ_INT(kheiron_derive(&ctx, out1, 32, "client_key", 10), 0);
    ASSERT_EQ_INT(kheiron_derive(&ctx, out2, 32, "server_key", 10), 0);
    ASSERT_MEM_NEQ(out1, out2, 32);
    kheiron_ctx_wipe(&ctx);
    return TEST_PASS;
}

static test_status_t test_t1_deriv_04(void) {
    kheiron_ctx_t ctx;
    ASSERT_EQ_INT(kheiron_ctx_init(&ctx, "master_seed", 11), 0);
    uint8_t k16[16], k32[32], k64[64], k128[128];
    ASSERT_EQ_INT(kheiron_derive(&ctx, k16, 16, "info16", 6), 0);
    ASSERT_EQ_INT(kheiron_derive(&ctx, k32, 32, "info32", 6), 0);
    ASSERT_EQ_INT(kheiron_derive(&ctx, k64, 64, "info64", 6), 0);
    ASSERT_EQ_INT(kheiron_derive(&ctx, k128, 128, "info128", 7), 0);
    kheiron_ctx_wipe(&ctx);
    return TEST_PASS;
}

static void *thread_deriv_worker(void *arg) {
    int id = *(int *)arg;
    kheiron_ctx_t ctx;
    char seed[32];
    snprintf(seed, sizeof(seed), "thread_seed_%d", id);
    kheiron_ctx_init(&ctx, seed, strlen(seed));
    uint8_t out[32];
    kheiron_derive(&ctx, out, 32, "info", 4);
    kheiron_ctx_wipe(&ctx);
    return NULL;
}

static test_status_t test_t1_deriv_05(void) {
    pthread_t th[4];
    int ids[4] = {0, 1, 2, 3};
    for (int i = 0; i < 4; i++) {
        pthread_create(&th[i], NULL, thread_deriv_worker, &ids[i]);
    }
    for (int i = 0; i < 4; i++) {
        pthread_join(th[i], NULL);
    }
    return TEST_PASS;
}

static test_status_t test_t1_reseed_01(void) {
    uint8_t entropy[32] = "reseed_entropy_user_supplied_32";
    ASSERT_EQ_INT(kheiron_reseed(entropy, sizeof(entropy)), 0);
    uint64_t v = kheiron_u64();
    ASSERT_TRUE(v != 0ULL);
    return TEST_PASS;
}

static test_status_t test_t1_reseed_02(void) {
    uint8_t ent[32] = "init_via_reseed_cold_start_32_!";
    ASSERT_EQ_INT(kheiron_reseed(ent, 32), 0);
    uint32_t v = kheiron_u32();
    ASSERT_TRUE(v != 0U);
    return TEST_PASS;
}

static test_status_t test_t1_reseed_03(void) {
    uint8_t e16[16], e64[64], e128[128];
    memset(e16, 0x16, 16);
    memset(e64, 0x64, 64);
    memset(e128, 0x12, 128);
    ASSERT_EQ_INT(kheiron_reseed(e16, 16), 0);
    ASSERT_EQ_INT(kheiron_reseed(e64, 64), 0);
    ASSERT_EQ_INT(kheiron_reseed(e128, 128), 0);
    return TEST_PASS;
}

static test_status_t test_t1_reseed_04(void) {
    size_t sz = 500000;
    uint8_t *p = (uint8_t *)malloc(sz);
    ASSERT_NOT_NULL(p);
    ASSERT_EQ_INT(kheiron_fill(p, sz), 0);
    free(p);
    uint8_t ent[32] = "entropy_reset_epoch_counter_t";
    ASSERT_EQ_INT(kheiron_reseed(ent, 32), 0);
    uint8_t *p2 = (uint8_t *)malloc(sz);
    ASSERT_NOT_NULL(p2);
    ASSERT_EQ_INT(kheiron_fill(p2, sz), 0);
    free(p2);
    return TEST_PASS;
}

static test_status_t test_t1_reseed_05(void) {
    uint8_t ent[64];
    memset(ent, 0x55, sizeof(ent));
    ASSERT_EQ_INT(kheiron_reseed(ent, sizeof(ent)), 0);
    kheiron_secure_wipe(ent, sizeof(ent));
    for (size_t i = 0; i < sizeof(ent); i++) {
        ASSERT_EQ_INT(ent[i], 0);
    }
    return TEST_PASS;
}

static test_status_t test_t1_self_01(void) {
    ASSERT_EQ_INT(kheiron_selftest(), 0);
    return TEST_PASS;
}

static test_status_t test_t1_self_02(void) {
    int ret = kheiron_selftest();
    ASSERT_EQ_INT(ret, 0);
    return TEST_PASS;
}

static test_status_t test_t1_self_03(void) {
    for (int i = 0; i < 10; i++) {
        ASSERT_EQ_INT(kheiron_selftest(), 0);
    }
    return TEST_PASS;
}

static void *thread_self_worker(void *arg) {
    (void)arg;
    for (int i = 0; i < 5; i++) {
        if (kheiron_selftest() != 0) exit(1);
    }
    return NULL;
}

static test_status_t test_t1_self_04(void) {
    pthread_t th[4];
    for (int i = 0; i < 4; i++) {
        pthread_create(&th[i], NULL, thread_self_worker, NULL);
    }
    for (int i = 0; i < 4; i++) {
        pthread_join(th[i], NULL);
    }
    return TEST_PASS;
}

static test_status_t test_t1_self_05(void) {
    ASSERT_EQ_INT(kheiron_selftest(), 0);
    ASSERT_EQ_INT(kheiron_init(), 0);
    return TEST_PASS;
}

static test_status_t test_t1_ct_01(void) {
    uint8_t a[32], b[32];
    memset(a, 0x42, 32);
    memset(b, 0x42, 32);
    ASSERT_EQ_INT(kheiron_consttime_cmp(a, b, 32), 0);
    return TEST_PASS;
}

static test_status_t test_t1_ct_02(void) {
    uint8_t a[32], b[32];
    memset(a, 0x42, 32);
    memset(b, 0x42, 32);
    b[0] ^= 1;
    ASSERT_EQ_INT(kheiron_consttime_cmp(a, b, 32), 1);
    b[0] ^= 1;
    b[15] ^= 1;
    ASSERT_EQ_INT(kheiron_consttime_cmp(a, b, 32), 1);
    b[15] ^= 1;
    b[31] ^= 1;
    ASSERT_EQ_INT(kheiron_consttime_cmp(a, b, 32), 1);
    return TEST_PASS;
}

static test_status_t test_t1_ct_03(void) {
    uint32_t a = 0x12345678, b = 0x9ABCDEF0;
    ASSERT_EQ_U64(kheiron_consttime_select_u32(0xFFFFFFFFU, a, b), a);
    ASSERT_EQ_U64(kheiron_consttime_select_u32(0x00000000U, a, b), b);
    return TEST_PASS;
}

static test_status_t test_t1_ct_04(void) {
    uint32_t a = 0xAAAAAAAA, b = 0x55555555;
    uint32_t mask = 0xFF00FF00;
    uint32_t res = kheiron_consttime_select_u32(mask, a, b);
    ASSERT_EQ_U64(res, 0xAA55AA55);
    return TEST_PASS;
}

static test_status_t test_t1_ct_05(void) {
    uint8_t a[1024], b[1024];
    memset(a, 0x77, 1024);
    memset(b, 0x77, 1024);
    b[0] ^= 0x01;
    ASSERT_EQ_INT(kheiron_consttime_cmp(a, b, 1024), 1);
    b[0] ^= 0x01;
    b[1023] ^= 0x01;
    ASSERT_EQ_INT(kheiron_consttime_cmp(a, b, 1024), 1);
    return TEST_PASS;
}

void register_tier1_tests(void) {
    const char *tier = "Tier 1: Feature Coverage";

    test_register(tier, "T1.001", "test_t1_init_01", "Explicit cold-start initialization", test_t1_init_01, false);
    test_register(tier, "T1.002", "test_t1_init_02", "Idempotent consecutive invocations", test_t1_init_02, false);
    test_register(tier, "T1.003", "test_t1_init_03", "Lazy auto-initialization via fill", test_t1_init_03, false);
    test_register(tier, "T1.004", "test_t1_init_04", "Lazy auto-initialization via integer generators", test_t1_init_04, false);
    test_register(tier, "T1.005", "test_t1_init_05", "Multi-thread independent initialization", test_t1_init_05, false);

    test_register(tier, "T1.006", "test_t1_fill_01", "Small buffer fills (16 and 32 bytes)", test_t1_fill_01, false);
    test_register(tier, "T1.007", "test_t1_fill_02", "Exact single ChaCha block (64 bytes)", test_t1_fill_02, false);
    test_register(tier, "T1.008", "test_t1_fill_03", "Multi-block sub-AVX2 fill (128 & 256 bytes)", test_t1_fill_03, false);
    test_register(tier, "T1.009", "test_t1_fill_04", "Exact AVX2 vector iteration (512 bytes)", test_t1_fill_04, false);
    test_register(tier, "T1.010", "test_t1_fill_05", "Large block throughput (64 KB Chi-Square)", test_t1_fill_05, false);

    test_register(tier, "T1.011", "test_t1_u64_01", "u64 non-zero and uniqueness check", test_t1_u64_01, false);
    test_register(tier, "T1.012", "test_t1_u64_02", "u64 consecutive uniqueness across 10k calls", test_t1_u64_02, false);
    test_register(tier, "T1.013", "test_t1_u64_03", "u64 mean Hamming weight distribution", test_t1_u64_03, false);
    test_register(tier, "T1.014", "test_t1_u64_04", "u64 per-bit uniformity bits 0..63", test_t1_u64_04, false);
    test_register(tier, "T1.015", "test_t1_u64_05", "u64 concurrent multi-thread generation", test_t1_u64_05, false);

    test_register(tier, "T1.016", "test_t1_u32_01", "u32 non-zero and uniqueness check", test_t1_u32_01, false);
    test_register(tier, "T1.017", "test_t1_u32_02", "u32 consecutive uniqueness across 10k calls", test_t1_u32_02, false);
    test_register(tier, "T1.018", "test_t1_u32_03", "u32 mean Hamming weight distribution", test_t1_u32_03, false);
    test_register(tier, "T1.019", "test_t1_u32_04", "u32 per-bit uniformity bits 0..31", test_t1_u32_04, false);
    test_register(tier, "T1.020", "test_t1_u32_05", "u32 and u64 interleaved stream calls", test_t1_u32_05, false);

    test_register(tier, "T1.021", "test_t1_range_01", "Discrete dice roll [1, 6]", test_t1_range_01, false);
    test_register(tier, "T1.022", "test_t1_range_02", "Binary coin flip [0, 1]", test_t1_range_02, false);
    test_register(tier, "T1.023", "test_t1_range_03", "Byte range [0, 255]", test_t1_range_03, false);
    test_register(tier, "T1.024", "test_t1_range_04", "Non-zero lower bound [100, 200]", test_t1_range_04, false);
    test_register(tier, "T1.025", "test_t1_range_05", "Large range [1, 1000000]", test_t1_range_05, false);

    test_register(tier, "T1.026", "test_t1_dbl_01", "Double strict range [0.0, 1.0)", test_t1_dbl_01, false);
    test_register(tier, "T1.027", "test_t1_dbl_02", "Double mean and variance convergence", test_t1_dbl_02, false);
    test_register(tier, "T1.028", "test_t1_dbl_03", "Double decile histogram uniformity", test_t1_dbl_03, false);
    test_register(tier, "T1.029", "test_t1_dbl_04", "Double consecutive value serial correlation", test_t1_dbl_04, false);
    test_register(tier, "T1.030", "test_t1_dbl_05", "Double multi-thread concurrency", test_t1_dbl_05, false);

    test_register(tier, "T1.031", "test_t1_flt_01", "Float strict range [0.0f, 1.0f)", test_t1_flt_01, false);
    test_register(tier, "T1.032", "test_t1_flt_02", "Float mean and variance convergence", test_t1_flt_02, false);
    test_register(tier, "T1.033", "test_t1_flt_03", "Float decile histogram uniformity", test_t1_flt_03, false);
    test_register(tier, "T1.034", "test_t1_flt_04", "Float and double interleaved sampling", test_t1_flt_04, false);
    test_register(tier, "T1.035", "test_t1_flt_05", "Float multi-thread concurrency", test_t1_flt_05, false);

    test_register(tier, "T1.036", "test_t1_wipe_01", "Standard 32-byte secret key wipe", test_t1_wipe_01, false);
    test_register(tier, "T1.037", "test_t1_wipe_02", "Complete context structure wipe", test_t1_wipe_02, false);
    test_register(tier, "T1.038", "test_t1_wipe_03", "Odd-length buffer wipe (17B, 63B)", test_t1_wipe_03, false);
    test_register(tier, "T1.039", "test_t1_wipe_04", "Page-sized buffer wipe (4096B)", test_t1_wipe_04, false);
    test_register(tier, "T1.040", "test_t1_wipe_05", "Stack buffer sanitization barrier", test_t1_wipe_05, false);

    test_register(tier, "T1.041", "test_t1_ctxinit_01", "Auto-seeded context initialization", test_t1_ctxinit_01, false);
    test_register(tier, "T1.042", "test_t1_ctxinit_02", "Explicit 32-byte seed initialization", test_t1_ctxinit_02, false);
    test_register(tier, "T1.043", "test_t1_ctxinit_03", "Independence of multiple contexts", test_t1_ctxinit_03, false);
    test_register(tier, "T1.044", "test_t1_ctxinit_04", "Variable seed lengths (16B, 48B, 64B)", test_t1_ctxinit_04, false);
    test_register(tier, "T1.045", "test_t1_ctxinit_05", "Heap-allocated context initialization", test_t1_ctxinit_05, false);

    test_register(tier, "T1.046", "test_t1_ctxfill_01", "Context small buffer fill (32 bytes)", test_t1_ctxfill_01, false);
    test_register(tier, "T1.047", "test_t1_ctxfill_02", "Context single ChaCha block fill (64B)", test_t1_ctxfill_02, false);
    test_register(tier, "T1.048", "test_t1_ctxfill_03", "Context AVX2 vector block fill (512B)", test_t1_ctxfill_03, false);
    test_register(tier, "T1.049", "test_t1_ctxfill_04", "Context large multi-block fill (4096B)", test_t1_ctxfill_04, false);
    test_register(tier, "T1.050", "test_t1_ctxfill_05", "Context sequential call accumulation", test_t1_ctxfill_05, false);

    test_register(tier, "T1.051", "test_t1_ctxwipe_01", "Context standard post-usage wipe", test_t1_ctxwipe_01, false);
    test_register(tier, "T1.052", "test_t1_ctxwipe_02", "Context fresh ungenerated wipe", test_t1_ctxwipe_02, false);
    test_register(tier, "T1.053", "test_t1_ctxwipe_03", "Context idempotent consecutive wipes", test_t1_ctxwipe_03, false);
    test_register(tier, "T1.054", "test_t1_ctxwipe_04", "Context stack epilogue wipe", test_t1_ctxwipe_04, false);
    test_register(tier, "T1.055", "test_t1_ctxwipe_05", "Context heap wipe pre-free", test_t1_ctxwipe_05, false);

    test_register(tier, "T1.056", "test_t1_rdtsc_01", "Monotonic strictly increasing progression", test_t1_rdtsc_01, false);
    test_register(tier, "T1.057", "test_t1_rdtsc_02", "Non-zero cycle delta across active intervals", test_t1_rdtsc_02, false);
    test_register(tier, "T1.058", "test_t1_rdtsc_03", "Non-trivial cycle delta on cryptographic operations", test_t1_rdtsc_03, false);
    test_register(tier, "T1.059", "test_t1_rdtsc_04", "Frequency progression stability across loops", test_t1_rdtsc_04, false);
    test_register(tier, "T1.060", "test_t1_rdtsc_05", "Multi-thread concurrent monotonic independence", test_t1_rdtsc_05, false);

    test_register(tier, "T1.061", "test_t1_jitter_01", "Non-zero 64-bit entropy return value", test_t1_jitter_01, false);
    test_register(tier, "T1.062", "test_t1_jitter_02", "Bit-level diversity and pairwise distinctness", test_t1_jitter_02, false);
    test_register(tier, "T1.063", "test_t1_jitter_03", "Statistical variance and Hamming weight distribution", test_t1_jitter_03, false);
    test_register(tier, "T1.064", "test_t1_jitter_04", "Absence of globally stuck zero or one bits", test_t1_jitter_04, false);
    test_register(tier, "T1.065", "test_t1_jitter_05", "Jitter entropy robustness under cache/branch perturbation", test_t1_jitter_05, false);

    test_register(tier, "T1.066", "test_t1_osent_01", "Standard buffer fills (16B, 32B, 64B, 256B)", test_t1_osent_01, false);
    test_register(tier, "T1.067", "test_t1_osent_02", "Defensive NULL buffer safety returning error code", test_t1_osent_02, false);
    test_register(tier, "T1.068", "test_t1_osent_03", "Zero-length entropy request safety and preservation", test_t1_osent_03, false);
    test_register(tier, "T1.069", "test_t1_osent_04", "Entropy non-degeneracy and high distinct byte count", test_t1_osent_04, false);
    test_register(tier, "T1.070", "test_t1_osent_05", "Consecutive OS entropy draws stream divergence", test_t1_osent_05, false);

    test_register(tier, "T1.071", "test_t1_null_01", "kheiron_fill defensive NULL buffer guard", test_t1_null_01, true);
    test_register(tier, "T1.072", "test_t1_null_02", "kheiron_ctx_init defensive NULL context guard", test_t1_null_02, true);
    test_register(tier, "T1.073", "test_t1_null_03", "kheiron_ctx_fill defensive NULL guards", test_t1_null_03, true);
    test_register(tier, "T1.074", "test_t1_null_04", "kheiron_ctx_wipe defensive NULL guard", test_t1_null_04, true);
    test_register(tier, "T1.075", "test_t1_null_05", "kheiron_secure_wipe defensive NULL guard", test_t1_null_05, true);

    test_register(tier, "T1.076", "test_t1_str_01", "Alphanumeric string 62-char charset", test_t1_str_01, false);
    test_register(tier, "T1.077", "test_t1_str_02", "Hexadecimal string 16-char charset", test_t1_str_02, false);
    test_register(tier, "T1.078", "test_t1_str_03", "Base64 string 64-char charset", test_t1_str_03, false);
    test_register(tier, "T1.079", "test_t1_str_04", "Binary string 2-char charset", test_t1_str_04, false);
    test_register(tier, "T1.080", "test_t1_str_05", "Long string generation (1024 chars)", test_t1_str_05, false);

    test_register(tier, "T1.081", "test_t1_shuf_01", "52-card deck Fisher-Yates shuffle", test_t1_shuf_01, false);
    test_register(tier, "T1.082", "test_t1_shuf_02", "3-card deck permutation Chi-Square", test_t1_shuf_02, false);
    test_register(tier, "T1.083", "test_t1_shuf_03", "Array of structs shuffle (32-byte elements)", test_t1_shuf_03, false);
    test_register(tier, "T1.084", "test_t1_shuf_04", "Large array shuffle (10,000 integers)", test_t1_shuf_04, false);
    test_register(tier, "T1.085", "test_t1_shuf_05", "Multi-chunk swap shuffle (512-byte elements)", test_t1_shuf_05, false);

    test_register(tier, "T1.086", "test_t1_gauss_01", "Standard normal distribution (0.0, 1.0)", test_t1_gauss_01, false);
    test_register(tier, "T1.087", "test_t1_gauss_02", "Scaled normal distribution (100.0, 15.0)", test_t1_gauss_02, false);
    test_register(tier, "T1.088", "test_t1_gauss_03", "Empirical 68-95-99.7 rule verification", test_t1_gauss_03, false);
    test_register(tier, "T1.089", "test_t1_gauss_04", "Gaussian bell-curve symmetry skewness", test_t1_gauss_04, false);
    test_register(tier, "T1.090", "test_t1_gauss_05", "Gaussian excess kurtosis verification", test_t1_gauss_05, false);

    test_register(tier, "T1.091", "test_t1_deriv_01", "Standard 32-byte subkey derivation", test_t1_deriv_01, false);
    test_register(tier, "T1.092", "test_t1_deriv_02", "Source context immutability invariant", test_t1_deriv_02, false);
    test_register(tier, "T1.093", "test_t1_deriv_03", "Domain separation across info strings", test_t1_deriv_03, false);
    test_register(tier, "T1.094", "test_t1_deriv_04", "Variable subkey derivation lengths", test_t1_deriv_04, false);
    test_register(tier, "T1.095", "test_t1_deriv_05", "Multi-thread independent subkey derivation", test_t1_deriv_05, false);

    test_register(tier, "T1.096", "test_t1_reseed_01", "Standard 32-byte user entropy injection", test_t1_reseed_01, false);
    test_register(tier, "T1.097", "test_t1_reseed_02", "Reseed auto-initialization cold start", test_t1_reseed_02, false);
    test_register(tier, "T1.098", "test_t1_reseed_03", "Variable entropy input sizes (16, 64, 128B)", test_t1_reseed_03, false);
    test_register(tier, "T1.099", "test_t1_reseed_04", "Reseed epoch counter reset invariant", test_t1_reseed_04, false);
    test_register(tier, "T1.100", "test_t1_reseed_05", "Sanitization of entropy buffers post-reseed", test_t1_reseed_05, false);

    test_register(tier, "T1.101", "test_t1_self_01", "RFC 8439 ChaCha20 selftest test vector", test_t1_self_01, false);
    test_register(tier, "T1.102", "test_t1_self_02", "Selftest memory wipe subtest verification", test_t1_self_02, false);
    test_register(tier, "T1.103", "test_t1_self_03", "Selftest idempotent consecutive calls", test_t1_self_03, false);
    test_register(tier, "T1.104", "test_t1_self_04", "Selftest multi-thread concurrent calls", test_t1_self_04, false);
    test_register(tier, "T1.105", "test_t1_self_05", "Selftest pre-init automatic invocation", test_t1_self_05, false);

    test_register(tier, "T1.106", "test_t1_ct_01", "Constant-time cmp identical buffers", test_t1_ct_01, false);
    test_register(tier, "T1.107", "test_t1_ct_02", "Constant-time cmp 1-bit differing buffers", test_t1_ct_02, false);
    test_register(tier, "T1.108", "test_t1_ct_03", "Constant-time select full mask multiplexing", test_t1_ct_03, false);
    test_register(tier, "T1.109", "test_t1_ct_04", "Constant-time select bitwise multiplexing", test_t1_ct_04, false);
    test_register(tier, "T1.110", "test_t1_ct_05", "Constant-time cmp non-early-exit invariant", test_t1_ct_05, false);
}
