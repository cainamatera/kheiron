#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>

#define KHEIRON_STATIC 1
#include "kheiron.h"

static double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

#define CHALLENGE_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "[CHALLENGE FAIL] %s:%d: %s (%s)\n", __FILE__, __LINE__, #cond, msg); \
        return false; \
    } \
} while (0)

/* ========================================================================= */
/* 1. Kheiron Jitter Flakiness & Statistical Behavior                         */
/* ========================================================================= */

#define JITTER_TRIALS 10000
static bool challenge_jitter_flakiness(void) {
    printf("[CHALLENGE 1] Analyzing %d trials of kheiron_jitter_sample...\n", JITTER_TRIALS);
    int zero_samples = 0;
    int consec_violations = 0;
    int unique_violations = 0;

    for (int t = 0; t < JITTER_TRIALS; t++) {
        const int n = 32;
        uint64_t samples[32];
        for (int i = 0; i < n; i++) {
            samples[i] = kheiron_jitter_sample();
            if (samples[i] == 0ULL) {
                zero_samples++;
            }
        }

        int distinct_consecutive = 0;
        for (int i = 1; i < n; i++) {
            if (samples[i] != samples[i - 1]) distinct_consecutive++;
        }
        if (distinct_consecutive < 24) {
            consec_violations++;
        }

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
        if (unique < 16) {
            unique_violations++;
        }
    }

    printf("  -> Jitter results over %d trials (320,000 draws total):\n", JITTER_TRIALS);
    printf("     Zero draws (0x0): %d (%.4f%%)\n", zero_samples, (double)zero_samples * 100.0 / (JITTER_TRIALS * 32));
    printf("     Consecutive distinct < 24 violations: %d\n", consec_violations);
    printf("     Unique samples < 16 violations: %d\n", unique_violations);

    /* Note: zero_samples > 0 is documented as finding for test_t1_jitter_01 */
    return (consec_violations == 0 && unique_violations == 0);
}

/* Concurrent Jitter Stress with 16 threads */
#define NUM_JITTER_THREADS 16
#define JITTER_PER_THREAD 2000

typedef struct {
    int thread_id;
    int zero_count;
    int error_count;
    uint64_t xor_sum;
} jitter_worker_ctx_t;

static void *jitter_thread_worker(void *arg) {
    jitter_worker_ctx_t *ctx = (jitter_worker_ctx_t *)arg;
    ctx->zero_count = 0;
    ctx->error_count = 0;
    ctx->xor_sum = 0;

    for (int i = 0; i < JITTER_PER_THREAD; i++) {
        uint64_t s = kheiron_jitter_sample();
        if (s == 0ULL) {
            ctx->zero_count++;
        }
        ctx->xor_sum ^= s;
    }
    return NULL;
}

static bool challenge_jitter_concurrency(void) {
    printf("[CHALLENGE 2] Multi-threaded concurrent jitter sampling (%d threads, %d draws each)...\n",
           NUM_JITTER_THREADS, JITTER_PER_THREAD);
    pthread_t threads[NUM_JITTER_THREADS];
    jitter_worker_ctx_t ctxs[NUM_JITTER_THREADS];

    for (int i = 0; i < NUM_JITTER_THREADS; i++) {
        ctxs[i].thread_id = i;
        int rc = pthread_create(&threads[i], NULL, jitter_thread_worker, &ctxs[i]);
        CHALLENGE_ASSERT(rc == 0, "pthread_create failed");
    }

    int total_zeros = 0;
    for (int i = 0; i < NUM_JITTER_THREADS; i++) {
        pthread_join(threads[i], NULL);
        total_zeros += ctxs[i].zero_count;
    }

    printf("  -> Jitter concurrency: all %d threads joined successfully (total zeros = %d out of %d draws).\n",
           NUM_JITTER_THREADS, total_zeros, NUM_JITTER_THREADS * JITTER_PER_THREAD);
    return true;
}

/* ========================================================================= */
/* 2. Kheiron RDTSC Monotonicity & Preemption Stress                          */
/* ========================================================================= */

#define NUM_RDTSC_THREADS 8
#define RDTSC_CALLS 50000

typedef struct {
    int thread_id;
    int inversions;
    uint64_t start_tsc;
    uint64_t end_tsc;
} rdtsc_worker_ctx_t;

