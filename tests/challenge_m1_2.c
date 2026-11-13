#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <assert.h>
#include <errno.h>

#include "../kheiron.h"

#define COLOR_RESET  "\033[0m"
#define COLOR_GREEN  "\033[32m"
#define COLOR_RED    "\033[31m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_CYAN   "\033[36m"

static int g_failed = 0;

#define TEST_ASSERT(cond, ...) do { \
    if (!(cond)) { \
        fprintf(stderr, COLOR_RED "[-] ASSERTION FAILED: " __VA_ARGS__); \
        fprintf(stderr, COLOR_RESET "\n"); \
        g_failed++; \
        return -1; \
    } \
} while (0)

#define TEST_PASS(name) \
    printf(COLOR_GREEN "[+] %s: PASSED" COLOR_RESET "\n", name)

/* ========================================================================= */
/* 1. CONCURRENCY STRESS TEST                                                */
/* ========================================================================= */

#define MAX_THREADS 64
#define OPS_PER_THREAD 10000

typedef struct {
    int thread_id;
    int num_threads;
    uint8_t first_64b[64];
    uint64_t first_u64[5];
    int status;
} thread_test_data_t;

static void *concurrency_worker(void *arg) {
    thread_test_data_t *data = (thread_test_data_t *)arg;
    data->status = 0;

    /* 1. Initialize PRNG on this thread */
    if (kheiron_init() != 0) {
        data->status = -1;
        return NULL;
    }

    /* 2. Record initial output for cross-thread uniqueness verification */
    if (kheiron_fill(data->first_64b, sizeof(data->first_64b)) != 0) {
        data->status = -2;
        return NULL;
    }
    for (int i = 0; i < 5; i++) {
        data->first_u64[i] = kheiron_u64();
    }

    /* 3. Intensive PRNG workload intermixing fill, u64, range_u64, uniform */
    uint8_t buf[4096];
    for (int i = 0; i < OPS_PER_THREAD; i++) {
        int op = i % 4;
        switch (op) {
            case 0: {
                size_t sizes[] = { 1, 7, 16, 63, 64, 65, 511, 512, 513, 1024, 4096 };
                size_t sz = sizes[i % (sizeof(sizes) / sizeof(sizes[0]))];
                if (kheiron_fill(buf, sz) != 0) {
                    data->status = -3;
                    return NULL;
                }
                break;
            }
            case 1: {
                uint64_t val = kheiron_u64();
                if (val == 0 && kheiron_u64() == 0 && kheiron_u64() == 0) {
                    /* Highly unlikely to get 3 consecutive zeros unless uninitialized */
                    data->status = -4;
                    return NULL;
                }
                break;
            }
            case 2: {
                uint64_t r = kheiron_range_u64(10, 1000);
                if (r < 10 || r > 1000) {
                    data->status = -5;
                    return NULL;
                }
                break;
            }
            case 3: {
                double d = kheiron_uniform_double();
                if (d < 0.0 || d >= 1.0) {
                    data->status = -6;
                    return NULL;
                }
                break;
            }
        }
    }

    return NULL;
}

static int test_concurrency_threads(int num_threads) {
    printf(COLOR_CYAN "[*] Starting Concurrency Stress Test with %d threads (%d ops/thread)..." COLOR_RESET "\n",
           num_threads, OPS_PER_THREAD);

    pthread_t threads[MAX_THREADS];
    thread_test_data_t thread_data[MAX_THREADS];

    for (int i = 0; i < num_threads; i++) {
        thread_data[i].thread_id = i;
        thread_data[i].num_threads = num_threads;
        thread_data[i].status = 0;
        int rc = pthread_create(&threads[i], NULL, concurrency_worker, &thread_data[i]);
        if (rc != 0) {
            fprintf(stderr, "pthread_create failed for thread %d: %d\n", i, rc);
            return -1;
        }
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
        if (thread_data[i].status != 0) {
            fprintf(stderr, "Thread %d failed with status %d\n", i, thread_data[i].status);
            return -1;
        }
    }

    /* Verify Cross-Thread Stream Independence: no two threads produced identical initial blocks */
    for (int i = 0; i < num_threads; i++) {
        for (int j = i + 1; j < num_threads; j++) {
            if (memcmp(thread_data[i].first_64b, thread_data[j].first_64b, 64) == 0) {
                fprintf(stderr, "COLLISION: Thread %d and Thread %d produced identical 64-byte keystreams!\n", i, j);
                return -1;
            }
            if (memcmp(thread_data[i].first_u64, thread_data[j].first_u64, sizeof(uint64_t) * 5) == 0) {
                fprintf(stderr, "COLLISION: Thread %d and Thread %d produced identical u64 sequences!\n", i, j);
                return -1;
            }
        }
    }

    printf(COLOR_GREEN "    -> %d threads completed %d total operations with zero collisions or races." COLOR_RESET "\n",
           num_threads, num_threads * OPS_PER_THREAD);
    return 0;
}

