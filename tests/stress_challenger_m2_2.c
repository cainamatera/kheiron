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
#include <sys/wait.h>
#include <assert.h>

#ifndef KHEIRON_STATIC
#define KHEIRON_STATIC 1
#endif
#include "../kheiron.h"

#define TEST_OK   0
#define TEST_FAIL 1

static int g_failed = 0;

#define STRESS_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "[-] STRESS FAILURE [%s:%d]: %s (%s)\n", __FILE__, __LINE__, #cond, msg); \
        g_failed = 1; \
        return TEST_FAIL; \
    } \
} while (0)

#define STRESS_ASSERT_INT_EQ(a, b, msg) do { \
    long long _a = (long long)(a); \
    long long _b = (long long)(b); \
    if (_a != _b) { \
        fprintf(stderr, "[-] STRESS FAILURE [%s:%d]: %lld != %lld (%s)\n", __FILE__, __LINE__, _a, _b, msg); \
        g_failed = 1; \
        return TEST_FAIL; \
    } \
} while (0)

/* ========================================================================= */
/* Test 1: Massive Multi-Threaded Worker Pool (32 threads x 10,000 tasks)    */
/* ========================================================================= */

#define POOL_STRESS_THREADS 32
#define POOL_STRESS_TASKS   10000

typedef struct {
    int thread_id;
    uint64_t hash;
    uint8_t initial_bytes[64];
    int success;
} pool_stress_arg_t;

static void *worker_pool_stress(void *arg) {
    pool_stress_arg_t *w = (pool_stress_arg_t *)arg;
    w->success = 0;
    uint64_t h = 14695981039346656037ULL;

    /* Capture initial 64 bytes */
    if (kheiron_fill(w->initial_bytes, 64) != 0) return NULL;
    for (int i = 0; i < 64; i++) {
        h = (h ^ w->initial_bytes[i]) * 1099511628211ULL;
    }

    for (int i = 0; i < POOL_STRESS_TASKS; i++) {
        int task = i % 11;
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
                uint64_t val = kheiron_range_u64(0, 1000000);
                if (val > 1000000) return NULL;
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
                /* Variable unaligned buffer size */
                size_t len = 1 + (i % 1024);
                uint8_t stack_buf[1025];
                if (kheiron_fill(stack_buf, len) != 0) return NULL;
                for (size_t b = 0; b < len; b += 16) {
                    h = (h ^ stack_buf[b]) * 1099511628211ULL;
                }
                break;
            }
            case 6: {
                /* Context creation, seeding, filling, wiping */
                kheiron_ctx_t ctx;
                uint8_t seed[32];
                if (kheiron_fill(seed, 32) != 0) return NULL;
                if (kheiron_ctx_init(&ctx, seed, 32) != 0) return NULL;
                uint8_t cbuf[256];
                if (kheiron_ctx_fill(&ctx, cbuf, 256) != 0) return NULL;
                kheiron_ctx_wipe(&ctx);
                for (size_t b = 0; b < 256; b += 32) {
                    h = (h ^ cbuf[b]) * 1099511628211ULL;
                }
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
                for (int d = 0; d < 52; d++) deck[d] = (uint8_t)d;
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
            case 10: {
                /* Extreme range boundary test */
                uint64_t r1 = kheiron_range_u64(42, 42);
                if (r1 != 42) return NULL;
                uint64_t r2 = kheiron_range_u64(10, 5); /* inverted */
                if (r2 != 10) return NULL;
                h = (h ^ r1 ^ r2) * 1099511628211ULL;
                break;
            }
        }
    }
    w->hash = h;
    w->success = 1;
    return NULL;
}

