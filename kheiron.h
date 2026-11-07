#ifndef KHEIRON_H
#define KHEIRON_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L) && !defined(__STDC_NO_THREADS__)
    #define KHEIRON_THREAD_LOCAL _Thread_local
#elif defined(__GNUC__) || defined(__clang__)
    #define KHEIRON_THREAD_LOCAL __thread
#elif defined(_MSC_VER)
    #define KHEIRON_THREAD_LOCAL __declspec(thread)
#else
    #define KHEIRON_THREAD_LOCAL
#endif

typedef struct kheiron_ctx {
    uint32_t state[16];
    size_t bytes_generated;
} kheiron_ctx_t;

#ifndef KHEIRON_API
#if defined(KHEIRON_STATIC)
#define KHEIRON_API static inline
#else
#define KHEIRON_API
#endif
#endif
KHEIRON_API int kheiron_selftest(void);
KHEIRON_API int kheiron_ctx_init(kheiron_ctx_t *ctx, const void *seed, size_t seed_len);
KHEIRON_API int kheiron_ctx_fill(kheiron_ctx_t *ctx, void *buf, size_t len);
KHEIRON_API void kheiron_ctx_wipe(kheiron_ctx_t *ctx);
KHEIRON_API int kheiron_derive(kheiron_ctx_t *ctx, void *out, size_t out_len, const void *info, size_t info_len);

KHEIRON_API uint64_t kheiron_rdtsc(void);
KHEIRON_API uint64_t kheiron_jitter_sample(void);
KHEIRON_API int kheiron_fill(void *buf, size_t len);
KHEIRON_API int kheiron_string(char *dest, size_t len, const char *charset);
KHEIRON_API uint64_t kheiron_range_u64(uint64_t min, uint64_t max);
KHEIRON_API double kheiron_uniform_double(void);
KHEIRON_API float kheiron_uniform_float(void);
KHEIRON_API int kheiron_shuffle(void *base, size_t nmemb, size_t size);
KHEIRON_API double kheiron_gaussian(double mean, double stddev);
KHEIRON_API int kheiron_reseed(const void *user_entropy, size_t len);
KHEIRON_API void kheiron_secure_wipe(void *v, size_t n);
KHEIRON_API int kheiron_os_entropy(void *buf, size_t len);
KHEIRON_API int kheiron_consttime_cmp(const void *a, const void *b, size_t len);
KHEIRON_API uint32_t kheiron_consttime_select_u32(uint32_t mask, uint32_t a, uint32_t b);

#if defined(KHEIRON_IMPLEMENTATION) || defined(KHEIRON_STATIC) || !defined(KHEIRON_HEADER_ONLY)

#include <math.h>

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
#ifndef KHEIRON_NO_OS_ENTROPY
    #include <bcrypt.h>
    #pragma comment(lib, "bcrypt.lib")
#endif
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
    #include <stdlib.h>
    #include <unistd.h>
    #include <sys/mman.h>
    #include <pthread.h>
#else
    #if !defined(_GNU_SOURCE)
        #define _GNU_SOURCE
    #endif
    #if !defined(_POSIX_C_SOURCE)
        #define _POSIX_C_SOURCE 200809L
    #endif
    #include <unistd.h>
    #include <fcntl.h>
    #include <sys/syscall.h>
    #include <sys/mman.h>
    #include <pthread.h>
    #if defined(__has_include)
        #if __has_include(<sys/random.h>)
            #include <sys/random.h>
        #endif
    #endif
#endif