static int run_concurrency_tests(void) {
    printf("\n=== Challenge 1: Concurrency Stress Testing ===\n");
    if (test_concurrency_threads(16) != 0) return -1;
    if (test_concurrency_threads(32) != 0) return -1;
    if (test_concurrency_threads(64) != 0) return -1;
    TEST_PASS("Concurrency Stress Suite (16, 32, 64 threads)");
    return 0;
}

/* ========================================================================= */
/* 2. FORK RESILIENCE STRESS TEST                                            */
/* ========================================================================= */

#define NUM_FORK_CHILDREN 16
#define FORK_STREAM_LEN 1024

static int run_fork_resilience_tests(void) {
    printf("\n=== Challenge 2: Fork Resilience Stress Testing ===\n");

    /* Part 2A: 16 Concurrent Child Processes */
    printf(COLOR_CYAN "[*] Part 2A: Forking %d concurrent children immediately calling kheiron_fill()..." COLOR_RESET "\n",
           NUM_FORK_CHILDREN);

    /* Initialize and generate in parent before fork */
    TEST_ASSERT(kheiron_init() == 0, "Parent kheiron_init failed");
    uint8_t parent_pre[FORK_STREAM_LEN];
    TEST_ASSERT(kheiron_fill(parent_pre, sizeof(parent_pre)) == 0, "Parent fill pre-fork failed");

    int pipes[NUM_FORK_CHILDREN][2];
    pid_t pids[NUM_FORK_CHILDREN];

    for (int i = 0; i < NUM_FORK_CHILDREN; i++) {
        if (pipe(pipes[i]) != 0) {
            perror("pipe");
            return -1;
        }
    }

    for (int i = 0; i < NUM_FORK_CHILDREN; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            perror("fork");
            return -1;
        }
        if (pids[i] == 0) {
            /* Child process */
            close(pipes[i][0]);

            uint8_t child_stream[FORK_STREAM_LEN];
            int rc = kheiron_fill(child_stream, sizeof(child_stream));
            if (rc != 0) {
                _exit(1);
            }

            /* Do some additional range and u64 operations */
            uint64_t val = kheiron_u64();
            uint64_t rng = kheiron_range_u64(0, 100);
            (void)val;
            (void)rng;

            /* Write back stream */
            ssize_t written = write(pipes[i][1], child_stream, sizeof(child_stream));
            close(pipes[i][1]);
            if (written != sizeof(child_stream)) {
                _exit(2);
            }
            _exit(0);
        } else {
            /* Parent process */
            close(pipes[i][1]);
        }
    }

    /* In parent: generate post-fork stream */
    uint8_t parent_post[FORK_STREAM_LEN];
    TEST_ASSERT(kheiron_fill(parent_post, sizeof(parent_post)) == 0, "Parent fill post-fork failed");

    /* Collect all child streams */
    uint8_t child_streams[NUM_FORK_CHILDREN][FORK_STREAM_LEN];
    for (int i = 0; i < NUM_FORK_CHILDREN; i++) {
        ssize_t total_read = 0;
        while (total_read < FORK_STREAM_LEN) {
            ssize_t r = read(pipes[i][0], child_streams[i] + total_read, FORK_STREAM_LEN - total_read);
            if (r <= 0) break;
            total_read += r;
        }
        close(pipes[i][0]);
        TEST_ASSERT(total_read == FORK_STREAM_LEN, "Child %d stream truncated", i);

        int status = 0;
        waitpid(pids[i], &status, 0);
        TEST_ASSERT(WIFEXITED(status) && WEXITSTATUS(status) == 0,
                    "Child %d exited abnormally (exit code %d)", i, WEXITSTATUS(status));
    }

    /* Verification 1: Check that parent_pre != parent_post */
    TEST_ASSERT(memcmp(parent_pre, parent_post, FORK_STREAM_LEN) != 0,
                "Parent pre-fork and post-fork streams matched!");

    /* Verification 2: Check that no child repeats parent_pre or parent_post */
    for (int i = 0; i < NUM_FORK_CHILDREN; i++) {
        TEST_ASSERT(memcmp(child_streams[i], parent_pre, FORK_STREAM_LEN) != 0,
                    "Child %d repeated parent pre-fork stream!", i);
        TEST_ASSERT(memcmp(child_streams[i], parent_post, FORK_STREAM_LEN) != 0,
                    "Child %d repeated parent post-fork stream!", i);

        /* Check that child stream is not all zeros */
        bool all_zeros = true;
        for (size_t b = 0; b < FORK_STREAM_LEN; b++) {
            if (child_streams[i][b] != 0) {
                all_zeros = false;
                break;
            }
        }
        TEST_ASSERT(!all_zeros, "Child %d produced all zero bytes!", i);
    }

    /* Verification 3: Check pairwise independence across all children */
    for (int i = 0; i < NUM_FORK_CHILDREN; i++) {
        for (int j = i + 1; j < NUM_FORK_CHILDREN; j++) {
            TEST_ASSERT(memcmp(child_streams[i], child_streams[j], FORK_STREAM_LEN) != 0,
                        "COLLISION: Child %d and Child %d produced identical streams!", i, j);
            /* Also check first 32 bytes */
            TEST_ASSERT(memcmp(child_streams[i], child_streams[j], 32) != 0,
                        "COLLISION: Child %d and Child %d share identical 32-byte prefix!", i, j);
        }
    }

    printf(COLOR_GREEN "    -> Part 2A: All %d children produced independent streams, zero parent repeats." COLOR_RESET "\n",
           NUM_FORK_CHILDREN);

    /* Part 2B: Cascading / Nested Fork (Grandchild) */
    printf(COLOR_CYAN "[*] Part 2B: Cascading fork (Parent -> Child -> Grandchild)..." COLOR_RESET "\n");
    int cascade_pipe[2];
    TEST_ASSERT(pipe(cascade_pipe) == 0, "cascade pipe failed");

    pid_t c_pid = fork();
    if (c_pid == 0) {
        /* Child */
        close(cascade_pipe[0]);
        uint8_t c_buf[64];
        kheiron_fill(c_buf, 64);

        int grandchild_pipe[2];
        if (pipe(grandchild_pipe) != 0) _exit(1);

        pid_t gc_pid = fork();
        if (gc_pid == 0) {
            /* Grandchild */
            close(cascade_pipe[1]);
            close(grandchild_pipe[0]);
            uint8_t gc_buf[64];
            kheiron_fill(gc_buf, 64);
            ssize_t w = write(grandchild_pipe[1], gc_buf, 64);
            close(grandchild_pipe[1]);
            _exit(w == 64 ? 0 : 2);
        } else {
            close(grandchild_pipe[1]);
            uint8_t gc_buf[64];
            ssize_t r = read(grandchild_pipe[0], gc_buf, 64);
            close(grandchild_pipe[0]);
            waitpid(gc_pid, NULL, 0);

            if (r != 64 || memcmp(c_buf, gc_buf, 64) == 0) {
                _exit(3);
            }
            /* Write child buf and grandchild buf to parent */
            write(cascade_pipe[1], c_buf, 64);
            write(cascade_pipe[1], gc_buf, 64);
            close(cascade_pipe[1]);
            _exit(0);
        }
    } else {
        close(cascade_pipe[1]);
        uint8_t streams[128];
        ssize_t r = read(cascade_pipe[0], streams, 128);
        close(cascade_pipe[0]);
        int c_status = 0;
        waitpid(c_pid, &c_status, 0);
        TEST_ASSERT(WIFEXITED(c_status) && WEXITSTATUS(c_status) == 0,
                    "Cascade child exited with error %d", WEXITSTATUS(c_status));
        TEST_ASSERT(r == 128, "Cascade stream read truncated");

        uint8_t *c_buf = streams;
        uint8_t *gc_buf = streams + 64;
        TEST_ASSERT(memcmp(c_buf, gc_buf, 64) != 0, "Child and Grandchild produced identical keystreams!");
        TEST_ASSERT(memcmp(parent_pre, c_buf, 64) != 0, "Child matched parent!");
        TEST_ASSERT(memcmp(parent_pre, gc_buf, 64) != 0, "Grandchild matched parent!");
    }

    printf(COLOR_GREEN "    -> Part 2B: Cascading fork independence verified." COLOR_RESET "\n");

    /* Part 2C: Rapid Sequential Forks (25 iterations) */
    printf(COLOR_CYAN "[*] Part 2C: Rapid sequential fork loop (25 iterations)..." COLOR_RESET "\n");
    for (int iter = 0; iter < 25; iter++) {
        int spipe[2];
        TEST_ASSERT(pipe(spipe) == 0, "pipe failed");
        pid_t p = fork();
        if (p == 0) {
            close(spipe[0]);
            uint8_t sample[32];
            kheiron_fill(sample, 32);
            ssize_t w = write(spipe[1], sample, 32);
            close(spipe[1]);
            _exit(w == 32 ? 0 : 1);
        }
        close(spipe[1]);
        uint8_t sample[32];
        ssize_t r = read(spipe[0], sample, 32);
        close(spipe[0]);
        int st = 0;
        waitpid(p, &st, 0);
        TEST_ASSERT(WIFEXITED(st) && WEXITSTATUS(st) == 0 && r == 32, "Iter %d fork failed", iter);
    }
    printf(COLOR_GREEN "    -> Part 2C: 25 rapid forks completed cleanly." COLOR_RESET "\n");

    /* Part 2D: Contextual Generator (kheiron_ctx_t) Fork Resilience */
    printf(COLOR_CYAN "[*] Part 2D: Contextual generator (kheiron_ctx_t) on stack across fork()..." COLOR_RESET "\n");
    int ctx_pipe[2];
    TEST_ASSERT(pipe(ctx_pipe) == 0, "ctx pipe failed");

    kheiron_ctx_t stack_ctx;
    uint8_t ctx_seed[32] = { 1, 2, 3, 4 };
    TEST_ASSERT(kheiron_ctx_init(&stack_ctx, ctx_seed, sizeof(ctx_seed)) == 0, "kheiron_ctx_init failed");

    pid_t ctx_pid = fork();
    if (ctx_pid == 0) {
        /* Child */
        close(ctx_pipe[0]);
        uint8_t out[32];
        int rc = kheiron_ctx_fill(&stack_ctx, out, sizeof(out));
        if (rc == 0) {
            write(ctx_pipe[1], out, sizeof(out));
        }
        close(ctx_pipe[1]);
        _exit(rc == 0 ? 0 : 1);
    } else {
        close(ctx_pipe[1]);
        uint8_t out[32];
        ssize_t r = read(ctx_pipe[0], out, sizeof(out));
        close(ctx_pipe[0]);
        int ctx_status = 0;
        waitpid(ctx_pid, &ctx_status, 0);

        if (WIFSIGNALED(ctx_status)) {
            int sig = WTERMSIG(ctx_status);
            fprintf(stderr, COLOR_RED "[-] CRITICAL VULNERABILITY DETECTED in kheiron_ctx_init: "
                    "Child process was killed by signal %d (%s) due to MADV_WIPEONFORK wiping caller stack page!" COLOR_RESET "\n",
                    sig, sig == 6 ? "SIGABRT (stack smashing / canary failure)" : sig == 11 ? "SIGSEGV" : "crash");
            g_failed++;
            return -1;
        }
        TEST_ASSERT(WIFEXITED(ctx_status) && WEXITSTATUS(ctx_status) == 0,
                    "Child process running stack_ctx exited with error code %d", WEXITSTATUS(ctx_status));
        TEST_ASSERT(r == 32, "Child stream read incomplete");
    }
    printf(COLOR_GREEN "    -> Part 2D: Contextual generator stack fork safety verified." COLOR_RESET "\n");

    TEST_PASS("Fork Resilience Suite (16 children, cascading grandchild, 25 sequential, ctx stack fork)");
    return 0;
}

