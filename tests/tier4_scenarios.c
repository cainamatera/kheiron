#include "e2e_common.h"

/* ========================================================================= */
/* Scenario 1: Monte Carlo Pi Estimation & Circle Sampling                   */
/* ========================================================================= */

static test_status_t test_t4_01_monte_carlo_pi(void) {
    const uint64_t N = 3000000; /* 3 million samples */
    uint64_t inside_double = 0;
    uint64_t inside_float = 0;

    for (uint64_t i = 0; i < N; i++) {
        double x = 2.0 * kheiron_uniform_double() - 1.0;
        double y = 2.0 * kheiron_uniform_double() - 1.0;
        if (x * x + y * y <= 1.0) inside_double++;
    }
    double pi_double = 4.0 * (double)inside_double / (double)N;
    double err_double = fabs(pi_double - 3.14159265358979323846);
    ASSERT_TRUE(err_double < 0.0040);

    for (uint64_t i = 0; i < N; i++) {
        float x = 2.0f * kheiron_uniform_float() - 1.0f;
        float y = 2.0f * kheiron_uniform_float() - 1.0f;
        if (x * x + y * y <= 1.0f) inside_float++;
    }
    double pi_float = 4.0 * (double)inside_float / (double)N;
    double err_float = fabs(pi_float - 3.14159265358979323846);
    ASSERT_TRUE(err_float < 0.0050);

    /* Equal-Area Annular Ring Chi-Squared Test */
    const uint64_t N_DISK = 500000;
    uint64_t ring_counts[10] = {0};
    for (uint64_t i = 0; i < N_DISK; i++) {
        double u1 = kheiron_uniform_double();
        double r = sqrt(u1);
        int ring = (int)(r * r * 10.0);
        if (ring >= 10) ring = 9;
        ring_counts[ring]++;
    }
    double expected_ring = (double)N_DISK / 10.0;
    double chi2 = 0.0;
    for (int k = 0; k < 10; k++) {
        double d = (double)ring_counts[k] - expected_ring;
        chi2 += (d * d) / expected_ring;
    }
    ASSERT_TRUE(chi2 < 30.0); /* df=9, p=0.001 */
    return TEST_PASS;
}

/* ========================================================================= */
/* Scenario 2: High-Throughput TLS 1.3 Session Cryptographic Engine          */
/* ========================================================================= */

typedef struct {
    uint64_t hi;
    uint64_t lo;
} id128_t;

static int compare_id128(const void *a, const void *b) {
    const id128_t *ia = (const id128_t *)a;
    const id128_t *ib = (const id128_t *)b;
    if (ia->hi != ib->hi) return ia->hi < ib->hi ? -1 : 1;
    if (ia->lo != ib->lo) return ia->lo < ib->lo ? -1 : 1;
    return 0;
}

