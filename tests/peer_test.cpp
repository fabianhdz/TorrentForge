#include "torrentforge/peer.hpp"

#include <array>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string_view>

int main() {
    int failures = 0;
    const auto check = [&failures](bool condition, std::string_view message) {
        if (!condition) {
            std::cerr << message << '\n';
            ++failures;
        }
    };

    torrentforge::PeerHandshake original{};
    original.reserved[5] = 0x10;
    for (std::size_t index = 0; index < original.info_hash.size(); ++index) {
        original.info_hash[index] = static_cast<unsigned char>(index);
        original.peer_id[index] = static_cast<unsigned char>(0xff - index);
    }

    const auto message = torrentforge::serialize_peer_handshake(original);
    check(message.size() == 68, "A peer handshake should contain exactly 68 bytes");
    check(message[0] == 19, "The first handshake byte should contain the protocol length");
    check(std::string_view{reinterpret_cast<const char*>(message.data() + 1), 19} ==
              "BitTorrent protocol",
          "The handshake should contain the BitTorrent protocol name");

    const auto parsed = torrentforge::parse_peer_handshake(message, original.info_hash);
    check(parsed.reserved == original.reserved, "Reserved handshake bytes should be preserved");
    check(parsed.info_hash == original.info_hash, "The handshake should preserve the info hash");
    check(parsed.peer_id == original.peer_id, "The handshake should preserve the peer ID");

    const auto expect_error = [&check, &original](std::span<const unsigned char> malformed) {
        try {
            static_cast<void>(
                torrentforge::parse_peer_handshake(malformed, original.info_hash));
            check(false, "Invalid peer handshakes should be rejected");
        } catch (const std::exception&) {
        }
    };

    expect_error(std::span<const unsigned char>{message}.first(67));
    auto wrong_length = message;
    wrong_length[0] = 18;
    expect_error(wrong_length);
    auto wrong_protocol = message;
    wrong_protocol[1] = 'X';
    expect_error(wrong_protocol);
    auto wrong_info_hash = message;
    wrong_info_hash[28] ^= 0xff;
    expect_error(wrong_info_hash);

    try {
        static_cast<void>(torrentforge::exchange_peer_handshake("invalid-endpoint", original));
        check(false, "Malformed peer endpoints should be rejected");
    } catch (const std::runtime_error&) {
    }

    return failures == 0 ? 0 : 1;
}
