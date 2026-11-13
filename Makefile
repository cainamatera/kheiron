CC ?= gcc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Wpedantic -Werror
ifneq ($(findstring clang,$(CC)),)
CFLAGS += -Wno-c11-extensions
else ifneq ($(shell $(CC) --version 2>/dev/null | grep -i clang),)
CFLAGS += -Wno-c11-extensions
endif
LDFLAGS ?= -lm -pthread
ifeq ($(OS),Windows_NT)
LDFLAGS += -lbcrypt
endif
TARGET = bench_tests
SRC = bench_tests.c

E2E_RUNNER = tests/e2e_runner
E2E_SRCS   = tests/e2e_runner.c tests/tier1_features.c tests/tier2_boundaries.c tests/tier3_cross.c tests/tier4_scenarios.c
E2E_CFLAGS = $(CFLAGS) -I.
E2E_LDFLAGS = $(LDFLAGS)

.PHONY: all test strict sanitize clean e2e_build e2e_test e2e_sanitize e2e_scalar e2e_all bench bench_quick bench_sanitize tier5_test tier5_sanitize

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

test: $(TARGET)
	./$(TARGET) --quick

quick: test

check: e2e_test

strict: $(TARGET)
	./$(TARGET) --strict

sanitize:
	$(CC) $(CFLAGS) -fsanitize=address,undefined -g $(SRC) -o $(TARGET) $(LDFLAGS)
	./$(TARGET) --quick

sanitizer: sanitize

e2e_build:
	$(CC) $(E2E_CFLAGS) $(E2E_SRCS) -o $(E2E_RUNNER) $(E2E_LDFLAGS)

e2e_test: e2e_build
	./$(E2E_RUNNER)

e2e_sanitize:
	$(CC) $(E2E_CFLAGS) -fsanitize=address,undefined -g $(E2E_SRCS) -o $(E2E_RUNNER)_san $(E2E_LDFLAGS)
	./$(E2E_RUNNER)_san

e2e_scalar:
	$(CC) $(E2E_CFLAGS) -DKHEIRON_NO_AVX2 $(E2E_SRCS) -o $(E2E_RUNNER)_scalar $(E2E_LDFLAGS)
	./$(E2E_RUNNER)_scalar

e2e_all: e2e_test e2e_sanitize e2e_scalar

BENCH_TARGET = bench_compare
BENCH_SRC    = bench_compare.c
BENCH_CFLAGS = $(CFLAGS) -I.
BENCH_LDFLAGS = $(LDFLAGS) -lcrypto

$(BENCH_TARGET): $(BENCH_SRC) kheiron.h
	$(CC) $(BENCH_CFLAGS) $(BENCH_SRC) -o $(BENCH_TARGET) $(BENCH_LDFLAGS)

bench: $(BENCH_TARGET)
	./$(BENCH_TARGET)

bench_quick: $(BENCH_TARGET)
	./$(BENCH_TARGET) --quick

bench_sanitize:
	$(CC) $(BENCH_CFLAGS) -fsanitize=address,undefined -g $(BENCH_SRC) -o $(BENCH_TARGET)_san $(BENCH_LDFLAGS)
	./$(BENCH_TARGET)_san --quick

tier5_test:
	$(CC) $(CFLAGS) -I. tests/tier5_adversarial_m4_1.c -o tests/tier5_adversarial_m4_1 $(LDFLAGS)
	./tests/tier5_adversarial_m4_1
	$(CC) $(CFLAGS) -I. tests/tier5_adversarial_m4_2.c -o tests/tier5_adversarial_m4_2 $(LDFLAGS)
	./tests/tier5_adversarial_m4_2

tier5_sanitize:
	$(CC) $(CFLAGS) -fsanitize=address,undefined -g -I. tests/tier5_adversarial_m4_1.c -o tests/tier5_adversarial_m4_1_san $(LDFLAGS)
	./tests/tier5_adversarial_m4_1_san
	$(CC) $(CFLAGS) -fsanitize=address,undefined -g -I. tests/tier5_adversarial_m4_2.c -o tests/tier5_adversarial_m4_2_san $(LDFLAGS)
	./tests/tier5_adversarial_m4_2_san

clean:
	rm -f $(TARGET) $(E2E_RUNNER) $(E2E_RUNNER)_san $(E2E_RUNNER)_scalar $(BENCH_TARGET) $(BENCH_TARGET)_san tests/tier5_adversarial_m4_1 tests/tier5_adversarial_m4_1_san tests/tier5_adversarial_m4_2 tests/tier5_adversarial_m4_2_san tests/tier5_adversarial_m4_2_scalar tests/test_dse_wipe *.o *.out
