CC ?= gcc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Wpedantic -Werror
LDFLAGS ?= -lm -pthread
ifeq ($(OS),Windows_NT)
LDFLAGS += -lbcrypt
endif
TARGET = bench_tests
SRC = bench_tests.c

.PHONY: all test strict sanitize clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

test: $(TARGET)
	./$(TARGET) --quick

strict: $(TARGET)
	./$(TARGET) --strict

sanitize:
	$(CC) $(CFLAGS) -fsanitize=address,undefined -g $(SRC) -o $(TARGET) $(LDFLAGS)
	./$(TARGET) --quick

clean:
	rm -f $(TARGET) *.o *.out
