#include "arena/net/socket.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cerrno>
#include <system_error>

namespace arena::net {

namespace {

[[noreturn]] void throw_errno(const char* what) {
    throw std::system_error(errno, std::generic_category(), what);
}

}  // namespace

Fd listen_tcp(std::uint16_t port, int backlog) {
    // SOCK_NONBLOCK: accept() returns EAGAIN instead of waiting when nobody is connecting.
    // SOCK_CLOEXEC: the socket is not inherited if this process ever starts another program.
    Fd fd{::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)};
    if (!fd) {
        throw_errno("socket");
    }

    // Lets a restarted server bind the port right away, instead of failing while old
    // connections sit in TIME_WAIT.
    int yes = 1;
    if (::setsockopt(fd.get(), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) < 0) {
        throw_errno("setsockopt(SO_REUSEADDR)");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(fd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof addr) < 0) {
        throw_errno("bind");
    }
    if (::listen(fd.get(), backlog) < 0) {
        throw_errno("listen");
    }
    return fd;
}

std::uint16_t local_port(int fd) {
    sockaddr_in addr{};
    socklen_t len = sizeof addr;
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) < 0) {
        throw_errno("getsockname");
    }
    return ntohs(addr.sin_port);
}

}  // namespace arena::net
