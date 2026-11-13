#ifndef KHEIRON_H
#define KHEIRON_H

#if !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#if !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE 1
#endif

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

#ifdef __cplusplus
extern "C" {
#endif

#ifndef KHEIRON_API
#if defined(KHEIRON_STATIC)
#define KHEIRON_API static inline
#else
#define KHEIRON_API
#endif
#endif
KHEIRON_API int kheiron_selftest(void);
KHEIRON_API int kheiron_init(void);
KHEIRON_API int kheiron_ctx_init(kheiron_ctx_t *ctx, const void *seed, size_t seed_len);
KHEIRON_API int kheiron_ctx_fill(kheiron_ctx_t *ctx, void *buf, size_t len);
KHEIRON_API void kheiron_ctx_wipe(kheiron_ctx_t *ctx);
KHEIRON_API int kheiron_derive(kheiron_ctx_t *ctx, void *out, size_t out_len, const void *info, size_t info_len);

KHEIRON_API uint64_t kheiron_rdtsc(void);
KHEIRON_API uint64_t kheiron_jitter_sample(void);
KHEIRON_API int kheiron_fill(void *buf, size_t len);
KHEIRON_API uint64_t kheiron_u64(void);
KHEIRON_API uint32_t kheiron_u32(void);
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

#ifdef __cplusplus
}
#endif

#if defined(KHEIRON_IMPLEMENTATION) || defined(KHEIRON_STATIC)

#include <math.h>

/* ========================================================================= */
/* KHEIRON AVX2 VECTORIZATION & PORTABILITY DISPATCH                         */
/* ========================================================================= */

#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__)) && !defined(KHEIRON_NO_AVX2)
    #define KHEIRON_HAVE_AVX2 1
    #include <immintrin.h>
    #if defined(__clang__)
        #define KHEIRON_TARGET_AVX2 __attribute__((target("avx2")))
    #elif defined(__GNUC__)
        #define KHEIRON_TARGET_AVX2 __attribute__((__target__("avx2")))
    #else
        #define KHEIRON_TARGET_AVX2
    #endif
#else
    #define KHEIRON_HAVE_AVX2 0
    #define KHEIRON_TARGET_AVX2
#endif

#if KHEIRON_HAVE_AVX2
static inline int kheiron_cpu_has_avx2(void) {
    return __builtin_cpu_supports("avx2") ? 1 : 0;
}
#else
static inline int kheiron_cpu_has_avx2(void) {
    return 0;
}
#endif

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
#ifndef KHEIRON_NO_OS_ENTROPY
    #include <bcrypt.h>
    #if defined(_MSC_VER)
    #pragma comment(lib, "bcrypt.lib")
    #endif
#endif
#elif defined(__APPLE__) || defined(__MACH__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
    #include <stdlib.h>
    #include <unistd.h>
    #include <sys/mman.h>
    #include <pthread.h>
    #if defined(__APPLE__) || defined(__MACH__)
        #include <sys/random.h>
        extern void arc4random_buf(void *buf, size_t nbytes);
    #endif
#else
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
    if (!buf) {
        return (len == 0) ? 0 : -1;
    }
    if (len == 0) {
        return 0;
    }

    uint8_t *p = (uint8_t *)buf;

#ifndef KHEIRON_NO_OS_ENTROPY
    size_t acquired = 0;
    (void)acquired;

#if defined(_WIN32) || defined(_WIN64)
    if (BCryptGenRandom(NULL, p, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0) {
        return 0;
    }
#elif defined(__APPLE__) || defined(__MACH__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
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
    kheiron_secure_wipe(buf, len);
    return -1;
#endif
#else
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
#endif
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
#if defined(__GNUC__) || defined(__clang__)
        for (int j = 0; j < 8; j++) {
            __asm__ __volatile__("" : "+r"(j) :: "memory");
        }
#else
        for (int j = 0; j < 8; j++) {
            (void)j;
        }
#endif
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
#if defined(__GNUC__) || defined(__clang__)
    __asm__ __volatile__("" : : "r"(v) : "memory");
#elif defined(_MSC_VER)
    SecureZeroMemory(v, n);
#endif
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

#if KHEIRON_HAVE_AVX2

#define KHEIRON_CHACHA20_QROUND_AVX2(a, b, c, d, rot16, rot8) do { \
    a = _mm256_add_epi32(a, b); \
    d = _mm256_xor_si256(d, a); \
    d = _mm256_shuffle_epi8(d, rot16); \
    \
    c = _mm256_add_epi32(c, d); \
    b = _mm256_xor_si256(b, c); \
    b = _mm256_or_si256(_mm256_slli_epi32(b, 12), _mm256_srli_epi32(b, 20)); \
    \
    a = _mm256_add_epi32(a, b); \
    d = _mm256_xor_si256(d, a); \
    d = _mm256_shuffle_epi8(d, rot8); \
    \
    c = _mm256_add_epi32(c, d); \
    b = _mm256_xor_si256(b, c); \
    b = _mm256_or_si256(_mm256_slli_epi32(b, 7),  _mm256_srli_epi32(b, 25)); \
} while(0)

