#include "torrentforge/bencode.hpp"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>

// Reused decoder: see third_party/licenses/bencode.MIT.txt.
namespace torrentforge {

using json = nlohmann::json;

json decode_bencoded_value(const std::string& encoded_value) {
    if (std::isdigit(encoded_value[0])) {
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
