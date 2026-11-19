include(FetchContent)

# GoogleTest is downloaded at configure time and used only by the tests.
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.17.0
  GIT_SHALLOW TRUE
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(arena_tests
  test_version.cpp
  test_tcp_server.cpp
)
target_link_libraries(arena_tests PRIVATE arena_core arena_options GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(arena_tests)
