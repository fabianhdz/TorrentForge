#include "torrentforge/bencode.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

int main() {
    int failures = 0;
    const std::array examples{
        std::pair{std::string{"5:hello"}, std::string{"hello"}},
        std::pair{std::string{"0:"}, std::string{}},
        std::pair{std::string{"12:hello world!"}, std::string{"hello world!"}},
        std::pair{std::string{"5:a:b:c"}, std::string{"a:b:c"}},
        std::pair{std::string{"5:caf\xc3\xa9"}, std::string{"caf\xc3\xa9"}},
        std::pair{std::string{"3:a\0b", 5}, std::string{"a\0b", 3}},
    };
    for (const auto& [encoded, expected] : examples) {
        const auto decoded = torrentforge::decode_bencoded_value(encoded);
        if (!decoded.is_string() || decoded.get<std::string>() != expected) {
            std::cerr << "Incorrect string decoding for " << encoded << '\n';
            ++failures;
        }
    }

    for (const auto* encoded : {"", "5hello", "i42e"}) {
        try {
            torrentforge::decode_bencoded_value(encoded);
            std::cerr << "Expected a decoding error for " << encoded << '\n';
            ++failures;
        } catch (const std::runtime_error&) {
        }
    }
    return failures == 0 ? 0 : 1;
}
