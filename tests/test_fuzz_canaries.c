#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <assert.h>
#include "../kheiron.h"

#define CANARY_SIZE 128
#define CANARY_PRE_BYTE  0x5A
#define CANARY_POST_BYTE 0xA5

static int check_canaries(const uint8_t *pre, const uint8_t *post, size_t canary_len) {
    for (size_t i = 0; i < canary_len; i++) {
        if (pre[i] != CANARY_PRE_BYTE) {
            fprintf(stderr, "FAIL: Pre-canary corrupted at index %zu: expected 0x%02X, got 0x%02X\n",
                    i, CANARY_PRE_BYTE, pre[i]);
            return -1;
        }
        if (post[i] != CANARY_POST_BYTE) {
            fprintf(stderr, "FAIL: Post-canary corrupted at index %zu: expected 0x%02X, got 0x%02X\n",
                    i, CANARY_POST_BYTE, post[i]);
            return -1;
        }
    }
    return 0;
}

static int test_single_length(size_t len, int use_ctx) {
    size_t total_alloc = CANARY_SIZE + len + CANARY_SIZE;
    uint8_t *raw = (uint8_t *)malloc(total_alloc);
    if (!raw) {
        fprintf(stderr, "FAIL: Out of memory allocating %zu bytes\n", total_alloc);
        return -1;
    }

    uint8_t *pre_canary = raw;
    uint8_t *payload = raw + CANARY_SIZE;
    uint8_t *post_canary = raw + CANARY_SIZE + len;

    memset(pre_canary, CANARY_PRE_BYTE, CANARY_SIZE);
    memset(payload, 0, len);
    memset(post_canary, CANARY_POST_BYTE, CANARY_SIZE);

    int ret = 0;
    if (use_ctx) {
        kheiron_ctx_t ctx;
        if (kheiron_ctx_init(&ctx, "fuzz_seed", 9) != 0) {
            fprintf(stderr, "FAIL: kheiron_ctx_init failed\n");
            free(raw);
            return -1;
        }
        ret = kheiron_ctx_fill(&ctx, payload, len);
        kheiron_ctx_wipe(&ctx);
    } else {
        ret = kheiron_fill(payload, len);
    }

    if (ret != 0) {
        fprintf(stderr, "FAIL: fill returned %d for len %zu (use_ctx=%d)\n", ret, len, use_ctx);
        free(raw);
        return -1;
    }

    if (check_canaries(pre_canary, post_canary, CANARY_SIZE) != 0) {
        fprintf(stderr, "FAIL: Canary check failed for len %zu (use_ctx=%d)\n", len, use_ctx);
        free(raw);
        return -1;
    }

    if (len > 0) {

        size_t non_zero = 0;
        size_t sample_size = len < 64 ? len : 64;
        for (size_t i = 0; i < sample_size; i++) {
            if (payload[i] != 0) non_zero++;
        }
        if (non_zero == 0 && len >= 8) {
            fprintf(stderr, "FAIL: Payload remained all zero for len %zu\n", len);
            free(raw);
            return -1;
        }
    }

    free(raw);
    return 0;
}

int main(void) {
    printf("=== [CHALLENGER M1-1] Fuzzing Buffer Lengths & Tails (1B to 10MB) ===\n");

    size_t boundary_cases[] = {
        0, 1, 2, 3, 4, 7, 8, 15, 16, 31, 32,
        63, 64, 65, 127, 128, 129,
        191, 192, 193, 255, 256, 257,
        383, 384, 385, 447, 448, 449,
        511, 512, 513,
        1023, 1024, 1025,
        1535, 1536, 1537,
        2047, 2048, 2049,
        4095, 4096, 4097,
        8191, 8192, 8193,
        65535, 65536, 65537,
        1048576 - 513, 1048576 - 512, 1048576 - 511,
        1048576 - 65,  1048576 - 64,  1048576 - 63,
        1048576 - 1,   1048576,       1048576 + 1,
        1048576 + 63,  1048576 + 64,  1048576 + 65,
        1048576 + 511, 1048576 + 512, 1048576 + 513,
        2097151, 2097152, 2097153,
        5242880 - 1, 5242880, 5242880 + 1,
        10485760 - 513, 10485760 - 512, 10485760 - 511,
        10485760 - 1,   10485760,       10485760 + 1
    };
    size_t num_boundaries = sizeof(boundary_cases) / sizeof(boundary_cases[0]);

    printf("[1/3] Testing %zu explicit boundary lengths on kheiron_fill...\n", num_boundaries);
    for (size_t i = 0; i < num_boundaries; i++) {
        if (test_single_length(boundary_cases[i], 0) != 0) {
            fprintf(stderr, "Boundary test failed at len %zu\n", boundary_cases[i]);
            return 1;
        }
    }
    printf("      -> kheiron_fill boundaries PASSED.\n");

    printf("[2/3] Testing %zu explicit boundary lengths on kheiron_ctx_fill...\n", num_boundaries);
    for (size_t i = 0; i < num_boundaries; i++) {
        if (test_single_length(boundary_cases[i], 1) != 0) {
            fprintf(stderr, "Boundary test failed at ctx len %zu\n", boundary_cases[i]);
            return 1;
        }
    }
    printf("      -> kheiron_ctx_fill boundaries PASSED.\n");

    printf("[3/3] Fuzzing 1,000 randomized lengths from 1 byte to 10 MB...\n");
    srand(1337);
    for (int iter = 0; iter < 1000; iter++) {
        size_t len;
        int mode = rand() % 4;
        if (mode == 0) {

            len = 1 + (rand() % 1024);
        } else if (mode == 1) {

            len = (rand() % 20000);
        } else if (mode == 2) {

            len = 1048576 - 2000 + (rand() % 4000);
        } else {

            len = 1 + ((size_t)rand() * 5ULL) % 10485760ULL;
        }

        int use_ctx = iter % 2;
        if (test_single_length(len, use_ctx) != 0) {
            fprintf(stderr, "Random fuzz test failed at iter %d (len %zu, use_ctx=%d)\n", iter, len, use_ctx);
            return 1;
        }
        if ((iter + 1) % 200 == 0) {
            printf("      Completed %d / 1000 iterations...\n", iter + 1);
        }
    }
    printf("      -> 1,000 randomized buffer lengths PASSED with zero canary overruns.\n");

    printf("=== ALL CANARY & BOUNDARY FUZZ TESTS PASSED ===\n");
    return 0;
}
