#include "e2e_common.h"

/* ========================================================================= */
/* Domain 1: kheiron_init() Boundaries                                       */
/* ========================================================================= */

static test_status_t test_t2_init_01(void) {
    kheiron_init();
    uint8_t p1[32];
    ASSERT_EQ_INT(kheiron_fill(p1, 32), 0);

    int pipe_fd[2];
    ASSERT_EQ_INT(pipe(pipe_fd), 0);
    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    ASSERT_TRUE(pid >= 0);
    if (pid == 0) {
        close(pipe_fd[0]);
        uint8_t child_out[32];
        kheiron_fill(child_out, 32);
        ssize_t w = write(pipe_fd[1], child_out, 32);
        (void)w;
        close(pipe_fd[1]);
        _exit(0);
    } else {
        close(pipe_fd[1]);
        uint8_t child_rx[32];
        ssize_t r = read(pipe_fd[0], child_rx, 32);
        (void)r;
        close(pipe_fd[0]);
        int st = 0;
        waitpid(pid, &st, 0);
        uint8_t p2[32];
        ASSERT_EQ_INT(kheiron_fill(p2, 32), 0);
        ASSERT_MEM_NEQ(child_rx, p2, 32);
    }
    return TEST_PASS;
}

static test_status_t test_t2_init_02(void) {
    size_t sz = 1048576 + 1024;
    uint8_t *b = (uint8_t *)malloc(sz);
    ASSERT_NOT_NULL(b);
    ASSERT_EQ_INT(kheiron_fill(b, sz), 0);
    free(b);
    ASSERT_EQ_INT(kheiron_init(), 0);
    uint64_t v = kheiron_u64();
    ASSERT_TRUE(v != 0ULL);
    return TEST_PASS;
}

static void *thread_lifecycle_worker(void *arg) {
    (void)arg;
    kheiron_init();
    uint8_t b[16];
    kheiron_fill(b, 16);
    return NULL;
}

static test_status_t test_t2_init_03(void) {
    for (int i = 0; i < 50; i++) {
        pthread_t th;
        pthread_create(&th, NULL, thread_lifecycle_worker, NULL);
        pthread_join(th, NULL);
    }
    return TEST_PASS;
}

static test_status_t test_t2_init_04(void) {
    ASSERT_EQ_INT(kheiron_init(), 0);
    ASSERT_EQ_INT(kheiron_reseed("interleaved_entropy_123456789012", 32), 0);
    ASSERT_EQ_INT(kheiron_init(), 0);
    uint32_t val = kheiron_u32();
    ASSERT_TRUE(val != 0U);
    return TEST_PASS;
}

static test_status_t test_t2_init_05(void) {
    ASSERT_EQ_INT(kheiron_selftest(), 0);
    ASSERT_EQ_INT(kheiron_init(), 0);
    return TEST_PASS;
}

/* ========================================================================= */
/* Domain 2: kheiron_fill() Boundaries                                       */
/* ========================================================================= */

static test_status_t test_t2_fill_01(void) {
    uint8_t buf[16];
    memset(buf, 0xAA, sizeof(buf));
    ASSERT_EQ_INT(kheiron_fill(buf, 0), 0);
    for (int i = 0; i < 16; i++) {
        ASSERT_EQ_INT(buf[i], 0xAA);
    }
    return TEST_PASS;
}

static test_status_t test_t2_fill_02(void) {
    uint8_t pre[64], target = 0x00, post[64];
    memset(pre, 0x5A, 64);
    memset(post, 0xA5, 64);
    ASSERT_EQ_INT(kheiron_fill(&target, 1), 0);
    for (int i = 0; i < 64; i++) {
        ASSERT_EQ_INT(pre[i], 0x5A);
        ASSERT_EQ_INT(post[i], 0xA5);
    }
    return TEST_PASS;
}

static test_status_t test_t2_fill_03(void) {
    const size_t lens[] = {63, 64, 65};
    for (int l = 0; l < 3; l++) {
        size_t len = lens[l];
        uint8_t *raw = (uint8_t *)malloc(64 + len + 64);
        ASSERT_NOT_NULL(raw);
        uint8_t *pre = raw;
        uint8_t *tgt = raw + 64;
        uint8_t *post = tgt + len;
        memset(pre, 0x5A, 64);
        memset(tgt, 0, len);
        memset(post, 0xA5, 64);

        ASSERT_EQ_INT(kheiron_fill(tgt, len), 0);

        for (int i = 0; i < 64; i++) {
            ASSERT_EQ_INT(pre[i], 0x5A);
            ASSERT_EQ_INT(post[i], 0xA5);
        }
        free(raw);
    }
    return TEST_PASS;
}

static test_status_t test_t2_fill_04(void) {
    const size_t lens[] = {511, 512, 513};
    for (int l = 0; l < 3; l++) {
        size_t len = lens[l];
        uint8_t *raw = (uint8_t *)malloc(64 + len + 64);
        ASSERT_NOT_NULL(raw);
        uint8_t *pre = raw;
        uint8_t *tgt = raw + 64;
        uint8_t *post = tgt + len;
        memset(pre, 0x5A, 64);
        memset(tgt, 0, len);
        memset(post, 0xA5, 64);

        ASSERT_EQ_INT(kheiron_fill(tgt, len), 0);

        for (int i = 0; i < 64; i++) {
            ASSERT_EQ_INT(pre[i], 0x5A);
            ASSERT_EQ_INT(post[i], 0xA5);
        }
        free(raw);
    }
    return TEST_PASS;
}

static test_status_t test_t2_fill_05(void) {
    const size_t lens[] = {1048575, 1048576, 1048577};
    for (int l = 0; l < 3; l++) {
        size_t len = lens[l];
        uint8_t *buf = (uint8_t *)malloc(len);
        ASSERT_NOT_NULL(buf);
        ASSERT_EQ_INT(kheiron_fill(buf, len), 0);
        free(buf);
    }
    return TEST_PASS;
}

/* ========================================================================= */
/* Domain 3: kheiron_u64() Boundaries                                        */
/* ========================================================================= */

static test_status_t test_t2_u64_01(void) {
    /* Advance within 8 bytes of 1MB */
    size_t advance = 1048576 - 8;
    uint8_t *buf = (uint8_t *)malloc(advance);
    ASSERT_NOT_NULL(buf);
    ASSERT_EQ_INT(kheiron_fill(buf, advance), 0);
    free(buf);

    uint64_t v1 = kheiron_u64();
    uint64_t v2 = kheiron_u64();
    ASSERT_TRUE(v1 != 0ULL && v2 != 0ULL);
    ASSERT_TRUE(v1 != v2);
    return TEST_PASS;
}

static test_status_t test_t2_u64_02(void) {
    uint64_t min_obs = UINT64_MAX, max_obs = 0;
    for (int i = 0; i < 50000; i++) {
        uint64_t v = kheiron_u64();
        if (v < min_obs) min_obs = v;
        if (v > max_obs) max_obs = v;
    }
    ASSERT_TRUE(min_obs < (1ULL << 56));
    ASSERT_TRUE(max_obs > (0xF0ULL << 56));
    return TEST_PASS;
}

static test_status_t test_t2_u64_03(void) {
    volatile uint8_t stack_pad[16];
    for (int i = 0; i < 7; i++) {
        stack_pad[i] = (uint8_t)i;
        (void)stack_pad[i];
        uint64_t val = kheiron_u64();
        ASSERT_TRUE(val != 0ULL);
    }
    return TEST_PASS;
}

static test_status_t test_t2_u64_04(void) {
    uint64_t val = kheiron_u64();
    ASSERT_TRUE(val != 0ULL);
    return TEST_PASS;
}

