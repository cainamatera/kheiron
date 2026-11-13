#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

echo "============================================================"
echo "KHEIRON-HYDROGEN AUTOMATED E2E TEST ORCHESTRATION"
echo "============================================================"

# Pass 1: Standard Release Build
echo ">>> [1/3] Building & Running Native C99 E2E Test Suite..."
make e2e_test

# Pass 2: AddressSanitizer & UndefinedBehaviorSanitizer Build
echo ">>> [2/3] Building & Running Sanitized (ASan/UBSan) E2E Test Suite..."
make e2e_sanitize

# Pass 3: Portable Scalar Fallback Build
echo ">>> [3/3] Building & Running Portable Scalar Fallback E2E Test Suite..."
make e2e_scalar

echo "============================================================"
echo "ALL E2E PROFILES (NATIVE, SANITIZED, SCALAR) PASSED CLEANLY!"
echo "============================================================"
