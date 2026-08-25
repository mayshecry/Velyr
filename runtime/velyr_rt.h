#ifndef VELYR_RT_H
#define VELYR_RT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__GNUC__) || defined(__clang__)
#define VY_NORETURN __attribute__((noreturn))
#define VY_HOT __attribute__((hot))
#else
#define VY_NORETURN
#define VY_HOT
#endif

typedef struct {
    const char *data;
    size_t len;
} vy_str;

#define VY_STR_LIT(s) ((vy_str){(s), sizeof(s) - 1u})

void vy_runtime_init(void);
void vy_runtime_shutdown(void);

int64_t vy_clock_ns(void);
int64_t vy_clock_resolution_ns(void);
int64_t vy_cycles(void);
int64_t vy_cpu_count(void);
void vy_cpu_relax(void);
void vy_bench_report(vy_str label, int64_t iterations, int64_t elapsed_ns, int64_t elapsed_ticks);

bool vy_str_equal(vy_str a, vy_str b);

/* stdin input helpers */
vy_str vy_read_line(void);
bool vy_is_number(vy_str s);
double vy_parse_number(vy_str s);
bool vy_eof(void);

void vy_out_lock(void);
void vy_out_unlock(void);
void vy_print_i64(int64_t value);
void vy_print_f64(double value);
void vy_print_bool(bool value);
void vy_print_str(vy_str value);
void vy_print_space(void);
void vy_print_newline(void);
VY_NORETURN void vy_panic(vy_str message, vy_str file, int line);

#endif