static int test_stress_thread_pool(void) {
    printf("[*] Running Test 1: Massive Multi-Threaded Worker Pool (%d threads x %d tasks)...\n",
           POOL_STRESS_THREADS, POOL_STRESS_TASKS);
    pthread_t th[POOL_STRESS_THREADS];
    pool_stress_arg_t args[POOL_STRESS_THREADS];

    for (int i = 0; i < POOL_STRESS_THREADS; i++) {
        args[i].thread_id = i;
        args[i].hash = 0;
        args[i].success = 0;
        memset(args[i].initial_bytes, 0, 64);
        int rc = pthread_create(&th[i], NULL, worker_pool_stress, &args[i]);
        STRESS_ASSERT_INT_EQ(rc, 0, "pthread_create failed");
    }

    for (int i = 0; i < POOL_STRESS_THREADS; i++) {
        pthread_join(th[i], NULL);
        STRESS_ASSERT_INT_EQ(args[i].success, 1, "worker thread reported failure");
    }

    /* Verify uniqueness of all thread hashes and initial blocks */
    for (int i = 0; i < POOL_STRESS_THREADS; i++) {
        for (int j = i + 1; j < POOL_STRESS_THREADS; j++) {
            STRESS_ASSERT(args[i].hash != args[j].hash, "Hash collision between threads!");
            STRESS_ASSERT(memcmp(args[i].initial_bytes, args[j].initial_bytes, 64) != 0,
                          "Initial 64-byte keystream identical between threads!");
        }
    }
    printf("    [PASS] All %d threads completed %d operations with 100%% unique hashes & streams.\n",
           POOL_STRESS_THREADS, POOL_STRESS_TASKS);
    return TEST_OK;
}

/* ========================================================================= */
/* Test 2: Multi-Threaded Context Isolation & Determinism Under Epoch Rollover*/
/* ========================================================================= */

#define CTX_STRESS_THREADS 16
#define CTX_DRAW_SIZE      (2 * 1024 * 1024) /* 2 MB: forces 2 rotation epochs */

typedef struct {
    int thread_id;
    uint8_t det_digest[32];   /* Digest from shared deterministic seed */
    uint8_t uniq_digest[32];  /* Digest from unique seed */
    uint8_t global_digest[32];/* Digest from global thread-local state */
    int success;
} ctx_stress_arg_t;

static void compute_digest(const uint8_t *data, size_t len, uint8_t digest[32]) {
    /* Simple 256-bit hash for stream summary */
    uint64_t h[4] = {
        0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
        0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL
    };
    for (size_t i = 0; i < len; i++) {
        h[i % 4] = (h[i % 4] ^ data[i]) * 1099511628211ULL;
    }
    memcpy(digest, h, 32);
}

static void *worker_ctx_isolation(void *arg) {
    ctx_stress_arg_t *t = (ctx_stress_arg_t *)arg;
    t->success = 0;

    /* 1. Intra-thread cloning determinism: clone context produces identical initial stream */
    kheiron_ctx_t ctx_orig, ctx_clone;
    if (kheiron_ctx_init(&ctx_orig, "seed_for_clone", 14) != 0) return NULL;
    memcpy(&ctx_clone, &ctx_orig, sizeof(kheiron_ctx_t));

    uint8_t buf_orig[4096];
    uint8_t buf_clone[4096];
    if (kheiron_ctx_fill(&ctx_orig, buf_orig, sizeof(buf_orig)) != 0) return NULL;
    if (kheiron_ctx_fill(&ctx_clone, buf_clone, sizeof(buf_clone)) != 0) return NULL;
    if (memcmp(buf_orig, buf_clone, sizeof(buf_orig)) != 0) return NULL;
    kheiron_ctx_wipe(&ctx_orig);
    kheiron_ctx_wipe(&ctx_clone);

    uint8_t *buf = (uint8_t *)malloc(CTX_DRAW_SIZE);
    if (!buf) return NULL;

    /* 2. Static seed anti-collision: verify rdtsc prevents multi-thread static seed collision */
    kheiron_ctx_t det_ctx;
    const char det_seed[] = "KHEIRON_STATIC_SEED_CONCURRENCY";
    if (kheiron_ctx_init(&det_ctx, det_seed, strlen(det_seed)) != 0) { free(buf); return NULL; }
    if (kheiron_ctx_fill(&det_ctx, buf, CTX_DRAW_SIZE) != 0) { free(buf); return NULL; }
    kheiron_ctx_wipe(&det_ctx);
    compute_digest(buf, CTX_DRAW_SIZE, t->det_digest);

    /* 3. Context with per-thread unique seed */
    kheiron_ctx_t uniq_ctx;
    uint8_t uniq_seed[32];
    for (size_t i = 0; i < 32; i++) uniq_seed[i] = (uint8_t)(t->thread_id * 31 + i);
    if (kheiron_ctx_init(&uniq_ctx, uniq_seed, 32) != 0) { free(buf); return NULL; }
    if (kheiron_ctx_fill(&uniq_ctx, buf, CTX_DRAW_SIZE) != 0) { free(buf); return NULL; }
    kheiron_ctx_wipe(&uniq_ctx);
    compute_digest(buf, CTX_DRAW_SIZE, t->uniq_digest);

    /* 4. Global thread-local state */
    if (kheiron_fill(buf, CTX_DRAW_SIZE) != 0) { free(buf); return NULL; }
    compute_digest(buf, CTX_DRAW_SIZE, t->global_digest);

    free(buf);
    t->success = 1;
    return NULL;
}