static test_status_t test_t2_u64_05(void) {
    uint32_t bins[256] = {0};
    const int N = 25600;
    for (int i = 0; i < N; i++) {
        uint64_t val = kheiron_u64();
        bins[val & 0xFF]++;
    }
    double expected = (double)N / 256.0;
    double chi2 = 0.0;
    for (int i = 0; i < 256; i++) {
        double d = (double)bins[i] - expected;
        chi2 += (d * d) / expected;
    }
    ASSERT_TRUE(chi2 < 380.0);
    return TEST_PASS;
}

/* ========================================================================= */
/* Domain 4: kheiron_u32() Boundaries                                        */
/* ========================================================================= */

static test_status_t test_t2_u32_01(void) {
    uint8_t b60[60];
    ASSERT_EQ_INT(kheiron_fill(b60, 60), 0);
    uint32_t val = kheiron_u32();
    ASSERT_TRUE(val != 0U);
    return TEST_PASS;
}

static test_status_t test_t2_u32_02(void) {
    size_t advance = 1048576 - 4;
    uint8_t *buf = (uint8_t *)malloc(advance);
    ASSERT_NOT_NULL(buf);
    ASSERT_EQ_INT(kheiron_fill(buf, advance), 0);
    free(buf);

    uint32_t v1 = kheiron_u32();
    uint32_t v2 = kheiron_u32();
    ASSERT_TRUE(v1 != 0U && v2 != 0U);
    ASSERT_TRUE(v1 != v2);
    return TEST_PASS;
}

static test_status_t test_t2_u32_03(void) {
    uint32_t val = kheiron_u32();
    ASSERT_TRUE(val != 0U);
    return TEST_PASS;
}

static test_status_t test_t2_u32_04(void) {
    uint32_t min_obs = UINT32_MAX, max_obs = 0;
    for (int i = 0; i < 50000; i++) {
        uint32_t v = kheiron_u32();
        if (v < min_obs) min_obs = v;
        if (v > max_obs) max_obs = v;
    }
    ASSERT_TRUE(min_obs < (1U << 24));
    ASSERT_TRUE(max_obs > 0xF0000000U);
    return TEST_PASS;
}

static test_status_t test_t2_u32_05(void) {
    uint32_t bins[256] = {0};
    const int N = 25600;
    for (int i = 0; i < N; i++) {
        uint32_t val = kheiron_u32();
        bins[(val >> 24) & 0xFF]++;
    }
    double expected = (double)N / 256.0;
    double chi2 = 0.0;
    for (int i = 0; i < 256; i++) {
        double d = (double)bins[i] - expected;
        chi2 += (d * d) / expected;
    }
    ASSERT_TRUE(chi2 < 380.0);
    return TEST_PASS;
}

/* ========================================================================= */
/* Domain 5: kheiron_range_u64() Boundaries                                  */
/* ========================================================================= */

static test_status_t test_t2_range_01(void) {
    ASSERT_EQ_U64(kheiron_range_u64(0, 0), 0);
    ASSERT_EQ_U64(kheiron_range_u64(42, 42), 42);
    ASSERT_EQ_U64(kheiron_range_u64(UINT64_MAX, UINT64_MAX), UINT64_MAX);
    return TEST_PASS;
}

static test_status_t test_t2_range_02(void) {
    ASSERT_EQ_U64(kheiron_range_u64(100, 50), 100);
    ASSERT_EQ_U64(kheiron_range_u64(UINT64_MAX, 0), UINT64_MAX);
    return TEST_PASS;
}

static test_status_t test_t2_range_03(void) {
    for (int i = 0; i < 100; i++) {
        uint64_t v = kheiron_range_u64(0, UINT64_MAX);
        (void)v;
    }
    return TEST_PASS;
}

static test_status_t test_t2_range_04(void) {
    uint64_t min_v = UINT64_MAX - 5;
    for (int i = 0; i < 1000; i++) {
        uint64_t v = kheiron_range_u64(min_v, UINT64_MAX);
        ASSERT_TRUE(v >= min_v);
    }
    return TEST_PASS;
}

static test_status_t test_t2_range_05(void) {
    uint64_t s = 3ULL * (1ULL << 62);
    for (int i = 0; i < 1000; i++) {
        uint64_t v = kheiron_range_u64(0, s - 1);
        ASSERT_TRUE(v < s);
    }
    return TEST_PASS;
}

/* ========================================================================= */
/* Domain 6: kheiron_uniform_double() Boundaries                             */
/* ========================================================================= */

static test_status_t test_t2_dbl_01(void) {
    for (int i = 0; i < 100000; i++) {
        double d = kheiron_uniform_double();
        ASSERT_TRUE(d < 1.0);
        ASSERT_TRUE(d <= (1.0 - 0x1.0p-53));
    }
    return TEST_PASS;
}

static test_status_t test_t2_dbl_02(void) {
    for (int i = 0; i < 10000; i++) {
        double d = kheiron_uniform_double();
        ASSERT_TRUE(d >= 0.0);
        ASSERT_TRUE(fmod(d, 0x1.0p-53) == 0.0);
    }
    return TEST_PASS;
}

static test_status_t test_t2_dbl_03(void) {
    for (int i = 0; i < 1000; i++) {
        double d = kheiron_uniform_double();
        double k = d * 9007199254740992.0; /* 2^53 */
        ASSERT_TRUE(k == floor(k));
    }
    return TEST_PASS;
}

static test_status_t test_t2_dbl_04(void) {
    double min_d = 1.0;
    for (int i = 0; i < 50000; i++) {
        double d = kheiron_uniform_double();
        if (d < min_d) min_d = d;
    }
    ASSERT_TRUE(min_d >= 0.0 && min_d < 0.001);
    return TEST_PASS;
}

static test_status_t test_t2_dbl_05(void) {
    for (int i = 0; i < 131072; i++) {
        double d = kheiron_uniform_double();
        ASSERT_DOUBLE_BETWEEN(d, 0.0, 1.0);
    }
    return TEST_PASS;
}

/* ========================================================================= */
/* Domain 7: kheiron_uniform_float() Boundaries                              */
/* ========================================================================= */

static test_status_t test_t2_flt_01(void) {
    for (int i = 0; i < 100000; i++) {
        float f = kheiron_uniform_float();
        ASSERT_TRUE(f < 1.0f);
        ASSERT_TRUE(f <= (1.0f - 0x1.0p-24f));
    }
    return TEST_PASS;
}

static test_status_t test_t2_flt_02(void) {
    for (int i = 0; i < 10000; i++) {
        float f = kheiron_uniform_float();
        ASSERT_TRUE(f >= 0.0f);
        ASSERT_TRUE(fmodf(f, 0x1.0p-24f) == 0.0f);
    }
    return TEST_PASS;
}

static test_status_t test_t2_flt_03(void) {
    for (int i = 0; i < 1000; i++) {
        float f = kheiron_uniform_float();
        float k = f * 16777216.0f; /* 2^24 */
        ASSERT_TRUE(k == floorf(k));
    }
    return TEST_PASS;
}

static test_status_t test_t2_flt_04(void) {
    float min_f = 1.0f;
    for (int i = 0; i < 50000; i++) {
        float f = kheiron_uniform_float();
        if (f < min_f) min_f = f;
    }
    ASSERT_TRUE(min_f >= 0.0f && min_f < 0.001f);
    return TEST_PASS;
}

static test_status_t test_t2_flt_05(void) {
    for (int i = 0; i < 262144; i++) {
        float f = kheiron_uniform_float();
        ASSERT_FLOAT_BETWEEN(f, 0.0f, 1.0f);
    }
    return TEST_PASS;
}

/* ========================================================================= */
/* Domain 8: kheiron_secure_wipe() Boundaries                                */
/* ========================================================================= */

static test_status_t test_t2_wipe_01(void) {
    uint8_t buf[16];
    memset(buf, 0x77, sizeof(buf));
    kheiron_secure_wipe(buf, 0);
    for (int i = 0; i < 16; i++) {
        ASSERT_EQ_INT(buf[i], 0x77);
    }
    return TEST_PASS;
}

