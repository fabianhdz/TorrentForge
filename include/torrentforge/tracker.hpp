#pragma once

#include "torrentforge/torrent.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace torrentforge {

using PeerId = std::array<unsigned char, 20>;

struct PeerEndpoint {
    std::array<std::uint8_t, 4> address;
    std::uint16_t port;

    bool operator==(const PeerEndpoint&) const = default;
};

PeerId generate_peer_id();
std::string build_tracker_url(const TorrentMetadata& metadata, const PeerId& peer_id);
std::vector<PeerEndpoint> parse_tracker_response(const std::string& encoded_response);
std::vector<PeerEndpoint> discover_peers(const TorrentMetadata& metadata,
                                         const PeerId& peer_id);
std::string to_string(const PeerEndpoint& peer);

} // namespace torrentforge
