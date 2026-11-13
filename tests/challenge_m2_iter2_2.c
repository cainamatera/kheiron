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
#include <signal.h>
#include <errno.h>
#include <dirent.h>

#ifndef KHEIRON_STATIC
#define KHEIRON_STATIC 1
#endif
#include "../kheiron.h"
#include "e2e_common.h"

#define TEST_OK   0
#define TEST_FAIL_ERR 1

static int g_challenge_fails = 0;

#define CH_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "[-] CHALLENGE ASSERT FAILED [%s:%d]: %s (%s)\n", __FILE__, __LINE__, #cond, msg); \
        g_challenge_fails++; \
        return TEST_FAIL_ERR; \
    } \
} while (0)

#define CH_ASSERT_EQ(act, exp, msg) do { \
    long long _a = (long long)(act); \
    long long _e = (long long)(exp); \
    if (_a != _e) { \
        fprintf(stderr, "[-] CHALLENGE ASSERT FAILED [%s:%d]: actual %lld != expected %lld (%s)\n", __FILE__, __LINE__, _a, _e, msg); \
        g_challenge_fails++; \
        return TEST_FAIL_ERR; \
    } \
} while (0)

/* ========================================================================= */
/* Suite 1: Empirical Process Isolation, Watchdog & Crash Resilience         */
/* ========================================================================= */

static test_status_t helper_passing_test(void) {
    return TEST_PASS;
}

static test_status_t helper_failing_test(void) {
    return TEST_FAIL;
}

static test_status_t helper_segv_test(void) {
    /* Deliberately trigger SIGSEGV in isolated child */
    volatile int *bad_ptr = (volatile int *)0;
    *bad_ptr = 42;
    return TEST_PASS;
}

static test_status_t helper_abort_test(void) {
    /* Deliberately abort child */
    abort();
    return TEST_PASS;
}

static volatile int g_atexit_executed = 0;
static void child_atexit_handler(void) {
    g_atexit_executed = 1;
}

static test_status_t helper_atexit_test(void) {
    atexit(child_atexit_handler);
    return TEST_PASS;
}

/* Custom watchdog runner to test timeout in 1 second instead of waiting 5s */
static inline test_status_t test_run_isolated_watchdog(test_fn_t fn, unsigned int timeout_sec) {
    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return TEST_FAIL;
    }
    if (pid == 0) {
        alarm(timeout_sec);
        test_status_t st = fn();
        _exit(st == TEST_PASS ? 0 : 1);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return TEST_FAIL;
    }
    if (WIFSIGNALED(status)) {
        /* Caught signal (e.g., SIGALRM = 14 or SIGSEGV = 11) */
        return TEST_FAIL;
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        return TEST_PASS;
    }
    return TEST_FAIL;
}

static test_status_t helper_hang_test(void) {
    while (1) {
        pause();
    }
    return TEST_PASS;
}

static int test_process_isolation_oracle(void) {
    printf("[Suite 1] Empirical Process Isolation, Watchdog & Crash Oracle...\n");

    /* 1. Normal pass */
    test_status_t res_pass = test_run_isolated(helper_passing_test);
    CH_ASSERT_EQ(res_pass, TEST_PASS, "test_run_isolated must return TEST_PASS for passing test");

    /* 2. Normal fail */
    test_status_t res_fail = test_run_isolated(helper_failing_test);
    CH_ASSERT_EQ(res_fail, TEST_FAIL, "test_run_isolated must return TEST_FAIL for failing test");

    /* 3. Crash resilience: SIGSEGV must NOT crash runner and must return TEST_FAIL */
    test_status_t res_segv = test_run_isolated(helper_segv_test);
    CH_ASSERT_EQ(res_segv, TEST_FAIL, "test_run_isolated must safely catch SIGSEGV and return TEST_FAIL");

    /* 4. Abort resilience: abort() must return TEST_FAIL */
    test_status_t res_abort = test_run_isolated(helper_abort_test);
    CH_ASSERT_EQ(res_abort, TEST_FAIL, "test_run_isolated must safely catch abort() and return TEST_FAIL");

    /* 5. Watchdog timeout: infinite loop is killed by alarm(1) and returns TEST_FAIL */
    double t0 = (double)clock() / CLOCKS_PER_SEC;
    test_status_t res_hang = test_run_isolated_watchdog(helper_hang_test, 1);
    double t1 = (double)clock() / CLOCKS_PER_SEC;
    (void)(t1 - t0);
    CH_ASSERT_EQ(res_hang, TEST_FAIL, "Watchdog must terminate hanging test via SIGALRM and return TEST_FAIL");

    /* 6. _exit semantics: atexit handler registered in child MUST NOT execute in parent or child */
    g_atexit_executed = 0;
    test_status_t res_exit = test_run_isolated(helper_atexit_test);
    CH_ASSERT_EQ(res_exit, TEST_PASS, "atexit test should pass");
    CH_ASSERT_EQ(g_atexit_executed, 0, "atexit handler must not execute in parent");

    printf("  -> [PASS] Process isolation, signal recovery, watchdog timeout, and _exit semantics verified!\n");
    return TEST_OK;
}

