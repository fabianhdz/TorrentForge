#include "torrentforge/hash.hpp"

#include <iostream>
#include <string_view>

int main() {
    int failures = 0;
    const auto check = [&failures](bool condition, std::string_view message) {
        if (!condition) {
            std::cerr << message << '\n';
            ++failures;
        }
    };

    check(torrentforge::to_hex(torrentforge::sha1("")) ==
              "da39a3ee5e6b4b0d3255bfef95601890afd80709",
          "SHA-1 should match the empty-input test vector");
    check(torrentforge::to_hex(torrentforge::sha1("abc")) ==
              "a9993e364706816aba3e25717850c26c9cd0d89d",
          "SHA-1 should match the abc test vector");

    return failures == 0 ? 0 : 1;
}
