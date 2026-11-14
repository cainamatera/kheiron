#define KHEIRON_IMPLEMENTATION
#include "kheiron.h"
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#if defined(_WIN32) || defined(_WIN64)
#include <io.h>
#include <fcntl.h>
#else
#include <signal.h>
#endif

#define KHEIRON_STREAM_BUF_SIZE (64U * 1024U)

int main(void) {
#if defined(_WIN32) || defined(_WIN64)
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) {
        return 1;
    }
#else
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        return 1;
    }
#endif

    if (kheiron_init() != 0) {
        return 1;
    }

    static uint8_t stream_buf[KHEIRON_STREAM_BUF_SIZE];

    while (1) {
        if (kheiron_fill(stream_buf, sizeof(stream_buf)) != 0) {
            kheiron_secure_wipe(stream_buf, sizeof(stream_buf));
            return 1;
        }
        size_t written = fwrite(stream_buf, 1, sizeof(stream_buf), stdout);
        if (written != sizeof(stream_buf)) {
            break;
        }
    }

    kheiron_secure_wipe(stream_buf, sizeof(stream_buf));
    return 0;
}
