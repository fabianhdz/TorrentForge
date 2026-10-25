#include "torrentforge/bencode.hpp"

#include <charconv>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace torrentforge {

using json = nlohmann::json;

namespace {

json decode_next_value(std::string_view& remaining, BencodeDecodeOptions options);

void append_bencoded_string(std::string_view text, std::string& encoded) {
    encoded += std::to_string(text.size());
    encoded += ':';
    encoded += text;
}

void append_bencoded_value(const json& value, std::string& encoded) {
    if (value.is_string()) {
        const auto& text = value.get_ref<const json::string_t&>();
        append_bencoded_string(text, encoded);
        return;
    }

    if (value.is_number_unsigned()) {
        encoded += 'i';
        encoded += std::to_string(value.get<std::uint64_t>());
        encoded += 'e';
        return;
    }

    if (value.is_number_integer()) {
        encoded += 'i';
        encoded += std::to_string(value.get<std::int64_t>());
        encoded += 'e';
        return;
    }

    if (value.is_array()) {
        encoded += 'l';
        for (const auto& element : value) {
            append_bencoded_value(element, encoded);
        }
        encoded += 'e';
        return;
    }

    if (value.is_object()) {
        encoded += 'd';
        for (auto member = value.cbegin(); member != value.cend(); ++member) {
            append_bencoded_string(member.key(), encoded);
            append_bencoded_value(member.value(), encoded);
        }
        encoded += 'e';
        return;
    }

    throw std::runtime_error("JSON value cannot be represented as bencode");
}

json decode_bencoded_string(std::string_view& remaining) {
    const std::size_t colon = remaining.find(':');
    if (colon == std::string_view::npos) {
        throw std::runtime_error("Bencoded string is missing its length separator");
    }

    const std::string_view length_text = remaining.substr(0, colon);
    if (length_text.empty() || (length_text.front() == '0' && length_text.size() != 1)) {
        throw std::runtime_error("Invalid bencoded string length");
    }

    std::size_t length{};
    const auto [end, error] = std::from_chars(
        length_text.data(), length_text.data() + length_text.size(), length);
    if (error == std::errc::result_out_of_range) {
        throw std::runtime_error("Bencoded string length is out of range");
    }
    if (error != std::errc{} || end != length_text.data() + length_text.size()) {
        throw std::runtime_error("Invalid bencoded string length");
    }

    const std::size_t payload_start = colon + 1;
    if (length > remaining.size() - payload_start) {
        throw std::runtime_error("Bencoded string is shorter than its declared length");
    }

    std::string decoded{remaining.substr(payload_start, length)};
    remaining.remove_prefix(payload_start + length);
    return json(std::move(decoded));
}

json decode_bencoded_integer(std::string_view& remaining) {
    const std::size_t terminator = remaining.find('e', 1);
    if (terminator == std::string_view::npos) {
        throw std::runtime_error("Bencoded integer is missing its terminator");
    }

    const std::string_view digits = remaining.substr(1, terminator - 1);
    if (digits.empty() || (digits.front() == '0' && digits.size() != 1) ||
        digits.starts_with("-0")) {
        throw std::runtime_error("Invalid bencoded integer");
    }

    std::int64_t number{};
    const auto [end, error] =
        std::from_chars(digits.data(), digits.data() + digits.size(), number);
    if (error == std::errc::result_out_of_range) {
        throw std::runtime_error("Bencoded integer is out of range");
    }
    if (error != std::errc{} || end != digits.data() + digits.size()) {
        throw std::runtime_error("Invalid bencoded integer");
    }

    remaining.remove_prefix(terminator + 1);
    return json(number);
}

json decode_bencoded_list(std::string_view& remaining, BencodeDecodeOptions options) {
    remaining.remove_prefix(1);
    json decoded = json::array();

    while (true) {
        if (remaining.empty()) {
            throw std::runtime_error("Bencoded list is missing its terminator");
        }
        if (remaining.front() == 'e') {
            remaining.remove_prefix(1);
            return decoded;
        }

        decoded.push_back(decode_next_value(remaining, options));
    }
}

json decode_bencoded_dictionary(std::string_view& remaining, BencodeDecodeOptions options) {
    remaining.remove_prefix(1);
    json decoded = json::object();
    std::string previous_key;
    bool has_previous_key = false;

    while (true) {
        if (remaining.empty()) {
            throw std::runtime_error("Bencoded dictionary is missing its terminator");
        }
        if (remaining.front() == 'e') {
            remaining.remove_prefix(1);
            return decoded;
        }
        if (!std::isdigit(static_cast<unsigned char>(remaining.front()))) {
            throw std::runtime_error("Bencoded dictionary keys must be strings");
        }

        const std::string key = decode_bencoded_string(remaining).get<std::string>();
        if (decoded.contains(key) ||
            (options.require_sorted_dictionary_keys && has_previous_key && key < previous_key)) {
            throw std::runtime_error(
                "Bencoded dictionary keys must be unique and lexicographically sorted");
        }
        if (remaining.empty() || remaining.front() == 'e') {
            throw std::runtime_error("Bencoded dictionary key is missing its value");
        }

        decoded.emplace(key, decode_next_value(remaining, options));
        previous_key = key;
        has_previous_key = true;
    }
}

json decode_next_value(std::string_view& remaining, BencodeDecodeOptions options) {
    if (remaining.empty()) {
        throw std::runtime_error("Cannot decode an empty value");
    }

    const char prefix = remaining.front();
    if (prefix == 'i') {
        return decode_bencoded_integer(remaining);
    }
    if (prefix == 'l') {
        return decode_bencoded_list(remaining, options);
    }
    if (prefix == 'd') {
        return decode_bencoded_dictionary(remaining, options);
    }
    if (std::isdigit(static_cast<unsigned char>(prefix))) {
        return decode_bencoded_string(remaining);
    }

    throw std::runtime_error("Unsupported bencoded value type");
}

} // namespace

json decode_bencoded_value(const std::string& encoded_value, BencodeDecodeOptions options) {
    std::string_view remaining{encoded_value};
    json decoded = decode_next_value(remaining, options);
    if (!remaining.empty()) {
        throw std::runtime_error("Unexpected data after bencoded value");
    }
    return decoded;
}

std::string encode_bencoded_value(const json& value) {
    std::string encoded;
    append_bencoded_value(value, encoded);
    return encoded;
}

} // namespace torrentforge
