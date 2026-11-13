#include "e2e_common.h"

static test_status_t test_t3_01_rotation_lemirerange(void) {
    const uint64_t ranges[] = {
        2, 3, 5, 7, 10, 100, 255, 1000, 65535, 1000000ULL,
        0xFFFFFFFFULL, 0x100000000ULL, 0xFFFFFFFFFFFFFFFEULL
    };
    const size_t num_ranges = sizeof(ranges) / sizeof(ranges[0]);

    for (int k = 1; k <= 8; k++) {
        ASSERT_EQ_INT(kheiron_init(), 0);
        size_t target = 1048576 - k;
        uint8_t *dummy = (uint8_t *)malloc(target);
        ASSERT_NOT_NULL(dummy);
        ASSERT_EQ_INT(kheiron_fill(dummy, target), 0);
        free(dummy);

        for (size_t r = 0; r < num_ranges; r++) {
            uint64_t max_val = ranges[r];
            for (int iter = 0; iter < 200; iter++) {
                uint64_t val = kheiron_range_u64(0, max_val);
                ASSERT_TRUE(val <= max_val);
            }
        }
    }

    uint64_t bins[10] = {0};
    const uint64_t total_samples = 200000;
    for (uint64_t i = 0; i < total_samples; i++) {
        uint64_t val = kheiron_range_u64(0, 9);
        ASSERT_TRUE(val < 10);
        bins[val]++;
    }
    double expected = (double)total_samples / 10.0;
    double chi2 = 0.0;
    for (int i = 0; i < 10; i++) {
        double diff = (double)bins[i] - expected;
        chi2 += (diff * diff) / expected;
    }
    ASSERT_TRUE(chi2 < 30.0);
    return TEST_PASS;
}

typedef struct {
    int thread_id;
    size_t iterations;
    int success;
} thread_t3_arg_t;

static void *worker_t3_mixed(void *arg) {
    thread_t3_arg_t *targ = (thread_t3_arg_t *)arg;
    targ->success = 0;
    uint8_t buf[2048];
    uint8_t seed[32];

    for (size_t i = 0; i < sizeof(seed); i++) {
        seed[i] = (uint8_t)(targ->thread_id * 17 + i);
    }

    kheiron_ctx_t ctx;
    if (kheiron_ctx_init(&ctx, seed, sizeof(seed)) != 0) return NULL;

    for (size_t iter = 0; iter < targ->iterations; iter++) {
        if (targ->thread_id % 2 == 0) {
            if (kheiron_fill(buf, sizeof(buf)) != 0) return NULL;
            if (kheiron_ctx_fill(&ctx, buf, sizeof(buf)) != 0) return NULL;
        } else {
            if (kheiron_ctx_fill(&ctx, buf, sizeof(buf)) != 0) return NULL;
            if (kheiron_fill(buf, sizeof(buf)) != 0) return NULL;
        }
    }
    kheiron_ctx_wipe(&ctx);
    targ->success = 1;
    return NULL;
}

static test_status_t test_t3_02_multithread_ctx_global(void) {
    const int NUM_THREADS = 8;
    pthread_t th[NUM_THREADS];
    thread_t3_arg_t args[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].thread_id = i;
        args[i].iterations = 1000;
        args[i].success = 0;
        pthread_create(&th[i], NULL, worker_t3_mixed, &args[i]);
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(th[i], NULL);
        ASSERT_EQ_INT(args[i].success, 1);
    }
    return TEST_PASS;
}

