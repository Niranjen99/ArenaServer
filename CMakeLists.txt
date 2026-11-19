cmake_minimum_required(VERSION 3.20)

project(ArenaServer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

option(ARENA_WERROR "Treat compiler warnings as errors" ON)
option(ARENA_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(ARENA_BUILD_TESTS "Build the unit tests" ON)

# Settings shared by every target in this project (but not by GoogleTest).
add_library(arena_options INTERFACE)
target_compile_options(arena_options INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
if(ARENA_WERROR)
  target_compile_options(arena_options INTERFACE -Werror)
endif()
if(ARENA_SANITIZE)
  # -fno-sanitize-recover: undefined behaviour stops the program (so a test fails) instead of
  # only printing a message and carrying on.
  target_compile_options(arena_options INTERFACE
    -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
  target_link_options(arena_options INTERFACE -fsanitize=address,undefined)
endif()

# Everything except main() lives in this library, so the tests can link against it.
add_library(arena_core
  src/arena/version.cpp
  src/arena/net/socket.cpp
  src/arena/net/tcp_server.cpp
)
target_include_directories(arena_core PUBLIC src)
target_compile_definitions(arena_core PRIVATE ARENA_VERSION="${PROJECT_VERSION}")
target_link_libraries(arena_core PRIVATE arena_options)

add_executable(arena_server src/main.cpp)
target_link_libraries(arena_server PRIVATE arena_core arena_options)

if(ARENA_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
