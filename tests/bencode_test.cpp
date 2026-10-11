#include "torrentforge/bencode.hpp"

#include <array>
#include <cstdint>
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

    const std::array integer_examples{
        std::pair{std::string{"i0e"}, std::int64_t{0}},
        std::pair{std::string{"i52e"}, std::int64_t{52}},
        std::pair{std::string{"i-52e"}, std::int64_t{-52}},
        std::pair{std::string{"i4294967300e"}, std::int64_t{4294967300}},
        std::pair{std::string{"i9223372036854775807e"}, std::int64_t{9223372036854775807}},
        std::pair{std::string{"i-9223372036854775808e"}, std::int64_t{-9223372036854775807 - 1}},
    };
    for (const auto& [encoded, expected] : integer_examples) {
        const auto decoded = torrentforge::decode_bencoded_value(encoded);
        if (!decoded.is_number_integer() || decoded.get<std::int64_t>() != expected) {
            std::cerr << "Incorrect integer decoding for " << encoded << '\n';
            ++failures;
        }
    }

    for (const auto* encoded : {"", "5hello", "i", "ie", "i-e", "i52", "i+1e",
                                "i01e", "i-0e", "i12xe", "i9223372036854775808e"}) {
        try {
            torrentforge::decode_bencoded_value(encoded);
            std::cerr << "Expected a decoding error for " << encoded << '\n';
            ++failures;
        } catch (const std::runtime_error&) {
        }
    }
    return failures == 0 ? 0 : 1;
}