static test_status_t test_t3_03_unaligned_epoch_rollover(void) {
    const size_t CANARY_LEN = 128;
    const size_t offsets[] = {1, 3, 7, 15, 17, 31, 32, 33, 47, 63};
    const size_t num_offsets = sizeof(offsets) / sizeof(offsets[0]);
    const size_t remaining_in_epoch[] = {1, 15, 63, 64, 65, 511, 512, 513, 1023};
    const size_t num_rem = sizeof(remaining_in_epoch) / sizeof(remaining_in_epoch[0]);

    for (size_t o = 0; o < num_offsets; o++) {
        size_t offset = offsets[o];
        for (size_t r = 0; r < num_rem; r++) {
            size_t rem = remaining_in_epoch[r];

            kheiron_ctx_t ctx;
            ASSERT_EQ_INT(kheiron_ctx_init(&ctx, "seed_unaligned", 14), 0);

            size_t drain_len = 1048576 - rem;
            uint8_t *drain = (uint8_t *)malloc(drain_len);
            ASSERT_NOT_NULL(drain);
            ASSERT_EQ_INT(kheiron_ctx_fill(&ctx, drain, drain_len), 0);
            free(drain);

            size_t payload_len = rem + 1024 + 17;
            size_t total_alloc = CANARY_LEN + offset + payload_len + CANARY_LEN + 64;
            uint8_t *raw = (uint8_t *)malloc(total_alloc);
            ASSERT_NOT_NULL(raw);

            uint8_t *base = (uint8_t *)(((uintptr_t)raw + 63) & ~(uintptr_t)63);
            uint8_t *pre_canary = base;
            uint8_t *payload = base + CANARY_LEN + offset;
            uint8_t *post_canary = payload + payload_len;

            memset(pre_canary, 0x5A, CANARY_LEN);
            memset(base + CANARY_LEN, 0x77, offset);
            memset(payload, 0x00, payload_len);
            memset(post_canary, 0xA5, CANARY_LEN);

            int fill_ret = kheiron_ctx_fill(&ctx, payload, payload_len);
            ASSERT_EQ_INT(fill_ret, 0);

            for (size_t i = 0; i < CANARY_LEN; i++) {
                ASSERT_EQ_INT(pre_canary[i], 0x5A);
                ASSERT_EQ_INT(post_canary[i], 0xA5);
            }
            for (size_t i = 0; i < offset; i++) {
                ASSERT_EQ_INT((base + CANARY_LEN)[i], 0x77);
            }

            int non_zero = 0;
            for (size_t i = 0; i < payload_len; i++) {
                if (payload[i] != 0) { non_zero = 1; break; }
            }
            ASSERT_TRUE(non_zero == 1);

            kheiron_ctx_wipe(&ctx);
            free(raw);
        }
    }
    return TEST_PASS;
}

static test_status_t test_t3_04_fork_active_contexts(void) {
    ASSERT_EQ_INT(kheiron_init(), 0);
    kheiron_ctx_t stack_ctx;
    kheiron_ctx_t *heap_ctx = (kheiron_ctx_t *)malloc(sizeof(kheiron_ctx_t));
    ASSERT_NOT_NULL(heap_ctx);

    ASSERT_EQ_INT(kheiron_ctx_init(&stack_ctx, "seed_stack", 10), 0);
    ASSERT_EQ_INT(kheiron_ctx_init(heap_ctx, "seed_heap", 9), 0);

    uint8_t pre_parent[64];
    ASSERT_EQ_INT(kheiron_fill(pre_parent, sizeof(pre_parent)), 0);

    int pipe_fd[2];
    ASSERT_EQ_INT(pipe(pipe_fd), 0);

    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    ASSERT_TRUE(pid >= 0);

    if (pid == 0) {
        close(pipe_fd[0]);
        uint8_t child_global[1024];
        uint8_t child_stack[1024];
        uint8_t child_heap[1024];

        if (kheiron_fill(child_global, sizeof(child_global)) != 0) _exit(1);
        if (kheiron_ctx_fill(&stack_ctx, child_stack, sizeof(child_stack)) != 0) _exit(2);
        if (kheiron_ctx_fill(heap_ctx, child_heap, sizeof(child_heap)) != 0) _exit(3);

        size_t big_len = 1048576 + 512;
        uint8_t *big_buf = (uint8_t *)malloc(big_len);
        if (!big_buf || kheiron_fill(big_buf, big_len) != 0) _exit(4);
        free(big_buf);

        ssize_t w = write(pipe_fd[1], child_global, sizeof(child_global));
        (void)w;
        close(pipe_fd[1]);

        kheiron_ctx_wipe(&stack_ctx);
        kheiron_ctx_wipe(heap_ctx);
        free(heap_ctx);
        _exit(0);
    } else {
        close(pipe_fd[1]);
        uint8_t parent_global[1024];
        uint8_t child_global_rx[1024];

        ASSERT_EQ_INT(kheiron_fill(parent_global, sizeof(parent_global)), 0);
        ssize_t r = read(pipe_fd[0], child_global_rx, sizeof(child_global_rx));
        ASSERT_EQ_INT((int)r, (int)sizeof(child_global_rx));
        close(pipe_fd[0]);

        int status = 0;
        waitpid(pid, &status, 0);
        ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);

        ASSERT_MEM_NEQ(parent_global, child_global_rx, sizeof(parent_global));

        kheiron_ctx_wipe(&stack_ctx);
        kheiron_ctx_wipe(heap_ctx);
        free(heap_ctx);
    }
    return TEST_PASS;
}

