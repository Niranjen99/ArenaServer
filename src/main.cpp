#include <iostream>

#include "arena/version.hpp"

int main() {
    std::cout << "ArenaServer " << arena::version() << "\n";
    return 0;
}