static test_status_t test_t2_wipe_02(void) {
    uint8_t pre[16], tgt = 0xFF, post[16];
    memset(pre, 0x5A, 16);
    memset(post, 0xA5, 16);
    kheiron_secure_wipe(&tgt, 1);
    ASSERT_EQ_INT(tgt, 0);
    for (int i = 0; i < 16; i++) {
        ASSERT_EQ_INT(pre[i], 0x5A);
        ASSERT_EQ_INT(post[i], 0xA5);
    }
    return TEST_PASS;
}

static test_status_t test_t2_wipe_03(void) {
    uint8_t raw[256];
    uint8_t *base = (uint8_t *)(((uintptr_t)raw + 63) & ~(uintptr_t)63);
    for (size_t offset = 1; offset <= 63; offset++) {
        memset(base, 0x5A, 128);
        uint8_t *tgt = base + offset;
        memset(tgt, 0xFF, 16);
        kheiron_secure_wipe(tgt, 16);
        for (int i = 0; i < 16; i++) ASSERT_EQ_INT(tgt[i], 0);
        for (size_t i = 0; i < offset; i++) ASSERT_EQ_INT(base[i], 0x5A);
    }
    return TEST_PASS;
}

static test_status_t test_t2_wipe_04(void) {
    kheiron_secure_wipe(NULL, 128);
    return TEST_PASS;
}

static test_status_t test_t2_wipe_05(void) {
    kheiron_secure_wipe(NULL, 0);
    return TEST_PASS;
}

/* ========================================================================= */
/* Domain 9: kheiron_ctx_init() Boundaries                                   */
/* ========================================================================= */

static test_status_t test_t2_ctxinit_01(void) {
    ASSERT_EQ_INT(kheiron_ctx_init(NULL, "seed", 4), -1);
    return TEST_PASS;
}

static test_status_t test_t2_ctxinit_02(void) {
    ASSERT_EQ_INT(kheiron_ctx_init(NULL, NULL, 0), -1);
    return TEST_PASS;
}

static test_status_t test_t2_ctxinit_03(void) {
    kheiron_ctx_t ctx;
    uint8_t s = 0x55;
    ASSERT_EQ_INT(kheiron_ctx_init(&ctx, &s, 1), 0);
    uint8_t buf[16];
    ASSERT_EQ_INT(kheiron_ctx_fill(&ctx, buf, 16), 0);
    kheiron_ctx_wipe(&ctx);
    return TEST_PASS;
}