static test_status_t test_t4_02_tls_crypto_engine(void) {
    const size_t NUM_SESSIONS = 5000;
    id128_t *key_ids = (id128_t *)malloc(NUM_SESSIONS * sizeof(id128_t));
    id128_t *nonce_ids = (id128_t *)malloc(NUM_SESSIONS * sizeof(id128_t));
    ASSERT_NOT_NULL(key_ids);
    ASSERT_NOT_NULL(nonce_ids);

    for (size_t s = 0; s < NUM_SESSIONS; s++) {
        uint8_t client_random[32];
        uint8_t server_random[32];
        uint8_t session_ticket[16];
        uint8_t gcm_nonce[12];
        uint8_t client_secret[32];
        uint8_t server_secret[32];
        uint8_t exporter_secret[32];

        ASSERT_EQ_INT(kheiron_fill(client_random, 32), 0);
        ASSERT_EQ_INT(kheiron_fill(server_random, 32), 0);
        ASSERT_EQ_INT(kheiron_fill(session_ticket, 16), 0);
        ASSERT_EQ_INT(kheiron_fill(gcm_nonce, 12), 0);

        kheiron_ctx_t session_ctx;
        ASSERT_EQ_INT(kheiron_ctx_init(&session_ctx, client_random, 32), 0);
        ASSERT_EQ_INT(kheiron_derive(&session_ctx, client_secret, 32, "c ap traffic", 12), 0);
        ASSERT_EQ_INT(kheiron_derive(&session_ctx, server_secret, 32, "s ap traffic", 12), 0);
        ASSERT_EQ_INT(kheiron_derive(&session_ctx, exporter_secret, 32, "exp master", 10), 0);

        ASSERT_MEM_NEQ(client_secret, server_secret, 32);
        ASSERT_MEM_NEQ(client_secret, exporter_secret, 32);

        memcpy(&key_ids[s].hi, client_secret, 8);
        memcpy(&key_ids[s].lo, client_secret + 8, 8);
        memset(&nonce_ids[s], 0, sizeof(id128_t));
        memcpy(&nonce_ids[s], gcm_nonce, 12);

        kheiron_ctx_wipe(&session_ctx);
        kheiron_secure_wipe(client_random, 32);
        kheiron_secure_wipe(server_random, 32);
        kheiron_secure_wipe(session_ticket, 16);
        kheiron_secure_wipe(client_secret, 32);
        kheiron_secure_wipe(server_secret, 32);
        kheiron_secure_wipe(exporter_secret, 32);
    }

    qsort(key_ids, NUM_SESSIONS, sizeof(id128_t), compare_id128);
    qsort(nonce_ids, NUM_SESSIONS, sizeof(id128_t), compare_id128);

    for (size_t i = 1; i < NUM_SESSIONS; i++) {
        ASSERT_TRUE(compare_id128(&key_ids[i-1], &key_ids[i]) != 0);
        ASSERT_TRUE(compare_id128(&nonce_ids[i-1], &nonce_ids[i]) != 0);
    }

    free(key_ids);
    free(nonce_ids);
    return TEST_PASS;
}

/* ========================================================================= */
/* Scenario 3: Fisher-Yates Array Shuffling Over Large Datasets              */
/* ========================================================================= */

static test_status_t test_t4_03_fisher_yates_scale(void) {
    const size_t N = 100000;
    uint64_t *arr = (uint64_t *)malloc(N * sizeof(uint64_t));
    ASSERT_NOT_NULL(arr);
    for (size_t i = 0; i < N; i++) arr[i] = i;

    ASSERT_EQ_INT(kheiron_shuffle(arr, N, sizeof(uint64_t)), 0);

    uint8_t *bitset = (uint8_t *)calloc((N + 7) / 8, 1);
    ASSERT_NOT_NULL(bitset);
    double displacement_sum = 0.0;

    for (size_t i = 0; i < N; i++) {
        uint64_t val = arr[i];
        ASSERT_TRUE(val < N);
        size_t byte_idx = val / 8;
        uint8_t mask = (uint8_t)(1 << (val % 8));
        ASSERT_TRUE((bitset[byte_idx] & mask) == 0); /* Zero duplicates */
        bitset[byte_idx] |= mask;
        displacement_sum += fabs((double)val - (double)i);
    }
    free(bitset);

    double avg_displacement = displacement_sum / (double)N;
    ASSERT_TRUE(avg_displacement > 31000.0 && avg_displacement < 35666.0);
    free(arr);

    /* 4-Card Permutation Chi-Squared Uniformity (M = 120,000 trials) */
    const uint64_t M = 120000;
    uint64_t perm_counts[24] = {0};
    uint8_t deck[4];

    for (uint64_t trial = 0; trial < M; trial++) {
        deck[0] = 0; deck[1] = 1; deck[2] = 2; deck[3] = 3;
        ASSERT_EQ_INT(kheiron_shuffle(deck, 4, 1), 0);

        int idx = 0;
        int factor = 6;
        for (int i = 0; i < 3; i++) {
            int count = 0;
            for (int j = i + 1; j < 4; j++) {
                if (deck[j] < deck[i]) count++;
            }
            idx += count * factor;
            factor /= (3 - i);
        }
        ASSERT_TRUE(idx >= 0 && idx < 24);
        perm_counts[idx]++;
    }

    double expected = (double)M / 24.0;
    double chi2 = 0.0;
    for (int i = 0; i < 24; i++) {
        double diff = (double)perm_counts[i] - expected;
        chi2 += (diff * diff) / expected;
    }
    ASSERT_TRUE(chi2 < 55.0); /* df=23, p=0.0002 */
    return TEST_PASS;
}

