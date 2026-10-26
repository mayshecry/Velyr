
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <time.h>

uint64_t vy_asm_cycles(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

void vy_asm_pause(void) {
    /* Fallback. */
}
