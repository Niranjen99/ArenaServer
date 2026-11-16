#pragma once

#include <string_view>

namespace arena {

// The server version, set from the project() line in CMakeLists.txt.
std::string_view version() noexcept;

}  // namespace arena
