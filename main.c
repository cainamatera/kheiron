#include <stdio.h>
#include <stdint.h>

#define KHEIRON_IMPLEMENTATION
#include "kheiron.h"

int main(void) {
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*()_+-=";
    char str[33];
    kheiron_string(str, 32, charset);
    printf("%s\n", str);
    return 0;
}
