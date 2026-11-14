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