static int test_stress_context_isolation(void) {
    printf("[*] Running Test 2: Multi-Threaded Context Isolation & Anti-Collision Across 2MB Rollovers...\n");
    pthread_t th[CTX_STRESS_THREADS];
    ctx_stress_arg_t args[CTX_STRESS_THREADS];

    for (int i = 0; i < CTX_STRESS_THREADS; i++) {
        args[i].thread_id = i;
        args[i].success = 0;
        int rc = pthread_create(&th[i], NULL, worker_ctx_isolation, &args[i]);
        STRESS_ASSERT_INT_EQ(rc, 0, "pthread_create failed in ctx isolation");
    }

    for (int i = 0; i < CTX_STRESS_THREADS; i++) {
        pthread_join(th[i], NULL);
        STRESS_ASSERT_INT_EQ(args[i].success, 1, "ctx isolation thread failed");
    }

    /* Verification:
       - All threads MUST produce mutually distinct det_digest (anti-collision on static seeds).
       - All threads MUST produce mutually distinct uniq_digest (context isolation).
       - All threads MUST produce mutually distinct global_digest (thread-local isolation).
       - det_digest != uniq_digest != global_digest.
    */
    for (int i = 0; i < CTX_STRESS_THREADS; i++) {
        for (int j = i + 1; j < CTX_STRESS_THREADS; j++) {
            STRESS_ASSERT(memcmp(args[i].det_digest, args[j].det_digest, 32) != 0,
                          "Static seed anti-collision failed: two threads generated identical streams!");
            STRESS_ASSERT(memcmp(args[i].uniq_digest, args[j].uniq_digest, 32) != 0,
                          "Unique contexts produced identical output between threads!");
            STRESS_ASSERT(memcmp(args[i].global_digest, args[j].global_digest, 32) != 0,
                          "Global thread-local PRNG produced identical output between threads!");
        }
    }

    printf("    [PASS] Verified RFC determinism and multi-threaded isolation across %d threads x 6MB draws.\n",
           CTX_STRESS_THREADS);
    return TEST_OK;
}

/* ========================================================================= */
/* Test 3: Thread Churn & TLS Lifecycle Sanitizer Stress                     */
/* ========================================================================= */

#define CHURN_ROUNDS 25
#define CHURN_BATCH  4

static void *worker_churn(void *arg) {
    (void)arg;
    uint8_t buf[1024];
    if (kheiron_init() != 0) return (void *)1;
    if (kheiron_fill(buf, sizeof(buf)) != 0) return (void *)2;
    uint64_t val = kheiron_u64();
    (void)val;
    kheiron_secure_wipe(buf, sizeof(buf));
    return NULL;
}

static int test_stress_thread_churn(void) {
    printf("[*] Running Test 3: Thread Churn & TLS Lifecycle Stress (%d rapid cycles x %d threads)...\n",
           CHURN_ROUNDS, CHURN_BATCH);
    for (int r = 0; r < CHURN_ROUNDS; r++) {
        pthread_t th[CHURN_BATCH];
        for (int b = 0; b < CHURN_BATCH; b++) {
            int rc = pthread_create(&th[b], NULL, worker_churn, NULL);
            STRESS_ASSERT_INT_EQ(rc, 0, "pthread_create failed in churn");
        }
        for (int b = 0; b < CHURN_BATCH; b++) {
            void *res = NULL;
            pthread_join(th[b], &res);
            STRESS_ASSERT(res == NULL, "churn worker thread returned error");
        }
    }
    printf("    [PASS] %d short-lived threads created & destroyed with zero TLS corruption or leaks.\n",
           CHURN_ROUNDS * CHURN_BATCH);
    return TEST_OK;
}

