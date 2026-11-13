#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include "../kheiron.h"

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

int main(void) {
    printf("=== [CHALLENGER M1-1] Stream Equivalence Differential Oracle (AVX2 vs Scalar) ===\n");

#if !KHEIRON_HAVE_AVX2
    printf("NOTICE: KHEIRON_HAVE_AVX2 is 0. Running scalar-only verification.\n");
#else
    if (!kheiron_cpu_has_avx2()) {
        printf("NOTICE: CPU does not support AVX2. Skipping AVX2-specific checks.\n");
        return 0;
    }

    printf("[1/5] Verifying RFC 7539 official test vector in AVX2...\n");
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
            0x00000001,
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
            fprintf(stderr, "FAIL: Scalar ChaCha20 does not match RFC 7539 test vector!\n");
            return 1;
        }

        uint8_t avx2_8blocks[512];
        uint32_t state_copy[16];
        memcpy(state_copy, rfc_state, sizeof(state_copy));
        kheiron_chacha20_blocks_avx2(state_copy, avx2_8blocks, 8);
        if (memcmp(avx2_8blocks, rfc_expected, 64) != 0) {
            fprintf(stderr, "FAIL: AVX2 ChaCha20 Block 0 does not match RFC 7539 test vector!\n");
            return 1;
        }
        printf("      -> RFC 7539 test vector bit-exact match in AVX2.\n");
    }

    printf("[2/5] Testing 10,000 randomized states (8 blocks / 512B each)...\n");
    {
        uint32_t rng = 0xDEADBEEF;
        for (int trial = 0; trial < 10000; trial++) {
            uint32_t s_avx[16], s_scalar[16];
            s_avx[0] = 0x61707865;
            s_avx[1] = 0x3320646e;
            s_avx[2] = 0x79622d32;
            s_avx[3] = 0x6b206574;
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
                for (int b = 0; b < 512; b++) {
                    if (out_avx[b] != out_scalar[b]) {
                        fprintf(stderr, "      Byte offset %d: AVX2=0x%02X vs Scalar=0x%02X\n",
                                b, out_avx[b], out_scalar[b]);
                        break;
                    }
                }
                return 1;
            }

            if (memcmp(s_avx, s_scalar, sizeof(s_avx)) != 0) {
                fprintf(stderr, "FAIL: State counter divergence after 8 blocks at trial %d!\n", trial);
                return 1;
            }
        }
        printf("      -> 10,000 random states verified bit-for-bit identical.\n");
    }

    printf("[3/5] Testing counter wrap across 2^32-1 boundary...\n");
    {
        uint32_t wrap_offsets[] = {
            0xFFFFFFF0U, 0xFFFFFFF7U, 0xFFFFFFF8U, 0xFFFFFFF9U,
            0xFFFFFFFAU, 0xFFFFFFFBU, 0xFFFFFFFCU, 0xFFFFFFFDU,
            0xFFFFFFFEU, 0xFFFFFFFFU, 0x00000000U, 0x00000001U
        };
        size_t n_wraps = sizeof(wrap_offsets) / sizeof(wrap_offsets[0]);

        for (size_t w = 0; w < n_wraps; w++) {
            uint32_t s_avx[16], s_scalar[16];
            s_avx[0] = 0x61707865; s_avx[1] = 0x3320646e;
            s_avx[2] = 0x79622d32; s_avx[3] = 0x6b206574;
            for (int i = 4; i < 12; i++) s_avx[i] = 0x01010101U * (uint32_t)i;
            s_avx[12] = wrap_offsets[w];
            s_avx[13] = 0x12345678U;
            s_avx[14] = 0xAAAAAAAAU;
            s_avx[15] = 0x55555555U;
            memcpy(s_scalar, s_avx, sizeof(s_avx));

            uint8_t out_avx[512 * 4];
            uint8_t out_scalar[512 * 4];

            kheiron_chacha20_blocks_avx2(s_avx, out_avx, 32);
            reference_scalar_blocks(s_scalar, out_scalar, 32);

            if (memcmp(out_avx, out_scalar, sizeof(out_avx)) != 0) {
                fprintf(stderr, "FAIL: Wraparound divergence at start counter 0x%08X!\n", wrap_offsets[w]);
                return 1;
            }
            if (s_avx[12] != s_scalar[12] || s_avx[13] != s_scalar[13]) {
                fprintf(stderr, "FAIL: Wraparound counter state divergence: AVX2=(%u, %u) vs Scalar=(%u, %u)\n",
                        s_avx[12], s_avx[13], s_scalar[12], s_scalar[13]);
                return 1;
            }
        }
        printf("      -> All counter wraparound scenarios bit-for-bit identical.\n");
    }

    printf("[4/5] Testing streaming differential equivalence across arbitrary chunk sizes...\n");
    {

        for (size_t sz = 1; sz <= 2048; sz++) {
            kheiron_ctx_t ctx_avx, ctx_scalar;
            ctx_avx.state[0] = 0x61707865; ctx_avx.state[1] = 0x3320646e;
            ctx_avx.state[2] = 0x79622d32; ctx_avx.state[3] = 0x6b206574;
            for (int i = 4; i < 16; i++) ctx_avx.state[i] = (uint32_t)i * 0x01234567U;
            ctx_avx.bytes_generated = 0;
            memcpy(&ctx_scalar, &ctx_avx, sizeof(ctx_avx));

            uint8_t *buf_avx = (uint8_t *)malloc(sz);
            uint8_t *buf_scalar = (uint8_t *)malloc(sz);
            assert(buf_avx && buf_scalar);

            kheiron_ctx_fill(&ctx_avx, buf_avx, sz);
            reference_scalar_ctx_fill(&ctx_scalar, buf_scalar, sz);

            if (memcmp(buf_avx, buf_scalar, sz) != 0) {
                fprintf(stderr, "FAIL: Streaming equivalence failed for single chunk of size %zu\n", sz);
                free(buf_avx);
                free(buf_scalar);
                return 1;
            }
            if (memcmp(ctx_avx.state, ctx_scalar.state, sizeof(ctx_avx.state)) != 0) {
                fprintf(stderr, "FAIL: Final state mismatch after chunk size %zu\n", sz);
                free(buf_avx);
                free(buf_scalar);
                return 1;
            }
            if (ctx_avx.bytes_generated != ctx_scalar.bytes_generated) {
                fprintf(stderr, "FAIL: bytes_generated mismatch (%zu vs %zu) after size %zu\n",
                        ctx_avx.bytes_generated, ctx_scalar.bytes_generated, sz);
                free(buf_avx);
                free(buf_scalar);
                return 1;
            }

            free(buf_avx);
            free(buf_scalar);
        }
        printf("      -> Exhaustive single-chunk sizes 1..2048 bytes PASSED.\n");

        kheiron_ctx_t ctx_avx, ctx_scalar;
        ctx_avx.state[0] = 0x61707865; ctx_avx.state[1] = 0x3320646e;
        ctx_avx.state[2] = 0x79622d32; ctx_avx.state[3] = 0x6b206574;
        for (int i = 4; i < 16; i++) ctx_avx.state[i] = (uint32_t)i * 0x07654321U;
        ctx_avx.bytes_generated = 0;
        memcpy(&ctx_scalar, &ctx_avx, sizeof(ctx_avx));

        uint32_t rng = 0xCAFEBABE;
        size_t total_streamed = 0;
        for (int req = 0; req < 2000; req++) {

            size_t req_sz = 1 + (xorshift32(&rng) % 4096);

            if (ctx_avx.bytes_generated + req_sz >= 1048000) {

                ctx_avx.bytes_generated = 0;
                ctx_scalar.bytes_generated = 0;
                ctx_avx.state[12] ^= 0x12345;
                ctx_scalar.state[12] ^= 0x12345;
            }

            uint8_t *b_avx = (uint8_t *)malloc(req_sz);
            uint8_t *b_scalar = (uint8_t *)malloc(req_sz);
            assert(b_avx && b_scalar);

            kheiron_ctx_fill(&ctx_avx, b_avx, req_sz);
            reference_scalar_ctx_fill(&ctx_scalar, b_scalar, req_sz);

            if (memcmp(b_avx, b_scalar, req_sz) != 0) {
                fprintf(stderr, "FAIL: Multi-call stream divergence at request %d (size %zu)!\n",
                        req, req_sz);
                free(b_avx);
                free(b_scalar);
                return 1;
            }
            if (memcmp(ctx_avx.state, ctx_scalar.state, sizeof(ctx_avx.state)) != 0) {
                fprintf(stderr, "FAIL: Multi-call state divergence at request %d!\n", req);
                free(b_avx);
                free(b_scalar);
                return 1;
            }

            total_streamed += req_sz;
            free(b_avx);
            free(b_scalar);
        }
        printf("      -> 2,000 multi-call randomized stream chunks (%zu bytes total) bit-for-bit identical.\n",
               total_streamed);
    }

    printf("[5/5] Testing 10 MB contiguous stream equivalence...\n");
    {

        size_t n_blocks = 163840;
        size_t chunk_blocks = 800;
        size_t chunk_bytes = chunk_blocks * 64;

        uint32_t s_avx[16], s_scalar[16];
        s_avx[0] = 0x61707865; s_avx[1] = 0x3320646e;
        s_avx[2] = 0x79622d32; s_avx[3] = 0x6b206574;
        for (int i = 4; i < 16; i++) s_avx[i] = (uint32_t)i * 0x09876543U;
        memcpy(s_scalar, s_avx, sizeof(s_avx));

        uint8_t *buf_avx = (uint8_t *)malloc(chunk_bytes);
        uint8_t *buf_scalar = (uint8_t *)malloc(chunk_bytes);
        assert(buf_avx && buf_scalar);

        size_t blocks_done = 0;
        while (blocks_done < n_blocks) {
            size_t cur = (n_blocks - blocks_done < chunk_blocks) ? (n_blocks - blocks_done) : chunk_blocks;
            kheiron_chacha20_blocks_avx2(s_avx, buf_avx, cur);
            reference_scalar_blocks(s_scalar, buf_scalar, cur);

            if (memcmp(buf_avx, buf_scalar, cur * 64) != 0) {
                fprintf(stderr, "FAIL: 10 MB stream divergence at block %zu!\n", blocks_done);
                free(buf_avx);
                free(buf_scalar);
                return 1;
            }
            blocks_done += cur;
        }

        if (s_avx[12] != s_scalar[12] || s_avx[13] != s_scalar[13]) {
            fprintf(stderr, "FAIL: Final 10 MB counter mismatch!\n");
            free(buf_avx);
            free(buf_scalar);
            return 1;
        }

        free(buf_avx);
        free(buf_scalar);
        printf("      -> 10 MB (163,840 blocks) stream bit-for-bit identical between AVX2 and scalar.\n");
    }

#endif

    printf("=== ALL STREAM EQUIVALENCE DIFFERENTIAL ORACLE TESTS PASSED ===\n");
    return 0;
}
