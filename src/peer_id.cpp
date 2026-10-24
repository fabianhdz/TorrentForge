#include "torrentforge/peer_id.hpp"

#include <algorithm>
#include <cstddef>
#include <random>
#include <string_view>

namespace torrentforge {

PeerId generate_peer_id() {
    constexpr std::string_view prefix = "-TF0001-";
    constexpr std::string_view alphabet =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

    PeerId peer_id{};
    std::copy(prefix.begin(), prefix.end(), peer_id.begin());

    std::random_device random_source;
    std::uniform_int_distribution<std::size_t> choose(0, alphabet.size() - 1);
    for (std::size_t index = prefix.size(); index < peer_id.size(); ++index) {
        peer_id[index] = static_cast<unsigned char>(alphabet[choose(random_source)]);
    }
    return peer_id;
}

} // namespace torrentforge
