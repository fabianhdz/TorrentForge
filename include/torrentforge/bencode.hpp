#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace torrentforge {

// Decodes a bencoded string into a JSON string. Other value types are unsupported.
nlohmann::json decode_bencoded_value(const std::string& encoded_value);

} // namespace torrentforge