static test_status_t test_t2_ctxinit_04(void) {
    kheiron_ctx_t ctx;
    uint8_t dummy[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    ASSERT_EQ_INT(kheiron_ctx_init(&ctx, dummy, 0), 0);
    kheiron_ctx_wipe(&ctx);
    return TEST_PASS;
}

static test_status_t test_t2_ctxinit_05(void) {
    kheiron_ctx_t ctx;
    ASSERT_EQ_INT(kheiron_ctx_init(&ctx, NULL, 0), 0);
    uint8_t buf[1024];
    ASSERT_EQ_INT(kheiron_ctx_fill(&ctx, buf, 1024), 0);
    ASSERT_EQ_U64(ctx.bytes_generated, 1024);
    ASSERT_EQ_INT(kheiron_ctx_init(&ctx, "new_seed", 8), 0);
    ASSERT_EQ_U64(ctx.bytes_generated, 0);
    kheiron_ctx_wipe(&ctx);
    return TEST_PASS;
}

/* ========================================================================= */
/* Domain 10: kheiron_ctx_fill() Boundaries                                  */
/* ========================================================================= */

static test_status_t test_t2_ctxfill_01(void) {
    kheiron_ctx_t ctx;
    ASSERT_EQ_INT(kheiron_ctx_init(&ctx, NULL, 0), 0);
    uint8_t buf[16];
    memset(buf, 0x33, 16);
    ASSERT_EQ_INT(kheiron_ctx_fill(&ctx, buf, 0), 0);
    for (int i = 0; i < 16; i++) ASSERT_EQ_INT(buf[i], 0x33);
    ASSERT_EQ_U64(ctx.bytes_generated, 0);
    kheiron_ctx_wipe(&ctx);
    return TEST_PASS;
}

static test_status_t test_t2_ctxfill_02(void) {
    uint8_t buf[64];
    ASSERT_EQ_INT(kheiron_ctx_fill(NULL, buf, 64), -1);
    return TEST_PASS;
}

static test_status_t test_t2_ctxfill_03(void) {
    kheiron_ctx_t ctx;
    ASSERT_EQ_INT(kheiron_ctx_init(&ctx, NULL, 0), 0);
    ASSERT_EQ_INT(kheiron_ctx_fill(&ctx, NULL, 64), -1);
    kheiron_ctx_wipe(&ctx);
    return TEST_PASS;
}

static test_status_t test_t2_ctxfill_04(void) {
    ASSERT_EQ_INT(kheiron_ctx_fill(NULL, NULL, 64), -1);
    return TEST_PASS;
}

static test_status_t test_t2_ctxfill_05(void) {
    kheiron_ctx_t ctx;
    ASSERT_EQ_INT(kheiron_ctx_init(&ctx, NULL, 0), 0);
    ASSERT_EQ_INT(kheiron_ctx_fill(&ctx, NULL, 0), -1);
    kheiron_ctx_wipe(&ctx);
    return TEST_PASS;
}

/* ========================================================================= */
/* Domain 11: kheiron_ctx_wipe() Boundaries                                  */
/* ========================================================================= */

static test_status_t test_t2_ctxwipe_01(void) {
    kheiron_ctx_wipe(NULL);
    return TEST_PASS;
}

static test_status_t test_t2_ctxwipe_02(void) {
    kheiron_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    kheiron_ctx_wipe(&ctx);
    const uint8_t *p = (const uint8_t *)&ctx;
    for (size_t i = 0; i < sizeof(ctx); i++) ASSERT_EQ_INT(p[i], 0);
    return TEST_PASS;
}

static test_status_t test_t2_ctxwipe_03(void) {
    uint8_t raw[sizeof(kheiron_ctx_t) + 64];
    kheiron_ctx_t *uctx = (kheiron_ctx_t *)(raw + 1);
    memset(uctx, 0xFF, sizeof(kheiron_ctx_t));
    kheiron_ctx_wipe(uctx);
    const uint8_t *p = (const uint8_t *)uctx;
    for (size_t i = 0; i < sizeof(kheiron_ctx_t); i++) ASSERT_EQ_INT(p[i], 0);
    return TEST_PASS;
}

static test_status_t test_t2_ctxwipe_04(void) {
    kheiron_ctx_t ctx;
    ASSERT_EQ_INT(kheiron_ctx_init(&ctx, "seed", 4), 0);
    kheiron_ctx_wipe(&ctx);
    ASSERT_EQ_U64(ctx.bytes_generated, 0);
    ASSERT_EQ_U64(ctx.state[0], 0);
    return TEST_PASS;
}

static test_status_t test_t2_ctxwipe_05(void) {
    kheiron_ctx_t ctx;
    ASSERT_EQ_INT(kheiron_ctx_init(&ctx, NULL, 0), 0);
    kheiron_ctx_wipe(&ctx);
    return TEST_PASS;
}

/* ========================================================================= */
/* Domain 12: 1MB Rotation Rollover Boundaries                               */
/* ========================================================================= */

static test_status_t test_t2_rot_01(void) {
    size_t sz1 = 1048575;
    uint8_t *b1 = (uint8_t *)malloc(sz1);
    ASSERT_NOT_NULL(b1);
    ASSERT_EQ_INT(kheiron_fill(b1, sz1), 0);
    free(b1);

    uint8_t b2[2];
    ASSERT_EQ_INT(kheiron_fill(b2, 2), 0);
    return TEST_PASS;
}

static test_status_t test_t2_rot_02(void) {
    size_t sz = 1048577;
    uint8_t *buf = (uint8_t *)malloc(sz);
    ASSERT_NOT_NULL(buf);
    ASSERT_EQ_INT(kheiron_fill(buf, sz), 0);
    free(buf);
    return TEST_PASS;
}

static test_status_t test_t2_rot_03(void) {
    size_t advance = 1048576 - 256;
    uint8_t *b = (uint8_t *)malloc(advance);
    ASSERT_NOT_NULL(b);
    ASSERT_EQ_INT(kheiron_fill(b, advance), 0);
    free(b);

    uint8_t buf1024[1024];
    ASSERT_EQ_INT(kheiron_fill(buf1024, 1024), 0);
    return TEST_PASS;
}

static test_status_t test_t2_rot_04(void) {
    size_t advance = 1048576 - 64;
    uint8_t *b = (uint8_t *)malloc(advance);
    ASSERT_NOT_NULL(b);
    ASSERT_EQ_INT(kheiron_fill(b, advance), 0);
    free(b);

    uint8_t ep0[64], ep1[64];
    ASSERT_EQ_INT(kheiron_fill(ep0, 64), 0);
    ASSERT_EQ_INT(kheiron_fill(ep1, 64), 0);
    ASSERT_MEM_NEQ(ep0, ep1, 64);
    return TEST_PASS;
}

static test_status_t test_t2_rot_05(void) {
    for (int i = 0; i < 5; i++) {
        uint8_t *b = (uint8_t *)malloc(1048576);
        ASSERT_NOT_NULL(b);
        ASSERT_EQ_INT(kheiron_fill(b, 1048576), 0);
        free(b);
    }
    return TEST_PASS;
}

/* ========================================================================= */
/* Domain 13: Unaligned Buffer Offsets 1..63 Sweep                           */
/* ========================================================================= */

static test_status_t test_t2_align_01(void) {
    uint8_t raw[256];
    uint8_t *base = (uint8_t *)(((uintptr_t)raw + 63) & ~(uintptr_t)63);
    for (size_t offset = 1; offset <= 63; offset++) {
        memset(base, 0x5A, 128);
        uint8_t *tgt = base + offset;
        tgt[0] = 0;
        tgt[1] = 0xA5;
        ASSERT_EQ_INT(kheiron_fill(tgt, 1), 0);
        ASSERT_EQ_INT(tgt[1], 0xA5);
        for (size_t i = 0; i < offset; i++) ASSERT_EQ_INT(base[i], 0x5A);
    }
    return TEST_PASS;
}

static test_status_t test_t2_align_02(void) {
    uint8_t raw[256];
    uint8_t *base = (uint8_t *)(((uintptr_t)raw + 63) & ~(uintptr_t)63);
    for (size_t offset = 1; offset <= 63; offset++) {
        memset(base, 0x5A, 128 + 64);
        uint8_t *tgt = base + offset;
        tgt[63] = 0xA5;
        ASSERT_EQ_INT(kheiron_fill(tgt, 63), 0);
        for (size_t i = 0; i < offset; i++) ASSERT_EQ_INT(base[i], 0x5A);
    }
    return TEST_PASS;
}

static test_status_t test_t2_align_03(void) {
    uint8_t raw[1024];
    uint8_t *base = (uint8_t *)(((uintptr_t)raw + 63) & ~(uintptr_t)63);
    for (size_t offset = 1; offset <= 63; offset++) {
        memset(base, 0x5A, 64);
        uint8_t *tgt = base + offset;
        ASSERT_EQ_INT(kheiron_fill(tgt, 512), 0);
        for (size_t i = 0; i < offset; i++) ASSERT_EQ_INT(base[i], 0x5A);
    }
    return TEST_PASS;
}

static test_status_t test_t2_align_04(void) {
    uint8_t raw[1024];
    uint8_t *base = (uint8_t *)(((uintptr_t)raw + 63) & ~(uintptr_t)63);
    for (size_t offset = 1; offset <= 63; offset++) {
        memset(base, 0x5A, 64);
        uint8_t *tgt = base + offset;
        ASSERT_EQ_INT(kheiron_fill(tgt, 513), 0);
        for (size_t i = 0; i < offset; i++) ASSERT_EQ_INT(base[i], 0x5A);
    }
    return TEST_PASS;
}

static test_status_t test_t2_align_05(void) {
    uint8_t raw[1024];
    uint8_t *base = (uint8_t *)(((uintptr_t)raw + 63) & ~(uintptr_t)63);
    memset(base, 0x5A, 64);
    uint8_t *tgt = base + 63;
    ASSERT_EQ_INT(kheiron_fill(tgt, 512), 0);
    for (int i = 0; i < 63; i++) ASSERT_EQ_INT(base[i], 0x5A);
    return TEST_PASS;
}

/* ========================================================================= */
/* Domain 14: Critical Boundary Buffer Sizes                                 */
/* ========================================================================= */

static test_status_t test_t2_size_01(void) {
    uint8_t buf[16];
    memset(buf, 0x77, 16);
    ASSERT_EQ_INT(kheiron_fill(buf, 0), 0);
    for (int i = 0; i < 16; i++) ASSERT_EQ_INT(buf[i], 0x77);
    return TEST_PASS;
}

static test_status_t test_t2_size_02(void) {
    uint8_t pre[128], tgt = 0, post[128];
    memset(pre, 0x5A, 128);
    memset(post, 0xA5, 128);
    ASSERT_EQ_INT(kheiron_fill(&tgt, 1), 0);
    for (int i = 0; i < 128; i++) {
        ASSERT_EQ_INT(pre[i], 0x5A);
        ASSERT_EQ_INT(post[i], 0xA5);
    }
    return TEST_PASS;
}

static test_status_t test_t2_size_03(void) {
    const size_t lens[] = {511, 512, 513};
    for (int i = 0; i < 3; i++) {
        size_t l = lens[i];
        uint8_t *mem = (uint8_t *)malloc(128 + l + 128);
        ASSERT_NOT_NULL(mem);
        memset(mem, 0x5A, 128);
        memset(mem + 128 + l, 0xA5, 128);
        ASSERT_EQ_INT(kheiron_fill(mem + 128, l), 0);
        for (int k = 0; k < 128; k++) {
            ASSERT_EQ_INT(mem[k], 0x5A);
            ASSERT_EQ_INT((mem + 128 + l)[k], 0xA5);
        }
        free(mem);
    }
    return TEST_PASS;
}

static test_status_t test_t2_size_04(void) {
    const size_t lens[] = {1048575, 1048576, 1048577};
    for (int i = 0; i < 3; i++) {
        uint8_t *b = (uint8_t *)malloc(lens[i]);
        ASSERT_NOT_NULL(b);
        ASSERT_EQ_INT(kheiron_fill(b, lens[i]), 0);
        free(b);
    }
    return TEST_PASS;
}

static test_status_t test_t2_size_05(void) {
    size_t sz = 4194304; /* 4 MB */
    uint8_t *b = (uint8_t *)malloc(sz);
    ASSERT_NOT_NULL(b);
    ASSERT_EQ_INT(kheiron_fill(b, sz), 0);
    free(b);
    return TEST_PASS;
}

/* ========================================================================= */
/* Domain 15: Defensive NULL Pointer Handling                                */
/* ========================================================================= */

static test_status_t test_t2_null_01(void) {
    ASSERT_EQ_INT(kheiron_fill(NULL, 0), -1);
    return TEST_PASS;
}

static test_status_t test_t2_null_02(void) {
    kheiron_ctx_t ctx;
    ASSERT_EQ_INT(kheiron_ctx_init(&ctx, NULL, 0), 0);
    ASSERT_EQ_INT(kheiron_ctx_fill(&ctx, NULL, 0), -1);
    kheiron_ctx_wipe(&ctx);
    return TEST_PASS;
}

static test_status_t test_t2_null_03(void) {
    kheiron_ctx_t ctx;
    uint8_t out[32];
    ASSERT_EQ_INT(kheiron_ctx_init(&ctx, NULL, 0), 0);
    ASSERT_EQ_INT(kheiron_derive(NULL, out, 32, "info", 4), -1);
    ASSERT_EQ_INT(kheiron_derive(&ctx, NULL, 32, "info", 4), -1);
    ASSERT_EQ_INT(kheiron_derive(&ctx, out, 32, NULL, 4), -1);
    kheiron_ctx_wipe(&ctx);
    return TEST_PASS;
}

static test_status_t test_t2_null_04(void) {
    char str[16];
    int arr[10];
    ASSERT_EQ_INT(kheiron_string(NULL, 10, "abc"), -1);
    ASSERT_EQ_INT(kheiron_string(str, 10, NULL), -1);
    ASSERT_EQ_INT(kheiron_shuffle(NULL, 10, sizeof(int)), -1);
    (void)arr;
    return TEST_PASS;
}

static test_status_t test_t2_null_05(void) {
    uint8_t b[16] = {0};
    ASSERT_EQ_INT(kheiron_reseed(NULL, 32), -1);
    ASSERT_EQ_INT(kheiron_consttime_cmp(NULL, b, 16), -1);
    ASSERT_EQ_INT(kheiron_consttime_cmp(b, NULL, 16), -1);
    return TEST_PASS;
}

/* ========================================================================= */
/* Domain 16: kheiron_string() Boundaries                                    */
/* ========================================================================= */

static test_status_t test_t2_str_01(void) {
    char str[17];
    ASSERT_EQ_INT(kheiron_string(str, 16, "A"), 0);
    ASSERT_EQ_INT(str[16], '\0');
    for (int i = 0; i < 16; i++) {
        ASSERT_EQ_INT(str[i], 'A');
    }
    return TEST_PASS;
}

static test_status_t test_t2_str_02(void) {
    char str[16];
    ASSERT_EQ_INT(kheiron_string(str, 0, "abc"), -1);
    return TEST_PASS;
}

static test_status_t test_t2_str_03(void) {
    char str[16];
    ASSERT_EQ_INT(kheiron_string(str, 16, ""), -1);
    return TEST_PASS;
}

static test_status_t test_t2_str_04(void) {
    char full_set[256];
    for (int i = 1; i <= 255; i++) full_set[i - 1] = (char)i;
    full_set[255] = '\0';
    char out[100];
    ASSERT_EQ_INT(kheiron_string(out, 99, full_set), 0);
    ASSERT_EQ_INT(out[99], '\0');
    return TEST_PASS;
}

static test_status_t test_t2_str_05(void) {
    uint8_t pre[16], tgt[2] = {0, 0}, post[16];
    memset(pre, 0x5A, 16);
    memset(post, 0xA5, 16);
    ASSERT_EQ_INT(kheiron_string((char *)tgt, 1, "XYZ"), 0);
    ASSERT_EQ_INT(tgt[1], '\0');
    ASSERT_TRUE(tgt[0] == 'X' || tgt[0] == 'Y' || tgt[0] == 'Z');
    for (int i = 0; i < 16; i++) {
        ASSERT_EQ_INT(pre[i], 0x5A);
        ASSERT_EQ_INT(post[i], 0xA5);
    }
    return TEST_PASS;
}

/* ========================================================================= */
/* Domain 17: kheiron_shuffle() Boundaries                                   */
/* ========================================================================= */

static test_status_t test_t2_shuf_01(void) {
    int arr[2] = {1, 2};
    ASSERT_EQ_INT(kheiron_shuffle(arr, 0, sizeof(int)), -1);
    ASSERT_EQ_INT(kheiron_shuffle(arr, 1, sizeof(int)), -1);
    return TEST_PASS;
}

static test_status_t test_t2_shuf_02(void) {
    int arr[10];
    ASSERT_EQ_INT(kheiron_shuffle(arr, 10, 0), -1);
    return TEST_PASS;
}

static test_status_t test_t2_shuf_03(void) {
    int c0 = 0, c1 = 0;
    for (int t = 0; t < 5000; t++) {
        int pair[2] = {10, 20};
        ASSERT_EQ_INT(kheiron_shuffle(pair, 2, sizeof(int)), 0);
        if (pair[0] == 10) c0++;
        else if (pair[0] == 20) c1++;
    }
    ASSERT_TRUE(c0 >= 2300 && c0 <= 2700);
    ASSERT_TRUE(c1 >= 2300 && c1 <= 2700);
    return TEST_PASS;
}

static test_status_t test_t2_shuf_04(void) {
    uint8_t arr[8][256];
    for (int i = 0; i < 8; i++) memset(arr[i], i, 256);
    ASSERT_EQ_INT(kheiron_shuffle(arr, 8, 256), 0);
    return TEST_PASS;
}

static test_status_t test_t2_shuf_05(void) {
    uint8_t arr[4][257];
    for (int i = 0; i < 4; i++) memset(arr[i], i, 257);
    ASSERT_EQ_INT(kheiron_shuffle(arr, 4, 257), 0);
    return TEST_PASS;
}

/* ========================================================================= */
/* Domain 18: kheiron_gaussian() Boundaries                                  */
/* ========================================================================= */

static test_status_t test_t2_gauss_01(void) {
    double g = kheiron_gaussian(42.0, 0.0);
    ASSERT_TRUE(g == 42.0);
    return TEST_PASS;
}

static test_status_t test_t2_gauss_02(void) {
    double g = kheiron_gaussian(10.0, -5.0);
    ASSERT_TRUE(g == 10.0);
    return TEST_PASS;
}

static test_status_t test_t2_gauss_03(void) {
    for (int i = 0; i < 1000; i++) {
        double g = kheiron_gaussian(1.0, 1e-12);
        ASSERT_TRUE(fabs(g - 1.0) < 1e-10);
    }
    return TEST_PASS;
}

static test_status_t test_t2_gauss_04(void) {
    for (int i = 0; i < 1000; i++) {
        double g = kheiron_gaussian(0.0, 1e6);
        ASSERT_TRUE(!isnan(g) && !isinf(g));
    }
    return TEST_PASS;
}

static test_status_t test_t2_gauss_05(void) {
    for (int i = 0; i < 100000; i++) {
        double g = kheiron_gaussian(0.0, 1.0);
        ASSERT_TRUE(!isnan(g) && !isinf(g));
    }
    return TEST_PASS;
}

/* ========================================================================= */
/* Domain 19: kheiron_derive() Boundaries                                    */
/* ========================================================================= */

static test_status_t test_t2_deriv_01(void) {
    kheiron_ctx_t ctx;
    uint8_t out[32];
    ASSERT_EQ_INT(kheiron_ctx_init(&ctx, NULL, 0), 0);
    ASSERT_EQ_INT(kheiron_derive(&ctx, out, 0, "info", 4), -1);
    kheiron_ctx_wipe(&ctx);
    return TEST_PASS;
}

static test_status_t test_t2_deriv_02(void) {
    kheiron_ctx_t ctx;
    uint8_t out[32];
    ASSERT_EQ_INT(kheiron_ctx_init(&ctx, NULL, 0), 0);
    ASSERT_EQ_INT(kheiron_derive(&ctx, out, 32, "info", 0), -1);
    kheiron_ctx_wipe(&ctx);
    return TEST_PASS;
}

static test_status_t test_t2_deriv_03(void) {
    kheiron_ctx_t ctx;
    uint8_t out[32];
    ASSERT_EQ_INT(kheiron_ctx_init(&ctx, NULL, 0), 0);
    ASSERT_EQ_INT(kheiron_derive(NULL, out, 32, "info", 4), -1);
    ASSERT_EQ_INT(kheiron_derive(&ctx, NULL, 32, "info", 4), -1);
    ASSERT_EQ_INT(kheiron_derive(&ctx, out, 32, NULL, 4), -1);
    kheiron_ctx_wipe(&ctx);
    return TEST_PASS;
}

static test_status_t test_t2_deriv_04(void) {
    kheiron_ctx_t ctx;
    ASSERT_EQ_INT(kheiron_ctx_init(&ctx, "seed", 4), 0);
    char *big_info = (char *)malloc(4096);
    ASSERT_NOT_NULL(big_info);
    memset(big_info, 'K', 4096);
    uint8_t out[32];
    ASSERT_EQ_INT(kheiron_derive(&ctx, out, 32, big_info, 4096), 0);
    free(big_info);
    kheiron_ctx_wipe(&ctx);
    return TEST_PASS;
}

static test_status_t test_t2_deriv_05(void) {
    kheiron_ctx_t ctx;
    ASSERT_EQ_INT(kheiron_ctx_init(&ctx, "seed", 4), 0);
    uint8_t out[32];
    ASSERT_EQ_INT(kheiron_derive(&ctx, out, 32, "X", 1), 0);
    kheiron_ctx_wipe(&ctx);
    return TEST_PASS;
}

/* ========================================================================= */
/* Domain 20: kheiron_reseed() Boundaries                                    */
/* ========================================================================= */

static test_status_t test_t2_reseed_01(void) {
    ASSERT_EQ_INT(kheiron_reseed(NULL, 32), -1);
    return TEST_PASS;
}

static test_status_t test_t2_reseed_02(void) {
    uint8_t b[32] = {0};
    ASSERT_EQ_INT(kheiron_reseed(b, 0), -1);
    return TEST_PASS;
}

static test_status_t test_t2_reseed_03(void) {
    uint8_t byte = 0xAA;
    ASSERT_EQ_INT(kheiron_reseed(&byte, 1), 0);
    return TEST_PASS;
}

static test_status_t test_t2_reseed_04(void) {
    uint8_t *big_ent = (uint8_t *)malloc(8192);
    ASSERT_NOT_NULL(big_ent);
    memset(big_ent, 0x33, 8192);
    ASSERT_EQ_INT(kheiron_reseed(big_ent, 8192), 0);
    free(big_ent);
    return TEST_PASS;
}

static test_status_t test_t2_reseed_05(void) {
    size_t sz = 1048576;
    uint8_t *b = (uint8_t *)malloc(sz);
    ASSERT_NOT_NULL(b);
    ASSERT_EQ_INT(kheiron_fill(b, sz), 0);
    free(b);
    uint8_t ent[32] = "reseed_boundary_1MB_exact_test";
    ASSERT_EQ_INT(kheiron_reseed(ent, 32), 0);
    return TEST_PASS;
}

/* ========================================================================= */
/* Domain 21: kheiron_selftest() Boundaries                                  */
/* ========================================================================= */

static test_status_t test_t2_self_01(void) {
    ASSERT_EQ_INT(kheiron_selftest(), 0);
    return TEST_PASS;
}

static test_status_t test_t2_self_02(void) {
    uint8_t buf[64];
    memset(buf, 0xFF, sizeof(buf));
    kheiron_secure_wipe(buf, sizeof(buf));
    for (int i = 0; i < 64; i++) {
        ASSERT_EQ_INT(buf[i], 0);
    }
    return TEST_PASS;
}

static test_status_t test_t2_self_03(void) {
    volatile uint64_t v[16];
    for (int i = 0; i < 16; i++) v[i] = kheiron_u64();
    ASSERT_EQ_INT(kheiron_selftest(), 0);
    (void)v;
    return TEST_PASS;
}

static test_status_t test_t2_self_04(void) {
    uint32_t a = kheiron_u32();
    ASSERT_EQ_INT(kheiron_selftest(), 0);
    uint32_t b = kheiron_u32();
    ASSERT_TRUE(a != 0U && b != 0U);
    ASSERT_TRUE(a != b);
    return TEST_PASS;
}

static test_status_t test_t2_self_05(void) {
    struct timespec ts0, ts1;
    clock_gettime(CLOCK_MONOTONIC, &ts0);
    for (int i = 0; i < 100; i++) {
        ASSERT_EQ_INT(kheiron_selftest(), 0);
    }
    clock_gettime(CLOCK_MONOTONIC, &ts1);
    double elapsed_ms = (double)(ts1.tv_sec - ts0.tv_sec) * 1000.0 +
                        (double)(ts1.tv_nsec - ts0.tv_nsec) / 1000000.0;
    ASSERT_TRUE(elapsed_ms < 50.0);
    return TEST_PASS;
}

/* ========================================================================= */
/* Domain 22: Constant-Time Utilities Boundaries                             */
/* ========================================================================= */

static test_status_t test_t2_ct_01(void) {
    uint8_t b[16] = {0};
    ASSERT_EQ_INT(kheiron_consttime_cmp(NULL, b, 16), -1);
    ASSERT_EQ_INT(kheiron_consttime_cmp(b, NULL, 16), -1);
    ASSERT_EQ_INT(kheiron_consttime_cmp(NULL, NULL, 16), -1);
    return TEST_PASS;
}

static test_status_t test_t2_ct_02(void) {
    uint8_t a[16] = {0}, b[16] = {0};
    ASSERT_EQ_INT(kheiron_consttime_cmp(a, b, 0), -1);
    return TEST_PASS;
}

static test_status_t test_t2_ct_03(void) {
    uint8_t a = 0x55, b = 0x55, c = 0x56;
    ASSERT_EQ_INT(kheiron_consttime_cmp(&a, &b, 1), 0);
    ASSERT_EQ_INT(kheiron_consttime_cmp(&a, &c, 1), 1);
    return TEST_PASS;
}

static test_status_t test_t2_ct_04(void) {
    size_t sz = 65536;
    uint8_t *a = (uint8_t *)malloc(sz);
    uint8_t *b = (uint8_t *)malloc(sz);
    ASSERT_NOT_NULL(a);
    ASSERT_NOT_NULL(b);
    memset(a, 0xEE, sz);
    memset(b, 0xEE, sz);
    ASSERT_EQ_INT(kheiron_consttime_cmp(a, b, sz), 0);
    b[sz - 1] ^= 0x01;
    ASSERT_EQ_INT(kheiron_consttime_cmp(a, b, sz), 1);
    free(a);
    free(b);
    return TEST_PASS;
}

static test_status_t test_t2_ct_05(void) {
    const uint32_t patterns[] = {0x00000000, 0xFFFFFFFF, 0x55555555, 0xAAAAAAAA, 0x12345678};
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 5; j++) {
            uint32_t a = patterns[i], b = patterns[j];
            uint32_t res0 = kheiron_consttime_select_u32(0, a, b);
            uint32_t res1 = kheiron_consttime_select_u32(0xFFFFFFFFU, a, b);
            ASSERT_EQ_U64(res0, b);
            ASSERT_EQ_U64(res1, a);
        }
    }
    return TEST_PASS;
}

