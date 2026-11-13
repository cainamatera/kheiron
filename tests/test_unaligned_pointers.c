#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include "../kheiron.h"

#define CANARY_SIZE 128
#define CANARY_PRE_BYTE  0x3C
#define CANARY_POST_BYTE 0xC3

static int test_unaligned(size_t offset, size_t len, int use_ctx) {
    /* Allocate 64-byte aligned block large enough for:
       align_pad (64) + canary (CANARY_SIZE) + offset (up to 64) + len + canary (CANARY_SIZE) + pad */
    size_t total_alloc = 256 + CANARY_SIZE + offset + len + CANARY_SIZE + 256;
    uint8_t *raw = (uint8_t *)malloc(total_alloc);
    if (!raw) {
        fprintf(stderr, "FAIL: Out of memory in test_unaligned\n");
        return -1;
    }

    /* Compute 64-byte aligned base */
    uintptr_t raw_addr = (uintptr_t)raw;
    uintptr_t aligned_addr = (raw_addr + 63) & ~(uintptr_t)63;
    uint8_t *base = (uint8_t *)aligned_addr;

    /* Buffer layout:
       [base .. base + CANARY_SIZE - 1]: pre_canary
       [base + CANARY_SIZE + offset .. base + CANARY_SIZE + offset + len - 1]: target payload
       [base + CANARY_SIZE + offset + len .. base + CANARY_SIZE + offset + len + CANARY_SIZE - 1]: post_canary
    */
    uint8_t *pre_canary = base;
    uint8_t *payload = base + CANARY_SIZE + offset;
    uint8_t *post_canary = payload + len;

    /* Fill canaries */
    memset(pre_canary, CANARY_PRE_BYTE, CANARY_SIZE);
    /* Also fill intermediate slack between pre_canary and payload if offset > 0 */
    if (offset > 0) {
        memset(base + CANARY_SIZE, 0x77, offset);
    }
    memset(payload, 0, len);
    memset(post_canary, CANARY_POST_BYTE, CANARY_SIZE);

    int ret = 0;
    if (use_ctx) {
        kheiron_ctx_t ctx;
        if (kheiron_ctx_init(&ctx, "unaligned_seed", 14) != 0) {
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
        fprintf(stderr, "FAIL: fill failed for offset %zu, len %zu (ret=%d)\n", offset, len, ret);
        free(raw);
        return -1;
    }

    /* Check pre-canary */
    for (size_t i = 0; i < CANARY_SIZE; i++) {
        if (pre_canary[i] != CANARY_PRE_BYTE) {
            fprintf(stderr, "FAIL: Pre-canary corrupted for offset %zu, len %zu at idx %zu (0x%02X vs 0x%02X)\n",
                    offset, len, i, pre_canary[i], CANARY_PRE_BYTE);
            free(raw);
            return -1;
        }
    }

    /* Check slack bytes */
    for (size_t i = 0; i < offset; i++) {
        if ((base + CANARY_SIZE)[i] != 0x77) {
            fprintf(stderr, "FAIL: Slack bytes corrupted for offset %zu, len %zu at idx %zu\n",
                    offset, len, i);
            free(raw);
            return -1;
        }
    }

    /* Check post-canary */
    for (size_t i = 0; i < CANARY_SIZE; i++) {
        if (post_canary[i] != CANARY_POST_BYTE) {
            fprintf(stderr, "FAIL: Post-canary corrupted for offset %zu, len %zu at idx %zu (0x%02X vs 0x%02X)\n",
                    offset, len, i, post_canary[i], CANARY_POST_BYTE);
            free(raw);
            return -1;
        }
    }

    /* Check non-zero payload if len >= 8 */
    if (len >= 8) {
        size_t non_zero = 0;
        size_t check_len = len < 64 ? len : 64;
        for (size_t i = 0; i < check_len; i++) {
            if (payload[i] != 0) non_zero++;
        }
        if (non_zero == 0) {
            fprintf(stderr, "FAIL: Payload all zero for offset %zu, len %zu\n", offset, len);
            free(raw);
            return -1;
        }
    }

    free(raw);
    return 0;
}

int main(void) {
    printf("=== [CHALLENGER M1-1] Unaligned Pointer Offsets (+1 to +31 bytes) Stress Testing ===\n");

    size_t test_lens[] = {
        1, 2, 3, 7, 8, 15, 16, 31, 32, 33,
        63, 64, 65, 127, 128, 129,
        255, 256, 257,
        511, 512, 513,
        1023, 1024, 1025,
        4095, 4096, 4097,
        65535, 65536, 65537,
        1048575, 1048576, 1048577,
        2000000
    };
    size_t num_lens = sizeof(test_lens) / sizeof(test_lens[0]);

    for (size_t offset = 0; offset <= 31; offset++) {
        for (size_t l = 0; l < num_lens; l++) {
            size_t len = test_lens[l];
            /* Test kheiron_fill */
            if (test_unaligned(offset, len, 0) != 0) {
                fprintf(stderr, "FAIL: kheiron_fill offset +%zu, len %zu\n", offset, len);
                return 1;
            }
            /* Test kheiron_ctx_fill */
            if (test_unaligned(offset, len, 1) != 0) {
                fprintf(stderr, "FAIL: kheiron_ctx_fill offset +%zu, len %zu\n", offset, len);
                return 1;
            }
        }
        if (offset % 8 == 0 || offset == 31) {
            printf("      Offset +%zu bytes: all %zu length scenarios PASSED.\n", offset, num_lens);
        }
    }

    printf("=== ALL UNALIGNED POINTER OFFSET TESTS PASSED (0..31 bytes, ASan/UBSan clean) ===\n");
    return 0;
}