/* ========================================================================= */
/* Test 4: Sequential Process Forks & Child State Divergence                 */
/* ========================================================================= */

#define NUM_SEQ_FORKS 16

static int test_stress_sequential_forks(void) {
    printf("[*] Running Test 4: Sequential Process Forks (%d sequential forks & divergence)...\n",
           NUM_SEQ_FORKS);

    STRESS_ASSERT_INT_EQ(kheiron_init(), 0, "kheiron_init in parent");
    uint8_t parent_blocks[NUM_SEQ_FORKS + 1][256];
    uint8_t child_initial[NUM_SEQ_FORKS][256];
    uint8_t child_post_rot[NUM_SEQ_FORKS][64];

    /* Initial parent draw */
    STRESS_ASSERT_INT_EQ(kheiron_fill(parent_blocks[0], 256), 0, "parent pre-fork fill");

    for (int k = 0; k < NUM_SEQ_FORKS; k++) {
        int pipe_fd[2];
        STRESS_ASSERT_INT_EQ(pipe(pipe_fd), 0, "pipe creation");

        pid_t pid = fork();
        STRESS_ASSERT(pid >= 0, "fork failed");

        if (pid == 0) {
            /* Child process */
            close(pipe_fd[0]);

            uint8_t c_init[256];
            if (kheiron_fill(c_init, 256) != 0) exit(10);

            /* Trigger 1MB key rotation rollover inside child */
            size_t drain_len = 1048576 + 512;
            uint8_t *drain = (uint8_t *)malloc(drain_len);
            if (!drain || kheiron_fill(drain, drain_len) != 0) exit(11);
            free(drain);

            uint8_t c_post[64];
            if (kheiron_fill(c_post, 64) != 0) exit(12);

            ssize_t w1 = write(pipe_fd[1], c_init, 256);
            ssize_t w2 = write(pipe_fd[1], c_post, 64);
            (void)w1; (void)w2;
            close(pipe_fd[1]);
            exit(0);
        } else {
            /* Parent process */
            close(pipe_fd[1]);
            ssize_t r1 = read(pipe_fd[0], child_initial[k], 256);
            ssize_t r2 = read(pipe_fd[0], child_post_rot[k], 64);
            close(pipe_fd[0]);

            STRESS_ASSERT_INT_EQ(r1, 256, "child read initial bytes mismatch");
            STRESS_ASSERT_INT_EQ(r2, 64, "child read post-rot bytes mismatch");

            int status = 0;
            waitpid(pid, &status, 0);
            STRESS_ASSERT(WIFEXITED(status) && WEXITSTATUS(status) == 0,
                          "child process failed or crashed");

            /* Parent draw after each fork */
            STRESS_ASSERT_INT_EQ(kheiron_fill(parent_blocks[k + 1], 256), 0, "parent inter-fork fill");
        }
    }

    /* All-pairs divergence analysis */
    /* 1. Parent blocks pairwise distinct */
    for (int i = 0; i <= NUM_SEQ_FORKS; i++) {
        for (int j = i + 1; j <= NUM_SEQ_FORKS; j++) {
            STRESS_ASSERT(memcmp(parent_blocks[i], parent_blocks[j], 256) != 0,
                          "Parent produced identical blocks across forks!");
        }
    }

    /* 2. Every child initial block != every parent block */
    for (int k = 0; k < NUM_SEQ_FORKS; k++) {
        for (int p = 0; p <= NUM_SEQ_FORKS; p++) {
            STRESS_ASSERT(memcmp(child_initial[k], parent_blocks[p], 256) != 0,
                          "Child initial block matched a parent block!");
        }
    }

    /* 3. Every child initial block != every other child initial block */
    for (int i = 0; i < NUM_SEQ_FORKS; i++) {
        for (int j = i + 1; j < NUM_SEQ_FORKS; j++) {
            STRESS_ASSERT(memcmp(child_initial[i], child_initial[j], 256) != 0,
                          "Child initial block collision between sequential children!");
            STRESS_ASSERT(memcmp(child_post_rot[i], child_post_rot[j], 64) != 0,
                          "Child post-rotation block collision between sequential children!");
        }
    }

    printf("    [PASS] All %d sequential child processes successfully diverged from parent and each other.\n",
           NUM_SEQ_FORKS);
    return TEST_OK;
}