KHEIRON_TARGET_AVX2
static inline void kheiron_transpose8x8(__m256i r[8]) {
    __m256i t0 = _mm256_unpacklo_epi32(r[0], r[1]);
    __m256i t1 = _mm256_unpackhi_epi32(r[0], r[1]);
    __m256i t2 = _mm256_unpacklo_epi32(r[2], r[3]);
    __m256i t3 = _mm256_unpackhi_epi32(r[2], r[3]);
    __m256i t4 = _mm256_unpacklo_epi32(r[4], r[5]);
    __m256i t5 = _mm256_unpackhi_epi32(r[4], r[5]);
    __m256i t6 = _mm256_unpacklo_epi32(r[6], r[7]);
    __m256i t7 = _mm256_unpackhi_epi32(r[6], r[7]);

    __m256i tt0 = _mm256_unpacklo_epi64(t0, t2);
    __m256i tt1 = _mm256_unpackhi_epi64(t0, t2);
    __m256i tt2 = _mm256_unpacklo_epi64(t1, t3);
    __m256i tt3 = _mm256_unpackhi_epi64(t1, t3);
    __m256i tt4 = _mm256_unpacklo_epi64(t4, t6);
    __m256i tt5 = _mm256_unpackhi_epi64(t4, t6);
    __m256i tt6 = _mm256_unpacklo_epi64(t5, t7);
    __m256i tt7 = _mm256_unpackhi_epi64(t5, t7);

    r[0] = _mm256_permute2x128_si256(tt0, tt4, 0x20);
    r[1] = _mm256_permute2x128_si256(tt1, tt5, 0x20);
    r[2] = _mm256_permute2x128_si256(tt2, tt6, 0x20);
    r[3] = _mm256_permute2x128_si256(tt3, tt7, 0x20);
    r[4] = _mm256_permute2x128_si256(tt0, tt4, 0x31);
    r[5] = _mm256_permute2x128_si256(tt1, tt5, 0x31);
    r[6] = _mm256_permute2x128_si256(tt2, tt6, 0x31);
    r[7] = _mm256_permute2x128_si256(tt3, tt7, 0x31);
}

