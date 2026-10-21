#include <stdio.h>
#include <stdint.h>

#define KHEIRON_IMPLEMENTATION
#include "kheiron.h"

int main(void) {
    for (int i = 0; i < 16; i++) {
        uint64_t valor = kheiron_jitter_sample();
        printf("%016lx\n", valor);
    }
    return 0;
}
