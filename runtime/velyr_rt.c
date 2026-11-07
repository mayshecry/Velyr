#define _GNU_SOURCE
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "velyr_rt.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

extern uint64_t vy_asm_cycles(void);
extern void vy_asm_pause(void);

static char vy_stdout_buffer[64u * 1024u];

static int g_argc = 0;
static char **g_argv = NULL;

void vy_runtime_init(int argc, char **argv) {
    g_argc = argc;
    g_argv = argv;
    (void)setvbuf(stdout, vy_stdout_buffer, _IOFBF, sizeof(vy_stdout_buffer));
}

int64_t vy_arg_count(void) {
    return g_argc > 0 ? g_argc - 1 : 0;
}

vy_str vy_arg(int64_t index) {
    if (index < 0 || index >= g_argc - 1 || !g_argv) return (vy_str){"", 0};
    const char *s = g_argv[index + 1];
    if (!s) return (vy_str){"", 0};
    return (vy_str){s, strlen(s)};
}

int64_t vy_parse_int(vy_str s) {
    char buf[64];
    size_t n = s.len < sizeof(buf) - 1 ? s.len : sizeof(buf) - 1;
    memcpy(buf, s.data, n);
    buf[n] = '\0';
    char *end = NULL;
    errno = 0;
    int64_t v = strtoll(buf, &end, 10);
    if (end == buf || errno == ERANGE) return 0;
    return v;
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

#define VY_NET_SLOTS 8u
#define VY_NET_SLOT_CAP 65536u

static char vy_net_pool[VY_NET_SLOTS][VY_NET_SLOT_CAP];
static unsigned vy_net_next = 0;

int64_t vy_net_af_inet(void) { return AF_INET; }
int64_t vy_net_af_inet6(void) { return AF_INET6; }
int64_t vy_net_sock_stream(void) { return SOCK_STREAM; }
int64_t vy_net_sock_dgram(void) { return SOCK_DGRAM; }
int64_t vy_net_sock_raw(void) { return SOCK_RAW; }
int64_t vy_net_ipproto_icmp(void) { return IPPROTO_ICMP; }
int64_t vy_net_ipproto_tcp(void) { return IPPROTO_TCP; }
int64_t vy_net_ipproto_udp(void) { return IPPROTO_UDP; }
int64_t vy_net_sol_socket(void) { return SOL_SOCKET; }
int64_t vy_net_sndbuf(void) { return SO_SNDBUF; }

int64_t vy_net_socket(int64_t domain, int64_t type, int64_t protocol) {
    return (int64_t)socket((int)domain, (int)type, (int)protocol);
}

static bool vy_net_addr_inet(const char *addr, size_t len, struct sockaddr_in *sa) {
    char buf[256];
    size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    memcpy(buf, addr, n);
    buf[n] = '\0';
    memset(sa, 0, sizeof(*sa));
    sa->sin_family = AF_INET;
    if (inet_pton(AF_INET, buf, &sa->sin_addr) <= 0) return false;
    return true;
}

bool vy_net_connect(int64_t fd, vy_str addr, int64_t port) {
    struct sockaddr_in sa;
    if (!vy_net_addr_inet(addr.data, addr.len, &sa)) {
        struct addrinfo *res = NULL;
        char port_s[16];
        char hbuf[256];
        size_t n = addr.len < sizeof(hbuf) - 1 ? addr.len : sizeof(hbuf) - 1;
        memcpy(hbuf, addr.data, n);
        hbuf[n] = '\0';
        snprintf(port_s, sizeof(port_s), "%lld", (long long)port);
        if (getaddrinfo(hbuf, port_s, NULL, &res) != 0) return false;
        bool ok = false;
        for (struct addrinfo *r = res; r; r = r->ai_next) {
            if (connect((int)fd, r->ai_addr, (socklen_t)r->ai_addrlen) == 0) { ok = true; break; }
        }
        freeaddrinfo(res);
        return ok;
    }
    sa.sin_port = htons((uint16_t)port);
    return connect((int)fd, (struct sockaddr *)&sa, sizeof(sa)) == 0;
}

bool vy_net_bind(int64_t fd, int64_t port) {
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons((uint16_t)port);
    int one = 1;
    setsockopt((int)fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    return bind((int)fd, (struct sockaddr *)&sa, sizeof(sa)) == 0;
}

bool vy_net_listen(int64_t fd, int64_t backlog) {
    return listen((int)fd, (int)backlog) == 0;
}

int64_t vy_net_accept(int64_t fd) {
    return (int64_t)accept((int)fd, NULL, NULL);
}

int64_t vy_net_send(int64_t fd, vy_str data) {
    ssize_t n = send((int)fd, data.data, data.len, 0);
    return n < 0 ? -1 : (int64_t)n;
}

vy_str vy_net_recv(int64_t fd, int64_t max_len) {
    char *slot = vy_net_pool[vy_net_next];
    vy_net_next = (vy_net_next + 1u) % VY_NET_SLOTS;
    size_t cap = max_len > 0 && (size_t)max_len < VY_NET_SLOT_CAP ? (size_t)max_len : VY_NET_SLOT_CAP - 1u;
    ssize_t n = recv((int)fd, slot, cap, 0);
    if (n <= 0) return (vy_str){slot, 0};
    return (vy_str){slot, (size_t)n};
}

int64_t vy_net_sendto(int64_t fd, vy_str data, vy_str addr, int64_t port) {
    struct sockaddr_in sa;
    if (!vy_net_addr_inet(addr.data, addr.len, &sa)) return -1;
    sa.sin_port = htons((uint16_t)port);
    return (int64_t)sendto((int)fd, data.data, data.len, 0, (struct sockaddr *)&sa, sizeof(sa));
}

vy_str vy_net_recvfrom(int64_t fd, int64_t max_len) {
    char *slot = vy_net_pool[vy_net_next];
    vy_net_next = (vy_net_next + 1u) % VY_NET_SLOTS;
    size_t cap = max_len > 0 && (size_t)max_len < VY_NET_SLOT_CAP ? (size_t)max_len : VY_NET_SLOT_CAP - 1u;
    ssize_t n = recvfrom((int)fd, slot, cap, 0, NULL, NULL);
    if (n <= 0) return (vy_str){slot, 0};
    return (vy_str){slot, (size_t)n};
}

void vy_net_close(int64_t fd) {
    close((int)fd);
}

void vy_net_setblocking(int64_t fd, bool blocking) {
    int flags = fcntl((int)fd, F_GETFL, 0);
    if (flags < 0) return;
    if (blocking) flags &= ~O_NONBLOCK;
    else flags |= O_NONBLOCK;
    (void)fcntl((int)fd, F_SETFL, flags);
}

void vy_net_settimeout(int64_t fd, int64_t ms) {
    struct timeval tv;
    tv.tv_sec = (long)(ms / 1000);
    tv.tv_usec = (long)((ms % 1000) * 1000);
    setsockopt((int)fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt((int)fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

bool vy_net_setsockopt(int64_t fd, int64_t level, int64_t optname, int64_t value) {
    return setsockopt((int)fd, (int)level, (int)optname, &value, sizeof(value)) == 0;
}

vy_str vy_net_resolve(vy_str host) {
    static char buf[INET_ADDRSTRLEN];
    char hbuf[256];
    size_t n = host.len < sizeof(hbuf) - 1 ? host.len : sizeof(hbuf) - 1;
    memcpy(hbuf, host.data, n);
    hbuf[n] = '\0';
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(hbuf, NULL, &hints, &res) != 0) return (vy_str){buf, 0};
    const char *ip = NULL;
    for (struct addrinfo *r = res; r; r = r->ai_next) {
        if (r->ai_family == AF_INET) {
            struct sockaddr_in *sa = (struct sockaddr_in *)r->ai_addr;
            ip = inet_ntop(AF_INET, &sa->sin_addr, buf, sizeof(buf));
            if (ip) break;
        }
    }
    freeaddrinfo(res);
    if (!ip) return (vy_str){buf, 0};
    return (vy_str){buf, strlen(ip)};
}

static uint16_t vy_net_checksum(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t sum = 0;
    while (len > 1) {
        sum += (uint32_t)((uint16_t)p[0] << 8 | p[1]);
        p += 2;
        len -= 2;
    }
    if (len) sum += (uint32_t)((uint16_t)p[0] << 8);
    while (sum >> 16) sum = (sum & 0xffffu) + (sum >> 16);
    return (uint16_t)~sum;
}

int64_t vy_net_ping_once(vy_str host, int64_t timeout_ms, int64_t size) {
    char hbuf[256];
    size_t n = host.len < sizeof(hbuf) - 1 ? host.len : sizeof(hbuf) - 1;
    memcpy(hbuf, host.data, n);
    hbuf[n] = '\0';
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_RAW;
    hints.ai_protocol = IPPROTO_ICMP;
    if (getaddrinfo(hbuf, NULL, &hints, &res) != 0) return -1;
    int fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (fd < 0) { freeaddrinfo(res); return -1; }
    struct timeval tv;
    tv.tv_sec = (long)(timeout_ms / 1000);
    tv.tv_usec = (long)((timeout_ms % 1000) * 1000);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    int ttl = 64;
    setsockopt(fd, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl));
    uint16_t ident = (uint16_t)(getpid() & 0xffffu);
    int payload = size > 0 ? (int)size : 32;
    if (payload > 60000) payload = 60000;
    uint8_t packet[60064];
    memset(packet, 0x41, sizeof(packet));
    struct icmphdr *icmp = (struct icmphdr *)packet;
    icmp->type = ICMP_ECHO;
    icmp->code = 0;
    icmp->un.echo.id = ident;
    icmp->un.echo.sequence = 1;
    size_t total = 8u + (size_t)payload;
    icmp->checksum = 0;
    icmp->checksum = vy_net_checksum(icmp, total);
    struct sockaddr_in *to = (struct sockaddr_in *)res->ai_addr;
    int64_t start = vy_clock_ns();
    int64_t result = -1;
    if (sendto(fd, packet, total, 0, (struct sockaddr *)to, sizeof(*to)) > 0) {
        uint8_t reply[65536];
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        if (poll(&pfd, 1, (int)timeout_ms) > 0) {
            ssize_t got = recv(fd, reply, sizeof(reply), 0);
            if (got >= (ssize_t)(sizeof(struct iphdr) + 8)) {
                struct iphdr *ip = (struct iphdr *)reply;
                if (ip->protocol == IPPROTO_ICMP) {
                    struct icmphdr *ic = (struct icmphdr *)(reply + (size_t)ip->ihl * 4u);
                    if (ic->type == ICMP_ECHOREPLY && ic->un.echo.id == ident && ic->un.echo.sequence == 1) {
                        int64_t end = vy_clock_ns();
                        result = end - start;
                    }
                }
            }
        }
    }
    close(fd);
    freeaddrinfo(res);
    return result;
}

int64_t vy_net_ping_fd(int64_t fd, vy_str addr, int64_t port, int64_t size) {
    struct sockaddr_in sa;
    if (!vy_net_addr_inet(addr.data, addr.len, &sa)) return -1;
    sa.sin_port = htons((uint16_t)port);

    static uint16_t seq = 0;
    uint16_t ident = (uint16_t)(getpid() & 0xffffu);
    int payload = size > 0 ? (int)size : 32;
    if (payload > 60000) payload = 60000;

    static uint8_t packet[60064];
    struct icmphdr *icmp = (struct icmphdr *)packet;
    icmp->type = ICMP_ECHO;
    icmp->code = 0;
    icmp->un.echo.id = ident;
    icmp->un.echo.sequence = ++seq;
    size_t total = 8u + (size_t)payload;
    memset(packet + 8, 0x41, (size_t)payload);
    icmp->checksum = 0;
    icmp->checksum = vy_net_checksum(icmp, total);

    int64_t start = vy_clock_ns();
    int64_t result = -1;
    if (sendto((int)fd, packet, total, 0, (struct sockaddr *)&sa, sizeof(sa)) > 0) {
        uint8_t reply[65536];
        ssize_t got = recv((int)fd, reply, sizeof(reply), 0);
        if (got >= (ssize_t)(sizeof(struct iphdr) + 8)) {
            struct iphdr *ip = (struct iphdr *)reply;
            if (ip->protocol == IPPROTO_ICMP) {
                struct icmphdr *ic = (struct icmphdr *)(reply + (size_t)ip->ihl * 4u);
                if (ic->type == ICMP_ECHOREPLY && ic->un.echo.id == ident) {
                    int64_t end = vy_clock_ns();
                    result = end - start;
                }
            }
        }
    }
    return result;
}

int64_t vy_net_sendmmsg_batch(int64_t fd, vy_str data, vy_str addr, int64_t port, int64_t count) {
    struct sockaddr_in sa;
    int connected = 0;
    if (addr.len > 0) {
        if (!vy_net_addr_inet(addr.data, addr.len, &sa)) return -1;
        sa.sin_port = htons((uint16_t)port);
    } else {
        connected = 1;
    }

    int batch = count > 256 ? 256 : (int)count;
    if (batch < 1) batch = 1;

    static struct mmsghdr msgvec[256];
    static struct iovec iovecs[256];

    for (int i = 0; i < batch; i++) {
        memset(&msgvec[i], 0, sizeof(msgvec[i]));
        if (!connected) {
            msgvec[i].msg_hdr.msg_name = &sa;
            msgvec[i].msg_hdr.msg_namelen = sizeof(sa);
        }
        msgvec[i].msg_hdr.msg_iov = &iovecs[i];
        msgvec[i].msg_hdr.msg_iovlen = 1;
        iovecs[i].iov_base = (void *)data.data;
        iovecs[i].iov_len = data.len;
    }

    int sent = sendmmsg((int)fd, msgvec, batch, 0);
    return sent < 0 ? -1 : (int64_t)sent;
}