KHEIRON_TARGET_AVX2
static void kheiron_chacha20_8blocks_avx2(const uint32_t state[16], uint8_t *out) {
    const __m256i rot16 = _mm256_setr_epi8(
        2, 3, 0, 1, 6, 7, 4, 5, 10, 11, 8, 9, 14, 15, 12, 13,
        2, 3, 0, 1, 6, 7, 4, 5, 10, 11, 8, 9, 14, 15, 12, 13
    );
    const __m256i rot8 = _mm256_setr_epi8(
        3, 0, 1, 2, 7, 4, 5, 6, 11, 8, 9, 10, 15, 12, 13, 14,
        3, 0, 1, 2, 7, 4, 5, 6, 11, 8, 9, 10, 15, 12, 13, 14
    );

    __m256i v[16];
    for (int i = 0; i < 12; i++) {
        v[i] = _mm256_set1_epi32((int)state[i]);
    }

    if (__builtin_expect(state[12] <= 0xFFFFFFF8U, 1)) {
        const __m256i ctr_offsets = _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7);
        v[12] = _mm256_add_epi32(_mm256_set1_epi32((int)state[12]), ctr_offsets);
        v[13] = _mm256_set1_epi32((int)state[13]);
    } else {
        uint32_t c12[8], c13[8];
        uint64_t ctr = (uint64_t)state[12] | ((uint64_t)state[13] << 32);
        for (int i = 0; i < 8; i++) {
            uint64_t c = ctr + (uint64_t)i;
            c12[i] = (uint32_t)c;
            c13[i] = (uint32_t)(c >> 32);
        }
        v[12] = _mm256_loadu_si256((const __m256i *)(const void *)c12);
        v[13] = _mm256_loadu_si256((const __m256i *)(const void *)c13);
    }

    v[14] = _mm256_set1_epi32((int)state[14]);
    v[15] = _mm256_set1_epi32((int)state[15]);

    __m256i orig12 = v[12];
    __m256i orig13 = v[13];

    for (int i = 0; i < 10; i++) {
        KHEIRON_CHACHA20_QROUND_AVX2(v[0], v[4], v[8],  v[12], rot16, rot8);
        KHEIRON_CHACHA20_QROUND_AVX2(v[1], v[5], v[9],  v[13], rot16, rot8);
        KHEIRON_CHACHA20_QROUND_AVX2(v[2], v[6], v[10], v[14], rot16, rot8);
        KHEIRON_CHACHA20_QROUND_AVX2(v[3], v[7], v[11], v[15], rot16, rot8);

        KHEIRON_CHACHA20_QROUND_AVX2(v[0], v[5], v[10], v[15], rot16, rot8);
        KHEIRON_CHACHA20_QROUND_AVX2(v[1], v[6], v[11], v[12], rot16, rot8);
        KHEIRON_CHACHA20_QROUND_AVX2(v[2], v[7], v[8],  v[13], rot16, rot8);
        KHEIRON_CHACHA20_QROUND_AVX2(v[3], v[4], v[9],  v[14], rot16, rot8);
    }

    for (int i = 0; i < 12; i++) {
        v[i] = _mm256_add_epi32(v[i], _mm256_set1_epi32((int)state[i]));
    }
    v[12] = _mm256_add_epi32(v[12], orig12);
    v[13] = _mm256_add_epi32(v[13], orig13);
    v[14] = _mm256_add_epi32(v[14], _mm256_set1_epi32((int)state[14]));
    v[15] = _mm256_add_epi32(v[15], _mm256_set1_epi32((int)state[15]));

    kheiron_transpose8x8(&v[0]);
    kheiron_transpose8x8(&v[8]);

    for (int b = 0; b < 8; b++) {
        _mm256_storeu_si256((__m256i *)(void *)(out + b * 64 + 0),  v[b]);
        _mm256_storeu_si256((__m256i *)(void *)(out + b * 64 + 32), v[b + 8]);
    }

    for (int i = 0; i < 16; i++) {
        v[i] = _mm256_setzero_si256();
    }
    orig12 = _mm256_setzero_si256();
    orig13 = _mm256_setzero_si256();
    __asm__ __volatile__("" : : "m"(v), "m"(orig12), "m"(orig13) : "memory");
}

KHEIRON_TARGET_AVX2
static inline void kheiron_chacha20_blocks_avx2(uint32_t state[16], uint8_t *out, size_t num_blocks) {
    while (num_blocks >= 8) {
        kheiron_chacha20_8blocks_avx2(state, out);
        out += 512;
        num_blocks -= 8;
        uint64_t ctr = ((uint64_t)state[12] | ((uint64_t)state[13] << 32)) + 8;
        state[12] = (uint32_t)ctr;
        state[13] = (uint32_t)(ctr >> 32);
    }
    _mm256_zeroupper();
}
#endif

/*
 * kheiron_protect_memory: Memory protection helper.
 * NOTE: Page-level advisories (such as MADV_DONTDUMP or VirtualLock) operate at
 * OS page granularity. To prevent disastrous page-table side effects (such as
 * zeroing stack frames or heap metadata upon fork), this helper must only be
 * applied to dedicated, page-aligned buffers. It must never be applied to stack,
 * heap arena chunks, or thread-local storage.
 */
static inline void kheiron_protect_memory(void *addr, size_t len) {
#if defined(_WIN32) || defined(_WIN64)
    VirtualLock(addr, len);
#else
#if defined(MADV_DONTDUMP)
    if (addr && len > 0) {
        long page_size = sysconf(_SC_PAGESIZE);
        if (page_size > 0) {
            uintptr_t start = (uintptr_t)addr;
            uintptr_t page_start = start & ~((uintptr_t)page_size - 1);
            if (start == page_start && len >= (size_t)page_size) {
                uintptr_t end = start + len;
                uintptr_t page_end = (end + (uintptr_t)page_size - 1) & ~((uintptr_t)page_size - 1);
                size_t page_len = (size_t)(page_end - page_start);
                (void)madvise((void *)page_start, page_len, MADV_DONTDUMP);
            }
        }
    }
#else
    (void)addr;
    (void)len;
#endif
#endif
}