/* ========================================================================= */
/* Scenario 4: Streaming One-Time Pad Buffer Generation                      */
/* ========================================================================= */

static test_status_t test_t4_04_streaming_pad(void) {
    size_t total_bytes = 20ULL * 1024ULL * 1024ULL; /* 20 MB */
    const size_t CHUNK_SIZE = 65536;
    uint8_t *chunk = (uint8_t *)malloc(CHUNK_SIZE);
    ASSERT_NOT_NULL(chunk);

    uint64_t byte_counts[256] = {0};
    size_t generated = 0;

    double sum_x = 0.0, sum_y = 0.0, sum_xx = 0.0, sum_yy = 0.0, sum_xy = 0.0;
    uint64_t pair_count = 0;
    uint32_t prev_word = 0;
    int has_prev = 0;

    while (generated < total_bytes) {
        size_t cur = (total_bytes - generated) < CHUNK_SIZE ? (total_bytes - generated) : CHUNK_SIZE;
        ASSERT_EQ_INT(kheiron_fill(chunk, cur), 0);

        for (size_t i = 0; i < cur; i++) {
            byte_counts[chunk[i]]++;
        }

        size_t words = cur / sizeof(uint32_t);
        const uint32_t *pw = (const uint32_t *)(const void *)chunk;
        for (size_t w = 0; w < words; w++) {
            uint32_t cur_word = pw[w];
            if (has_prev) {
                double x = (double)prev_word;
                double y = (double)cur_word;
                sum_x += x; sum_y += y;
                sum_xx += x * x; sum_yy += y * y;
                sum_xy += x * y;
                pair_count++;
            }
            prev_word = cur_word;
            has_prev = 1;
        }

        generated += cur;
    }
    free(chunk);

    /* 1. Shannon Entropy */
    double entropy = 0.0;
    double total_d = (double)total_bytes;
    for (int i = 0; i < 256; i++) {
        if (byte_counts[i] > 0) {
            double p = (double)byte_counts[i] / total_d;
            entropy -= p * (log(p) / log(2.0));
        }
    }
    ASSERT_TRUE(entropy >= 7.9990);

    /* 2. Chi-Squared Test on Byte Frequencies */
    double expected = total_d / 256.0;
    double chi2 = 0.0;
    for (int i = 0; i < 256; i++) {
        double diff = (double)byte_counts[i] - expected;
        chi2 += (diff * diff) / expected;
    }
    ASSERT_TRUE(chi2 >= 160.0 && chi2 <= 360.0);

    /* 3. Serial Autocorrelation */
    if (pair_count > 0) {
        double num = (double)pair_count * sum_xy - sum_x * sum_y;
        double den = sqrt(((double)pair_count * sum_xx - sum_x * sum_x) *
                          ((double)pair_count * sum_yy - sum_y * sum_y));
        double r = (den > 0.0) ? (num / den) : 0.0;
        ASSERT_TRUE(fabs(r) < 0.005);
    }
    return TEST_PASS;
}

/* ========================================================================= */
/* Scenario 5: Multi-Threaded Worker Pool with Heterogeneous Random Data Types*/
/* ========================================================================= */

#define POOL_THREADS 16
#define POOL_TASKS_PER_THREAD 5000

typedef struct {
    int thread_id;
    uint64_t hash;
    int success;
} pool_worker_t;