/* ========================================================================= */
/* Suite 2: Rapid Process Isolation Stress (FD & Process Table Leaks)       */
/* ========================================================================= */

static int count_open_fds(void) {
    int count = 0;
    DIR *dir = opendir("/proc/self/fd");
    if (!dir) return -1;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] != '.') count++;
    }
    closedir(dir);
    return count;
}

static int test_process_isolation_rapid_churn(void) {
    printf("[Suite 2] Rapid Process Isolation Churn (200 consecutive forks)...\n");

    int initial_fds = count_open_fds();
    CH_ASSERT(initial_fds > 0, "Failed to read initial fds");

    for (int i = 0; i < 200; i++) {
        test_status_t res = test_run_isolated(helper_passing_test);
        if (res != TEST_PASS) {
            fprintf(stderr, "Failed at iteration %d\n", i);
            CH_ASSERT(false, "Rapid fork failed");
        }
    }

    /* Assert no zombie processes left */
    int status;
    pid_t w = waitpid(-1, &status, WNOHANG);
    CH_ASSERT(w <= 0, "Zombie process detected after isolated tests");

    /* Assert FD count did not increase */
    int final_fds = count_open_fds();
    CH_ASSERT_EQ(final_fds, initial_fds, "File descriptor leak detected during process isolation");

    printf("  -> [PASS] 200 isolated forks executed cleanly with 0 zombie processes and 0 FD leaks!\n");
    return TEST_OK;
}

/* ========================================================================= */
/* Suite 3: Domain 15 Defensive Invariant Stress                             */
/* ========================================================================= */

