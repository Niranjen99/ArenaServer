#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <cerrno>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "arena/net/fd.hpp"
#include "arena/net/socket.hpp"
#include "arena/net/tcp_server.hpp"

using arena::net::Fd;
using arena::net::TcpServer;

namespace {

bool is_open(int fd) { return ::fcntl(fd, F_GETFD) != -1; }

// Runs a server on a free port in a background thread for the length of a test.
class ServerThread {
public:
    ServerThread() : thread_([this] { server_.run(); }) {}
    ~ServerThread() {
        server_.stop();
        thread_.join();
    }
    std::uint16_t port() const { return server_.port(); }

private:
    TcpServer server_{0};
    std::thread thread_;
};

// A blocking client socket with a 5 second receive timeout, so a broken server fails the test
// instead of hanging it.
Fd connect_to(std::uint16_t port) {
    Fd fd{::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)};
    if (!fd) {
        ADD_FAILURE() << "socket: " << errno;
        return fd;
    }
    timeval timeout{5, 0};
    ::setsockopt(fd.get(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (::connect(fd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof addr) < 0) {
        ADD_FAILURE() << "connect: " << errno;
        fd.reset();
    }
    return fd;
}

bool send_all(int fd, const std::string& data) {
    std::size_t done = 0;
    while (done < data.size()) {
        const ssize_t n = ::send(fd, data.data() + done, data.size() - done, MSG_NOSIGNAL);
        if (n <= 0) {
            return false;
        }
        done += static_cast<std::size_t>(n);
    }
    return true;
}

// Reads exactly `size` bytes, or fewer if the connection closes or the timeout expires.
std::string recv_exact(int fd, std::size_t size) {
    std::string data(size, '\0');
    std::size_t done = 0;
    while (done < size) {
        const ssize_t n = ::recv(fd, data.data() + done, size - done, 0);
        if (n <= 0) {
            break;
        }
        done += static_cast<std::size_t>(n);
    }
    data.resize(done);
    return data;
}

}  // namespace

// ---------- Fd ----------

TEST(Fd, ClosesTheDescriptorWhenDestroyed) {
    int raw = -1;
    {
        Fd fd{::socket(AF_INET, SOCK_STREAM, 0)};
        raw = fd.get();
        ASSERT_TRUE(is_open(raw));
    }
    EXPECT_FALSE(is_open(raw));
}

TEST(Fd, MoveTransfersOwnership) {
    Fd a{::socket(AF_INET, SOCK_STREAM, 0)};
    const int raw = a.get();

    Fd b = std::move(a);
    EXPECT_FALSE(a.valid());  // NOLINT(bugprone-use-after-move): checking the moved-from state
    EXPECT_EQ(b.get(), raw);
    EXPECT_TRUE(is_open(raw));
}

TEST(Fd, MoveAssignmentClosesTheOldDescriptor) {
    Fd a{::socket(AF_INET, SOCK_STREAM, 0)};
    Fd b{::socket(AF_INET, SOCK_STREAM, 0)};
    const int old_b = b.get();
    const int raw_a = a.get();

    b = std::move(a);
    EXPECT_FALSE(is_open(old_b));
    EXPECT_EQ(b.get(), raw_a);
}

// ---------- sockets ----------

TEST(Socket, PortZeroGetsARealPortFromTheOs) {
    Fd fd = arena::net::listen_tcp(0);
    EXPECT_NE(arena::net::local_port(fd.get()), 0);
}

TEST(Socket, ListeningOnABusyPortThrows) {
    Fd first = arena::net::listen_tcp(0);
    const auto port = arena::net::local_port(first.get());
    EXPECT_THROW(arena::net::listen_tcp(port), std::system_error);
}

// ---------- TcpServer ----------

TEST(TcpServer, StopBeforeRunReturnsImmediately) {
    TcpServer server{0};
    server.stop();
    server.run();  // would hang forever if stop() were lost
    SUCCEED();
}

TEST(TcpServer, EchoesAMessage) {
    ServerThread server;
    Fd client = connect_to(server.port());
    ASSERT_TRUE(client);

    ASSERT_TRUE(send_all(client.get(), "hello arena"));
    EXPECT_EQ(recv_exact(client.get(), 11), "hello arena");
}

TEST(TcpServer, EachClientGetsOnlyItsOwnData) {
    ServerThread server;
    constexpr int kClients = 50;

    std::vector<Fd> clients;
    for (int i = 0; i < kClients; ++i) {
        clients.push_back(connect_to(server.port()));
        ASSERT_TRUE(clients.back());
    }
    for (int i = 0; i < kClients; ++i) {
        ASSERT_TRUE(send_all(clients[static_cast<std::size_t>(i)].get(),
                             "client-" + std::to_string(1000 + i)));
    }
    for (int i = 0; i < kClients; ++i) {
        EXPECT_EQ(recv_exact(clients[static_cast<std::size_t>(i)].get(), 11),
                  "client-" + std::to_string(1000 + i));
    }
}

TEST(TcpServer, ADisconnectingClientDoesNotAffectOthers) {
    ServerThread server;
    Fd stays = connect_to(server.port());
    ASSERT_TRUE(stays);

    {
        // Sends data and leaves without reading the echo. The server's send then fails
        // (EPIPE or ECONNRESET); without MSG_NOSIGNAL that would kill the server.
        Fd leaves = connect_to(server.port());
        ASSERT_TRUE(leaves);
        ASSERT_TRUE(send_all(leaves.get(), std::string(100'000, 'x')));
    }

    ASSERT_TRUE(send_all(stays.get(), "still here"));
    EXPECT_EQ(recv_exact(stays.get(), 10), "still here");
}

TEST(TcpServer, LargeMessageSurvivesPartialWrites) {
    // 8 MB is far more than the kernel socket buffers hold, so the server's send() is forced
    // to write only part of it and wait for EPOLLOUT many times.
    ServerThread server;
    Fd client = connect_to(server.port());
    ASSERT_TRUE(client);

    std::string message(8 * 1024 * 1024, '\0');
    for (std::size_t i = 0; i < message.size(); ++i) {
        message[i] = static_cast<char>('a' + i % 26);
    }

    // Send from another thread while this thread reads. Doing both on one thread could
    // deadlock: we would block sending while the server blocks waiting for us to read.
    bool sent = false;
    std::thread sender([&] { sent = send_all(client.get(), message); });
    const std::string echoed = recv_exact(client.get(), message.size());
    sender.join();

    EXPECT_TRUE(sent);
    ASSERT_EQ(echoed.size(), message.size());
    EXPECT_TRUE(echoed == message);
}