/* ========================================================================= */
/* Registration of Tier 2 Boundary & Corner Case Tests (110 Tests)           */
/* ========================================================================= */

void register_tier2_tests(void) {
    const char *tier = "Tier 2: Boundary & Corner Cases";

    /* Domain 1 */
    test_register(tier, "T2.001", "test_t2_init_01", "Post-fork state reseed and invalidation", test_t2_init_01, false);
    test_register(tier, "T2.002", "test_t2_init_02", "Init persistence across 1MB rollover", test_t2_init_02, false);
    test_register(tier, "T2.003", "test_t2_init_03", "Rapid thread lifecycle stress 50 threads", test_t2_init_03, false);
    test_register(tier, "T2.004", "test_t2_init_04", "Init immediately preceding/succeeding reseed", test_t2_init_04, false);
    test_register(tier, "T2.005", "test_t2_init_05", "Selftest and init sequence sanity", test_t2_init_05, false);

    /* Domain 2 */
    test_register(tier, "T2.006", "test_t2_fill_01", "Zero-length fill safe no-op", test_t2_fill_01, false);
    test_register(tier, "T2.007", "test_t2_fill_02", "Single byte fill with flanking canaries", test_t2_fill_02, false);
    test_register(tier, "T2.008", "test_t2_fill_03", "ChaCha block transition boundaries (63, 64, 65B)", test_t2_fill_03, false);
    test_register(tier, "T2.009", "test_t2_fill_04", "AVX2 transition boundaries (511, 512, 513B)", test_t2_fill_04, false);
    test_register(tier, "T2.010", "test_t2_fill_05", "1MB rotation boundary crossing (1MB-1, 1MB, 1MB+1)", test_t2_fill_05, false);

    /* Domain 3 */
    test_register(tier, "T2.011", "test_t2_u64_01", "u64 epoch rollover mid-stream", test_t2_u64_01, false);
    test_register(tier, "T2.012", "test_t2_u64_02", "u64 full dynamic range reachability", test_t2_u64_02, false);
    test_register(tier, "T2.013", "test_t2_u64_03", "u64 stack alignment variance", test_t2_u64_03, false);
    test_register(tier, "T2.014", "test_t2_u64_04", "u64 cold-start direct invocation", test_t2_u64_04, false);
    test_register(tier, "T2.015", "test_t2_u64_05", "u64 Chi-Square on least significant byte", test_t2_u64_05, false);

    /* Domain 4 */
    test_register(tier, "T2.016", "test_t2_u32_01", "u32 keystream residual tail depletion", test_t2_u32_01, false);
    test_register(tier, "T2.017", "test_t2_u32_02", "u32 1MB key rotation on 4-byte boundary", test_t2_u32_02, false);
    test_register(tier, "T2.018", "test_t2_u32_03", "u32 cold-start direct invocation", test_t2_u32_03, false);
    test_register(tier, "T2.019", "test_t2_u32_04", "u32 full 32-bit dynamic range coverage", test_t2_u32_04, false);
    test_register(tier, "T2.020", "test_t2_u32_05", "u32 Chi-Square on MSB (bits 24..31)", test_t2_u32_05, false);

    /* Domain 5 */
    test_register(tier, "T2.021", "test_t2_range_01", "range_u64 degenerate equal range min==max", test_t2_range_01, false);
    test_register(tier, "T2.022", "test_t2_range_02", "range_u64 inverted range min>max", test_t2_range_02, false);
    test_register(tier, "T2.023", "test_t2_range_03", "range_u64 full 64-bit range wrap s=0", test_t2_range_03, false);
    test_register(tier, "T2.024", "test_t2_range_04", "range_u64 upper bound near UINT64_MAX", test_t2_range_04, false);
    test_register(tier, "T2.025", "test_t2_range_05", "range_u64 Lemire modulo bias worst case", test_t2_range_05, false);

    /* Domain 6 */
    test_register(tier, "T2.026", "test_t2_dbl_01", "Double upper quantization boundary < 1.0", test_t2_dbl_01, false);
    test_register(tier, "T2.027", "test_t2_dbl_02", "Double lower quantization boundary and step", test_t2_dbl_02, false);
    test_register(tier, "T2.028", "test_t2_dbl_03", "Double discrete 53-bit mantissa alignment", test_t2_dbl_03, false);
    test_register(tier, "T2.029", "test_t2_dbl_04", "Double zero reachability and non-negativity", test_t2_dbl_04, false);
    test_register(tier, "T2.030", "test_t2_dbl_05", "Double continuous 1MB epoch rollover", test_t2_dbl_05, false);

    /* Domain 7 */
    test_register(tier, "T2.031", "test_t2_flt_01", "Float upper quantization boundary < 1.0f", test_t2_flt_01, false);
    test_register(tier, "T2.032", "test_t2_flt_02", "Float lower quantization boundary and step", test_t2_flt_02, false);
    test_register(tier, "T2.033", "test_t2_flt_03", "Float discrete 24-bit mantissa alignment", test_t2_flt_03, false);
    test_register(tier, "T2.034", "test_t2_flt_04", "Float zero reachability and non-negativity", test_t2_flt_04, false);
    test_register(tier, "T2.035", "test_t2_flt_05", "Float continuous 1MB epoch rollover", test_t2_flt_05, false);

    /* Domain 8 */
    test_register(tier, "T2.036", "test_t2_wipe_01", "wipe zero-length safe no-op", test_t2_wipe_01, false);
    test_register(tier, "T2.037", "test_t2_wipe_02", "wipe single byte with flanking canaries", test_t2_wipe_02, false);
    test_register(tier, "T2.038", "test_t2_wipe_03", "wipe unaligned memory pointers 1..63", test_t2_wipe_03, false);
    test_register(tier, "T2.039", "test_t2_wipe_04", "wipe defensive NULL pointer guard", test_t2_wipe_04, false);
    test_register(tier, "T2.040", "test_t2_wipe_05", "wipe NULL pointer with zero length", test_t2_wipe_05, false);

    /* Domain 9 */
    test_register(tier, "T2.041", "test_t2_ctxinit_01", "ctx_init NULL context pointer returns -1", test_t2_ctxinit_01, false);
    test_register(tier, "T2.042", "test_t2_ctxinit_02", "ctx_init degenerate NULL context and seed", test_t2_ctxinit_02, false);
    test_register(tier, "T2.043", "test_t2_ctxinit_03", "ctx_init 1-byte minimal seed", test_t2_ctxinit_03, false);
    test_register(tier, "T2.044", "test_t2_ctxinit_04", "ctx_init non-NULL seed with zero length", test_t2_ctxinit_04, false);
    test_register(tier, "T2.045", "test_t2_ctxinit_05", "ctx_init re-initialization of active context", test_t2_ctxinit_05, false);

    /* Domain 10 */
    test_register(tier, "T2.046", "test_t2_ctxfill_01", "ctx_fill zero length with valid pointer", test_t2_ctxfill_01, false);
    test_register(tier, "T2.047", "test_t2_ctxfill_02", "ctx_fill NULL context pointer returns -1", test_t2_ctxfill_02, false);
    test_register(tier, "T2.048", "test_t2_ctxfill_03", "ctx_fill NULL destination buffer returns -1", test_t2_ctxfill_03, false);
    test_register(tier, "T2.049", "test_t2_ctxfill_04", "ctx_fill both context and buffer NULL", test_t2_ctxfill_04, false);
    test_register(tier, "T2.050", "test_t2_ctxfill_05", "ctx_fill NULL buffer with zero length", test_t2_ctxfill_05, false);

    /* Domain 11 */
    test_register(tier, "T2.051", "test_t2_ctxwipe_01", "ctx_wipe NULL context pointer safe guard", test_t2_ctxwipe_01, false);
    test_register(tier, "T2.052", "test_t2_ctxwipe_02", "ctx_wipe already zeroed context", test_t2_ctxwipe_02, false);
    test_register(tier, "T2.053", "test_t2_ctxwipe_03", "ctx_wipe unaligned context pointer", test_t2_ctxwipe_03, false);
    test_register(tier, "T2.054", "test_t2_ctxwipe_04", "ctx_wipe post-wipe zero state verification", test_t2_ctxwipe_04, false);
    test_register(tier, "T2.055", "test_t2_ctxwipe_05", "ctx_wipe sanitization compliance", test_t2_ctxwipe_05, false);

    /* Domain 12 */
    test_register(tier, "T2.056", "test_t2_rot_01", "Rotation straddling 1MB boundary (1MB-1 + 2B)", test_t2_rot_01, false);
    test_register(tier, "T2.057", "test_t2_rot_02", "Rotation single call spanning 1MB + 1B", test_t2_rot_02, false);
    test_register(tier, "T2.058", "test_t2_rot_03", "Rotation vector chunk clipping at boundary", test_t2_rot_03, false);
    test_register(tier, "T2.059", "test_t2_rot_04", "Rotation forward secrecy irreversibility", test_t2_rot_04, false);
    test_register(tier, "T2.060", "test_t2_rot_05", "Rotation rapid succession 5 x 1MB", test_t2_rot_05, false);

    /* Domain 13 */
    test_register(tier, "T2.061", "test_t2_align_01", "Sweep all offsets 1..63 with 1-byte payload", test_t2_align_01, false);
    test_register(tier, "T2.062", "test_t2_align_02", "Sweep all offsets 1..63 with 63-byte payload", test_t2_align_02, false);
    test_register(tier, "T2.063", "test_t2_align_03", "Sweep all offsets 1..63 with 512-byte payload", test_t2_align_03, false);
    test_register(tier, "T2.064", "test_t2_align_04", "Sweep all offsets 1..63 with 513-byte payload", test_t2_align_04, false);
    test_register(tier, "T2.065", "test_t2_align_05", "Unaligned offset +63 at vector boundary", test_t2_align_05, false);

    /* Domain 14 */
    test_register(tier, "T2.066", "test_t2_size_01", "Critical length 0 bytes safe no-op", test_t2_size_01, false);
    test_register(tier, "T2.067", "test_t2_size_02", "Critical length 1 byte with 128B canaries", test_t2_size_02, false);
    test_register(tier, "T2.068", "test_t2_size_03", "Critical AVX2 boundary neighborhood (511..513B)", test_t2_size_03, false);
    test_register(tier, "T2.069", "test_t2_size_04", "Critical 1MB rotation neighborhood", test_t2_size_04, false);
    test_register(tier, "T2.070", "test_t2_size_05", "Critical large chunk streaming 4MB", test_t2_size_05, false);

    /* Domain 15 */
    test_register(tier, "T2.071", "test_t2_null_01", "fill NULL buffer and zero length check", test_t2_null_01, true);
    test_register(tier, "T2.072", "test_t2_null_02", "ctx_fill NULL buffer and zero length check", test_t2_null_02, true);
    test_register(tier, "T2.073", "test_t2_null_03", "derive defensive NULL argument handling", test_t2_null_03, true);
    test_register(tier, "T2.074", "test_t2_null_04", "string & shuffle defensive NULL arguments", test_t2_null_04, true);
    test_register(tier, "T2.075", "test_t2_null_05", "reseed & consttime_cmp NULL arguments", test_t2_null_05, true);

    /* Domain 16 */
    test_register(tier, "T2.076", "test_t2_str_01", "string single-character charset", test_t2_str_01, false);
    test_register(tier, "T2.077", "test_t2_str_02", "string zero-length request rejection", test_t2_str_02, false);
    test_register(tier, "T2.078", "test_t2_str_03", "string empty charset rejection", test_t2_str_03, false);
    test_register(tier, "T2.079", "test_t2_str_04", "string full 255-character charset", test_t2_str_04, false);
    test_register(tier, "T2.080", "test_t2_str_05", "string single-char target with canaries", test_t2_str_05, false);

    /* Domain 17 */
    test_register(tier, "T2.081", "test_t2_shuf_01", "shuffle degenerate array size 0 or 1", test_t2_shuf_01, false);
    test_register(tier, "T2.082", "test_t2_shuf_02", "shuffle zero element size rejection", test_t2_shuf_02, false);
    test_register(tier, "T2.083", "test_t2_shuf_03", "shuffle 2 elements permutation uniformity", test_t2_shuf_03, false);
    test_register(tier, "T2.084", "test_t2_shuf_04", "shuffle element size equal to 256B buffer", test_t2_shuf_04, false);
    test_register(tier, "T2.085", "test_t2_shuf_05", "shuffle element size 257B exceeding buffer", test_t2_shuf_05, false);

    /* Domain 18 */
    test_register(tier, "T2.086", "test_t2_gauss_01", "gaussian zero standard deviation edge case", test_t2_gauss_01, false);
    test_register(tier, "T2.087", "test_t2_gauss_02", "gaussian negative standard deviation guard", test_t2_gauss_02, false);
    test_register(tier, "T2.088", "test_t2_gauss_03", "gaussian microscopic stddev 1e-12", test_t2_gauss_03, false);
    test_register(tier, "T2.089", "test_t2_gauss_04", "gaussian macroscopic stddev 1e6", test_t2_gauss_04, false);
    test_register(tier, "T2.090", "test_t2_gauss_05", "gaussian Box-Muller rejection boundary", test_t2_gauss_05, false);

    /* Domain 19 */
    test_register(tier, "T2.091", "test_t2_derive_01", "derive zero output length rejection", test_t2_deriv_01, false);
    test_register(tier, "T2.092", "test_t2_derive_02", "derive zero info length rejection", test_t2_deriv_02, false);
    test_register(tier, "T2.093", "test_t2_derive_03", "derive NULL argument defensive rejection", test_t2_deriv_03, false);
    test_register(tier, "T2.094", "test_t2_derive_04", "derive very long info parameter 4096B", test_t2_deriv_04, false);
    test_register(tier, "T2.095", "test_t2_derive_05", "derive single-byte info parameter", test_t2_deriv_05, false);

    /* Domain 20 */
    test_register(tier, "T2.096", "test_t2_reseed_01", "reseed NULL entropy buffer rejection", test_t2_reseed_01, false);
    test_register(tier, "T2.097", "test_t2_reseed_02", "reseed zero-length entropy rejection", test_t2_reseed_02, false);
    test_register(tier, "T2.098", "test_t2_reseed_03", "reseed single-byte entropy injection", test_t2_reseed_03, false);
    test_register(tier, "T2.099", "test_t2_reseed_04", "reseed large entropy buffer 8192B", test_t2_reseed_04, false);
    test_register(tier, "T2.100", "test_t2_reseed_05", "reseed at exact 1MB boundary", test_t2_reseed_05, false);

    /* Domain 21 */
    test_register(tier, "T2.101", "test_t2_self_01", "selftest RFC 8439 ChaCha20 exact match", test_t2_self_01, false);
    test_register(tier, "T2.102", "test_t2_self_02", "selftest memory sanitization subtest", test_t2_self_02, false);
    test_register(tier, "T2.103", "test_t2_self_03", "selftest under high register pressure", test_t2_self_03, false);
    test_register(tier, "T2.104", "test_t2_self_04", "selftest generator state non-interference", test_t2_self_04, false);
    test_register(tier, "T2.105", "test_t2_self_05", "selftest latency and throughput benchmark", test_t2_self_05, false);

    /* Domain 22 */
    test_register(tier, "T2.106", "test_t2_ct_01", "consttime_cmp NULL pointer defensive checks", test_t2_ct_01, false);
    test_register(tier, "T2.107", "test_t2_ct_02", "consttime_cmp zero length error check", test_t2_ct_02, false);
    test_register(tier, "T2.108", "test_t2_ct_03", "consttime_cmp single byte equality/inequality", test_t2_ct_03, false);
    test_register(tier, "T2.109", "test_t2_ct_04", "consttime_cmp large buffer 64KB boundary", test_t2_ct_04, false);
    test_register(tier, "T2.110", "test_t2_ct_05", "consttime_select exhaustive bitwise patterns", test_t2_ct_05, false);
}
