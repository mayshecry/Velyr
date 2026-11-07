start {
    let fd := net_socket(net_af_inet(), net_sock_stream(), net_ipproto_tcp())
    say "tcp socket:", fd
    let ok := net_connect(fd, "127.0.0.1", 34567)
    say "tcp connect:", ok
    when ok {
        let sent := net_send(fd, "hello velyr")
        say "tcp sent:", sent
        let data := net_recv(fd, 256)
        say "tcp recv:", data
        net_close(fd)
    }

    let ufd := net_socket(net_af_inet(), net_sock_dgram(), net_ipproto_udp())
    say "udp socket:", ufd
    let usent := net_sendto(ufd, "udp velyr", "127.0.0.1", 34568)
    say "udp sentto:", usent
    net_settimeout(ufd, 500)
    let udata := net_recvfrom(ufd, 256)
    say "udp recvfrom:", udata
    net_close(ufd)

    let ip := net_resolve("example.com")
    say "resolve:", ip
    let r := net_ping_once("127.0.0.1", 500, 32)
    say "ping loopback:", r
}
