#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <string_view>

#include "arena/net/tcp_server.hpp"
#include "arena/version.hpp"

namespace {

// The signal handler can't take arguments, so it reaches the server through this pointer.
arena::net::TcpServer* g_server = nullptr;

extern "C" void on_signal(int) {
    if (g_server != nullptr) {
        g_server->stop();
    }
}

}  // namespace

int main(int argc, char** argv) {
    unsigned long port = 9000;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--port" && i + 1 < argc) {
            char* end = nullptr;
            port = std::strtoul(argv[++i], &end, 10);
            if (*end != '\0' || port > 65535) {
                std::fprintf(stderr, "invalid port: %s\n", argv[i]);
                return 2;
            }
        } else {
            std::fprintf(stderr, "usage: %s [--port N]\n", argv[0]);
            return 2;
        }
    }

    try {
        arena::net::TcpServer server(static_cast<std::uint16_t>(port), /*log=*/true);
        g_server = &server;
        std::signal(SIGINT, on_signal);
        std::signal(SIGTERM, on_signal);

        std::printf("ArenaServer %s: echo server listening on port %u\n",
                    std::string(arena::version()).c_str(), static_cast<unsigned>(server.port()));
        std::fflush(stdout);
        server.run();
        g_server = nullptr;
        std::printf("shutting down\n");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
