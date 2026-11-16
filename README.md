# ArenaServer

[![CI](https://github.com/Niranjen99/ArenaServer/actions/workflows/ci.yml/badge.svg)](https://github.com/Niranjen99/ArenaServer/actions/workflows/ci.yml)

A real-time multiplayer game server in C++20 for Linux: one server process decides where every player is, and browsers connect to it over WebSockets to play a small top-down arena game.

**Status: work in progress.** Only the project skeleton exists so far (build, tests, CI). See the [Roadmap](#roadmap).

**Tech stack:** C++20, Linux (`epoll`, `timerfd`), CMake, GoogleTest, GitHub Actions. The server itself has no third-party dependencies.

## Build and run

Linux only. On Windows, use WSL (Ubuntu).

One-time setup on Ubuntu:

```bash
sudo apt update
sudo apt install -y build-essential cmake gdb valgrind
```

Build, test and run:

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/arena_server
```

From a Windows Command Prompt, the same through WSL:

```bat
wsl -d Ubuntu --cd /mnt/d/Projects/ArenaServer -- cmake -S . -B build
wsl -d Ubuntu --cd /mnt/d/Projects/ArenaServer -- cmake --build build -j
wsl -d Ubuntu --cd /mnt/d/Projects/ArenaServer -- ctest --test-dir build --output-on-failure
```

To build with AddressSanitizer and UndefinedBehaviorSanitizer (this is what CI does):

```bash
cmake -S . -B build-asan -DARENA_SANITIZE=ON
cmake --build build-asan -j
ctest --test-dir build-asan --output-on-failure
```

## Layout

```
src/
  main.cpp         the server executable
  arena/           everything else, built as a library so tests can link it
tests/             GoogleTest unit tests
.github/workflows/ CI: gcc and clang, sanitizers on
```

## Roadmap

- [x] Project skeleton: CMake build, unit tests, CI
- [ ] TCP server on `epoll` with non-blocking sockets
- [ ] WebSocket handshake and framing, written by hand
- [ ] Game world with a fixed tick rate
- [ ] Message format and the browser client
- [ ] Walls, coins and a scoreboard
- [ ] Load-testing bots and measurements
- [ ] Handling slow, broken and lagging clients
- [ ] Sending only what changed
- [ ] Valgrind and sanitizer runs under load