#define KHEIRON_KEY_ROTATION_THRESHOLD (1024 * 1024) /* 1 MB = 1,048,576 bytes */

static KHEIRON_THREAD_LOCAL uint32_t kheiron_state[16];
static KHEIRON_THREAD_LOCAL int kheiron_initialized = 0;
static KHEIRON_THREAD_LOCAL size_t kheiron_bytes_generated = 0;
#if defined(__unix__) || defined(__APPLE__) || defined(__HAIKU__)
static pthread_once_t kheiron_atfork_once = PTHREAD_ONCE_INIT;

static void kheiron_atfork_child(void) {
    kheiron_initialized = 0;
    kheiron_bytes_generated = 0;
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

static void kheiron_register_atfork(void) {
    (void)pthread_atfork(NULL, NULL, kheiron_atfork_child);
}
#endif

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

static inline int kheiron_init_state(void) {
    kheiron_state[0] = 0x61707865;
    kheiron_state[1] = 0x3320646e;
    kheiron_state[2] = 0x79622d32;
    kheiron_state[3] = 0x6b206574;

    uint8_t os_seed[32];
    if (kheiron_os_entropy(os_seed, sizeof(os_seed)) != 0) {
        kheiron_secure_wipe(os_seed, sizeof(os_seed));
        return -1;
    }
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

#if defined(__unix__) || defined(__APPLE__) || defined(__HAIKU__)
    (void)pthread_once(&kheiron_atfork_once, kheiron_register_atfork);
#endif

    kheiron_initialized = 1;
    kheiron_bytes_generated = 0;
    return 0;
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
        uint8_t os_seed[32];
        if (kheiron_os_entropy(os_seed, sizeof(os_seed)) != 0) {
            kheiron_secure_wipe(os_seed, sizeof(os_seed));
            kheiron_secure_wipe(ctx, sizeof(*ctx));
            return -1;
        }
        for (int i = 0; i < 8; i++) {
            uint32_t val = (uint32_t)os_seed[i * 4 + 0] |
                           ((uint32_t)os_seed[i * 4 + 1] << 8) |
                           ((uint32_t)os_seed[i * 4 + 2] << 16) |
                           ((uint32_t)os_seed[i * 4 + 3] << 24);
            ctx->state[4 + i] = val;
        }
        kheiron_secure_wipe(os_seed, sizeof(os_seed));

        for (int i = 0; i < 4; i++) {
            uint64_t sample = kheiron_jitter_sample();
            ctx->state[4 + i * 2] ^= (uint32_t)(sample & 0xFFFFFFFF);
            ctx->state[4 + i * 2 + 1] ^= (uint32_t)(sample >> 32);
        }
        uint64_t t1 = kheiron_rdtsc();
        uint64_t t2 = kheiron_rdtsc();
        ctx->state[12] = (uint32_t)(t1 & 0xFFFFFFFF);
        ctx->state[13] = (uint32_t)(t1 >> 32);
        ctx->state[14] = (uint32_t)(t2 & 0xFFFFFFFF);
        ctx->state[15] = (uint32_t)(t2 >> 32);
    }

    ctx->bytes_generated = 0;
    return 0;
}

