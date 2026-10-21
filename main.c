#include <stdio.h>
#include <stdint.h>

#define KHEIRON_IMPLEMENTATION
#include "kheiron.h"

int main(void) {
    for (int i = 0; i < 5; i++) {
        uint64_t val = kheiron_range_u64(10, 50);
        printf("%lu\n", val);
    }
    for (int i = 0; i < 5; i++) {
        double dbl = kheiron_uniform_double();
        printf("%.6f\n", dbl);
    }
    return 0;
}