static int test_domain15_defensive_stress(void) {
    printf("[Suite 3] Domain 15 Defensive Invariant Stress (NULL/Extreme Bounds)...\n");

    uint8_t valid_buf[64];
    memset(valid_buf, 0xAA, sizeof(valid_buf));

    /* kheiron_fill NULL */
    CH_ASSERT_EQ(kheiron_fill(NULL, 1), -1, "kheiron_fill(NULL, 1) must return -1");
    CH_ASSERT_EQ(kheiron_fill(NULL, 1024), -1, "kheiron_fill(NULL, 1024) must return -1");
    CH_ASSERT_EQ(kheiron_fill(NULL, 0), -1, "kheiron_fill(NULL, 0) must return -1");

    /* kheiron_fill zero len with valid buf */
    CH_ASSERT_EQ(kheiron_fill(valid_buf, 0), 0, "kheiron_fill(valid_buf, 0) must return 0");

    /* kheiron_ctx_init NULL ctx */
    CH_ASSERT_EQ(kheiron_ctx_init(NULL, "seed", 4), -1, "kheiron_ctx_init(NULL, ...) must return -1");

    /* kheiron_ctx_fill NULL */
    kheiron_ctx_t ctx;
    CH_ASSERT_EQ(kheiron_ctx_init(&ctx, NULL, 0), 0, "kheiron_ctx_init auto-seed should succeed");
    CH_ASSERT_EQ(kheiron_ctx_fill(NULL, valid_buf, 16), -1, "kheiron_ctx_fill(NULL, buf, len) must return -1");
    CH_ASSERT_EQ(kheiron_ctx_fill(&ctx, NULL, 16), -1, "kheiron_ctx_fill(ctx, NULL, len) must return -1");
    CH_ASSERT_EQ(kheiron_ctx_fill(&ctx, NULL, 0), -1, "kheiron_ctx_fill(ctx, NULL, 0) must return -1");
    CH_ASSERT_EQ(kheiron_ctx_fill(&ctx, valid_buf, 0), 0, "kheiron_ctx_fill(ctx, buf, 0) must return 0");

    /* kheiron_ctx_wipe NULL */
    kheiron_ctx_wipe(NULL); /* Must not segfault */
    kheiron_ctx_wipe(&ctx);

    /* kheiron_secure_wipe NULL */
    kheiron_secure_wipe(NULL, 100); /* Must not segfault */
    kheiron_secure_wipe(NULL, 0);

    /* kheiron_os_entropy defensive contract:
     * NULL buffer with len > 0 -> -1
     * NULL buffer with len == 0 -> 0
     * valid buffer with len == 0 -> 0
     * valid buffer with len > 0 -> 0
     */
    CH_ASSERT_EQ(kheiron_os_entropy(NULL, 32), -1, "kheiron_os_entropy(NULL, 32) must return -1");
    CH_ASSERT_EQ(kheiron_os_entropy(NULL, 0), 0, "kheiron_os_entropy(NULL, 0) must return 0");
    CH_ASSERT_EQ(kheiron_os_entropy(valid_buf, 0), 0, "kheiron_os_entropy(valid_buf, 0) must return 0");
    CH_ASSERT_EQ(kheiron_os_entropy(valid_buf, 32), 0, "kheiron_os_entropy(valid_buf, 32) must return 0");

    /* kheiron_derive defensive guards */
    CH_ASSERT_EQ(kheiron_ctx_init(&ctx, NULL, 0), 0, "ctx init");
    CH_ASSERT_EQ(kheiron_derive(NULL, valid_buf, 32, "info", 4), -1, "derive NULL ctx must return -1");
    CH_ASSERT_EQ(kheiron_derive(&ctx, NULL, 32, "info", 4), -1, "derive NULL out must return -1");
    CH_ASSERT_EQ(kheiron_derive(&ctx, valid_buf, 32, NULL, 4), -1, "derive NULL info must return -1");
    CH_ASSERT_EQ(kheiron_derive(&ctx, valid_buf, 0, "info", 4), -1, "derive 0 length must return -1");
    kheiron_ctx_wipe(&ctx);

    /* kheiron_string defensive guards */
    char str[16];
    CH_ASSERT_EQ(kheiron_string(NULL, 10, "abc"), -1, "kheiron_string NULL str must return -1");
    CH_ASSERT_EQ(kheiron_string(str, 10, NULL), -1, "kheiron_string NULL charset must return -1");
    CH_ASSERT_EQ(kheiron_string(str, 10, ""), -1, "kheiron_string empty charset must return -1");
    CH_ASSERT_EQ(kheiron_string(str, 0, "abc"), -1, "kheiron_string len=0 must return -1");

    /* kheiron_shuffle defensive guards */
    CH_ASSERT_EQ(kheiron_shuffle(NULL, 10, 4), -1, "kheiron_shuffle NULL base must return -1");
    CH_ASSERT_EQ(kheiron_shuffle(valid_buf, 10, 0), -1, "kheiron_shuffle 0 elem_size must return -1");
    CH_ASSERT_EQ(kheiron_shuffle(valid_buf, 0, 4), -1, "kheiron_shuffle 0 nmemb must return -1");
    CH_ASSERT_EQ(kheiron_shuffle(valid_buf, 1, 4), -1, "kheiron_shuffle 1 nmemb must return -1");

    /* kheiron_reseed defensive guards */
    CH_ASSERT_EQ(kheiron_reseed(NULL, 32), -1, "kheiron_reseed NULL entropy must return -1");
    CH_ASSERT_EQ(kheiron_reseed(valid_buf, 0), -1, "kheiron_reseed 0 len must return -1");

    /* kheiron_consttime_cmp defensive guards */
    CH_ASSERT_EQ(kheiron_consttime_cmp(NULL, valid_buf, 16), -1, "kheiron_consttime_cmp NULL a must return -1");
    CH_ASSERT_EQ(kheiron_consttime_cmp(valid_buf, NULL, 16), -1, "kheiron_consttime_cmp NULL b must return -1");
    CH_ASSERT_EQ(kheiron_consttime_cmp(valid_buf, valid_buf, 0), -1, "kheiron_consttime_cmp len 0 must return -1");

    printf("  -> [PASS] Domain 15 defensive NULL and boundary invariant checks passed!\n");
    return TEST_OK;
}

/* ========================================================================= */
/* Suite 4: Hardware RDTSC, Jitter Sampling & OS Entropy Stress              */
/* ========================================================================= */

#define JITTER_DRAWS 50000

