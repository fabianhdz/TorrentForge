#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace torrentforge {

// Decodes one complete bencoded string or integer into its JSON representation.
nlohmann::json decode_bencoded_value(const std::string& encoded_value);

} // namespace torrentforge