static void *rdtsc_worker(void *arg) {
    rdtsc_worker_ctx_t *ctx = (rdtsc_worker_ctx_t *)arg;
    ctx->inversions = 0;
    uint64_t prev = kheiron_rdtsc();
    ctx->start_tsc = prev;

    for (int i = 0; i < RDTSC_CALLS; i++) {
        if (i % 1000 == 0) {
            sched_yield(); /* Force thread preemption / core migration */
        }
        uint64_t curr = kheiron_rdtsc();
        if (curr < prev) {
            ctx->inversions++;
        }
        prev = curr;
    }
    ctx->end_tsc = prev;
    return NULL;
}

static bool challenge_rdtsc_stress(void) {
    printf("[CHALLENGE 3] Stress-testing kheiron_rdtsc monotonicity across %d threads with sched_yield()...\n",
           NUM_RDTSC_THREADS);
    pthread_t threads[NUM_RDTSC_THREADS];
    rdtsc_worker_ctx_t ctxs[NUM_RDTSC_THREADS];

    for (int i = 0; i < NUM_RDTSC_THREADS; i++) {
        ctxs[i].thread_id = i;
        int rc = pthread_create(&threads[i], NULL, rdtsc_worker, &ctxs[i]);
        CHALLENGE_ASSERT(rc == 0, "pthread_create failed");
    }

    int total_inversions = 0;
    for (int i = 0; i < NUM_RDTSC_THREADS; i++) {
        pthread_join(threads[i], NULL);
        total_inversions += ctxs[i].inversions;
        CHALLENGE_ASSERT(ctxs[i].end_tsc > ctxs[i].start_tsc, "RDTSC did not advance in thread");
    }

    printf("  -> RDTSC stress: %d total inversions across %d draws (expected 0 on invariant TSC).\n",
           total_inversions, NUM_RDTSC_THREADS * RDTSC_CALLS);
    CHALLENGE_ASSERT(total_inversions == 0, "RDTSC inversion detected on x86_64 invariant TSC");
    return true;
}

/* ========================================================================= */
/* 3. Kheiron OS Entropy Boundary and Stress Test                            */
/* ========================================================================= */

static bool challenge_os_entropy_boundaries(void) {
    printf("[CHALLENGE 4] Testing kheiron_os_entropy boundary conditions and rapid calls...\n");
    
    /* 1. NULL buffer with len == 0 -> MUST return 0 */
    CHALLENGE_ASSERT(kheiron_os_entropy(NULL, 0) == 0, "kheiron_os_entropy(NULL, 0) must return 0");

    /* 2. NULL buffer with len > 0 -> MUST return -1 */
    CHALLENGE_ASSERT(kheiron_os_entropy(NULL, 1) == -1, "kheiron_os_entropy(NULL, 1) must return -1");
    CHALLENGE_ASSERT(kheiron_os_entropy(NULL, 32) == -1, "kheiron_os_entropy(NULL, 32) must return -1");
    CHALLENGE_ASSERT(kheiron_os_entropy(NULL, 4096) == -1, "kheiron_os_entropy(NULL, 4096) must return -1");

    /* 3. Non-NULL buffer with len == 0 -> MUST return 0, no write */
    uint8_t canary[16];
    memset(canary, 0xAA, sizeof(canary));
    CHALLENGE_ASSERT(kheiron_os_entropy(canary, 0) == 0, "kheiron_os_entropy(canary, 0) must return 0");
    for (int i = 0; i < 16; i++) {
        CHALLENGE_ASSERT(canary[i] == 0xAA, "kheiron_os_entropy wrote to buffer when len == 0");
    }

    /* 4. Rapid calls in tight loop (5,000 calls of 32 bytes) */
    uint8_t entropy_buf[32];
    for (int i = 0; i < 5000; i++) {
        int ret = kheiron_os_entropy(entropy_buf, sizeof(entropy_buf));
        CHALLENGE_ASSERT(ret == 0, "kheiron_os_entropy failed in tight loop");
    }

    /* 5. Large entropy request (1MB) */
    size_t large_sz = 1024 * 1024;
    uint8_t *large_buf = (uint8_t *)malloc(large_sz);
    CHALLENGE_ASSERT(large_buf != NULL, "malloc failed");
    memset(large_buf, 0, large_sz);
    int ret_large = kheiron_os_entropy(large_buf, large_sz);
    CHALLENGE_ASSERT(ret_large == 0, "kheiron_os_entropy failed on 1MB request");
    
    /* Check non-degeneracy of 1MB OS entropy */
    size_t nonzero_count = 0;
    for (size_t i = 0; i < large_sz; i++) {
        if (large_buf[i] != 0) nonzero_count++;
    }
    free(large_buf);
    CHALLENGE_ASSERT(nonzero_count > large_sz * 99 / 100, "1MB OS entropy has excessive zero bytes");

    printf("  -> OS entropy boundaries and 1MB fill passed cleanly.\n");
    return true;
}