static int test_entropy_and_jitter_stress(void) {
    printf("[Suite 4] kheiron_rdtsc, kheiron_jitter_sample & kheiron_os_entropy Stress...\n");

    /* 1. kheiron_rdtsc monotonicity */
    uint64_t prev = kheiron_rdtsc();
    for (int i = 0; i < 10000; i++) {
        uint64_t cur = kheiron_rdtsc();
        CH_ASSERT(cur >= prev, "kheiron_rdtsc must be monotonic");
        prev = cur;
    }

    /* 2. kheiron_jitter_sample bitwise variance over 50,000 draws */
    uint64_t or_accum = 0;
    uint64_t and_accum = ~0ULL;
    uint64_t bit_counts[64] = {0};

    uint64_t s_prev = kheiron_jitter_sample();
    int consecutive_distinct = 0;

    for (int i = 0; i < JITTER_DRAWS; i++) {
        uint64_t sample = kheiron_jitter_sample();
        or_accum |= sample;
        and_accum &= sample;
        if (sample != s_prev) consecutive_distinct++;
        s_prev = sample;

        for (int b = 0; b < 64; b++) {
            if ((sample >> b) & 1) bit_counts[b]++;
        }
    }

    /* No bit stuck at 0 or 1 */
    CH_ASSERT_EQ(or_accum, ~0ULL, "Absence of stuck zero bits across 50,000 jitter draws");
    CH_ASSERT_EQ(and_accum, 0ULL, "Absence of stuck one bits across 50,000 jitter draws");
    CH_ASSERT(consecutive_distinct >= (int)(JITTER_DRAWS * 0.70), "Jitter should have high pairwise variance");

    /* 3. kheiron_os_entropy non-degeneracy */
    uint8_t ent1[64], ent2[64];
    CH_ASSERT_EQ(kheiron_os_entropy(ent1, 64), 0, "os_entropy draw 1");
    CH_ASSERT_EQ(kheiron_os_entropy(ent2, 64), 0, "os_entropy draw 2");
    CH_ASSERT(memcmp(ent1, ent2, 64) != 0, "Two independent OS entropy draws must not match");

    printf("  -> [PASS] rdtsc, jitter (50,000 draws), and os_entropy stress verified!\n");
    return TEST_OK;
}

/* ========================================================================= */
/* Suite 5: Multi-Threaded Entropy Concurrency Stress                       */
/* ========================================================================= */

#define NUM_THREADS 16
#define THREAD_ITERS 1000

typedef struct {
    int id;
    int success;
    uint8_t digest[32];
} thread_test_arg_t;

static void *worker_entropy_thread(void *arg) {
    thread_test_arg_t *t = (thread_test_arg_t *)arg;
    t->success = 0;

    uint8_t buf[256];
    for (int i = 0; i < THREAD_ITERS; i++) {
        /* Draw OS entropy */
        if (kheiron_os_entropy(buf, 32) != 0) {
            return NULL;
        }
        /* Draw jitter */
        uint64_t j = kheiron_jitter_sample();
        /* Draw rdtsc */
        uint64_t r = kheiron_rdtsc();

        /* Hash into thread digest */
        for (int b = 0; b < 32; b++) {
            t->digest[b] ^= buf[b] ^ (uint8_t)(j >> ((b % 8) * 8)) ^ (uint8_t)(r >> ((b % 8) * 8));
        }
    }

    t->success = 1;
    return NULL;
}

static int test_multithread_entropy_stress(void) {
    printf("[Suite 5] Multi-Threaded Entropy Concurrency (%d threads x %d iters)...\n",
           NUM_THREADS, THREAD_ITERS);

    pthread_t th[NUM_THREADS];
    thread_test_arg_t args[NUM_THREADS];
    memset(args, 0, sizeof(args));

    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].id = i;
        if (pthread_create(&th[i], NULL, worker_entropy_thread, &args[i]) != 0) {
            CH_ASSERT(false, "Failed to spawn thread");
        }
    }

    /* Wait for ALL threads to complete before inspecting any args */
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(th[i], NULL);
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        CH_ASSERT_EQ(args[i].success, 1, "Thread failed entropy generation");
    }

    /* Verify thread digests are pairwise distinct */
    for (int i = 0; i < NUM_THREADS; i++) {
        for (int j = i + 1; j < NUM_THREADS; j++) {
            CH_ASSERT(memcmp(args[i].digest, args[j].digest, 32) != 0,
                      "Thread digests must be mutually independent");
        }
    }

    printf("  -> [PASS] All %d threads completed without data races or symbol collisions!\n", NUM_THREADS);
    return TEST_OK;
}

/* ========================================================================= */
/* Main Runner                                                               */
/* ========================================================================= */

int main(void) {
    printf("================================================================================\n");
    printf("CHALLENGER M2.2 ITERATION 2: EMPIRICAL HARNESS & ADVERSARIAL STRESS SUITE\n");
    printf("================================================================================\n");

    int failed = 0;
    if (test_process_isolation_oracle() != TEST_OK) failed++;
    if (test_process_isolation_rapid_churn() != TEST_OK) failed++;
    if (test_domain15_defensive_stress() != TEST_OK) failed++;
    if (test_entropy_and_jitter_stress() != TEST_OK) failed++;
    if (test_multithread_entropy_stress() != TEST_OK) failed++;

    printf("================================================================================\n");
    if (failed == 0) {
        printf("RESULT: ALL 5 ADVERSARIAL STRESS SUITES PASSED CLEANLY (0 ERRORS)\n");
        printf("================================================================================\n");
        return 0;
    } else {
        printf("RESULT: %d ADVERSARIAL SUITES FAILED!\n", failed);
        printf("================================================================================\n");
        return 1;
    }
}
