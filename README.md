> [Versão em Português](README.pt-BR.md)

# Kheiron v2.0.1 — High-Performance Single-Header C99 CSPRNG with AVX2 Vectorization

[![CI](https://github.com/cainamatera/kheiron/actions/workflows/ci.yml/badge.svg)](https://github.com/cainamatera/kheiron/actions/workflows/ci.yml)
[![OpenSSF Best Practices](https://www.bestpractices.dev/projects/16676/badge)](https://www.bestpractices.dev/projects/16676)
[![OpenSSF Scorecard](https://api.scorecard.dev/projects/github.com/cainamatera/kheiron/badge)](https://scorecard.dev/viewer/?uri=github.com/cainamatera/kheiron)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

**Kheiron** is a zero-dependency, single-header Cryptographically Secure Pseudo-Random Number Generator (CSPRNG) written in C99/C11, built on the 20-round **ChaCha20** stream cipher (**RFC 8439**) with **8-block parallel AVX2 vectorization (~2.5 GB/s)**, runtime CPUID dispatch, and a transparent portable C99 scalar fallback for any architecture (ARM, RISC-V, legacy x86) without requiring global compiler flags.

---

## Key Features

- **8-Block Parallel AVX2 Engine**: Processes 512 bytes per iteration using 16 `__m256i` registers on compatible `x86_64` CPUs, with safe unaligned load/store operations.
- **Dynamic CPUID Dispatch**: Automatically detects AVX2 support at runtime via `__builtin_cpu_supports("avx2")` and falls back seamlessly to portable C99 scalar execution.
- **Active Forward Secrecy**: Mandatory automatic key rotation every 1 MB of generated output (`kheiron_rotate_key`), preventing backtracking even if internal state is later compromised.
- **Fail-Closed OS Entropy**: Strict reliance on OS CSPRNGs (`getrandom`, `/dev/urandom`, `arc4random_buf`, `BCryptGenRandom`) — initialization and reseed abort with `-1` if OS entropy fails, never silently degrading to weak sources.
- **Unbiased Sampling**: Daniel Lemire's 128-bit multiplication rejection sampling (`__uint128_t`) for modulo-bias-free integers and exact IEEE 754 mantissa mapping (`[0.0, 1.0)`).
- **DSE-Resistant Memory Sanitization**: `kheiron_secure_wipe` uses volatile pointers and compiler memory barriers to prevent Dead Store Elimination under `-O2`/`-O3`.
- **Fork Resilience & Lock-Free TLS**: `pthread_atfork` child reseed handlers (without hazardous `madvise` calls on arbitrary stack/heap pointers) and thread-local global state (`KHEIRON_THREAD_LOCAL`) plus explicit `kheiron_ctx_t` contexts.
- **Proven Empirical Randomness**: Verified with PractRand v0.94 up to 1 Terabyte (`2^40` bytes) with zero statistical anomalies across all 397 tests. See [docs/statistical_validation.md](docs/statistical_validation.md).

---

## Empirical Benchmark Comparison (Kheiron vs. OpenSSL `RAND_bytes`)

Measured on Linux `x86_64` (AVX2 enabled, with active 1 MB forward secrecy key rotation):

| Metric | Kheiron v2.0.1 | OpenSSL (`RAND_bytes`) | Relative Advantage |
| :--- | :---: | :---: | :---: |
| **Small-Chunk Latency (64 B)** | **~455 MB/s** | ~128–144 MB/s | **3.16x–3.54x faster** |
| **Continuous Vector Throughput (1 KB)** | **~2,518 MB/s** | ~1,478 MB/s | **1.70x faster** |
| **Binary Footprint** | **~35 KB** (static/amalgamated) | ~7.76 MB (`libcrypto.so.3` + deps) | **~226x smaller** |
| **Dynamic Dependencies (`.so`)** | **0 (Zero)** | 10 mapped shared objects | **Zero dependencies** |

See [BENCHMARK.md](BENCHMARK.md) for the full measurement methodology.

---

## Quickstart & Integration

In **exactly one** `.c` translation unit, define `KHEIRON_IMPLEMENTATION` before including the header:

```c
#define KHEIRON_IMPLEMENTATION
#include "kheiron.h"
```

In all other `.c` files, simply include `"kheiron.h"`. To emit all routines as `static inline` without exporting global symbols (useful for multi-module isolation), define `KHEIRON_STATIC` before `#include "kheiron.h"`.

### Complete Example

```c
#define KHEIRON_IMPLEMENTATION
#include "kheiron.h"
#include <stdio.h>

int main(void) {
    uint8_t key[32];
    if (kheiron_fill(key, sizeof(key)) != 0) {
        return 1;
    }

    uint64_t roll = kheiron_range_u64(1, 6);
    double unit = kheiron_uniform_double();
    char token[17];
    kheiron_string(token, sizeof(token), KHEIRON_CHARSET_ALNUM);

    printf("Roll: %llu | Uniform: %.8f | Token: %s\n",
           (unsigned long long)roll, unit, token);

    kheiron_secure_wipe(key, sizeof(key));
    return 0;
}
```

Compile cleanly with C99:

```bash
gcc -std=c99 -O3 -Wall -Wextra -Werror example.c -o example
```

---

## API Reference

### Initialization & Contexts

| Function | Description |
| :--- | :--- |
| `int kheiron_ctx_init(kheiron_ctx_t *ctx)` | Initializes an explicit context from fail-closed OS entropy (`0` on success, `-1` on failure). |
| `int kheiron_ctx_reseed(kheiron_ctx_t *ctx)` | Forces immediate reseed of an explicit context from OS entropy. |
| `void kheiron_ctx_fill(kheiron_ctx_t *ctx, void *buf, size_t len)` | Fills `buf` with `len` cryptographically secure bytes using `ctx`. |
| `void kheiron_ctx_wipe(kheiron_ctx_t *ctx)` | Securely wipes all internal key/nonce/counter state of `ctx`. |

### Global Generation & Utilities

| Function | Description |
| :--- | :--- |
| `int kheiron_fill(void *buf, size_t len)` | Fills `buf` with `len` bytes using the thread-local global context. |
| `uint32_t kheiron_u32(void)` / `uint64_t kheiron_u64(void)` | Returns a uniform 32-bit or 64-bit unsigned integer. |
| `uint64_t kheiron_range_u64(uint64_t min, uint64_t max)` | Unbiased uniform integer in `[min, max]` using Lemire's 128-bit method. |
| `double kheiron_uniform_double(void)` | Unbiased IEEE 754 `double` in `[0.0, 1.0)` (53-bit mantissa). |
| `float kheiron_uniform_float(void)` | Unbiased IEEE 754 `float` in `[0.0f, 1.0f)` (24-bit mantissa). |
| `void kheiron_string(char *out, size_t len, const char *charset)` | Generates a null-terminated random string of `len - 1` characters from `charset`. |

### Security Primitives

| Function | Description |
| :--- | :--- |
| `void kheiron_secure_wipe(void *ptr, size_t len)` | DSE-resistant memory zeroization using volatile writes and compiler barriers. |
| `void kheiron_rotate_key(kheiron_ctx_t *ctx)` | Rotates key and resets counter for active forward secrecy (automatic every 1 MB). |
| `int kheiron_bist(void)` | Runs the built-in self-test against the official RFC 8439 ChaCha20 test vector. |

---

## Build & Test Commands

| Command | Description |
| :--- | :--- |
| `make check` | Runs the full unit, RFC 8439, BIST, and fork-resilience test suite. |
| `make quick` | Runs fast smoke tests for rapid iteration. |
| `make bench` | Builds and executes the throughput/latency benchmark suite. |
| `make stream` | Builds `bin/kheiron_stream` for external statistical test batteries (PractRand/TestU01). |
| `make sanitizer` | Compiles and runs tests under ASan + UBSan (`-fsanitize=address,undefined`). |
| `make clean` | Removes all compiled binaries and build artifacts. |

---

## Governance & Documentation

- [LICENSE](LICENSE) — MIT License
- [SECURITY.md](SECURITY.md) — Vulnerability Disclosure Policy & Supported Versions
- [BENCHMARK.md](BENCHMARK.md) — Formal Comparative Benchmark Methodology
- [docs/statistical_validation.md](docs/statistical_validation.md) — PractRand v0.94 Statistical Validation (16 GB & 1 TB)
