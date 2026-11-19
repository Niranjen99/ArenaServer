#include "arena/net/tcp_server.hpp"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <system_error>

#include "arena/net/socket.hpp"

namespace arena::net {

namespace {

[[noreturn]] void throw_errno(const char* what) {
    throw std::system_error(errno, std::generic_category(), what);
}

constexpr std::size_t kReadChunk = 64 * 1024;
constexpr int kMaxEvents = 256;

}  // namespace

TcpServer::TcpServer(std::uint16_t port, bool log)
    : listen_fd_(listen_tcp(port)),
      epoll_fd_(::epoll_create1(EPOLL_CLOEXEC)),
      wake_fd_(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)),
      port_(local_port(listen_fd_.get())),
      log_(log) {
    if (!epoll_fd_) {
        throw_errno("epoll_create1");
    }
    if (!wake_fd_) {
        throw_errno("eventfd");
    }
    watch(listen_fd_.get(), EPOLLIN, EPOLL_CTL_ADD);
    watch(wake_fd_.get(), EPOLLIN, EPOLL_CTL_ADD);
}

void TcpServer::stop() noexcept {
    // write() is async-signal-safe, so this is allowed inside a signal handler.
    const std::uint64_t one = 1;
    [[maybe_unused]] const auto n = ::write(wake_fd_.get(), &one, sizeof one);
}

void TcpServer::run() {
    std::array<epoll_event, kMaxEvents> events{};
    for (;;) {
        const int n = ::epoll_wait(epoll_fd_.get(), events.data(), kMaxEvents, -1);
        if (n < 0) {
            if (errno == EINTR) {
                continue;  // interrupted by a signal; if it was ours, wake_fd_ is now readable
            }
            throw_errno("epoll_wait");
        }

        for (std::size_t i = 0; i < static_cast<std::size_t>(n); ++i) {
            const int fd = events[i].data.fd;
            const std::uint32_t ev = events[i].events;

            if (fd == wake_fd_.get()) {
                std::uint64_t value = 0;
                [[maybe_unused]] const auto r = ::read(wake_fd_.get(), &value, sizeof value);
                return;
            }
            if (fd == listen_fd_.get()) {
                accept_clients();
                continue;
            }

            // The connection may have been closed by an earlier event in this same batch.
            auto it = conns_.find(fd);
            if (it == conns_.end()) {
                continue;
            }
            if (ev & (EPOLLERR | EPOLLHUP)) {
                close_connection(fd);
                continue;
            }
            if (ev & EPOLLIN) {
                on_readable(it->second);
                it = conns_.find(fd);  // on_readable may have closed it
                if (it == conns_.end()) {
                    continue;
                }
            }
            if (ev & EPOLLOUT) {
                flush(it->second);
            }
        }
    }
}

void TcpServer::accept_clients() {
    // Several clients may be waiting. Take them all until accept says "no more" (EAGAIN).
    for (;;) {
        Fd client{::accept4(listen_fd_.get(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC)};
        if (!client) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            if (errno == EINTR || errno == ECONNABORTED) {
                continue;
            }
            // Out of descriptors or memory: give up for now, epoll reports the listener again.
            std::perror("accept4");
            return;
        }
        const int fd = client.get();
        watch(fd, EPOLLIN, EPOLL_CTL_ADD);
        conns_.emplace(fd, Connection{std::move(client), {}, 0, false});
        if (log_) {
            std::fprintf(stderr, "client connected (fd %d), clients: %zu\n", fd, conns_.size());
        }
    }
}

void TcpServer::on_readable(Connection& conn) {
    // One read per event. Level-triggered epoll reports the socket again if more is waiting,
    // which keeps one busy client from starving the others.
    std::array<char, kReadChunk> buf;
    const ssize_t n = ::recv(conn.fd.get(), buf.data(), buf.size(), 0);
    if (n > 0) {
        conn.out.append(buf.data(), static_cast<std::size_t>(n));
        flush(conn);
        return;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
        return;
    }
    close_connection(conn.fd.get());  // n == 0: client closed; n < 0: real error
}

void TcpServer::flush(Connection& conn) {
    while (conn.sent < conn.out.size()) {
        // MSG_NOSIGNAL: if the client is gone, return EPIPE instead of raising SIGPIPE
        // (which would kill the whole server).
        const ssize_t n = ::send(conn.fd.get(), conn.out.data() + conn.sent,
                                 conn.out.size() - conn.sent, MSG_NOSIGNAL);
        if (n > 0) {
            conn.sent += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // The kernel send buffer is full (the client reads slower than we write).
            // Keep the rest and ask epoll to tell us when there is room again.
            if (!conn.want_write) {
                watch(conn.fd.get(), EPOLLIN | EPOLLOUT, EPOLL_CTL_MOD);
                conn.want_write = true;
            }
            return;
        }
        close_connection(conn.fd.get());  // EPIPE, ECONNRESET, ...
        return;
    }

    // Everything sent. Stop asking for EPOLLOUT, or level-triggered epoll would report
    // "writable" on every loop and spin the CPU.
    conn.out.clear();
    conn.sent = 0;
    if (conn.want_write) {
        watch(conn.fd.get(), EPOLLIN, EPOLL_CTL_MOD);
        conn.want_write = false;
    }
}

void TcpServer::close_connection(int fd) {
    ::epoll_ctl(epoll_fd_.get(), EPOLL_CTL_DEL, fd, nullptr);
    conns_.erase(fd);  // destroys the Connection; its Fd destructor closes the socket
    if (log_) {
        std::fprintf(stderr, "client disconnected (fd %d), clients: %zu\n", fd, conns_.size());
    }
}

void TcpServer::watch(int fd, std::uint32_t events, int op) {
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    if (::epoll_ctl(epoll_fd_.get(), op, fd, &ev) < 0) {
        throw_errno("epoll_ctl");
    }
}

}  // namespace arena::net
