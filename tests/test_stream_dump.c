#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../kheiron.h"

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    /* Fixed deterministic state */
    kheiron_ctx_t ctx;
    ctx.state[0] = 0x61707865; ctx.state[1] = 0x3320646e;
    ctx.state[2] = 0x79622d32; ctx.state[3] = 0x6b206574;
    for (int i = 4; i < 16; i++) {
        ctx.state[i] = (uint32_t)i * 0x05A5A5A5U;
    }
    ctx.bytes_generated = 0;

    /* Fuzzed pattern of chunk sizes totaling ~10 MB */
    size_t chunk_patterns[] = {
        1, 2, 3, 7, 15, 31, 63, 64, 65, 127, 128, 129,
        511, 512, 513, 700, 1023, 1024, 1025, 1536, 4096, 8192,
        65536, 131072, 262144
    };
    size_t num_patterns = sizeof(chunk_patterns) / sizeof(chunk_patterns[0]);

    uint32_t h1 = 0x811c9dc5U; /* FNV-1a 32-bit */
    uint64_t total_bytes = 0;

    for (int rep = 0; rep < 20; rep++) {
        for (size_t p = 0; p < num_patterns; p++) {
            size_t sz = chunk_patterns[p];
            /* Stay within epoch to avoid non-deterministic rdtsc rotation */
            if (ctx.bytes_generated + sz >= 1048000) {
                ctx.bytes_generated = 0;
                ctx.state[12] ^= 0x55AA55AAU;
                ctx.state[13] += 1;
            }

            uint8_t *buf = (uint8_t *)malloc(sz);
            if (!buf) return 1;
            kheiron_ctx_fill(&ctx, buf, sz);

            for (size_t i = 0; i < sz; i++) {
                h1 ^= buf[i];
                h1 *= 0x01000193U;
            }
            total_bytes += sz;
            free(buf);
        }
    }

    printf("TOTAL_BYTES: %zu | HASH: 0x%08X | FINAL_STATE_12: 0x%08X | FINAL_STATE_13: 0x%08X\n",
           total_bytes, h1, ctx.state[12], ctx.state[13]);
    return 0;
}
