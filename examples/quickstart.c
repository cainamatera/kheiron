#define KHEIRON_IMPLEMENTATION
#include "../kheiron.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>

typedef struct {
    int id;
    uint8_t buffer[32];
} worker_arg_t;

static void *worker_func(void *arg) {
    worker_arg_t *w = (worker_arg_t *)arg;
    kheiron_fill(w->buffer, sizeof(w->buffer));
    return NULL;
}

int main(void) {
    char token[33];
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%^&*";
    kheiron_string(token, 32, charset);
    printf("Token: %s\n", token);

    int deck[52];
    for (int i = 0; i < 52; i++) {
        deck[i] = i + 1;
    }
    kheiron_shuffle(deck, 52, sizeof(int));
    printf("First 5 cards: %d %d %d %d %d\n", deck[0], deck[1], deck[2], deck[3], deck[4]);

    double u_val = kheiron_uniform_double();
    double g_val = kheiron_gaussian(0.0, 1.0);
    printf("Uniform [0.0, 1.0): %.8f\n", u_val);
    printf("Gaussian (mean=0, stddev=1): %.8f\n", g_val);

    pthread_t thread;
    worker_arg_t worker;
    worker.id = 1;
    pthread_create(&thread, NULL, worker_func, &worker);
    pthread_join(thread, NULL);

    kheiron_ctx_t ctx;
    uint8_t seed[32] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    kheiron_ctx_init(&ctx, seed, sizeof(seed));
    uint8_t ctx_bytes[16];
    kheiron_ctx_fill(&ctx, ctx_bytes, sizeof(ctx_bytes));
    printf("Thread worker byte 0: %02x, Ctx byte 0: %02x\n", worker.buffer[0], ctx_bytes[0]);

    uint8_t subkey_auth[32];
    uint8_t subkey_enc[32];
    const char info_auth[] = "kheiron-auth-v1";
    const char info_enc[] = "kheiron-enc-v1";
    kheiron_derive(&ctx, subkey_auth, sizeof(subkey_auth), info_auth, strlen(info_auth));
    kheiron_derive(&ctx, subkey_enc, sizeof(subkey_enc), info_enc, strlen(info_enc));
    kheiron_ctx_wipe(&ctx);

    printf("Subkey Auth byte 0: %02x\n", subkey_auth[0]);
    printf("Subkey Enc byte 0: %02x\n", subkey_enc[0]);

    kheiron_secure_wipe(subkey_auth, sizeof(subkey_auth));
    kheiron_secure_wipe(subkey_enc, sizeof(subkey_enc));
    return 0;
}