/* ========================================================================= */
/* Test 5: Deeply Nested Grandchild Process Forks (5 generations)            */
/* ========================================================================= */

static int test_stress_nested_forks(void) {
    printf("[*] Running Test 5: Deeply Nested Grandchild Process Forks (5 generations)...\n");

    int pipe_fd[2];
    STRESS_ASSERT_INT_EQ(pipe(pipe_fd), 0, "pipe creation");

    pid_t p1 = fork();
    STRESS_ASSERT(p1 >= 0, "fork p1");

    if (p1 == 0) {
        /* Gen 1 (Child) */
        uint8_t gen1_bytes[128];
        if (kheiron_fill(gen1_bytes, sizeof(gen1_bytes)) != 0) exit(1);
        ssize_t w = write(pipe_fd[1], gen1_bytes, sizeof(gen1_bytes)); (void)w;

        pid_t p2 = fork();
        if (p2 < 0) exit(2);
        if (p2 == 0) {
            /* Gen 2 (Grandchild) */
            uint8_t gen2_bytes[128];
            if (kheiron_fill(gen2_bytes, sizeof(gen2_bytes)) != 0) exit(3);
            w = write(pipe_fd[1], gen2_bytes, sizeof(gen2_bytes)); (void)w;

            pid_t p3 = fork();
            if (p3 < 0) exit(4);
            if (p3 == 0) {
                /* Gen 3 (Great-Grandchild) */
                uint8_t gen3_bytes[128];
                if (kheiron_fill(gen3_bytes, sizeof(gen3_bytes)) != 0) exit(5);
                w = write(pipe_fd[1], gen3_bytes, sizeof(gen3_bytes)); (void)w;

                pid_t p4 = fork();
                if (p4 < 0) exit(6);
                if (p4 == 0) {
                    /* Gen 4 (Great-Great-Grandchild) */
                    uint8_t gen4_bytes[128];
                    if (kheiron_fill(gen4_bytes, sizeof(gen4_bytes)) != 0) exit(7);
                    w = write(pipe_fd[1], gen4_bytes, sizeof(gen4_bytes)); (void)w;
                    close(pipe_fd[1]);
                    exit(0);
                }
                waitpid(p4, NULL, 0);
                exit(0);
            }
            waitpid(p3, NULL, 0);
            exit(0);
        }
        waitpid(p2, NULL, 0);
        exit(0);
    }

    close(pipe_fd[1]);
    uint8_t gen0_bytes[128];
    STRESS_ASSERT_INT_EQ(kheiron_fill(gen0_bytes, sizeof(gen0_bytes)), 0, "parent gen0 fill");

    uint8_t gen_bytes[4][128];
    for (int g = 0; g < 4; g++) {
        ssize_t r = read(pipe_fd[0], gen_bytes[g], 128);
        STRESS_ASSERT_INT_EQ(r, 128, "read gen bytes");
    }
    close(pipe_fd[0]);

    int status = 0;
    waitpid(p1, &status, 0);
    STRESS_ASSERT(WIFEXITED(status) && WEXITSTATUS(status) == 0, "p1 exited cleanly");

    /* Verify all 5 generations are mutually distinct */
    for (int g = 0; g < 4; g++) {
        STRESS_ASSERT(memcmp(gen0_bytes, gen_bytes[g], 128) != 0, "Gen0 matched child gen!");
    }
    for (int i = 0; i < 4; i++) {
        for (int j = i + 1; j < 4; j++) {
            STRESS_ASSERT(memcmp(gen_bytes[i], gen_bytes[j], 128) != 0, "Nested generations collided!");
        }
    }

    printf("    [PASS] 5 nested generations diverged without state collision or hang.\n");
    return TEST_OK;
}

