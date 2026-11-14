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