static test_status_t test_t3_05_secure_wipe_reseed_lifecycle(void) {
    uint8_t user_entropy[64];
    for (size_t i = 0; i < sizeof(user_entropy); i++) {
        user_entropy[i] = (uint8_t)(0x11 * (i + 1));
    }

    uint8_t stream_before[64];
    ASSERT_EQ_INT(kheiron_fill(stream_before, sizeof(stream_before)), 0);

    ASSERT_EQ_INT(kheiron_reseed(user_entropy, sizeof(user_entropy)), 0);
    kheiron_secure_wipe(user_entropy, sizeof(user_entropy));

    for (size_t i = 0; i < sizeof(user_entropy); i++) {
        ASSERT_EQ_INT(user_entropy[i], 0);
    }

    uint8_t stream_after[64];
    ASSERT_EQ_INT(kheiron_fill(stream_after, sizeof(stream_after)), 0);
    ASSERT_MEM_NEQ(stream_before, stream_after, sizeof(stream_before));

    for (int cycle = 0; cycle < 50; cycle++) {
        uint8_t entropy[32];
        memset(entropy, 0xBE, sizeof(entropy));
        ASSERT_EQ_INT(kheiron_reseed(entropy, sizeof(entropy)), 0);
        kheiron_secure_wipe(entropy, sizeof(entropy));
        for (size_t i = 0; i < sizeof(entropy); i++) {
            ASSERT_EQ_INT(entropy[i], 0);
        }
    }
    return TEST_PASS;
}

static test_status_t test_t3_06_derive_epoch_isolation(void) {
    uint8_t seed[32] = "deterministic_derive_seed_test_";
    kheiron_ctx_t c1, c2;
    ASSERT_EQ_INT(kheiron_ctx_init(&c1, seed, sizeof(seed)), 0);
    memcpy(&c2, &c1, sizeof(kheiron_ctx_t));

    size_t pre_len = 1048576 - 1024;
    uint8_t *pre_buf1 = (uint8_t *)malloc(pre_len);
    uint8_t *pre_buf2 = (uint8_t *)malloc(pre_len);
    ASSERT_NOT_NULL(pre_buf1);
    ASSERT_NOT_NULL(pre_buf2);
    ASSERT_EQ_INT(kheiron_ctx_fill(&c1, pre_buf1, pre_len), 0);
    ASSERT_EQ_INT(kheiron_ctx_fill(&c2, pre_buf2, pre_len), 0);
    ASSERT_MEM_EQ(pre_buf1, pre_buf2, pre_len);
    free(pre_buf1);
    free(pre_buf2);

    uint8_t derived_a[64], derived_b[64];
    uint8_t *derived_big = (uint8_t *)malloc(1048576 + 1024);
    ASSERT_NOT_NULL(derived_big);
    ASSERT_EQ_INT(kheiron_derive(&c1, derived_a, sizeof(derived_a), "tls13_c_key", 11), 0);
    ASSERT_EQ_INT(kheiron_derive(&c1, derived_b, sizeof(derived_b), "tls13_s_key", 11), 0);
    ASSERT_EQ_INT(kheiron_derive(&c1, derived_big, 1048576 + 1024, "big_stream", 10), 0);
    free(derived_big);

    ASSERT_MEM_NEQ(derived_a, derived_b, sizeof(derived_a));

    uint8_t post_c1[2048], post_c2[2048];
    ASSERT_EQ_INT(kheiron_ctx_fill(&c1, post_c1, sizeof(post_c1)), 0);
    ASSERT_EQ_INT(kheiron_ctx_fill(&c2, post_c2, sizeof(post_c2)), 0);

    ASSERT_MEM_EQ(post_c1, post_c2, 1024);

    ASSERT_MEM_NEQ(post_c1 + 1024, post_c2 + 1024, 1024);

    kheiron_ctx_wipe(&c1);
    kheiron_ctx_wipe(&c2);
    return TEST_PASS;
}