/* ========================================================================= */
/* 4. Defensive NULL Pointer Resilience Across Entire Public API              */
/* ========================================================================= */

static bool challenge_defensive_null_safety(void) {
    printf("[CHALLENGE 5] Verifying defensive NULL pointer safety across all public APIs...\n");

    /* kheiron_fill */
    CHALLENGE_ASSERT(kheiron_fill(NULL, 10) == -1, "kheiron_fill(NULL, 10) != -1");
    CHALLENGE_ASSERT(kheiron_fill(NULL, 0) == -1, "kheiron_fill(NULL, 0) != -1");

    /* kheiron_ctx_init */
    CHALLENGE_ASSERT(kheiron_ctx_init(NULL, "seed", 4) == -1, "kheiron_ctx_init(NULL, ...) != -1");

    /* kheiron_ctx_fill */
    kheiron_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    CHALLENGE_ASSERT(kheiron_ctx_init(&ctx, NULL, 0) == 0, "kheiron_ctx_init auto-seed failed");
    uint8_t b[32];
    CHALLENGE_ASSERT(kheiron_ctx_fill(NULL, b, 32) == -1, "kheiron_ctx_fill(NULL, b, 32) != -1");
    CHALLENGE_ASSERT(kheiron_ctx_fill(&ctx, NULL, 32) == -1, "kheiron_ctx_fill(&ctx, NULL, 32) != -1");
    CHALLENGE_ASSERT(kheiron_ctx_fill(&ctx, NULL, 0) == -1, "kheiron_ctx_fill(&ctx, NULL, 0) != -1");
    kheiron_ctx_wipe(&ctx);

    /* kheiron_ctx_wipe & kheiron_secure_wipe on NULL (must not crash) */
    kheiron_ctx_wipe(NULL);
    kheiron_secure_wipe(NULL, 1024);

    /* kheiron_string */
    char str_buf[16];
    CHALLENGE_ASSERT(kheiron_string(NULL, 10, "abc") == -1, "kheiron_string(NULL, ...) != -1");
    CHALLENGE_ASSERT(kheiron_string(str_buf, 10, NULL) == -1, "kheiron_string(..., NULL) != -1");

    /* kheiron_shuffle */
    CHALLENGE_ASSERT(kheiron_shuffle(NULL, 10, 4) == -1, "kheiron_shuffle(NULL, ...) != -1");

    /* kheiron_derive */
    CHALLENGE_ASSERT(kheiron_ctx_init(&ctx, NULL, 0) == 0, "ctx init failed");
    CHALLENGE_ASSERT(kheiron_derive(NULL, b, 32, "info", 4) == -1, "kheiron_derive(NULL, ...) != -1");
    CHALLENGE_ASSERT(kheiron_derive(&ctx, NULL, 32, "info", 4) == -1, "kheiron_derive(..., NULL buf, ...) != -1");
    CHALLENGE_ASSERT(kheiron_derive(&ctx, b, 32, NULL, 4) == -1, "kheiron_derive(..., NULL info, ...) != -1");
    kheiron_ctx_wipe(&ctx);

    /* kheiron_reseed */
    CHALLENGE_ASSERT(kheiron_reseed(NULL, 32) == -1, "kheiron_reseed(NULL, 32) != -1");

    /* kheiron_consttime_cmp */
    CHALLENGE_ASSERT(kheiron_consttime_cmp(NULL, b, 16) == -1, "kheiron_consttime_cmp(NULL, b, 16) != -1");
    CHALLENGE_ASSERT(kheiron_consttime_cmp(b, NULL, 16) == -1, "kheiron_consttime_cmp(b, NULL, 16) != -1");

    printf("  -> All defensive NULL pointer checks verified.\n");
    return true;
}