KHEIRON_API int kheiron_os_entropy(void *buf, size_t len) {
    if (!buf || len == 0) {
        return 0;
    }

    uint8_t *p = (uint8_t *)buf;

#ifndef KHEIRON_NO_OS_ENTROPY
    size_t acquired = 0;

#if defined(_WIN32) || defined(_WIN64)
    if (BCryptGenRandom(NULL, p, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0) {
        return 0;
    }
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
    arc4random_buf(p, len);
    return 0;
#else
#if defined(SYS_getrandom) || defined(GRND_NONBLOCK)
    while (acquired < len) {
        ssize_t ret;
#if defined(GRND_NONBLOCK)
        ret = getrandom(p + acquired, len - acquired, GRND_NONBLOCK);
#else
        ret = syscall(SYS_getrandom, p + acquired, len - acquired, 1);
#endif
        if (ret > 0) {
            acquired += (size_t)ret;
        } else {
            break;
        }
    }
#endif

    if (acquired < len) {
#if defined(O_CLOEXEC)
        int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC | O_NONBLOCK);
#else
        int fd = open("/dev/urandom", O_RDONLY | O_NONBLOCK);
#endif
        if (fd >= 0) {
            while (acquired < len) {
                ssize_t ret = read(fd, p + acquired, len - acquired);
                if (ret > 0) {
                    acquired += (size_t)ret;
                } else {
                    break;
                }
            }
            close(fd);
        }
    }

    if (acquired >= len) {
        return 0;
    }
    p += acquired;
    len -= acquired;
#endif
#endif

    while (len > 0) {
        uint64_t sample = kheiron_jitter_sample();
        uint64_t t = kheiron_rdtsc();
        sample ^= (t + (t << 13));
        size_t chunk = len < sizeof(sample) ? len : sizeof(sample);
        memcpy(p, &sample, chunk);
        p += chunk;
        len -= chunk;
    }

    return 0;
}

KHEIRON_API uint64_t kheiron_rdtsc(void) {
#if defined(__x86_64__)
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
#elif defined(__aarch64__)
    uint64_t val;
    __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(val));
    return val;
#else
    return 0ULL;
#endif
}

KHEIRON_API uint64_t kheiron_jitter_sample(void) {
    uint64_t res = 0;
    for (int i = 0; i < 64; i++) {
        uint64_t t0 = kheiron_rdtsc();
        for (volatile int j = 0; j < 8; j++) {
            __asm__ __volatile__("" ::: "memory");
        }
        uint64_t t1 = kheiron_rdtsc();
        uint64_t bit = (t1 - t0) & 1ULL;
        res = (res << 1) | bit;
    }
    return res;
}

KHEIRON_API void kheiron_secure_wipe(void *v, size_t n) {
    if (!v || n == 0) {
        return;
    }
    volatile uint8_t *p = (volatile uint8_t *)v;
    while (n--) {
        *p++ = 0;
    }
    __asm__ __volatile__("" : : "r"(p) : "memory");
}