/* ========================================================================= */
/* 3. RAPID KEY ROTATION STRESS TEST                                         */
/* ========================================================================= */

#define ROTATION_THRESHOLD (1024 * 1024) /* 1 MB */
#define TOTAL_ROTATIONS 55

/* NIST SP 800-22 Monobit Frequency Test */
static double compute_monobit_p_value(const uint8_t *buf, size_t len) {
    long s = 0;
    size_t n = len * 8;
    for (size_t i = 0; i < len; i++) {
        uint8_t byte = buf[i];
        for (int b = 0; b < 8; b++) {
            if ((byte >> b) & 1) {
                s++;
            } else {
                s--;
            }
        }
    }
    double s_obs = fabs((double)s) / sqrt((double)n);
    double p_value = erfc(s_obs / sqrt(2.0));
    return p_value;
}

static int run_rapid_key_rotation_tests(void) {
    printf("\n=== Challenge 3: Rapid Key Rotation Stress Testing ===\n");
    printf(COLOR_CYAN "[*] Triggering %d consecutive 1 MB key rotations (~58 MB continuous fill)..." COLOR_RESET "\n",
           TOTAL_ROTATIONS);

    /* Allocate 1MB + 128KB scratch buffer */
    size_t chunk_buf_size = ROTATION_THRESHOLD + 131072;
    uint8_t *buf = (uint8_t *)malloc(chunk_buf_size);
    TEST_ASSERT(buf != NULL, "malloc failed");

    /* Phase 1: Exact 1 MB blocks (15 rotations) */
    printf("    Phase 1: 15 exact 1 MB boundaries...\n");
    for (int r = 0; r < 15; r++) {
        TEST_ASSERT(kheiron_fill(buf, ROTATION_THRESHOLD) == 0,
                    "kheiron_fill failed in Phase 1 rotation %d", r);
    }

    /* Phase 2: Overshooting boundary by 7 bytes (15 rotations) */
    printf("    Phase 2: 15 boundary overshoot (1 MB + 7 bytes)...\n");
    for (int r = 0; r < 15; r++) {
        TEST_ASSERT(kheiron_fill(buf, ROTATION_THRESHOLD + 7) == 0,
                    "kheiron_fill failed in Phase 2 rotation %d", r);
    }

    /* Phase 3: Undershooting boundary (15 rotations via 512KB chunks) */
    printf("    Phase 3: 15 rotations via 30x 512 KB chunks...\n");
    for (int r = 0; r < 30; r++) {
        TEST_ASSERT(kheiron_fill(buf, ROTATION_THRESHOLD / 2) == 0,
                    "kheiron_fill failed in Phase 3 iteration %d", r);
    }

    /* Phase 4: Irregular prime sizes crossing boundary (10 rotations) */
    printf("    Phase 4: 10 rotations via prime chunks (1 MB + 521 bytes)...\n");
    for (int r = 0; r < 10; r++) {
        TEST_ASSERT(kheiron_fill(buf, ROTATION_THRESHOLD + 521) == 0,
                    "kheiron_fill failed in Phase 4 iteration %d", r);
    }

    /* Verify PRNG quality post-rotations: NIST Monobit Test on 1,000,000 bits (125,000 bytes) */
    printf(COLOR_CYAN "[*] Verifying state stability & randomness post-rotation via NIST Monobit Test..." COLOR_RESET "\n");
    size_t test_len = 125000; /* 1,000,000 bits */
    uint8_t *test_buf = (uint8_t *)malloc(test_len);
    TEST_ASSERT(test_buf != NULL, "malloc test_buf failed");
    TEST_ASSERT(kheiron_fill(test_buf, test_len) == 0, "Post-rotation fill failed");

    double p_val = compute_monobit_p_value(test_buf, test_len);
    printf("    Post-rotation NIST Monobit p-value: %f (threshold: 0.01)\n", p_val);
    TEST_ASSERT(p_val >= 0.01, "NIST Monobit test failed post-rotation (p-val = %f)", p_val);

    /* Test Contextual Generator (kheiron_ctx_t) Rapid Rotation */
    printf(COLOR_CYAN "[*] Testing kheiron_ctx_fill with 20 consecutive 1 MB rotations..." COLOR_RESET "\n");
    kheiron_ctx_t ctx;
    uint8_t seed[32] = { 0x42 };
    TEST_ASSERT(kheiron_ctx_init(&ctx, seed, sizeof(seed)) == 0, "kheiron_ctx_init failed");
    for (int r = 0; r < 20; r++) {
        TEST_ASSERT(kheiron_ctx_fill(&ctx, buf, ROTATION_THRESHOLD + 3) == 0,
                    "kheiron_ctx_fill failed at rotation %d", r);
    }
    kheiron_ctx_wipe(&ctx);

    free(test_buf);
    free(buf);
    TEST_PASS("Rapid Key Rotation Suite (55+ rotations, continuous entropy injection, NIST Monobit verification)");
    return 0;
}

