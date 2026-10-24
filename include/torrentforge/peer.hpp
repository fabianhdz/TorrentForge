#pragma once

#include "torrentforge/hash.hpp"
#include "torrentforge/peer_id.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace torrentforge {

inline constexpr std::size_t peer_handshake_size = 68;
using PeerReservedBytes = std::array<unsigned char, 8>;
using PeerHandshakeBytes = std::array<unsigned char, peer_handshake_size>;

struct PeerHandshake {
    PeerReservedBytes reserved;
    Sha1Digest info_hash;
    PeerId peer_id;
};

PeerHandshakeBytes serialize_peer_handshake(const PeerHandshake& handshake);
PeerHandshake parse_peer_handshake(std::span<const unsigned char> message,
                                   const Sha1Digest& expected_info_hash);
PeerHandshake exchange_peer_handshake(std::string_view endpoint,
                                      const PeerHandshake& local_handshake);

} // namespace torrentforge
