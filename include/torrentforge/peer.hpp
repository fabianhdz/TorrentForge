#pragma once

#include "torrentforge/hash.hpp"
#include "torrentforge/peer_id.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace torrentforge {

inline constexpr std::size_t peer_handshake_size = 68;
using PeerReservedBytes = std::array<unsigned char, 8>;
using PeerHandshakeBytes = std::array<unsigned char, peer_handshake_size>;

struct PeerHandshake {
    PeerReservedBytes reserved;
    Sha1Digest info_hash;
    PeerId peer_id;
};

enum class PeerMessageId : std::uint8_t {
    choke = 0,
    unchoke = 1,
    interested = 2,
    not_interested = 3,
    have = 4,
    bitfield = 5,
    request = 6,
    piece = 7,
    cancel = 8,
};

struct PeerMessage {
    std::optional<PeerMessageId> id;
    std::vector<unsigned char> payload;
};

class PeerConnection {
public:
    explicit PeerConnection(std::string_view endpoint);
    ~PeerConnection();

    PeerConnection(const PeerConnection&) = delete;
    PeerConnection& operator=(const PeerConnection&) = delete;
    PeerConnection(PeerConnection&& other) noexcept;
    PeerConnection& operator=(PeerConnection&& other) noexcept;

    PeerHandshake exchange_handshake(const PeerHandshake& local_handshake);
    void send_message(const PeerMessage& message);
    PeerMessage receive_message();

private:
    int descriptor_ = -1;
};

PeerHandshakeBytes serialize_peer_handshake(const PeerHandshake& handshake);
PeerHandshake parse_peer_handshake(std::span<const unsigned char> message,
                                   const Sha1Digest& expected_info_hash);
std::vector<unsigned char> serialize_peer_message(const PeerMessage& message);
PeerMessage parse_peer_message(std::span<const unsigned char> message);
PeerHandshake exchange_peer_handshake(std::string_view endpoint,
                                      const PeerHandshake& local_handshake);

} // namespace torrentforge
