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
        if (torrentforge::encode_bencoded_value(decoded) != encoded) {
            std::cerr << "Incorrect string encoding for " << encoded << '\n';
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
        if (torrentforge::encode_bencoded_value(decoded) != encoded) {
            std::cerr << "Incorrect integer encoding for " << encoded << '\n';
            ++failures;
        }
    }

    const std::array list_examples{
        std::pair{std::string{"le"}, std::string{"[]"}},
        std::pair{std::string{"l5:helloi52ee"}, std::string{"[\"hello\",52]"}},
        std::pair{std::string{"ll5:helloi52eee"}, std::string{"[[\"hello\",52]]"}},
        std::pair{std::string{"lli4eei5ee"}, std::string{"[[4],5]"}},
        std::pair{std::string{"l0:lee"}, std::string{"[\"\",[]]"}},
    };
    for (const auto& [encoded, expected] : list_examples) {
        const auto decoded = torrentforge::decode_bencoded_value(encoded);
        if (!decoded.is_array() || decoded.dump() != expected) {
            std::cerr << "Incorrect list decoding for " << encoded << '\n';
            ++failures;
        }
        if (torrentforge::encode_bencoded_value(decoded) != encoded) {
            std::cerr << "Incorrect list encoding for " << encoded << '\n';
            ++failures;
        }
    }

    const std::array dictionary_examples{
        std::pair{std::string{"de"}, std::string{"{}"}},
        std::pair{std::string{"d3:foo3:bar5:helloi52ee"},
                  std::string{"{\"foo\":\"bar\",\"hello\":52}"}},
        std::pair{std::string{"d4:listl5:helloi52eee"},
                  std::string{"{\"list\":[\"hello\",52]}"}},
        std::pair{std::string{"d5:innerd1:ai1e1:bleee"},
                  std::string{"{\"inner\":{\"a\":1,\"b\":[]}}"}},
        std::pair{std::string{"ld1:ai1eee"}, std::string{"[{\"a\":1}]"}},
    };
    for (const auto& [encoded, expected] : dictionary_examples) {
        const auto decoded = torrentforge::decode_bencoded_value(encoded);
        if (!decoded.is_structured() || decoded.dump() != expected) {
            std::cerr << "Incorrect dictionary decoding for " << encoded << '\n';
            ++failures;
        }
        if (torrentforge::encode_bencoded_value(decoded) != encoded) {
            std::cerr << "Incorrect dictionary encoding for " << encoded << '\n';
            ++failures;
        }
    }

    for (const auto* encoded : {"", "5hello", "i", "ie", "i-e", "i52", "i+1e",
                                "i01e", "i-0e", "i12xe", "i9223372036854775808e",
                                "l", "li1e", "lxe", "lee", "l3:abe", "1:a1:b",
                                "d", "di1e3:fooe", "d3:fooe", "d1:bi1e1:ai2ee",
                                "d1:ai1e1:ai2ee"}) {
        try {
            torrentforge::decode_bencoded_value(encoded);
            std::cerr << "Expected a decoding error for " << encoded << '\n';
            ++failures;
        } catch (const std::runtime_error&) {
        }
    }

    const auto unordered_dictionary = torrentforge::decode_bencoded_value(
        "d1:bi1e1:ai2ee",
        torrentforge::BencodeDecodeOptions{.require_sorted_dictionary_keys = false});
    if (unordered_dictionary.dump() != "{\"a\":2,\"b\":1}") {
        std::cerr << "Permissive dictionary decoding should retain unordered fields\n";
        ++failures;
    }
    try {
        static_cast<void>(torrentforge::decode_bencoded_value(
            "d1:ai1e1:ai2ee",
            torrentforge::BencodeDecodeOptions{.require_sorted_dictionary_keys = false}));
        std::cerr << "Permissive dictionary decoding should still reject duplicate keys\n";
        ++failures;
    } catch (const std::runtime_error&) {
    }
    return failures == 0 ? 0 : 1;
}
