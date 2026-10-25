#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace torrentforge {

struct BencodeDecodeOptions {
    bool require_sorted_dictionary_keys = true;
};

// Decodes one complete supported bencoded value into its JSON representation.
nlohmann::json decode_bencoded_value(
    const std::string& encoded_value,
    BencodeDecodeOptions options = {});

// Encodes a supported JSON value using canonical bencode ordering.
std::string encode_bencoded_value(const nlohmann::json& value);

} // namespace torrentforge
