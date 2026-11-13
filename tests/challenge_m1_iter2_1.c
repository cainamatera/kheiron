#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <assert.h>
#include "../kheiron.h"

#define CANARY_SIZE 128
#define CANARY_PRE_BYTE  0x5A
#define CANARY_POST_BYTE 0xA5
#define SLACK_BYTE       0x77

/* Helper to verify canaries */
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

/* Reference pure scalar context fill implementing the exact scalar logic of Kheiron */
static int reference_scalar_ctx_fill(kheiron_ctx_t *ctx, void *buf, size_t len) {
    if (!ctx || !buf) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }

    uint8_t *p = (uint8_t *)buf;
    size_t remaining = len;

    while (remaining > 0) {
        size_t chunk_limit = remaining;

        if (chunk_limit >= 64) {
            size_t scalar_blocks = chunk_limit / 64;
            for (size_t b = 0; b < scalar_blocks; b++) {
                kheiron_chacha_block(ctx->state, p);
                ctx->state[12]++;
                if (ctx->state[12] == 0) {
                    ctx->state[13]++;
                }
                p += 64;
                remaining -= 64;
                ctx->bytes_generated += 64;
            }
            continue;
        }

        uint8_t keystream[64];
        kheiron_chacha_block(ctx->state, keystream);
        ctx->state[12]++;
        if (ctx->state[12] == 0) {
            ctx->state[13]++;
        }
        for (size_t i = 0; i < chunk_limit; i++) {
            p[i] = keystream[i];
        }
        kheiron_secure_wipe(keystream, sizeof(keystream));
        p += chunk_limit;
        remaining -= chunk_limit;
        ctx->bytes_generated += chunk_limit;
    }

    return 0;
}

/* Reference pure scalar multi-block generator */
static void reference_scalar_blocks(uint32_t state[16], uint8_t *out, size_t num_blocks) {
    for (size_t b = 0; b < num_blocks; b++) {
        kheiron_chacha_block(state, out + b * 64);
        state[12]++;
        if (state[12] == 0) {
            state[13]++;
        }
    }
}

static uint32_t xorshift32(uint32_t *rng) {
    uint32_t x = *rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *rng = x;
    return x;
}

/* ========================================================================= */
/* SECTION 1: Buffer Size & Tail Fuzzing with Canaries                       */
/* ========================================================================= */

