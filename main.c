#include <stdio.h>
#include <stdint.h>

#define KHEIRON_IMPLEMENTATION
#include "kheiron.h"

int main(void) {
    uint8_t buf[16];
    kheiron_fill(buf, sizeof(buf));
    kheiron_secure_wipe(buf, sizeof(buf));
    uint32_t sum = 0;
    for (size_t i = 0; i < sizeof(buf); i++) {
        sum += buf[i];
    }
    printf("%u\n", sum);
    return 0;
}
