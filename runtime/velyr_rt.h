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

void vy_runtime_init(int argc, char **argv);
void vy_runtime_shutdown(void);
int64_t vy_arg_count(void);
vy_str vy_arg(int64_t index);
int64_t vy_parse_int(vy_str s);

int64_t vy_clock_ns(void);
int64_t vy_clock_resolution_ns(void);
int64_t vy_cycles(void);
int64_t vy_cpu_count(void);
void vy_cpu_relax(void);
void vy_bench_report(vy_str label, int64_t iterations, int64_t elapsed_ns, int64_t elapsed_ticks);

bool vy_str_equal(vy_str a, vy_str b);

vy_str vy_read_line(void);
bool vy_is_number(vy_str s);
double vy_parse_number(vy_str s);
bool vy_eof(void);

int64_t vy_net_af_inet(void);
int64_t vy_net_af_inet6(void);
int64_t vy_net_sock_stream(void);
int64_t vy_net_sock_dgram(void);
int64_t vy_net_sock_raw(void);
int64_t vy_net_ipproto_icmp(void);
int64_t vy_net_ipproto_tcp(void);
int64_t vy_net_ipproto_udp(void);
int64_t vy_net_sol_socket(void);
int64_t vy_net_sndbuf(void);
int64_t vy_net_socket(int64_t domain, int64_t type, int64_t protocol);
bool vy_net_connect(int64_t fd, vy_str addr, int64_t port);
bool vy_net_bind(int64_t fd, int64_t port);
bool vy_net_listen(int64_t fd, int64_t backlog);
int64_t vy_net_accept(int64_t fd);
int64_t vy_net_send(int64_t fd, vy_str data);
vy_str vy_net_recv(int64_t fd, int64_t max_len);
int64_t vy_net_sendto(int64_t fd, vy_str data, vy_str addr, int64_t port);
vy_str vy_net_recvfrom(int64_t fd, int64_t max_len);
void vy_net_close(int64_t fd);
int64_t vy_net_sendmmsg_batch(int64_t fd, vy_str data, vy_str addr, int64_t port, int64_t count);
void vy_net_setblocking(int64_t fd, bool blocking);
void vy_net_settimeout(int64_t fd, int64_t ms);
bool vy_net_setsockopt(int64_t fd, int64_t level, int64_t optname, int64_t value);
vy_str vy_net_resolve(vy_str host);
int64_t vy_net_ping_once(vy_str host, int64_t timeout_ms, int64_t size);
int64_t vy_net_ping_fd(int64_t fd, vy_str addr, int64_t port, int64_t size);

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