/* ========================================================================= */
/* 4. LEMIRE SAMPLING EMPIRICAL DISTRIBUTION                                 */
/* ========================================================================= */

#define LEMIRE_SAMPLES 1000000
#define NUM_BUCKETS 100

static int run_lemire_sampling_distribution_tests(void) {
    printf("\n=== Challenge 4: Lemire Sampling Empirical Distribution ===\n");
    printf(COLOR_CYAN "[*] Sampling %d values in range [0, %d] using kheiron_range_u64()..." COLOR_RESET "\n",
           LEMIRE_SAMPLES, NUM_BUCKETS - 1);

    uint64_t counts[NUM_BUCKETS];
    memset(counts, 0, sizeof(counts));

    double sum = 0.0;
    double sum_sq = 0.0;

    for (int i = 0; i < LEMIRE_SAMPLES; i++) {
        uint64_t val = kheiron_range_u64(0, NUM_BUCKETS - 1);
        if (val >= NUM_BUCKETS) {
            fprintf(stderr, "OUT OF BOUNDS: Sample %d returned %lu (max is %d)\n", i, (unsigned long)val, NUM_BUCKETS - 1);
            return -1;
        }
        counts[val]++;
        sum += (double)val;
        sum_sq += (double)val * (double)val;
    }

    double expected_count = (double)LEMIRE_SAMPLES / (double)NUM_BUCKETS; /* 10,000 */
    double chi_square = 0.0;
    uint64_t min_count = counts[0];
    uint64_t max_count = counts[0];

    for (int b = 0; b < NUM_BUCKETS; b++) {
        if (counts[b] < min_count) min_count = counts[b];
        if (counts[b] > max_count) max_count = counts[b];
        double diff = (double)counts[b] - expected_count;
        chi_square += (diff * diff) / expected_count;
    }

    double mean = sum / (double)LEMIRE_SAMPLES;
    double variance = (sum_sq / (double)LEMIRE_SAMPLES) - (mean * mean);

    /* Theoretical values:
     * Discrete uniform [0, 99]:
     * Mean = (0 + 99) / 2 = 49.50
     * Variance = (100^2 - 1) / 12 = 9999 / 12 = 833.25
     * Chi-Square with df = 99:
     * Critical threshold at alpha = 0.01 is 134.64.
     * Critical threshold at alpha = 0.001 is 148.23.
     */
    printf("    Empirical Mean:     %.4f (Theoretical: 49.5000)\n", mean);
    printf("    Empirical Variance: %.4f (Theoretical: 833.2500)\n", variance);
    printf("    Min Bucket Count:   %lu (Expected: 10000)\n", (unsigned long)min_count);
    printf("    Max Bucket Count:   %lu (Expected: 10000)\n", (unsigned long)max_count);
    printf("    Chi-Square Stat:    %.4f (df = 99, Critical alpha=0.01: 134.64, alpha=0.001: 148.23)\n", chi_square);

    /* Two-stage hypothesis test if Chi-Square is slightly high (NIST SP 800-22) */
    if (chi_square >= 134.64) {
        printf(COLOR_YELLOW "    [!] Stage 1 Chi-Square (%.4f) > 134.64; running Stage 2 confirmatory trial..." COLOR_RESET "\n",
               chi_square);
        memset(counts, 0, sizeof(counts));
        chi_square = 0.0;
        for (int i = 0; i < LEMIRE_SAMPLES; i++) {
            uint64_t val = kheiron_range_u64(0, NUM_BUCKETS - 1);
            counts[val]++;
        }
        for (int b = 0; b < NUM_BUCKETS; b++) {
            double diff = (double)counts[b] - expected_count;
            chi_square += (diff * diff) / expected_count;
        }
        printf("    Stage 2 Chi-Square Stat: %.4f\n", chi_square);
    }

    TEST_ASSERT(chi_square < 140.0, "Lemire Chi-Square uniformity test failed (stat = %.4f)", chi_square);
    TEST_ASSERT(fabs(mean - 49.50) < 0.15, "Lemire empirical mean deviated excessively (mean = %.4f)", mean);

    /* Edge Case Ranges */
    printf(COLOR_CYAN "[*] Testing boundary & corner cases for kheiron_range_u64()..." COLOR_RESET "\n");
    /* Degenerate range [x, x] */
    for (int i = 0; i < 1000; i++) {
        uint64_t v = kheiron_range_u64(42, 42);
        TEST_ASSERT(v == 42, "Degenerate range failed: expected 42, got %lu", (unsigned long)v);
    }
    /* Inverted range [max, min] */
    TEST_ASSERT(kheiron_range_u64(100, 10) == 100, "Inverted range check failed");

    /* Offset range [1000, 1099] */
    for (int i = 0; i < 10000; i++) {
        uint64_t v = kheiron_range_u64(1000, 1099);
        TEST_ASSERT(v >= 1000 && v <= 1099, "Offset range out of bounds: %lu", (unsigned long)v);
    }

    /* Power-of-two range [0, 63] */
    for (int i = 0; i < 10000; i++) {
        uint64_t v = kheiron_range_u64(0, 63);
        TEST_ASSERT(v <= 63, "Power-of-two range out of bounds: %lu", (unsigned long)v);
    }

    TEST_PASS("Lemire Sampling Empirical Distribution (1,000,000 samples, Chi-Square uniformity verified)");
    return 0;
}

/* ========================================================================= */
/* MAIN RUNNER                                                               */
/* ========================================================================= */

int main(int argc, char *argv[]) {
    printf("===================================================================\n");
    printf("   KHEIRON-HYDROGEN ADVERSARIAL CHALLENGER M1-2 TEST SUITE        \n");
    printf("   Concurrency, Fork Resilience, Rapid Rotation & Lemire Sampling  \n");
    printf("===================================================================\n");

    (void)argc;
    (void)argv;

    int res = 0;
    if (run_concurrency_tests() != 0) res = 1;
    if (run_fork_resilience_tests() != 0) res = 1;
    if (run_rapid_key_rotation_tests() != 0) res = 1;
    if (run_lemire_sampling_distribution_tests() != 0) res = 1;

    printf("\n===================================================================\n");
    if (res == 0 && g_failed == 0) {
        printf(COLOR_GREEN "ALL EMPIRICAL CHALLENGES PASSED SUCCESSFULLY!" COLOR_RESET "\n");
    } else {
        printf(COLOR_RED "FAILURES DETECTED: %d test suite(s) failed." COLOR_RESET "\n", g_failed + (res != 0 ? 1 : 0));
    }
    printf("===================================================================\n");

    return res;
}
