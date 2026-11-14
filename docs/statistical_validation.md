# Statistical Validation Methodology (PractRand / Dieharder / TestU01)

This document describes how to run empirical randomness batteries against **Kheiron** using the raw binary stream emitter `bin/kheiron_stream`.

## Building the Stream Harness

```bash
make stream
```

This compiles `tools/kheiron_stream.c` with `-O3` (and runtime AVX2 dispatch) into `bin/kheiron_stream`.

## Running PractRand

Pipe the continuous 64 KB-buffered binary stream directly into PractRand's `RNG_test`:

```bash
./bin/kheiron_stream | ./RNG_test stdin64 -tlmax 1TB
```

When `RNG_test` completes or is terminated, `kheiron_stream` handles `SIGPIPE` / write termination cleanly, wipes its stream buffer with `kheiron_secure_wipe`, and exits with status `0`.

## Interpreting p-value Results

- **Normal / Pass**: `p-value` values well within `[0.001, 0.999]` (PractRand reports `no anomalies` across all tested power-of-two byte checkpoints).
- **Suspicious / Mild**: isolated `p-value` near `0.001` or `0.999` (e.g., `unusual` / `mildly suspicious`) — may occur rarely by chance and should be re-tested at larger `-tlmax`.
- **Failure**: `p-value < 1e-6` or `p-value > 1 - 1e-6` (`FAIL` / `very suspicious`), indicating a genuine statistical defect.

## Practical Randomness Test Results (PractRand v0.94)

Empirical validation executed using `bin/kheiron_stream` piped into PractRand v0.94 (`RNG_test stdin64 -tlmin 1GB -tlmax 16GB`), processing 16 GB (2^34 bytes) in 130 seconds (~126 MB/s end-to-end pipeline throughput including statistical folding and analysis).

```text
RNG_test using PractRand version 0.94
RNG = RNG_stdin64, seed = unknown
test set = core, folding = standard (64 bit)

rng=RNG_stdin64, seed=unknown
length= 1 gigabyte (2^30 bytes), time= 7.9 seconds
  no anomalies in 243 test result(s)

rng=RNG_stdin64, seed=unknown
length= 2 gigabytes (2^31 bytes), time= 16.3 seconds
  no anomalies in 261 test result(s)

rng=RNG_stdin64, seed=unknown
length= 4 gigabytes (2^32 bytes), time= 32.3 seconds
  no anomalies in 277 test result(s)

rng=RNG_stdin64, seed=unknown
length= 8 gigabytes (2^33 bytes), time= 65.1 seconds
  no anomalies in 294 test result(s)

rng=RNG_stdin64, seed=unknown
length= 16 gigabytes (2^34 bytes), time= 130 seconds
  no anomalies in 310 test result(s)
```

### Conclusion

Across all 310 statistical tests evaluated up to 16 GB, **zero anomalies** were detected (`no anomalies`). This confirms that Kheiron's 256-bit ChaCha20 core, RFC 8439 permutation, and AVX2 vectorization exhibit no measurable bias or stochastic anomaly.

## 1 Terabyte Stress Test Results (PractRand v0.94 Multithreaded)

Full-scale statistical stress test executed up to **1 Terabyte (`2^40` bytes)** using PractRand v0.94 multithreaded against `bin/kheiron_stream` (full raw log preserved in [`docs/practrand_1tb.log`](practrand_1tb.log)).

- **Total Data Processed**: 1 terabyte (`2^40` bytes)
- **Total Execution Time**: 2703 seconds (~45.05 minutes)
- **Total Tests Evaluated at 1 TB**: 397 test results
- **Final Status**: **Zero anomalies** (`no anomalies in 397 test result(s)`)

### Checkpoint Summary

| Length | Time | Result |
| :--- | :--- | :--- |
| 1 GB (`2^30` bytes) | 2.8 s | `no anomalies in 243 test result(s)` |
| 2 GB (`2^31` bytes) | 5.8 s | `no anomalies in 261 test result(s)` |
| 4 GB (`2^32` bytes) | 11.3 s | 1 `unusual` (`[Low4/64]Gap-16:B`, `p = 1-2.9e-4`), 276 without anomalies |
| 8 GB (`2^33` bytes) | 22.9 s | `no anomalies in 294 test result(s)` |
| 16 GB (`2^34` bytes) | 45.1 s | `no anomalies in 310 test result(s)` |
| 32 GB (`2^35` bytes) | 86.5 s | `no anomalies in 325 test result(s)` |
| 64 GB (`2^36` bytes) | 176 s | `no anomalies in 340 test result(s)` |
| 128 GB (`2^37` bytes) | 346 s | `no anomalies in 355 test result(s)` |
| 256 GB (`2^38` bytes) | 668 s | `no anomalies in 369 test result(s)` |
| 512 GB (`2^39` bytes) | 1358 s | `no anomalies in 383 test result(s)` |
| **1 TB (`2^40` bytes)** | **2703 s** | **`no anomalies in 397 test result(s)`** |
