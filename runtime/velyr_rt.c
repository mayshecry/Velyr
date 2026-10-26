#define _POSIX_C_SOURCE 200809L
#include "velyr_rt.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

extern uint64_t vy_asm_cycles(void);
extern void vy_asm_pause(void);

static char vy_stdout_buffer[64u * 1024u];

void vy_runtime_init(void) {
    (void)setvbuf(stdout, vy_stdout_buffer, _IOFBF, sizeof(vy_stdout_buffer));
}

void vy_runtime_shutdown(void) {
    (void)fflush(stdout);
}

int64_t vy_clock_ns(void) {
    struct timespec ts;
#if defined(CLOCK_MONOTONIC_RAW)
    const clockid_t clock_id = CLOCK_MONOTONIC_RAW;
#else
    const clockid_t clock_id = CLOCK_MONOTONIC;
#endif
    if (clock_gettime(clock_id, &ts) != 0) return -1;
    return (int64_t)ts.tv_sec * INT64_C(1000000000) + (int64_t)ts.tv_nsec;
}

int64_t vy_clock_resolution_ns(void) {
    struct timespec ts;
#if defined(CLOCK_MONOTONIC_RAW)
    const clockid_t clock_id = CLOCK_MONOTONIC_RAW;
#else
    const clockid_t clock_id = CLOCK_MONOTONIC;
#endif
    if (clock_getres(clock_id, &ts) != 0) return -1;
    return (int64_t)ts.tv_sec * INT64_C(1000000000) + (int64_t)ts.tv_nsec;
}

int64_t vy_cycles(void) {
    return (int64_t)vy_asm_cycles();
}

int64_t vy_cpu_count(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int64_t)n : 1;
}

void vy_cpu_relax(void) {
    vy_asm_pause();
}

void vy_bench_report(vy_str label, int64_t iterations, int64_t elapsed_ns, int64_t elapsed_ticks) {
    const long double ns = (long double)elapsed_ns;
    const long double ticks = (long double)elapsed_ticks;
    const long double count = (long double)iterations;
    const int64_t resolution_ns = vy_clock_resolution_ns();

    flockfile(stdout);
    (void)fwrite("benchmark: ", 1, 11, stdout);
    if (label.len) (void)fwrite(label.data, 1, label.len, stdout);
    (void)fputc('\n', stdout);

    if (elapsed_ns <= 0) {
        fprintf(stdout, "  timing error: elapsed_ns must be positive (got %lld)\n", (long long)elapsed_ns);
        funlockfile(stdout);
        return;
    }

    fprintf(stdout, "  elapsed seconds:      %.9Lf s\n", ns / 1000000000.0L);
    fprintf(stdout, "  elapsed milliseconds: %.6Lf ms\n", ns / 1000000.0L);
    fprintf(stdout, "  elapsed microseconds: %.3Lf us\n", ns / 1000.0L);
    fprintf(stdout, "  elapsed nanoseconds:  %lld ns\n", (long long)elapsed_ns);
    fprintf(stdout, "  elapsed picoseconds:  %.0Lf ps (converted from ns)\n", ns * 1000.0L);
    fprintf(stdout, "  hardware ticks:       %lld\n", (long long)elapsed_ticks);
    if (elapsed_ticks > 0) fprintf(stdout, "  counter frequency:    %.6Lf GHz\n", ticks / ns);
    fprintf(stdout, "  clock resolution:     %lld ns (reported by OS)\n", (long long)resolution_ns);

    if (iterations > 0) {
        fprintf(stdout, "  iterations:           %lld\n", (long long)iterations);
        fprintf(stdout, "  iterations/second:    %.0Lf\n", count * 1000000000.0L / ns);
        fprintf(stdout, "  time/iteration:       %.9Lf ns\n", ns / count);
        fprintf(stdout, "  time/iteration:       %.3Lf ps (derived average)\n", ns * 1000.0L / count);
    }
    funlockfile(stdout);
}

