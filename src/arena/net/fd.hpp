#pragma once

#include <unistd.h>

#include <utility>

namespace arena::net {

// Owns one file descriptor (a socket, an epoll instance, an eventfd...) and closes it in the
// destructor. Move-only: exactly one Fd owns a descriptor at any time, so it can never be
// closed twice or forgotten.
class Fd {
public:
    Fd() noexcept = default;
    explicit Fd(int fd) noexcept : fd_(fd) {}

    ~Fd() { reset(); }

    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;

    Fd(Fd&& other) noexcept : fd_(other.release()) {}
    Fd& operator=(Fd&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    [[nodiscard]] int get() const noexcept { return fd_; }
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }
    explicit operator bool() const noexcept { return valid(); }

    // Gives up ownership without closing. The caller must close the returned descriptor.
    [[nodiscard]] int release() noexcept { return std::exchange(fd_, -1); }

    // Closes the current descriptor (if any) and takes ownership of new_fd.
    void reset(int new_fd = -1) noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = new_fd;
    }

private:
    int fd_ = -1;
};

}  // namespace arena::net
