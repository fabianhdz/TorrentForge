#include "torrentforge/bencode.hpp"

#include <charconv>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace torrentforge {

using json = nlohmann::json;

namespace {

json decode_bencoded_integer(const std::string& encoded_value) {
    if (encoded_value.size() < 3 || encoded_value.front() != 'i' ||
        encoded_value.back() != 'e') {
        throw std::runtime_error("Invalid bencoded integer: " + encoded_value);
    }

    const std::string_view digits{encoded_value.data() + 1, encoded_value.size() - 2};
    if (digits.empty() || (digits.front() == '0' && digits.size() != 1) ||
        digits.starts_with("-0")) {
        throw std::runtime_error("Invalid bencoded integer: " + encoded_value);
    }

    std::int64_t number{};
    const auto [end, error] =
        std::from_chars(digits.data(), digits.data() + digits.size(), number);
    if (error == std::errc::result_out_of_range) {
        throw std::runtime_error("Bencoded integer is out of range: " + encoded_value);
    }
    if (error != std::errc{} || end != digits.data() + digits.size()) {
        throw std::runtime_error("Invalid bencoded integer: " + encoded_value);
    }

    return json(number);
}

} // namespace

json decode_bencoded_value(const std::string& encoded_value) {
    if (encoded_value.empty()) {
        throw std::runtime_error("Cannot decode an empty value");
    }

    if (encoded_value.front() == 'i') {
        return decode_bencoded_integer(encoded_value);
    }

    if (std::isdigit(static_cast<unsigned char>(encoded_value.front()))) {
        // Example: "5:hello" -> "hello"
        size_t colon_index = encoded_value.find(':');
        if (colon_index != std::string::npos) {
            std::string number_string = encoded_value.substr(0, colon_index);
            int64_t number = std::atoll(number_string.c_str());
            std::string str = encoded_value.substr(colon_index + 1, number);
            return json(str);
        } else {
            throw std::runtime_error("Invalid encoded value: " + encoded_value);
        }
    } else {
        throw std::runtime_error("Unhandled encoded value: " + encoded_value);
    }
}

} // namespace torrentforge