static test_status_t test_t3_07_float_double_rotation(void) {
    ASSERT_EQ_INT(kheiron_init(), 0);
    size_t drain_sz = 1048576 - 32;
    uint8_t *pad = (uint8_t *)malloc(drain_sz);
    ASSERT_NOT_NULL(pad);
    ASSERT_EQ_INT(kheiron_fill(pad, drain_sz), 0);
    free(pad);

    const int N = 50000;
    double d_sum = 0.0;
    float f_sum = 0.0f;

    for (int i = 0; i < N; i++) {
        double d = kheiron_uniform_double();
        float f = kheiron_uniform_float();

        ASSERT_TRUE(!isnan(d) && !isinf(d));
        ASSERT_DOUBLE_BETWEEN(d, 0.0, 1.0);
        ASSERT_TRUE(!isnan(f) && !isinf(f));
        ASSERT_FLOAT_BETWEEN(f, 0.0f, 1.0f);

        d_sum += d;
        f_sum += f;
    }

    double d_mean = d_sum / (double)N;
    float f_mean = f_sum / (float)N;
    ASSERT_TRUE(fabs(d_mean - 0.5) < 0.015);
    ASSERT_TRUE(fabsf(f_mean - 0.5f) < 0.015f);
    return TEST_PASS;
}

static test_status_t test_t3_08_string_mask_batch_rotation(void) {
    const char *charset62 = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    const size_t STR_LEN = 1200000;

    char *str = (char *)malloc(STR_LEN + 129);
    ASSERT_NOT_NULL(str);
    memset(str + STR_LEN + 1, 0xCC, 128);

    ASSERT_EQ_INT(kheiron_string(str, STR_LEN, charset62), 0);

    ASSERT_EQ_INT(str[STR_LEN], '\0');

    for (size_t i = 0; i < 128; i++) {
        ASSERT_EQ_INT((uint8_t)str[STR_LEN + 1 + i], 0xCC);
    }

    for (size_t i = 0; i < 10000; i++) {
        ASSERT_NOT_NULL(strchr(charset62, str[i]));
    }
    for (size_t i = STR_LEN - 10000; i < STR_LEN; i++) {
        ASSERT_NOT_NULL(strchr(charset62, str[i]));
    }

    free(str);
    return TEST_PASS;
}

static test_status_t test_t3_09_shuffle_multi_epoch(void) {
    const size_t N = 250000;
    uint64_t *arr = (uint64_t *)malloc(N * sizeof(uint64_t));
    ASSERT_NOT_NULL(arr);

    uint64_t sum_init = 0;
    uint64_t xor_init = 0;
    for (size_t i = 0; i < N; i++) {
        arr[i] = (uint64_t)i;
        sum_init += i;
        xor_init ^= i;
    }

    ASSERT_EQ_INT(kheiron_shuffle(arr, N, sizeof(uint64_t)), 0);

    uint64_t sum_shuffled = 0;
    uint64_t xor_shuffled = 0;
    size_t fixed_points = 0;

    for (size_t i = 0; i < N; i++) {
        sum_shuffled += arr[i];
        xor_shuffled ^= arr[i];
        if (arr[i] == i) {
            fixed_points++;
        }
    }

    ASSERT_EQ_U64(sum_shuffled, sum_init);
    ASSERT_EQ_U64(xor_shuffled, xor_init);
    ASSERT_TRUE(fixed_points < 25);

    free(arr);
    return TEST_PASS;
}

