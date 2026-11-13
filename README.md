# Kheiron v2.0.1 — High-Performance Single-Header C99 CSPRNG with AVX2 Vectorization

[![CI](https://github.com/cainamatera/kheiron/actions/workflows/ci.yml/badge.svg)](https://github.com/cainamatera/kheiron/actions/workflows/ci.yml)
[![CodeQL](https://github.com/cainamatera/kheiron/actions/workflows/codeql.yml/badge.svg)](https://github.com/cainamatera/kheiron/actions/workflows/codeql.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Standards: C99 / C11](https://img.shields.io/badge/Standards-C99%20%2F%20C11-brightgreen.svg)](#)
[![Zero Dependency](https://img.shields.io/badge/Dependencies-Zero-orange.svg)](#)

> [Versão em Português](README.pt-BR.md)

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

---

## Empirical Benchmark Comparison (Kheiron vs. OpenSSL 3.x `RAND_bytes`)

Measured on Linux `x86_64` (AVX2 enabled, with active 1 MB forward secrecy key rotation):

| Metric | Kheiron v2.0.1 | OpenSSL (`RAND_bytes`) | Relative Advantage |
| :--- | :---: | :---: | :---: |
| **Small-Chunk Latency (64 B)** | **~455 MB/s** | ~128–144 MB/s | **3.16x–3.54x faster** |
| **Continuous Vector Throughput (1 KB)** | **~2,518 MB/s** | ~1,478 MB/s | **1.70x faster** |
| **Binary Footprint** | **~35 KB** (static/amalgamated) | ~7.76 MB (`libcrypto.so.3` + deps) | **~226x smaller** |
| **Dynamic Dependencies (`.so`)** | **0 (Zero)** | 10 mapped shared objects | **Zero dependencies** |

See [BENCHMARK.md](BENCHMARK.md) for the full measurement methodology.

---

## Quickstart

In exactly one `.c` translation unit:

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
    printf("Roll: %llu | Uniform: %.8f\n", (unsigned long long)roll, unit);
    return 0;
}
```

Compile and run:

```bash
cc -std=c99 -O2 -Wall -Wextra -Wpedantic -Werror main.c -o main -lm -pthread
./main
```

---

## Build and Test Commands

```bash
make check       # Run the full 235-test E2E suite
make quick       # Run fast smoke tests (--quick)
make bench       # Run comparative benchmarks against OpenSSL
make sanitizer   # Run full AddressSanitizer + UndefinedBehaviorSanitizer suite
make clean       # Clean generated binaries and artifacts
```

---

## Governance & Documentation

- **License**: [MIT License](LICENSE)
- **Security Policy**: [SECURITY.md](SECURITY.md)
- **Benchmark Methodology**: [BENCHMARK.md](BENCHMARK.md)
