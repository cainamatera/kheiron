#ifndef KHEIRON_E2E_COMMON_H
#define KHEIRON_E2E_COMMON_H

#if !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

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

/* Use KHEIRON_STATIC to allow multi-TU compilation without duplicate symbol collisions */
#ifndef KHEIRON_STATIC
#define KHEIRON_STATIC 1
#endif
#include "../kheiron.h"

typedef enum {
    TEST_PASS = 0,
    TEST_FAIL = 1,
    TEST_SKIP = 2
} test_status_t;

typedef test_status_t (*test_fn_t)(void);

typedef struct {
    const char *tier_id;     /* e.g., "Tier 1: Feature Coverage" */
    const char *test_id;     /* e.g., "T1.01" */
    const char *name;        /* e.g., "test_kheiron_init_cold" */
    const char *description; /* e.g., "Verifies kheiron_init returns 0 on cold call" */
    test_fn_t   func;
    bool        is_isolated; /* run in child fork if true */
} test_case_t;

#define MAX_TESTS 512
extern test_case_t g_test_registry[MAX_TESTS];
extern size_t g_test_count;

static inline void test_register(const char *tier, const char *id, const char *name,
                                 const char *desc, test_fn_t fn, bool isolated) {
    if (g_test_count < MAX_TESTS) {
        g_test_registry[g_test_count].tier_id = tier;
        g_test_registry[g_test_count].test_id = id;
        g_test_registry[g_test_count].name = name;
        g_test_registry[g_test_count].description = desc;
        g_test_registry[g_test_count].func = fn;
        g_test_registry[g_test_count].is_isolated = isolated;
        g_test_count++;
    } else {
        fprintf(stderr, "Warning: MAX_TESTS (%d) exceeded in test_register!\n", MAX_TESTS);
    }
}

/* Informative assertion macros returning TEST_FAIL upon failure */
#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "    [FAIL] %s:%d: Assertion failed: %s\n", __FILE__, __LINE__, #cond); \
        return TEST_FAIL; \
    } \
} while (0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

#define ASSERT_EQ_INT(actual, expected) do { \
    long long _act = (long long)(actual); \
    long long _exp = (long long)(expected); \
    if (_act != _exp) { \
        fprintf(stderr, "    [FAIL] %s:%d: %s == %s (actual %lld != expected %lld)\n", \
                __FILE__, __LINE__, #actual, #expected, _act, _exp); \
        return TEST_FAIL; \
    } \
} while (0)

#define ASSERT_NEQ_INT(actual, expected) do { \
    long long _act = (long long)(actual); \
    long long _exp = (long long)(expected); \
    if (_act == _exp) { \
        fprintf(stderr, "    [FAIL] %s:%d: %s != %s (both are %lld)\n", \
                __FILE__, __LINE__, #actual, #expected, _act); \
        return TEST_FAIL; \
    } \
} while (0)

#define ASSERT_EQ_U64(actual, expected) do { \
    uint64_t _act = (uint64_t)(actual); \
    uint64_t _exp = (uint64_t)(expected); \
    if (_act != _exp) { \
        fprintf(stderr, "    [FAIL] %s:%d: %s == %s (actual 0x%llx != expected 0x%llx)\n", \
                __FILE__, __LINE__, #actual, #expected, (unsigned long long)_act, (unsigned long long)_exp); \
        return TEST_FAIL; \
    } \
} while (0)

#define ASSERT_DOUBLE_BETWEEN(val, min_v, max_v) do { \
    double _v = (double)(val); \
    if (_v < (min_v) || _v >= (max_v)) { \
        fprintf(stderr, "    [FAIL] %s:%d: %f not in [%f, %f)\n", __FILE__, __LINE__, _v, (double)(min_v), (double)(max_v)); \
        return TEST_FAIL; \
    } \
} while (0)

#define ASSERT_FLOAT_BETWEEN(val, min_v, max_v) do { \
    float _v = (float)(val); \
    if (_v < (min_v) || _v >= (max_v)) { \
        fprintf(stderr, "    [FAIL] %s:%d: %f not in [%f, %f)\n", __FILE__, __LINE__, _v, (float)(min_v), (float)(max_v)); \
        return TEST_FAIL; \
    } \
} while (0)

#define ASSERT_NOT_NULL(ptr) do { \
    if ((ptr) == NULL) { \
        fprintf(stderr, "    [FAIL] %s:%d: Expected non-NULL pointer: %s\n", __FILE__, __LINE__, #ptr); \
        return TEST_FAIL; \
    } \
} while (0)

#define ASSERT_NULL(ptr) do { \
    if ((ptr) != NULL) { \
        fprintf(stderr, "    [FAIL] %s:%d: Expected NULL pointer: %s\n", __FILE__, __LINE__, #ptr); \
        return TEST_FAIL; \
    } \
} while (0)

#define ASSERT_MEM_EQ(p1, p2, len) do { \
    if (memcmp((p1), (p2), (len)) != 0) { \
        fprintf(stderr, "    [FAIL] %s:%d: Memory mismatch across %zu bytes\n", __FILE__, __LINE__, (size_t)(len)); \
        return TEST_FAIL; \
    } \
} while (0)

#define ASSERT_MEM_NEQ(p1, p2, len) do { \
    if (memcmp((p1), (p2), (len)) == 0) { \
        fprintf(stderr, "    [FAIL] %s:%d: Memory unexpectedly identical across %zu bytes\n", __FILE__, __LINE__, (size_t)(len)); \
        return TEST_FAIL; \
    } \
} while (0)

/* Process isolation helper */
static inline test_status_t test_run_isolated(test_fn_t fn) {
    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return TEST_FAIL;
    }
    if (pid == 0) {
        alarm(5); /* 5-second watchdog against hangs or infinite loops */
        test_status_t st = fn();
        _exit(st == TEST_PASS ? 0 : 1);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return TEST_FAIL;
    }
    if (WIFSIGNALED(status)) {
        fprintf(stderr, "    [FAIL] Child terminated abnormally by signal %d\n", WTERMSIG(status));
        return TEST_FAIL;
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        return TEST_PASS;
    }
    return TEST_FAIL;
}

#endif /* KHEIRON_E2E_COMMON_H */