bool vy_str_equal(vy_str a, vy_str b) {
    return a.len == b.len && (a.len == 0 || memcmp(a.data, b.data, a.len) == 0);
}

#define VY_LINE_SLOTS 16u
#define VY_LINE_CAP 4096u

static char vy_line_pool[VY_LINE_SLOTS][VY_LINE_CAP];
static unsigned vy_line_next = 0;

vy_str vy_read_line(void) {
    char *slot = vy_line_pool[vy_line_next];
    vy_line_next = (vy_line_next + 1u) % VY_LINE_SLOTS;
    if (fgets(slot, (int)VY_LINE_CAP, stdin) == NULL) {
        slot[0] = '\0';
        return (vy_str){slot, 0};
    }
    size_t n = strlen(slot);
    while (n && (slot[n - 1] == '\n' || slot[n - 1] == '\r')) n--;
    slot[n] = '\0';
    return (vy_str){slot, n};
}

bool vy_is_number(vy_str s) {
    if (s.len == 0 || s.len >= 128) return false;
    char buf[128];
    memcpy(buf, s.data, s.len);
    buf[s.len] = '\0';

    char *p = buf;
    if (*p == '+' || *p == '-') p++;
    bool any_digit = false;
    while (*p >= '0' && *p <= '9') { any_digit = true; p++; }
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') { any_digit = true; p++; }
    }
    if (!any_digit) return false;
    if (*p == 'e' || *p == 'E') {
        p++;
        if (*p == '+' || *p == '-') p++;
        if (!(*p >= '0' && *p <= '9')) return false;
        while (*p >= '0' && *p <= '9') p++;
    }
    return *p == '\0';
}

double vy_parse_number(vy_str s) {
    if (!vy_is_number(s)) return 0.0;
    char buf[128];
    memcpy(buf, s.data, s.len);
    buf[s.len] = '\0';
    char *end = NULL;
    errno = 0;
    double value = strtod(buf, &end);
    if (end == buf || (end && *end != '\0') || errno == ERANGE) return 0.0;
    return value;
}

bool vy_eof(void) {
    return feof(stdin) != 0;
}

void vy_out_lock(void) { flockfile(stdout); }
void vy_out_unlock(void) { funlockfile(stdout); }

void vy_print_i64(int64_t value) {
    char buffer[32];
    char *p = buffer + sizeof(buffer);
    uint64_t magnitude;
    if (value < 0) magnitude = (uint64_t)(-(value + 1)) + 1u;
    else magnitude = (uint64_t)value;
    do {
        *--p = (char)('0' + magnitude % 10u);
        magnitude /= 10u;
    } while (magnitude);
    if (value < 0) *--p = '-';
    (void)fwrite(p, 1, (size_t)(buffer + sizeof(buffer) - p), stdout);
}

void vy_print_f64(double value) {
    char buffer[64];
    int n = snprintf(buffer, sizeof(buffer), "%.17g", value);
    if (n > 0) (void)fwrite(buffer, 1, (size_t)n, stdout);
}

void vy_print_bool(bool value) {
    if (value) (void)fwrite("true", 1, 4, stdout);
    else (void)fwrite("false", 1, 5, stdout);
}

void vy_print_str(vy_str value) {
    if (value.len) (void)fwrite(value.data, 1, value.len, stdout);
}

void vy_print_space(void) { (void)fputc(' ', stdout); }
void vy_print_newline(void) { (void)fputc('\n', stdout); }

void vy_panic(vy_str message, vy_str file, int line) {
    (void)fflush(stdout);
    (void)fwrite("panic: ", 1, 7, stderr);
    (void)fwrite(message.data, 1, message.len, stderr);
    (void)fwrite(" at ", 1, 4, stderr);
    (void)fwrite(file.data, 1, file.len, stderr);
    fprintf(stderr, ":%d\n", line);
    abort();
}