static void *worker_pool_task(void *arg) {
    pool_worker_t *w = (pool_worker_t *)arg;
    w->success = 0;
    uint64_t h = 14695981039346656037ULL;

    for (int i = 0; i < POOL_TASKS_PER_THREAD; i++) {
        int task = i % 10;
        switch (task) {
            case 0: {
                uint32_t val = kheiron_u32();
                h = (h ^ val) * 1099511628211ULL;
                break;
            }
            case 1: {
                uint64_t val = kheiron_u64();
                h = (h ^ val) * 1099511628211ULL;
                break;
            }
            case 2: {
                uint64_t val = kheiron_range_u64(10, 1000);
                if (val < 10 || val > 1000) return NULL;
                h = (h ^ val) * 1099511628211ULL;
                break;
            }
            case 3: {
                double d = kheiron_uniform_double();
                if (d < 0.0 || d >= 1.0) return NULL;
                uint64_t raw; memcpy(&raw, &d, 8);
                h = (h ^ raw) * 1099511628211ULL;
                break;
            }
            case 4: {
                float f = kheiron_uniform_float();
                if (f < 0.0f || f >= 1.0f) return NULL;
                uint32_t raw; memcpy(&raw, &f, 4);
                h = (h ^ raw) * 1099511628211ULL;
                break;
            }
            case 5: {
                uint8_t buf[257];
                if (kheiron_fill(buf, sizeof(buf)) != 0) return NULL;
                for (size_t b = 0; b < sizeof(buf); b++) h = (h ^ buf[b]) * 1099511628211ULL;
                break;
            }
            case 6: {
                kheiron_ctx_t ctx;
                uint8_t seed[32];
                if (kheiron_fill(seed, 32) != 0) return NULL;
                if (kheiron_ctx_init(&ctx, seed, 32) != 0) return NULL;
                uint8_t cbuf[512];
                if (kheiron_ctx_fill(&ctx, cbuf, 512) != 0) return NULL;
                kheiron_ctx_wipe(&ctx);
                for (size_t b = 0; b < 512; b++) h = (h ^ cbuf[b]) * 1099511628211ULL;
                break;
            }
            case 7: {
                char str[33];
                if (kheiron_string(str, 32, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789") != 0) return NULL;
                if (str[32] != '\0') return NULL;
                for (size_t b = 0; b < 32; b++) h = (h ^ (uint8_t)str[b]) * 1099511628211ULL;
                break;
            }
            case 8: {
                uint8_t deck[52];
                for (int d = 0; d < 52; d++) deck[d] = d;
                if (kheiron_shuffle(deck, 52, 1) != 0) return NULL;
                for (int d = 0; d < 52; d++) h = (h ^ deck[d]) * 1099511628211ULL;
                break;
            }
            case 9: {
                double g = kheiron_gaussian(0.0, 1.0);
                uint64_t raw; memcpy(&raw, &g, 8);
                h = (h ^ raw) * 1099511628211ULL;
                break;
            }
        }
    }
    w->hash = h;
    w->success = 1;
    return NULL;
}

static test_status_t test_t4_05_worker_pool(void) {
    pthread_t th[POOL_THREADS];
    pool_worker_t workers[POOL_THREADS];

    for (int i = 0; i < POOL_THREADS; i++) {
        workers[i].thread_id = i;
        workers[i].hash = 0;
        workers[i].success = 0;
        pthread_create(&th[i], NULL, worker_pool_task, &workers[i]);
    }

    for (int i = 0; i < POOL_THREADS; i++) {
        pthread_join(th[i], NULL);
        ASSERT_EQ_INT(workers[i].success, 1);
    }

    for (int i = 0; i < POOL_THREADS; i++) {
        for (int j = i + 1; j < POOL_THREADS; j++) {
            ASSERT_TRUE(workers[i].hash != workers[j].hash);
        }
    }
    return TEST_PASS;
}

/* ========================================================================= */
/* Registration of Tier 4 Real-World Application Scenario Tests (5 Tests)    */
/* ========================================================================= */

void register_tier4_tests(void) {
    const char *tier = "Tier 4: Real-World Application Scenarios";

    test_register(tier, "T4.01", "test_t4_01_monte_carlo_pi",
                  "Monte Carlo Pi Estimation & Circle Sampling",
                  test_t4_01_monte_carlo_pi, false);

    test_register(tier, "T4.02", "test_t4_02_tls_crypto_engine",
                  "High-Throughput TLS 1.3 Session Cryptographic Engine",
                  test_t4_02_tls_crypto_engine, false);

    test_register(tier, "T4.03", "test_t4_03_fisher_yates_scale",
                  "Fisher-Yates Array Shuffling Over Large Datasets",
                  test_t4_03_fisher_yates_scale, false);

    test_register(tier, "T4.04", "test_t4_04_streaming_pad",
                  "Streaming One-Time Pad Buffer Generation",
                  test_t4_04_streaming_pad, false);

    test_register(tier, "T4.05", "test_t4_05_worker_pool",
                  "Multi-Threaded Worker Pool with Heterogeneous Random Data Types",
                  test_t4_05_worker_pool, false);
}