static inline uint32_t kheiron_rotl32(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

static inline void kheiron_chacha_qr(uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    *a += *b; *d ^= *a; *d = kheiron_rotl32(*d, 16);
    *c += *d; *b ^= *c; *b = kheiron_rotl32(*b, 12);
    *a += *b; *d ^= *a; *d = kheiron_rotl32(*d, 8);
    *c += *d; *b ^= *c; *b = kheiron_rotl32(*b, 7);
}

static inline void kheiron_chacha_block(const uint32_t in[16], uint8_t out[64]) {
    uint32_t x[16];
    for (int i = 0; i < 16; i++) {
        x[i] = in[i];
    }

    for (int i = 0; i < 10; i++) {
        kheiron_chacha_qr(&x[0], &x[4], &x[8], &x[12]);
        kheiron_chacha_qr(&x[1], &x[5], &x[9], &x[13]);
        kheiron_chacha_qr(&x[2], &x[6], &x[10], &x[14]);
        kheiron_chacha_qr(&x[3], &x[7], &x[11], &x[15]);

        kheiron_chacha_qr(&x[0], &x[5], &x[10], &x[15]);
        kheiron_chacha_qr(&x[1], &x[6], &x[11], &x[12]);
        kheiron_chacha_qr(&x[2], &x[7], &x[8], &x[13]);
        kheiron_chacha_qr(&x[3], &x[4], &x[9], &x[14]);
    }

    for (int i = 0; i < 16; i++) {
        uint32_t val = in[i] + x[i];
        out[i * 4 + 0] = (uint8_t)(val & 0xFF);
        out[i * 4 + 1] = (uint8_t)((val >> 8) & 0xFF);
        out[i * 4 + 2] = (uint8_t)((val >> 16) & 0xFF);
        out[i * 4 + 3] = (uint8_t)((val >> 24) & 0xFF);
    }

    kheiron_secure_wipe(x, sizeof(x));
}

static inline void kheiron_protect_memory(void *addr, size_t len) {
#if defined(_WIN32) || defined(_WIN64)
    VirtualLock(addr, len);
#else
#if defined(MADV_WIPEONFORK) || defined(MADV_DONTDUMP)
    if (addr && len > 0) {
        long page_size = sysconf(_SC_PAGESIZE);
        if (page_size > 0) {
            uintptr_t start = (uintptr_t)addr;
            uintptr_t end = start + len;
            uintptr_t page_start = start & ~((uintptr_t)page_size - 1);
            uintptr_t page_end = (end + (uintptr_t)page_size - 1) & ~((uintptr_t)page_size - 1);
            size_t page_len = (size_t)(page_end - page_start);
#if defined(MADV_WIPEONFORK)
            (void)madvise((void *)page_start, page_len, MADV_WIPEONFORK);
#endif
#if defined(MADV_DONTDUMP)
            (void)madvise((void *)page_start, page_len, MADV_DONTDUMP);
#endif
        }
    }
#else
    (void)addr;
    (void)len;
#endif
#endif
}

static KHEIRON_THREAD_LOCAL uint32_t kheiron_state[16];
static KHEIRON_THREAD_LOCAL int kheiron_initialized = 0;
static KHEIRON_THREAD_LOCAL size_t kheiron_bytes_generated = 0;
#if !defined(_WIN32) && !defined(_WIN64)
static int kheiron_atfork_registered = 0;
#endif

static void kheiron_atfork_child(void) {
    kheiron_initialized = 0;
    kheiron_secure_wipe(kheiron_state, sizeof(kheiron_state));
    uint8_t child_entropy[32];
    kheiron_os_entropy(child_entropy, sizeof(child_entropy));
    uint64_t t = kheiron_rdtsc();
    for (int i = 0; i < 8; i++) {
        child_entropy[i] ^= (uint8_t)((t >> (i * 8)) & 0xFF);
    }
    kheiron_reseed(child_entropy, sizeof(child_entropy));
    kheiron_secure_wipe(child_entropy, sizeof(child_entropy));
}

KHEIRON_API int kheiron_selftest(void) {
    const uint32_t rfc_state[16] = {
        0x61707865, 0x3320646e, 0x79622d32, 0x6b206574,
        0x03020100, 0x07060504, 0x0b0a0908, 0x0f0e0d0c,
        0x13121110, 0x17161514, 0x1b1a1918, 0x1f1e1d1c,
        0x00000001, 0x09000000, 0x4a000000, 0x00000000
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
    uint8_t rfc_out[64];
    kheiron_chacha_block(rfc_state, rfc_out);
    if (kheiron_consttime_cmp(rfc_out, rfc_expected, sizeof(rfc_out)) != 0) {
        kheiron_secure_wipe(rfc_out, sizeof(rfc_out));
        return -1;
    }
    kheiron_secure_wipe(rfc_out, sizeof(rfc_out));

    uint8_t wipe_test[64];
    for (size_t i = 0; i < sizeof(wipe_test); i++) {
        wipe_test[i] = (uint8_t)(i + 1);
    }
    kheiron_secure_wipe(wipe_test, sizeof(wipe_test));
    for (size_t i = 0; i < sizeof(wipe_test); i++) {
        if (wipe_test[i] != 0) {
            return -2;
        }
    }

    return 0;
}

static inline void kheiron_init_state(void) {
    kheiron_state[0] = 0x61707865;
    kheiron_state[1] = 0x3320646e;
    kheiron_state[2] = 0x79622d32;
    kheiron_state[3] = 0x6b206574;

    uint8_t os_seed[32];
    kheiron_os_entropy(os_seed, sizeof(os_seed));
    for (int i = 0; i < 8; i++) {
        uint32_t val = (uint32_t)os_seed[i * 4 + 0] |
                       ((uint32_t)os_seed[i * 4 + 1] << 8) |
                       ((uint32_t)os_seed[i * 4 + 2] << 16) |
                       ((uint32_t)os_seed[i * 4 + 3] << 24);
        kheiron_state[4 + i] = val;
    }
    kheiron_secure_wipe(os_seed, sizeof(os_seed));

    for (int i = 0; i < 4; i++) {
        uint64_t sample = kheiron_jitter_sample();
        kheiron_state[4 + i * 2] ^= (uint32_t)(sample & 0xFFFFFFFF);
        kheiron_state[4 + i * 2 + 1] ^= (uint32_t)(sample >> 32);
    }

    uint64_t t1 = kheiron_rdtsc();
    uint64_t t2 = kheiron_rdtsc();
    kheiron_state[12] = (uint32_t)(t1 & 0xFFFFFFFF);
    kheiron_state[13] = (uint32_t)(t1 >> 32);
    kheiron_state[14] = (uint32_t)(t2 & 0xFFFFFFFF);
    kheiron_state[15] = (uint32_t)(t2 >> 32);

#if !defined(_WIN32) && !defined(_WIN64)
    if (!kheiron_atfork_registered) {
        if (pthread_atfork(NULL, NULL, kheiron_atfork_child) == 0) {
            kheiron_atfork_registered = 1;
        }
    }
#endif

    kheiron_initialized = 1;
    kheiron_bytes_generated = 0;
}

static inline void kheiron_rotate_key(void) {
    uint64_t t = kheiron_rdtsc();
    kheiron_state[12] ^= (uint32_t)(t & 0xFFFFFFFF);
    kheiron_state[13] ^= (uint32_t)(t >> 32);

    uint8_t block[64];
    kheiron_chacha_block(kheiron_state, block);

    for (int i = 0; i < 8; i++) {
        uint32_t k = (uint32_t)block[i * 4 + 0] |
                     ((uint32_t)block[i * 4 + 1] << 8) |
                     ((uint32_t)block[i * 4 + 2] << 16) |
                     ((uint32_t)block[i * 4 + 3] << 24);
        kheiron_state[4 + i] ^= k;
    }

    kheiron_protect_memory(block, sizeof(block));
    kheiron_state[14] += (uint32_t)(t & 0xFFFFFFFF);
    kheiron_state[15] += (uint32_t)(t >> 32);

    kheiron_secure_wipe(block, sizeof(block));
    kheiron_bytes_generated = 0;
}

static inline void kheiron_ctx_rotate(kheiron_ctx_t *ctx) {
    uint64_t t = kheiron_rdtsc();
    ctx->state[12] ^= (uint32_t)(t & 0xFFFFFFFF);
    ctx->state[13] ^= (uint32_t)(t >> 32);

    uint8_t block[64];
    kheiron_chacha_block(ctx->state, block);

    for (int i = 0; i < 8; i++) {
        uint32_t k = (uint32_t)block[i * 4 + 0] |
                     ((uint32_t)block[i * 4 + 1] << 8) |
                     ((uint32_t)block[i * 4 + 2] << 16) |
                     ((uint32_t)block[i * 4 + 3] << 24);
        ctx->state[4 + i] ^= k;
    }

    ctx->state[14] += (uint32_t)(t & 0xFFFFFFFF);
    ctx->state[15] += (uint32_t)(t >> 32);

    kheiron_secure_wipe(block, sizeof(block));
    ctx->bytes_generated = 0;
}

KHEIRON_API int kheiron_ctx_init(kheiron_ctx_t *ctx, const void *seed, size_t seed_len) {
    if (!ctx) {
        return -1;
    }

    ctx->state[0] = 0x61707865;
    ctx->state[1] = 0x3320646e;
    ctx->state[2] = 0x79622d32;
    ctx->state[3] = 0x6b206574;

    if (seed && seed_len > 0) {
        for (int i = 4; i < 16; i++) {
            ctx->state[i] = 0;
        }
        const uint8_t *s = (const uint8_t *)seed;
        for (size_t i = 0; i < seed_len; i++) {
            size_t idx = 4 + ((i / 4) % 12);
            size_t shift = (i % 4) * 8;
            ctx->state[idx] ^= ((uint32_t)s[i] << shift);
        }
        uint64_t t = kheiron_rdtsc();
        ctx->state[12] ^= (uint32_t)(t & 0xFFFFFFFF);
        ctx->state[13] ^= (uint32_t)(t >> 32);
        uint8_t out[64];
        kheiron_chacha_block(ctx->state, out);
        for (int i = 0; i < 8; i++) {
            uint32_t k = (uint32_t)out[i * 4 + 0] |
                         ((uint32_t)out[i * 4 + 1] << 8) |
                         ((uint32_t)out[i * 4 + 2] << 16) |
                         ((uint32_t)out[i * 4 + 3] << 24);
            ctx->state[4 + i] ^= k;
        }
        kheiron_secure_wipe(out, sizeof(out));
    } else {
        for (int i = 0; i < 4; i++) {
            uint64_t sample = kheiron_jitter_sample();
            ctx->state[4 + i * 2] = (uint32_t)(sample & 0xFFFFFFFF);
            ctx->state[4 + i * 2 + 1] = (uint32_t)(sample >> 32);
        }
        uint64_t t1 = kheiron_rdtsc();
        uint64_t t2 = kheiron_rdtsc();
        ctx->state[12] = (uint32_t)(t1 & 0xFFFFFFFF);
        ctx->state[13] = (uint32_t)(t1 >> 32);
        ctx->state[14] = (uint32_t)(t2 & 0xFFFFFFFF);
        ctx->state[15] = (uint32_t)(t2 >> 32);
    }

    kheiron_protect_memory(ctx->state, sizeof(ctx->state));
    ctx->bytes_generated = 0;
    return 0;
}

KHEIRON_API int kheiron_ctx_fill(kheiron_ctx_t *ctx, void *buf, size_t len) {
    if (!ctx || !buf || len == 0) {
        return -1;
    }

    uint8_t *p = (uint8_t *)buf;
    size_t remaining = len;
    uint8_t keystream[64];

    while (remaining > 0) {
        if ((ctx->bytes_generated & ~0xFFFFFUL) != 0 || ctx->bytes_generated >= 1048576) {
            kheiron_ctx_rotate(ctx);
        }

        kheiron_chacha_block(ctx->state, keystream);

        ctx->state[12]++;
        if (ctx->state[12] == 0) {
            ctx->state[13]++;
        }

        size_t chunk = remaining < 64 ? remaining : 64;
        for (size_t i = 0; i < chunk; i++) {
            p[i] = keystream[i];
        }

        p += chunk;
        remaining -= chunk;
        ctx->bytes_generated += chunk;
    }

    kheiron_secure_wipe(keystream, sizeof(keystream));
    return 0;
}

KHEIRON_API void kheiron_ctx_wipe(kheiron_ctx_t *ctx) {
    if (ctx) {
        kheiron_secure_wipe(ctx, sizeof(*ctx));
    }
}

KHEIRON_API int kheiron_derive(kheiron_ctx_t *ctx, void *out, size_t out_len, const void *info, size_t info_len) {
    if (!ctx || !out || out_len == 0 || !info || info_len == 0) {
        return -1;
    }

    kheiron_ctx_t temp_ctx;
    memcpy(&temp_ctx, ctx, sizeof(temp_ctx));

    const uint8_t *inf = (const uint8_t *)info;
    for (size_t i = 0; i < info_len; i++) {
        size_t idx = 4 + ((i / 4) % 12);
        size_t shift = (i % 4) * 8;
        temp_ctx.state[idx] ^= ((uint32_t)inf[i] << shift);
    }

    kheiron_ctx_rotate(&temp_ctx);

    if (kheiron_ctx_fill(&temp_ctx, out, out_len) != 0) {
        kheiron_ctx_wipe(&temp_ctx);
        return -1;
    }

    kheiron_ctx_wipe(&temp_ctx);
    return 0;
}

KHEIRON_API int kheiron_reseed(const void *user_entropy, size_t len) {
    if (!user_entropy || len == 0) {
        return -1;
    }

    if (!kheiron_initialized) {
        kheiron_init_state();
    }

    uint8_t os_mix[32];
    kheiron_os_entropy(os_mix, sizeof(os_mix));

    const uint8_t *p = (const uint8_t *)user_entropy;
    uint32_t temp_buf[16];
    memcpy(temp_buf, kheiron_state, sizeof(temp_buf));

    for (int i = 0; i < 8; i++) {
        uint32_t val = (uint32_t)os_mix[i * 4 + 0] |
                       ((uint32_t)os_mix[i * 4 + 1] << 8) |
                       ((uint32_t)os_mix[i * 4 + 2] << 16) |
                       ((uint32_t)os_mix[i * 4 + 3] << 24);
        temp_buf[4 + i] ^= val;
    }
    kheiron_secure_wipe(os_mix, sizeof(os_mix));

    for (size_t i = 0; i < len; i++) {
        size_t idx = 4 + ((i / 4) % 12);
        size_t shift = (i % 4) * 8;
        temp_buf[idx] ^= ((uint32_t)p[i] << shift);
    }

    uint64_t t = kheiron_rdtsc();
    temp_buf[12] ^= (uint32_t)(t & 0xFFFFFFFF);
    temp_buf[13] ^= (uint32_t)(t >> 32);

    uint8_t out_block[64];
    kheiron_chacha_block(temp_buf, out_block);

    for (int i = 0; i < 8; i++) {
        uint32_t k = (uint32_t)out_block[i * 4 + 0] |
                     ((uint32_t)out_block[i * 4 + 1] << 8) |
                     ((uint32_t)out_block[i * 4 + 2] << 16) |
                     ((uint32_t)out_block[i * 4 + 3] << 24);
        kheiron_state[4 + i] ^= k;
    }

    kheiron_secure_wipe(out_block, sizeof(out_block));
    kheiron_secure_wipe(temp_buf, sizeof(temp_buf));
    kheiron_bytes_generated = 0;
    return 0;
}

KHEIRON_API int kheiron_fill(void *buf, size_t len) {
    if (!buf || len == 0) {
        return -1;
    }

    if (kheiron_initialized == -1) {
        return -1;
    }

    if (!kheiron_initialized) {
        if (kheiron_selftest() != 0) {
            kheiron_initialized = -1;
            return -1;
        }
        kheiron_init_state();
    }

    uint8_t *p = (uint8_t *)buf;
    size_t remaining = len;
    uint8_t keystream[64];

    while (remaining > 0) {
        if ((kheiron_bytes_generated & ~0xFFFFFUL) != 0 || kheiron_bytes_generated >= 1048576) {
            kheiron_rotate_key();
        }

        kheiron_chacha_block(kheiron_state, keystream);

        kheiron_state[12]++;
        if (kheiron_state[12] == 0) {
            kheiron_state[13]++;
        }

        size_t chunk = remaining < 64 ? remaining : 64;
        for (size_t i = 0; i < chunk; i++) {
            p[i] = keystream[i];
        }

        p += chunk;
        remaining -= chunk;
        kheiron_bytes_generated += chunk;
    }

    kheiron_secure_wipe(keystream, sizeof(keystream));
    return 0;
}

KHEIRON_API int kheiron_string(char *dest, size_t len, const char *charset) {
    if (!dest || len == 0 || !charset) {
        return -1;
    }

    size_t c_len = strlen(charset);
    if (c_len == 0 || c_len > 256) {
        return -1;
    }

    uint32_t mask;
    if (c_len == 1) {
        mask = 0;
    } else {
        mask = (1U << (32 - __builtin_clz((uint32_t)(c_len - 1)))) - 1U;
    }

    uint8_t batch[64];
    size_t batch_idx = 64;

    for (size_t i = 0; i < len; i++) {
        for (;;) {
            if (batch_idx >= 64) {
                kheiron_fill(batch, sizeof(batch));
                batch_idx = 0;
            }
            uint8_t raw = batch[batch_idx++];
            uint32_t val = (uint32_t)raw & mask;
            if (val < c_len) {
                dest[i] = charset[val];
                break;
            }
        }
    }

    dest[len] = '\0';
    kheiron_secure_wipe(batch, sizeof(batch));
    return 0;
}

KHEIRON_API uint64_t kheiron_range_u64(uint64_t min, uint64_t max) {
    if (min >= max) {
        return min;
    }

    uint64_t s = max - min + 1ULL;
    if (s == 0) {
        uint64_t val = 0;
        kheiron_fill(&val, sizeof(val));
        return val;
    }

    uint64_t t = -s % s;
    for (;;) {
        uint64_t x = 0;
        kheiron_fill(&x, sizeof(x));
        __uint128_t m = (__uint128_t)x * (__uint128_t)s;
        uint64_t l = (uint64_t)m;
        if (l >= t) {
            return (uint64_t)(m >> 64) + min;
        }
    }
}

KHEIRON_API double kheiron_uniform_double(void) {
    uint64_t r = 0;
    kheiron_fill(&r, sizeof(r));
    return (double)(r >> 11) * 0x1.0p-53;
}

KHEIRON_API float kheiron_uniform_float(void) {
    uint32_t r = 0;
    kheiron_fill(&r, sizeof(r));
    return (float)(r >> 8) * 0x1.0p-24f;
}

KHEIRON_API int kheiron_shuffle(void *base, size_t nmemb, size_t size) {
    if (base == NULL || nmemb <= 1 || size == 0) {
        return -1;
    }

    uint8_t *p = (uint8_t *)base;
    uint8_t tmp[256];

    for (size_t i = nmemb - 1; i >= 1; i--) {
        size_t j = (size_t)kheiron_range_u64(0, (uint64_t)i);
        if (i != j) {
            uint8_t *a = p + (i * size);
            uint8_t *b = p + (j * size);
            size_t remaining = size;
            while (remaining > 0) {
                size_t chunk = remaining > sizeof(tmp) ? sizeof(tmp) : remaining;
                memcpy(tmp, a, chunk);
                memcpy(a, b, chunk);
                memcpy(b, tmp, chunk);
                a += chunk;
                b += chunk;
                remaining -= chunk;
            }
        }
    }

    kheiron_secure_wipe(tmp, sizeof(tmp));
    return 0;
}

KHEIRON_API double kheiron_gaussian(double mean, double stddev) {
    if (stddev <= 0.0) {
        return mean;
    }

    for (;;) {
        double u1 = 2.0 * kheiron_uniform_double() - 1.0;
        double u2 = 2.0 * kheiron_uniform_double() - 1.0;
        double s = u1 * u1 + u2 * u2;
        if (s >= 1.0 || s == 0.0) {
            continue;
        }
        double m = sqrt((-2.0 * log(s)) / s);
        return mean + stddev * (u1 * m);
    }
}

KHEIRON_API int kheiron_consttime_cmp(const void *a, const void *b, size_t len) {
    if (a == NULL || b == NULL || len == 0) {
        return -1;
    }

    const volatile uint8_t *pa = (const volatile uint8_t *)a;
    const volatile uint8_t *pb = (const volatile uint8_t *)b;
    volatile uint8_t diff = 0;

    for (size_t i = 0; i < len; i++) {
        diff |= (pa[i] ^ pb[i]);
    }

    return (diff != 0);
}

KHEIRON_API uint32_t kheiron_consttime_select_u32(uint32_t mask, uint32_t a, uint32_t b) {
    return (mask & a) | (~mask & b);
}

#endif

#endif