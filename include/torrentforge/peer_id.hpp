#pragma once

#include <array>

namespace torrentforge {

using PeerId = std::array<unsigned char, 20>;

PeerId generate_peer_id();

} // namespace torrentforge
