#include <stdio.h>
#include <stdint.h>

#define KHEIRON_IMPLEMENTATION
#include "kheiron.h"

int main(void) {
    uint8_t block[64];
    kheiron_fill(block, sizeof(block));
    for (size_t i = 0; i < sizeof(block); i++) {
        printf("%02x", block[i]);
    }
    printf("\n");

    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*()_+-=";
    char pwd[25];
    kheiron_string(pwd, 24, charset);
    printf("%s\n", pwd);

    uint64_t num = kheiron_range_u64(1000, 9999);
    printf("%lu\n", num);

    double dbl = kheiron_uniform_double();
    printf("%.8f\n", dbl);

    return 0;
}