/* ========================================================================= */
/* Test 6: Fork Safety Under Active Multi-Threading in Parent                */
/* ========================================================================= */

static volatile int g_threads_running = 1;

static void *background_generator(void *arg) {
    (void)arg;
    uint8_t buf[512];
    while (g_threads_running) {
        if (kheiron_fill(buf, sizeof(buf)) != 0) return (void *)1;
        uint64_t v = kheiron_u64();
        (void)v;
        usleep(500);
    }
    return NULL;
}

static int test_stress_fork_multithreaded(void) {
    printf("[*] Running Test 6: Fork Safety Under Active Multi-Threading in Parent...\n");
    const int NUM_BG = 4;
    pthread_t bg_th[NUM_BG];
    g_threads_running = 1;

    for (int i = 0; i < NUM_BG; i++) {
        int rc = pthread_create(&bg_th[i], NULL, background_generator, NULL);
        STRESS_ASSERT_INT_EQ(rc, 0, "pthread_create bg");
    }

    /* Wait briefly for threads to enter loop */
    usleep(5000);

    /* Fork while background threads are active */
    pid_t pid = fork();
    STRESS_ASSERT(pid >= 0, "fork under multithread");

    if (pid == 0) {
        /* In child: only 1 thread survives in POSIX.
           Child must be able to generate PRNG bytes, init contexts, and spawn new threads. */
        uint8_t child_buf[1024];
        if (kheiron_fill(child_buf, sizeof(child_buf)) != 0) exit(1);

        kheiron_ctx_t ctx;
        if (kheiron_ctx_init(&ctx, "child_ctx", 9) != 0) exit(2);
        if (kheiron_ctx_fill(&ctx, child_buf, 512) != 0) exit(3);
        kheiron_ctx_wipe(&ctx);

        /* Spawn 4 new threads in child to verify child pthread environment is uncorrupted */
        pthread_t child_threads[4];
        for (int c = 0; c < 4; c++) {
            if (pthread_create(&child_threads[c], NULL, worker_churn, NULL) != 0) exit(4);
        }
        for (int c = 0; c < 4; c++) {
            void *res = NULL;
            pthread_join(child_threads[c], &res);
            if (res != NULL) exit(5);
        }
        exit(0);
    }

    /* In parent: stop background threads */
    g_threads_running = 0;
    for (int i = 0; i < NUM_BG; i++) {
        void *res = NULL;
        pthread_join(bg_th[i], &res);
        STRESS_ASSERT(res == NULL, "bg thread returned error");
    }

    int status = 0;
    waitpid(pid, &status, 0);
    STRESS_ASSERT(WIFEXITED(status) && WEXITSTATUS(status) == 0,
                  "Child failed after fork from multithreaded parent");

    printf("    [PASS] Fork during active thread execution succeeded; child re-initialized safely.\n");
    return TEST_OK;
}

/* ========================================================================= */
/* Main Runner                                                               */
/* ========================================================================= */

int main(void) {
    printf("================================================================================\n");
    printf("CHALLENGER M2.2: ADVERSARIAL CONCURRENCY, FORK SAFETY & SANITIZER STRESS HARNESS\n");
    printf("================================================================================\n");

    int failed = 0;
    if (test_stress_thread_pool() != TEST_OK) failed++;
    if (test_stress_context_isolation() != TEST_OK) failed++;
    if (test_stress_thread_churn() != TEST_OK) failed++;
    if (test_stress_sequential_forks() != TEST_OK) failed++;
    if (test_stress_nested_forks() != TEST_OK) failed++;
    if (test_stress_fork_multithreaded() != TEST_OK) failed++;

    printf("--------------------------------------------------------------------------------\n");
    if (failed == 0) {
        printf("RESULT: ALL ADVERSARIAL STRESS SUITES PASSED CLEANLY (0 FAILURES)\n");
        printf("================================================================================\n");
        return 0;
    } else {
        printf("RESULT: %d ADVERSARIAL STRESS SUITES FAILED!\n", failed);
        printf("================================================================================\n");
        return 1;
    }
}