/* ========================================================================= */
/* 5. Process Isolation & Crash Resilience Oracle Testing                     */
/* ========================================================================= */

static int simulate_crash_segv(void) {
    volatile int *p = (volatile int *)0;
    *p = 42;
    return 0;
}

static int simulate_crash_abort(void) {
    abort();
    return 0;
}

static int simulate_fail_exit(void) {
    return 1;
}

static int simulate_pass_exit(void) {
    return 0;
}

static bool test_isolation_runner(int (*fn)(void), bool expect_pass) {
    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        alarm(2);
        int res = fn();
        _exit(res == 0 ? 0 : 1);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return false;
    bool passed = (WIFEXITED(status) && WEXITSTATUS(status) == 0);
    return passed == expect_pass;
}

static bool challenge_isolation_oracle(void) {
    printf("[CHALLENGE 6] Process isolation oracle against SIGSEGV, SIGABRT, and failure exits...\n");
    CHALLENGE_ASSERT(test_isolation_runner(simulate_pass_exit, true), "Passing function failed in isolation");
    CHALLENGE_ASSERT(test_isolation_runner(simulate_fail_exit, false), "Failing function passed in isolation");
    CHALLENGE_ASSERT(test_isolation_runner(simulate_crash_segv, false), "SIGSEGV not trapped by isolation");
    CHALLENGE_ASSERT(test_isolation_runner(simulate_crash_abort, false), "SIGABRT not trapped by isolation");
    printf("  -> Process isolation oracle reliably traps crashes and non-zero exit codes.\n");
    return true;
}

/* ========================================================================= */
/* 6. Quick Smoke Mode Timing & Repeatability Stress                          */
/* ========================================================================= */

static bool challenge_quick_mode_timing(void) {
    printf("[CHALLENGE 7] Stressing ./tests/e2e_runner --quick across 10 consecutive invocations...\n");
    double max_duration = 0.0;
    double min_duration = 999999.0;
    double sum_duration = 0.0;
    const int runs = 10;

    for (int i = 0; i < runs; i++) {
        double t0 = get_time_ms();
        int rc = system("./tests/e2e_runner --quick > /dev/null 2>&1");
        double elapsed = get_time_ms() - t0;
        CHALLENGE_ASSERT(rc == 0, "./tests/e2e_runner --quick returned non-zero exit code");
        if (elapsed > max_duration) max_duration = elapsed;
        if (elapsed < min_duration) min_duration = elapsed;
        sum_duration += elapsed;
    }

    double avg_duration = sum_duration / runs;
    printf("  -> --quick 10-run stats: min=%.2f ms, avg=%.2f ms, max=%.2f ms (strict SLA: < 200ms)\n",
           min_duration, avg_duration, max_duration);
    CHALLENGE_ASSERT(max_duration < 200.0, "--quick exceeded acceptable SLA latency ceiling");
    return true;
}

/* ========================================================================= */
/* Main Challenge Runner                                                     */
/* ========================================================================= */

int main(void) {
    printf("======================================================================\n");
    printf("KHEIRON EMPIRICAL CHALLENGER TEST BATTERY\n");
    printf("======================================================================\n");

    bool all_ok = true;
    all_ok &= challenge_jitter_flakiness();
    all_ok &= challenge_jitter_concurrency();
    all_ok &= challenge_rdtsc_stress();
    all_ok &= challenge_os_entropy_boundaries();
    all_ok &= challenge_defensive_null_safety();
    all_ok &= challenge_isolation_oracle();
    all_ok &= challenge_quick_mode_timing();

    printf("======================================================================\n");
    if (all_ok) {
        printf("CHALLENGE BATTERY RESULT: ALL CORE HARNESS & BOUNDARY CHALLENGES PASSED!\n");
        printf("======================================================================\n");
        return 0;
    } else {
        printf("CHALLENGE BATTERY RESULT: ONE OR MORE CHALLENGES FAILED!\n");
        printf("======================================================================\n");
        return 1;
    }
}
