#pragma once

#include <cstdint>

#include "arena/net/fd.hpp"

namespace arena::net {

// Creates a non-blocking TCP socket listening on all interfaces at `port`.
// Port 0 lets the operating system pick a free port (see local_port()).
// Throws std::system_error on failure.
Fd listen_tcp(std::uint16_t port, int backlog = 128);

// The port a socket is bound to. Throws std::system_error on failure.
std::uint16_t local_port(int fd);

}  // namespace arena::net