static test_status_t test_t3_10_consttime_cmp_wipe(void) {
    uint8_t a[64], b[64], zero[64];
    ASSERT_EQ_INT(kheiron_fill(a, sizeof(a)), 0);
    memcpy(b, a, sizeof(b));
    memset(zero, 0xFF, sizeof(zero));
    kheiron_secure_wipe(zero, sizeof(zero));

    ASSERT_EQ_INT(kheiron_consttime_cmp(a, b, sizeof(a)), 0);
    ASSERT_EQ_INT(kheiron_consttime_cmp(a, zero, sizeof(a)), 1);

    b[0] ^= 0x01;
    ASSERT_EQ_INT(kheiron_consttime_cmp(a, b, sizeof(a)), 1);
    b[0] ^= 0x01;
    b[63] ^= 0x80;
    ASSERT_EQ_INT(kheiron_consttime_cmp(a, b, sizeof(a)), 1);

    uint32_t x = 0xAAAAAAAA;
    uint32_t y = 0x55555555;
    ASSERT_EQ_U64(kheiron_consttime_select_u32(0xFFFFFFFFU, x, y), x);
    ASSERT_EQ_U64(kheiron_consttime_select_u32(0x00000000U, x, y), y);

    ASSERT_EQ_INT(kheiron_consttime_cmp(NULL, b, 64), -1);
    ASSERT_EQ_INT(kheiron_consttime_cmp(a, NULL, 64), -1);
    return TEST_PASS;
}

void register_tier3_tests(void) {
    const char *tier = "Tier 3: Cross-Feature Interactions";

    test_register(tier, "T3.01", "test_t3_01_rotation_lemirerange",
                  "1MB Key Rotation + Lemire Modulo Range Sampling",
                  test_t3_01_rotation_lemirerange, false);

    test_register(tier, "T3.02", "test_t3_02_multithread_ctx_global",
                  "Multi-Threaded Concurrent Context API vs Global State",
                  test_t3_02_multithread_ctx_global, false);

    test_register(tier, "T3.03", "test_t3_03_unaligned_epoch_rollover",
                  "Unaligned Buffer Filling Spanning Key Rotation Epoch",
                  test_t3_03_unaligned_epoch_rollover, false);

    test_register(tier, "T3.04", "test_t3_04_fork_active_contexts",
                  "Fork Safety with Active Initialized Contexts & Post-Fork Rot",
                  test_t3_04_fork_active_contexts, false);

    test_register(tier, "T3.05", "test_t3_05_secure_wipe_reseed_lifecycle",
                  "Volatile Memory Sanitization Pre/Post Reseed Lifecycle",
                  test_t3_05_secure_wipe_reseed_lifecycle, false);

    test_register(tier, "T3.06", "test_t3_06_derive_epoch_isolation",
                  "Ephemeral Context Subkey Derivation Intertwined with Epoch Rot",
                  test_t3_06_derive_epoch_isolation, false);

    test_register(tier, "T3.07", "test_t3_07_float_double_rotation",
                  "IEEE-754 Double & Float Uniformity Crossing 1 MB Rotation",
                  test_t3_07_float_double_rotation, false);

    test_register(tier, "T3.08", "test_t3_08_string_mask_batch_rotation",
                  "Modulo Bias-Free String Generation Crossing 1 MB Rotation",
                  test_t3_08_string_mask_batch_rotation, false);

    test_register(tier, "T3.09", "test_t3_09_shuffle_multi_epoch",
                  "Fisher-Yates Array Shuffle Spanning Multiple Key Rotations",
                  test_t3_09_shuffle_multi_epoch, false);

    test_register(tier, "T3.10", "test_t3_10_consttime_cmp_wipe",
                  "Constant-Time Selection & Comparison Against Sanitized Memory",
                  test_t3_10_consttime_cmp_wipe, false);
}