static int test_fuzz_single_length(size_t len, int use_ctx) {
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
        if (kheiron_ctx_init(&ctx, "fuzz_iter2_seed", 15) != 0) {
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

    if (len >= 8) {
        size_t non_zero = 0;
        size_t sample_size = len < 64 ? len : 64;
        for (size_t i = 0; i < sample_size; i++) {
            if (payload[i] != 0) non_zero++;
        }
        if (non_zero == 0) {
            fprintf(stderr, "FAIL: Payload remained all zero for len %zu\n", len);
            free(raw);
            return -1;
        }
    }

    free(raw);
    return 0;
}

static int run_buffer_fuzz_suite(void) {
    printf("=== [CHALLENGER M1 Iter 2] Section 1: Buffer Sizes & Odd Tails Fuzzing ===\n");

    /* 1.1 Canary Oracle Self-Test */
    {
        uint8_t mock[CANARY_SIZE * 2 + 10];
        memset(mock, CANARY_PRE_BYTE, CANARY_SIZE);
        memset(mock + CANARY_SIZE + 10, CANARY_POST_BYTE, CANARY_SIZE);
        assert(check_canaries(mock, mock + CANARY_SIZE + 10, CANARY_SIZE) == 0);
        /* Corrupt pre-canary */
        mock[5] = 0x00;
        assert(check_canaries(mock, mock + CANARY_SIZE + 10, CANARY_SIZE) != 0);
        mock[5] = CANARY_PRE_BYTE;
        /* Corrupt post-canary */
        mock[CANARY_SIZE + 10 + 7] = 0x00;
        assert(check_canaries(mock, mock + CANARY_SIZE + 10, CANARY_SIZE) != 0);
        printf("[+] Canary detector self-test verified.\n");
    }

    /* 1.2 Exhaustive test of all lengths 0 through 4096 bytes */
    printf("[*] Exhaustive testing of ALL lengths from 0 to 4096 bytes (4097 lengths)...\n");
    for (size_t sz = 0; sz <= 4096; sz++) {
        if (test_fuzz_single_length(sz, 0) != 0) {
            fprintf(stderr, "FAIL: kheiron_fill failed at exhaustive size %zu\n", sz);
            return 1;
        }
        if (test_fuzz_single_length(sz, 1) != 0) {
            fprintf(stderr, "FAIL: kheiron_ctx_fill failed at exhaustive size %zu\n", sz);
            return 1;
        }
    }
    printf("    -> Exhaustive 0..4096 bytes PASSED with zero canary corruptions.\n");

    /* 1.3 Strategic Boundary Cases (Non-multiples of 64 & 512, rotation limits, multi-MB) */
    printf("[*] Testing 90 strategic boundary lengths up to 10 MB...\n");
    size_t boundary_cases[] = {
        0, 1, 2, 3, 4, 7, 8, 15, 16, 31, 32, 47, 48,
        63, 64, 65, 127, 128, 129,
        191, 192, 193, 255, 256, 257,
        319, 320, 321, 383, 384, 385, 447, 448, 449,
        511, 512, 513, 575, 576, 577, 639, 640, 641,
        1023, 1024, 1025, 1535, 1536, 1537,
        2047, 2048, 2049, 4095, 4096, 4097,
        8191, 8192, 8193, 65535, 65536, 65537,
        1048576 - 1025, 1048576 - 1024, 1048576 - 1023,
        1048576 - 513,  1048576 - 512,  1048576 - 511,
        1048576 - 65,   1048576 - 64,   1048576 - 63,
        1048576 - 1,    1048576,        1048576 + 1,
        1048576 + 63,   1048576 + 64,   1048576 + 65,
        1048576 + 511,  1048576 + 512,  1048576 + 513,
        1048576 + 1023, 1048576 + 1024, 1048576 + 1025,
        2097151, 2097152, 2097153,
        5242880 - 1,    5242880,        5242880 + 1,
        10485760 - 513, 10485760 - 512, 10485760 - 511,
        10485760 - 1,   10485760,       10485760 + 1
    };
    size_t num_boundaries = sizeof(boundary_cases) / sizeof(boundary_cases[0]);
    for (size_t i = 0; i < num_boundaries; i++) {
        if (test_fuzz_single_length(boundary_cases[i], 0) != 0) {
            fprintf(stderr, "Boundary test failed at len %zu\n", boundary_cases[i]);
            return 1;
        }
        if (test_fuzz_single_length(boundary_cases[i], 1) != 0) {
            fprintf(stderr, "Boundary test failed at ctx len %zu\n", boundary_cases[i]);
            return 1;
        }
    }
    printf("    -> 90 strategic boundary lengths PASSED.\n");

    /* 1.4 Randomized Fuzzing: 2,500 trials up to 8 MB */
    printf("[*] Fuzzing 2,500 randomized lengths from 1 byte to 8 MB...\n");
    srand(424242);
    for (int iter = 0; iter < 2500; iter++) {
        size_t len;
        int mode = rand() % 5;
        if (mode == 0) {
            /* Small: 1..512 */
            len = 1 + (rand() % 512);
        } else if (mode == 1) {
            /* Non-multiples of 64 or 512 (odd tails) */
            size_t blocks = rand() % 50;
            size_t odd_tail = 1 + (rand() % 511);
            len = blocks * 512 + odd_tail;
        } else if (mode == 2) {
            /* Near 1MB key rotation boundary */
            len = 1048576 - 5000 + (rand() % 10000);
        } else if (mode == 3) {
            /* Medium: up to 100 KB */
            len = 1 + (rand() % 100000);
        } else {
            /* Large: up to 8 MB */
            len = 1 + ((size_t)rand() * 4ULL) % 8388608ULL;
        }

        int use_ctx = iter % 2;
        if (test_fuzz_single_length(len, use_ctx) != 0) {
            fprintf(stderr, "FAIL: Fuzzing failed at iteration %d (len %zu, use_ctx=%d)\n",
                    iter, len, use_ctx);
            return 1;
        }
        if ((iter + 1) % 500 == 0) {
            printf("    Completed %d / 2500 iterations...\n", iter + 1);
        }
    }
    printf("    -> 2,500 randomized lengths PASSED with zero canary overruns.\n");
    return 0;
}

/* ========================================================================= */
/* SECTION 2: Unaligned Pointer Offsets (+1 to +31 bytes, extending to +63)  */
/* ========================================================================= */

static int test_unaligned_single(size_t offset, size_t len, int use_ctx) {
    size_t total_alloc = 256 + CANARY_SIZE + offset + len + CANARY_SIZE + 256;
    uint8_t *raw = (uint8_t *)malloc(total_alloc);
    if (!raw) {
        fprintf(stderr, "FAIL: Out of memory in test_unaligned_single\n");
        return -1;
    }

    uintptr_t raw_addr = (uintptr_t)raw;
    uintptr_t aligned_addr = (raw_addr + 63) & ~(uintptr_t)63;
    uint8_t *base = (uint8_t *)aligned_addr;

    uint8_t *pre_canary = base;
    uint8_t *slack = base + CANARY_SIZE;
    uint8_t *payload = base + CANARY_SIZE + offset;
    uint8_t *post_canary = payload + len;

    memset(pre_canary, CANARY_PRE_BYTE, CANARY_SIZE);
    if (offset > 0) {
        memset(slack, SLACK_BYTE, offset);
    }
    memset(payload, 0, len);
    memset(post_canary, CANARY_POST_BYTE, CANARY_SIZE);

    int ret = 0;
    if (use_ctx) {
        kheiron_ctx_t ctx;
        if (kheiron_ctx_init(&ctx, "unaligned_iter2_seed", 20) != 0) {
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
        fprintf(stderr, "FAIL: fill returned %d for offset %zu, len %zu\n", ret, offset, len);
        free(raw);
        return -1;
    }

    /* Verify pre-canary */
    for (size_t i = 0; i < CANARY_SIZE; i++) {
        if (pre_canary[i] != CANARY_PRE_BYTE) {
            fprintf(stderr, "FAIL: Pre-canary corrupted (offset=%zu, len=%zu, i=%zu)\n", offset, len, i);
            free(raw);
            return -1;
        }
    }

    /* Verify slack bytes */
    for (size_t i = 0; i < offset; i++) {
        if (slack[i] != SLACK_BYTE) {
            fprintf(stderr, "FAIL: Slack bytes corrupted (offset=%zu, len=%zu, i=%zu)\n", offset, len, i);
            free(raw);
            return -1;
        }
    }

    /* Verify post-canary */
    for (size_t i = 0; i < CANARY_SIZE; i++) {
        if (post_canary[i] != CANARY_POST_BYTE) {
            fprintf(stderr, "FAIL: Post-canary corrupted (offset=%zu, len=%zu, i=%zu)\n", offset, len, i);
            free(raw);
            return -1;
        }
    }

    /* Verify payload non-zero if len >= 8 */
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

static int test_unaligned_stack(size_t offset, size_t len) {
    /* Stack buffer with unaligned offset */
    uint8_t stack_buf[1024 + 128];
    uintptr_t addr = (uintptr_t)stack_buf;
    uintptr_t aligned_addr = (addr + 63) & ~(uintptr_t)63;
    uint8_t *base = (uint8_t *)aligned_addr;
    uint8_t *target = base + offset;

    memset(stack_buf, 0xEE, sizeof(stack_buf));
    int ret = kheiron_fill(target, len);
    if (ret != 0) {
        return -1;
    }
    if (len > 0 && target[0] == 0xEE && len >= 8 && target[1] == 0xEE && target[2] == 0xEE) {
        return -1;
    }
    return 0;
}

static int run_unaligned_suite(void) {
    printf("=== [CHALLENGER M1 Iter 2] Section 2: Unaligned Pointer Offsets (+1..+31 and +32..+63) ===\n");

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

    printf("[*] Testing offsets +0 through +63 bytes relative to 64-byte alignment across %zu sizes...\n",
           num_lens);

    for (size_t offset = 0; offset <= 63; offset++) {
        for (size_t l = 0; l < num_lens; l++) {
            size_t len = test_lens[l];
            if (test_unaligned_single(offset, len, 0) != 0) {
                fprintf(stderr, "FAIL: kheiron_fill offset +%zu, len %zu\n", offset, len);
                return 1;
            }
            if (test_unaligned_single(offset, len, 1) != 0) {
                fprintf(stderr, "FAIL: kheiron_ctx_fill offset +%zu, len %zu\n", offset, len);
                return 1;
            }
        }
        if (offset % 16 == 0 || offset == 31 || offset == 63) {
            printf("    Offset +%zu bytes: all %zu lengths PASSED.\n", offset, num_lens);
        }
    }

    printf("[*] Testing stack-allocated unaligned buffers...\n");
    for (size_t offset = 1; offset <= 31; offset++) {
        for (size_t len = 1; len <= 512; len += 17) {
            if (test_unaligned_stack(offset, len) != 0) {
                fprintf(stderr, "FAIL: stack unaligned offset +%zu, len %zu\n", offset, len);
                return 1;
            }
        }
    }
    printf("    -> Stack-allocated unaligned buffers PASSED.\n");
    return 0;
}

/* ========================================================================= */
/* SECTION 3: Differential Oracle (AVX2 vs Scalar ChaCha20 Reference)        */
/* ========================================================================= */

static int run_differential_oracle_suite(void) {
    printf("=== [CHALLENGER M1 Iter 2] Section 3: Differential Oracle (AVX2 vs Scalar) ===\n");

#if !KHEIRON_HAVE_AVX2
    printf("NOTICE: KHEIRON_HAVE_AVX2 is disabled at compile time. Skipping AVX2 tests.\n");
    return 0;
#else
    if (!kheiron_cpu_has_avx2()) {
        printf("NOTICE: Host CPU lacks AVX2 support. Skipping AVX2 tests.\n");
        return 0;
    }

    /* 3.1 RFC 7539 Official Vector */
    printf("[1/6] RFC 7539 official test vector bit-exact verification...\n");
    {
        const uint32_t rfc_key[8] = {
            0x03020100, 0x07060504, 0x0b0a0908, 0x0f0e0d0c,
            0x13121110, 0x17161514, 0x1b1a1918, 0x1f1e1d1c
        };
        const uint32_t rfc_nonce[3] = {
            0x09000000, 0x4a000000, 0x00000000
        };
        uint32_t rfc_state[16] = {
            0x61707865, 0x3320646e, 0x79622d32, 0x6b206574,
            rfc_key[0], rfc_key[1], rfc_key[2], rfc_key[3],
            rfc_key[4], rfc_key[5], rfc_key[6], rfc_key[7],
            0x00000001, /* block counter = 1 */
            rfc_nonce[0], rfc_nonce[1], rfc_nonce[2]
        };
        const uint8_t rfc_expected[64] = {
            0x10, 0xf1, 0xe7, 0xe4, 0xd1, 0x3b, 0x59, 0x15,
            0x50, 0x0f, 0xdd, 0x1f, 0xa3, 0x20, 0x71, 0xc4,
            0xc7, 0xd1, 0xf4, 0xc7, 0x33, 0xc0, 0x68, 0x03,
            0x04, 0x22, 0xaa, 0x9a, 0xc3, 0xd4, 0x6c, 0x4e,
            0xd2, 0x82, 0x64, 0x46, 0x07, 0x9f, 0xaa, 0x09,
            0x14, 0xc2, 0xd7, 0x05, 0xd9, 0x8b, 0x02, 0xa2,
            0xb5, 0x12, 0x9c, 0xd1, 0xde, 0x16, 0x4e, 0xb9,
            0xcb, 0xd0, 0x83, 0xe8, 0xa2, 0x50, 0x3c, 0x4e
        };

        uint8_t scalar_block[64];
        kheiron_chacha_block(rfc_state, scalar_block);
        if (memcmp(scalar_block, rfc_expected, 64) != 0) {
            fprintf(stderr, "FAIL: Scalar ChaCha20 does not match RFC 7539!\n");
            return 1;
        }

        uint8_t avx2_8blocks[512];
        uint32_t state_copy[16];
        memcpy(state_copy, rfc_state, sizeof(state_copy));
        kheiron_chacha20_blocks_avx2(state_copy, avx2_8blocks, 8);
        if (memcmp(avx2_8blocks, rfc_expected, 64) != 0) {
            fprintf(stderr, "FAIL: AVX2 Block 0 does not match RFC 7539!\n");
            return 1;
        }
        printf("    -> RFC 7539 verified bit-exact on AVX2.\n");
    }

    /* 3.2 25,000 Randomized States (512 bytes each = 12.8 MB) */
    printf("[2/6] Differential oracle across 25,000 randomized states (12.8 MB)...\n");
    {
        uint32_t rng = 0x12345678;
        for (int trial = 0; trial < 25000; trial++) {
            uint32_t s_avx[16], s_scalar[16];
            s_avx[0] = 0x61707865; s_avx[1] = 0x3320646e;
            s_avx[2] = 0x79622d32; s_avx[3] = 0x6b206574;
            for (int i = 4; i < 16; i++) {
                s_avx[i] = xorshift32(&rng);
            }
            memcpy(s_scalar, s_avx, sizeof(s_avx));

            uint8_t out_avx[512];
            uint8_t out_scalar[512];
            memset(out_avx, 0, sizeof(out_avx));
            memset(out_scalar, 0, sizeof(out_scalar));

            kheiron_chacha20_blocks_avx2(s_avx, out_avx, 8);
            reference_scalar_blocks(s_scalar, out_scalar, 8);

            if (memcmp(out_avx, out_scalar, 512) != 0) {
                fprintf(stderr, "FAIL: Differential divergence at trial %d!\n", trial);
                return 1;
            }
            if (memcmp(s_avx, s_scalar, sizeof(s_avx)) != 0) {
                fprintf(stderr, "FAIL: State counter divergence at trial %d!\n", trial);
                return 1;
            }
            if ((trial + 1) % 5000 == 0) {
                printf("    Completed %d / 25000 trials...\n", trial + 1);
            }
        }
        printf("    -> 25,000 randomized states bit-for-bit identical.\n");
    }

    /* 3.3 Exhaustive Counter Wraparound Oracle across all block boundary positions */
    printf("[3/6] Exhaustive counter wrap-around oracle (all 8 block alignment positions)...\n");
    {
        /* Test wraparound starting at offsets from 0xFFFFFFE0 to 0x00000010 */
        for (uint64_t start = 0xFFFFFFE0ULL; start <= 0x100000010ULL; start++) {
            uint32_t s_avx[16], s_scalar[16];
            s_avx[0] = 0x61707865; s_avx[1] = 0x3320646e;
            s_avx[2] = 0x79622d32; s_avx[3] = 0x6b206574;
            for (int i = 4; i < 12; i++) s_avx[i] = 0x02020202U * (uint32_t)i;
            s_avx[12] = (uint32_t)start;
            s_avx[13] = 0x70000000U;
            s_avx[14] = 0xAAAAAAAAU;
            s_avx[15] = 0x55555555U;
            memcpy(s_scalar, s_avx, sizeof(s_avx));

            uint8_t out_avx[512 * 2];
            uint8_t out_scalar[512 * 2];

            kheiron_chacha20_blocks_avx2(s_avx, out_avx, 16);
            reference_scalar_blocks(s_scalar, out_scalar, 16);

            if (memcmp(out_avx, out_scalar, sizeof(out_avx)) != 0) {
                fprintf(stderr, "FAIL: Wraparound mismatch starting at counter 0x%08X!\n", (uint32_t)start);
                return 1;
            }
            if (s_avx[12] != s_scalar[12] || s_avx[13] != s_scalar[13]) {
                fprintf(stderr, "FAIL: Counter mismatch after wrap! AVX2=(0x%08X, 0x%08X) vs Scalar=(0x%08X, 0x%08X)\n",
                        s_avx[12], s_avx[13], s_scalar[12], s_scalar[13]);
                return 1;
            }
        }
        printf("    -> Exhaustive counter wrap across 2^32-1 boundary bit-for-bit identical.\n");
    }

    /* 3.4 Exhaustive Single-Chunk Differential Stream (1 to 4096 bytes) */
    printf("[4/6] Exhaustive single-chunk differential stream (1..4096 bytes)...\n");
    {
        for (size_t sz = 1; sz <= 4096; sz++) {
            kheiron_ctx_t ctx_avx, ctx_scalar;
            ctx_avx.state[0] = 0x61707865; ctx_avx.state[1] = 0x3320646e;
            ctx_avx.state[2] = 0x79622d32; ctx_avx.state[3] = 0x6b206574;
            for (int i = 4; i < 16; i++) ctx_avx.state[i] = (uint32_t)i * 0x013579BDU;
            ctx_avx.bytes_generated = 0;
            memcpy(&ctx_scalar, &ctx_avx, sizeof(ctx_avx));

            uint8_t *buf_avx = (uint8_t *)malloc(sz);
            uint8_t *buf_scalar = (uint8_t *)malloc(sz);
            assert(buf_avx && buf_scalar);

            kheiron_ctx_fill(&ctx_avx, buf_avx, sz);
            reference_scalar_ctx_fill(&ctx_scalar, buf_scalar, sz);

            if (memcmp(buf_avx, buf_scalar, sz) != 0) {
                fprintf(stderr, "FAIL: Streaming divergence for single chunk size %zu!\n", sz);
                free(buf_avx);
                free(buf_scalar);
                return 1;
            }
            if (memcmp(ctx_avx.state, ctx_scalar.state, sizeof(ctx_avx.state)) != 0) {
                fprintf(stderr, "FAIL: Final state mismatch after chunk size %zu!\n", sz);
                free(buf_avx);
                free(buf_scalar);
                return 1;
            }
            if (ctx_avx.bytes_generated != ctx_scalar.bytes_generated) {
                fprintf(stderr, "FAIL: bytes_generated mismatch (%zu vs %zu) after size %zu!\n",
                        ctx_avx.bytes_generated, ctx_scalar.bytes_generated, sz);
                free(buf_avx);
                free(buf_scalar);
                return 1;
            }

            free(buf_avx);
            free(buf_scalar);
        }
        printf("    -> Single-chunk sizes 1..4096 bytes PASSED.\n");
    }

    /* 3.5 Randomized Multi-Call Streaming (3,000 requests, up to 8192B each) */
    printf("[5/6] Randomized multi-call streaming (3,000 calls, ~12 MB total)...\n");
    {
        kheiron_ctx_t ctx_avx, ctx_scalar;
        ctx_avx.state[0] = 0x61707865; ctx_avx.state[1] = 0x3320646e;
        ctx_avx.state[2] = 0x79622d32; ctx_avx.state[3] = 0x6b206574;
        for (int i = 4; i < 16; i++) ctx_avx.state[i] = (uint32_t)i * 0x0FEDCBA9U;
        ctx_avx.bytes_generated = 0;
        memcpy(&ctx_scalar, &ctx_avx, sizeof(ctx_avx));

        uint32_t rng = 0xFEEDFACE;
        size_t total_streamed = 0;
        for (int req = 0; req < 3000; req++) {
            size_t req_sz = 1 + (xorshift32(&rng) % 8192);

            /* Simulate synchronized key rotation */
            if (ctx_avx.bytes_generated + req_sz >= 1048000) {
                ctx_avx.bytes_generated = 0;
                ctx_scalar.bytes_generated = 0;
                ctx_avx.state[12] ^= 0xABCDE;
                ctx_scalar.state[12] ^= 0xABCDE;
            }

            uint8_t *b_avx = (uint8_t *)malloc(req_sz);
            uint8_t *b_scalar = (uint8_t *)malloc(req_sz);
            assert(b_avx && b_scalar);

            kheiron_ctx_fill(&ctx_avx, b_avx, req_sz);
            reference_scalar_ctx_fill(&ctx_scalar, b_scalar, req_sz);

            if (memcmp(b_avx, b_scalar, req_sz) != 0) {
                fprintf(stderr, "FAIL: Multi-call stream divergence at req %d (size %zu)!\n", req, req_sz);
                free(b_avx);
                free(b_scalar);
                return 1;
            }
            if (memcmp(ctx_avx.state, ctx_scalar.state, sizeof(ctx_avx.state)) != 0) {
                fprintf(stderr, "FAIL: Multi-call state divergence at req %d!\n", req);
                free(b_avx);
                free(b_scalar);
                return 1;
            }

            total_streamed += req_sz;
            free(b_avx);
            free(b_scalar);
        }
        printf("    -> 3,000 multi-call chunks (%zu bytes) bit-for-bit identical.\n", total_streamed);
    }

    /* 3.6 Contiguous 10 MB Stream Verification */
    printf("[6/6] Contiguous 10 MB stream equivalence (163,840 ChaCha20 blocks)...\n");
    {
        size_t n_blocks = 163840;
        size_t chunk_blocks = 1024;
        size_t chunk_bytes = chunk_blocks * 64;

        uint32_t s_avx[16], s_scalar[16];
        s_avx[0] = 0x61707865; s_avx[1] = 0x3320646e;
        s_avx[2] = 0x79622d32; s_avx[3] = 0x6b206574;
        for (int i = 4; i < 16; i++) s_avx[i] = (uint32_t)i * 0x02468ACEU;
        memcpy(s_scalar, s_avx, sizeof(s_avx));

        uint8_t *buf_avx = (uint8_t *)malloc(chunk_bytes);
        uint8_t *buf_scalar = (uint8_t *)malloc(chunk_bytes);
        assert(buf_avx && buf_scalar);

        size_t done = 0;
        while (done < n_blocks) {
            size_t cur = (n_blocks - done < chunk_blocks) ? (n_blocks - done) : chunk_blocks;
            kheiron_chacha20_blocks_avx2(s_avx, buf_avx, cur);
            reference_scalar_blocks(s_scalar, buf_scalar, cur);

            if (memcmp(buf_avx, buf_scalar, cur * 64) != 0) {
                fprintf(stderr, "FAIL: 10 MB contiguous stream divergence at block %zu!\n", done);
                free(buf_avx);
                free(buf_scalar);
                return 1;
            }
            done += cur;
        }

        if (s_avx[12] != s_scalar[12] || s_avx[13] != s_scalar[13]) {
            fprintf(stderr, "FAIL: Final state mismatch on 10 MB stream!\n");
            free(buf_avx);
            free(buf_scalar);
            return 1;
        }

        free(buf_avx);
        free(buf_scalar);
        printf("    -> 10 MB stream bit-for-bit identical between AVX2 and scalar.\n");
    }

#endif
    return 0;
}

/* ========================================================================= */
/* SECTION 4: Context Fork & Memory Protection Verification                  */
/* ========================================================================= */

static int run_context_fork_safety_suite(void) {
    printf("=== [CHALLENGER M1 Iter 2] Section 4: Context Fork & Memory Protection Verification ===\n");

    /* 4.1 Verify stack-allocated kheiron_protect_memory no-op behavior */
    {
        uint8_t stack_dummy[64];
        /* Must be safely ignored (no madvise called on unaligned/subpage stack) */
        kheiron_protect_memory(stack_dummy, sizeof(stack_dummy));
        printf("[+] kheiron_protect_memory safely ignored unaligned sub-page address.\n");
    }

    /* 4.2 Multi-fork stack context stress test (10 rapid stack forks) */
    printf("[*] Running 10 rapid forks with stack-allocated kheiron_ctx_t...\n");
    for (int i = 0; i < 10; i++) {
        int fds[2];
        assert(pipe(fds) == 0);
        kheiron_ctx_t stack_ctx;
        uint8_t seed[32] = { (uint8_t)i, (uint8_t)(i + 1), (uint8_t)(i + 2), (uint8_t)(i + 3) };
        assert(kheiron_ctx_init(&stack_ctx, seed, sizeof(seed)) == 0);

        pid_t pid = fork();
        if (pid == 0) {
            close(fds[0]);
            uint8_t child_buf[128];
            int rc = kheiron_ctx_fill(&stack_ctx, child_buf, sizeof(child_buf));
            kheiron_ctx_wipe(&stack_ctx);
            ssize_t w = write(fds[1], child_buf, sizeof(child_buf));
            (void)w;
            close(fds[1]);
            exit(rc == 0 ? 0 : 1);
        } else if (pid > 0) {
            close(fds[1]);
            uint8_t child_buf[128];
            ssize_t r = read(fds[0], child_buf, sizeof(child_buf));
            close(fds[0]);
            int status = 0;
            waitpid(pid, &status, 0);
            assert(!WIFSIGNALED(status));
            assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
            assert(r == (ssize_t)sizeof(child_buf));
            kheiron_ctx_wipe(&stack_ctx);
        } else {
            fprintf(stderr, "FAIL: fork failed\n");
            return 1;
        }
    }
    printf("    -> 10 stack-allocated context forks executed cleanly.\n");

    return 0;
}

int main(void) {
    printf("===================================================================\n");
    printf("  KHEIRON-HYDROGEN ADVERSARIAL CHALLENGER M1 ITERATION 2 SUITE     \n");
    printf("  Fuzzing, Unaligned Offsets, Differential Oracle & Fork Safety    \n");
    printf("===================================================================\n\n");

    if (run_buffer_fuzz_suite() != 0) {
        fprintf(stderr, "\n[-] FAILED in Section 1: Buffer Fuzzing Suite\n");
        return 1;
    }
    printf("\n");

    if (run_unaligned_suite() != 0) {
        fprintf(stderr, "\n[-] FAILED in Section 2: Unaligned Offsets Suite\n");
        return 1;
    }
    printf("\n");

    if (run_differential_oracle_suite() != 0) {
        fprintf(stderr, "\n[-] FAILED in Section 3: Differential Oracle Suite\n");
        return 1;
    }
    printf("\n");

    if (run_context_fork_safety_suite() != 0) {
        fprintf(stderr, "\n[-] FAILED in Section 4: Context Fork Safety Suite\n");
        return 1;
    }
    printf("\n");

    printf("===================================================================\n");
    printf("  ALL EMPIRICAL CHALLENGES IN M1 ITERATION 2 PASSED CLEANLY!       \n");
    printf("===================================================================\n");
    return 0;
}
