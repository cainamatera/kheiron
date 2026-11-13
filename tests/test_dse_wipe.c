#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#define KHEIRON_STATIC 1
#include "../kheiron.h"

static volatile uint8_t *g_stack_target = NULL;

__attribute__((noinline))
void sensitive_operation_stack(void) {
    uint8_t secret[128];
    for (size_t i = 0; i < sizeof(secret); i++) {
        secret[i] = (uint8_t)(0xA5 ^ (uint8_t)i);
    }
    g_stack_target = secret;

    kheiron_secure_wipe(secret, sizeof(secret));

}

__attribute__((noinline))
void sensitive_operation_heap(uint8_t **out_ptr) {
    uint8_t *heap_buf = (uint8_t *)malloc(128);
    for (size_t i = 0; i < 128; i++) {
        heap_buf[i] = (uint8_t)(0x5A + (uint8_t)i);
    }
    *out_ptr = heap_buf;

    kheiron_secure_wipe(heap_buf, 128);
}

int main(void) {
    printf("[DSE TEST] Testing kheiron_secure_wipe under -O3 -flto...\n");

    sensitive_operation_stack();
    int stack_non_zero = 0;
    for (size_t i = 0; i < 128; i++) {
        if (g_stack_target[i] != 0) {
            stack_non_zero++;
        }
    }
    if (stack_non_zero > 0) {
        fprintf(stderr, "[-] FAILURE: Compiler dead-store eliminated stack wipe! Non-zero bytes: %d/128\n", stack_non_zero);
        return 1;
    }
    printf("[+] Stack sanitization verified: 128/128 bytes zeroed (no DSE).\n");

    uint8_t *heap_ptr = NULL;
    sensitive_operation_heap(&heap_ptr);
    int heap_non_zero = 0;
    for (size_t i = 0; i < 128; i++) {
        if (heap_ptr[i] != 0) {
            heap_non_zero++;
        }
    }
    free(heap_ptr);
    if (heap_non_zero > 0) {
        fprintf(stderr, "[-] FAILURE: Compiler dead-store eliminated heap wipe! Non-zero bytes: %d/128\n", heap_non_zero);
        return 1;
    }
    printf("[+] Heap sanitization verified: 128/128 bytes zeroed (no DSE).\n");

    printf("[+] ALL VOLATILE WIPE DSE TESTS PASSED.\n");
    return 0;
}
