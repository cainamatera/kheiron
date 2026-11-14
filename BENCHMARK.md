> [Português](BENCHMARK.pt-BR.md)

# Formal Benchmark Methodology — Kheiron v2.0.x vs. OpenSSL 3.x

This document defines the reproducible methodology used to evaluate throughput, latency, and binary footprint of **Kheiron** (`kheiron.h`) in direct comparison with **OpenSSL 3.x** (`RAND_bytes` and `EVP_chacha20`).

## Test Environment Specification

- **Architecture / CPU**: `x86_64` with **AVX2 / FMA** instruction support (runtime dynamic dispatch via CPUID).
- **Compiler**: GCC and Clang with strict flags (`-O3 -mavx2`, plus standard validation under `-std=c99 -O2 -Wall -Wextra -Wpedantic -Werror`).
- **Operating System**: Linux `x86_64` (AVX2 execution environment).
- **Reference Baseline**: OpenSSL 3.x (`libcrypto.so.3` / `RAND_bytes`).

## Measurement and Isolation Protocol

1. **High-Precision Timing**: Measurements use hardware cycle counters (`RDTSC`) and `clock_gettime(CLOCK_MONOTONIC_RAW)` / `CLOCK_MONOTONIC` to avoid system clock adjustment skew.
2. **Cache Warming**: Each benchmark suite performs warmup iterations before entering the timed window, ensuring hot instructions and buffers reside in L1/L2 cache.
3. **Multiple Repetitions**: Multiple rounds and thousands of iterations per chunk size are executed to discard OS scheduling jitter.
4. **Active Cryptographic Invariants**: All Kheiron measurements run with **active Forward Secrecy** (automatic key rotation every 1 MB), secure memory sanitization (`kheiron_secure_wipe`), and post-`fork` protection enabled.

## Evaluated Granularities

- **Small-Chunk Latency (`64 B`)**: Models key, nonce, IV, and short token generation — where Kheiron AVX2 achieves **~455 MB/s** (**3.16x to 3.54x** faster than OpenSSL `RAND_bytes`).
- **Continuous Vector Throughput (`1 KB` and above)**: Evaluates full occupancy of the 8-block parallel AVX2 pipeline (512 bytes/iteration), reaching **~2,518 MB/s (~2.5 GB/s)** at `1 KB` (**1.70x** faster than `RAND_bytes`).

## Throughput by Buffer Size

| Buffer Size | Kheiron AVX2 (v2.0.1) | OpenSSL 3.x (`RAND_bytes`) | Relative Speedup | Primary Use Case |
| :--- | :--- | :--- | :--- | :--- |
| **16 B** | ~180 MB/s | ~55 MB/s | **~3.27x** | UUIDs, Salts, Nonces |
| **64 B** | **~455 MB/s** | ~128–144 MB/s | **~3.35x** | Symmetric Keys (ChaCha/AES), IVs |
| **256 B** | ~1,250 MB/s | ~610 MB/s | **~2.05x** | Seed derivation, TLS sessions |
| **1 KB** | **~2,518 MB/s** | ~1,478 MB/s | **~1.70x** | Bulk packet encryption, stream mixing |
| **64 KB** | ~2,550 MB/s | ~1,820 MB/s | **~1.40x** | Continuous streaming / pipe throughput |

## Binary Footprint Comparison

| Dimension | Kheiron (Single-Header) | OpenSSL 3.x (`libcrypto.so.3`) |
| :--- | :---: | :---: |
| **Disk Footprint** | **~35 KB** (static/amalgamated binary) | **~7.76 MB** (library + dependencies) |
| **Dynamic Dependencies (`.so`)** | **0 (Zero)** | **10** mapped shared objects |

## Local Reproduction

```bash
make bench
```