KHEIRON_API int kheiron_ctx_fill(kheiron_ctx_t *ctx, void *buf, size_t len) {
    if (!ctx || !buf) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }

    uint8_t *p = (uint8_t *)buf;
    size_t remaining = len;

    while (remaining > 0) {
        if (ctx->bytes_generated >= KHEIRON_KEY_ROTATION_THRESHOLD) {
            kheiron_ctx_rotate(ctx);
        }

        size_t available_in_epoch = KHEIRON_KEY_ROTATION_THRESHOLD - ctx->bytes_generated;
        size_t chunk_limit = remaining < available_in_epoch ? remaining : available_in_epoch;

#if KHEIRON_HAVE_AVX2
        if (kheiron_cpu_has_avx2() && chunk_limit >= 512) {
            size_t avx_iters = chunk_limit / 512;
            size_t avx_bytes = avx_iters * 512;
            kheiron_chacha20_blocks_avx2(ctx->state, p, avx_iters * 8);
            p += avx_bytes;
            remaining -= avx_bytes;
            ctx->bytes_generated += avx_bytes;
            continue;
        }
#endif

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
        if (kheiron_init_state() != 0) {
            return -1;
        }
    }

    uint8_t os_mix[32];
    if (kheiron_os_entropy(os_mix, sizeof(os_mix)) != 0) {
        kheiron_secure_wipe(os_mix, sizeof(os_mix));
        return -1;
    }

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
    if (!buf) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }

    if (kheiron_initialized == -1) {
        return -1;
    }

    if (!kheiron_initialized) {
        if (kheiron_selftest() != 0) {
            kheiron_initialized = -1;
            return -1;
        }
        if (kheiron_init_state() != 0) {
            kheiron_initialized = -1;
            return -1;
        }
    }

    uint8_t *p = (uint8_t *)buf;
    size_t remaining = len;

    while (remaining > 0) {
        if (kheiron_bytes_generated >= KHEIRON_KEY_ROTATION_THRESHOLD) {
            kheiron_rotate_key();
        }

        size_t available_in_epoch = KHEIRON_KEY_ROTATION_THRESHOLD - kheiron_bytes_generated;
        size_t chunk_limit = remaining < available_in_epoch ? remaining : available_in_epoch;

#if KHEIRON_HAVE_AVX2
        if (kheiron_cpu_has_avx2() && chunk_limit >= 512) {
            size_t avx_iters = chunk_limit / 512;
            size_t avx_bytes = avx_iters * 512;
            kheiron_chacha20_blocks_avx2(kheiron_state, p, avx_iters * 8);
            p += avx_bytes;
            remaining -= avx_bytes;
            kheiron_bytes_generated += avx_bytes;
            continue;
        }
#endif

        if (chunk_limit >= 64) {
            size_t scalar_blocks = chunk_limit / 64;
            for (size_t b = 0; b < scalar_blocks; b++) {
                kheiron_chacha_block(kheiron_state, p);
                kheiron_state[12]++;
                if (kheiron_state[12] == 0) {
                    kheiron_state[13]++;
                }
                p += 64;
                remaining -= 64;
                kheiron_bytes_generated += 64;
            }
            continue;
        }

        uint8_t keystream[64];
        kheiron_chacha_block(kheiron_state, keystream);
        kheiron_state[12]++;
        if (kheiron_state[12] == 0) {
            kheiron_state[13]++;
        }
        for (size_t i = 0; i < chunk_limit; i++) {
            p[i] = keystream[i];
        }
        kheiron_secure_wipe(keystream, sizeof(keystream));
        p += chunk_limit;
        remaining -= chunk_limit;
        kheiron_bytes_generated += chunk_limit;
    }

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
        if (kheiron_fill(&val, sizeof(val)) != 0) {
            return min;
        }
        return val;
    }

    uint64_t t = -s % s;
    for (;;) {
        uint64_t x = 0;
        if (kheiron_fill(&x, sizeof(x)) != 0) {
            return min;
        }
        __uint128_t m = (__uint128_t)x * (__uint128_t)s;
        uint64_t l = (uint64_t)m;
        if (l >= t) {
            return (uint64_t)(m >> 64) + min;
        }
    }
}

KHEIRON_API double kheiron_uniform_double(void) {
    uint64_t r = 0;
    if (kheiron_fill(&r, sizeof(r)) != 0) {
        return 0.0;
    }
    return (double)(r >> 11) * 0x1.0p-53;
}

KHEIRON_API float kheiron_uniform_float(void) {
    uint32_t r = 0;
    if (kheiron_fill(&r, sizeof(r)) != 0) {
        return 0.0f;
    }
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

KHEIRON_API int kheiron_init(void) {
    if (kheiron_initialized == -1) {
        return -1;
    }
    if (!kheiron_initialized) {
        if (kheiron_selftest() != 0) {
            kheiron_initialized = -1;
            return -1;
        }
        if (kheiron_init_state() != 0) {
            kheiron_initialized = -1;
            return -1;
        }
    }
    return 0;
}

KHEIRON_API uint64_t kheiron_u64(void) {
    uint64_t val = 0;
    kheiron_fill(&val, sizeof(val));
    return val;
}

KHEIRON_API uint32_t kheiron_u32(void) {
    uint32_t val = 0;
    kheiron_fill(&val, sizeof(val));
    return val;
}

#endif

#endif
