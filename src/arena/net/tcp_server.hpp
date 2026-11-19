#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "arena/net/fd.hpp"

namespace arena::net {

// A single-threaded TCP echo server built on epoll (level-triggered).
// Every byte a client sends is sent back to that client.
//
// Threading: run() runs on one thread. stop() may be called from any thread or from a
// signal handler.
class TcpServer {
public:
    // Opens the listening socket right away, so port() is valid before run() starts.
    explicit TcpServer(std::uint16_t port, bool log = false);

    [[nodiscard]] std::uint16_t port() const noexcept { return port_; }

    // Handles events until stop() is called.
    void run();

    // Asks run() to return. Safe from another thread or a signal handler.
    void stop() noexcept;

private:
    struct Connection {
        Fd fd;
        std::string out;          // bytes waiting to be sent
        std::size_t sent = 0;     // how much of `out` has been sent already
        bool want_write = false;  // EPOLLOUT is registered for this socket
    };

    void accept_clients();
    void on_readable(Connection& conn);
    void flush(Connection& conn);
    void close_connection(int fd);
    void watch(int fd, std::uint32_t events, int op);

    Fd listen_fd_;
    Fd epoll_fd_;
    Fd wake_fd_;  // eventfd: stop() writes to it, which wakes epoll_wait
    std::uint16_t port_ = 0;
    bool log_ = false;
    std::unordered_map<int, Connection> conns_;  // key: the socket's fd
};

}  // namespace arena::net
