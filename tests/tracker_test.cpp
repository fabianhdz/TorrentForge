#include "torrentforge/tracker.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

std::string bencoded_string(std::string_view value) {
    return std::to_string(value.size()) + ':' + std::string{value};
}

} // namespace

int main() {
    int failures = 0;
    const auto check = [&failures](bool condition, std::string_view message) {
        if (!condition) {
            std::cerr << message << '\n';
            ++failures;
        }
    };

    torrentforge::TorrentMetadata metadata{
        .tracker_url = "http://tracker.example/announce?token=abc",
        .length = 92063,
        .info_hash = {0x00, 0x2f, 0xff, 0x7e, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46,
                      0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50},
        .piece_length = 32768,
        .piece_hashes = {},
    };
    torrentforge::PeerId peer_id{};
    constexpr std::string_view peer_id_text = "-TF0001-123456789012";
    for (std::size_t index = 0; index < peer_id.size(); ++index) {
        peer_id[index] = static_cast<unsigned char>(peer_id_text[index]);
    }

    const std::string request_url = torrentforge::build_tracker_url(metadata, peer_id);
    check(request_url.starts_with("http://tracker.example/announce?token=abc&"),
          "Tracker parameters should preserve an existing query");
    check(request_url.find("info_hash=%00%2F%FF~ABCDEFGHIJKLMNOP") != std::string::npos,
          "The raw 20-byte info hash should be percent encoded");
    check(request_url.find("peer_id=-TF0001-123456789012") != std::string::npos,
          "The 20-byte peer ID should be included");
    check(request_url.find("port=6881&uploaded=0&downloaded=0&left=92063&compact=1") !=
              std::string::npos,
          "All required tracker parameters should be included");

    std::string packed_peers;
    const std::array<std::uint8_t, 12> peer_bytes{
        165, 232, 41, 73, 201, 100,
        71, 224, 0, 29, 200, 214,
    };
    for (const std::uint8_t byte : peer_bytes) {
        packed_peers += static_cast<char>(byte);
    }
    const std::string response =
        "d8:intervali60e5:peers" + bencoded_string(packed_peers) + "e";
    const auto peers = torrentforge::parse_tracker_response(response);
    check(peers.size() == 2, "Every compact six-byte peer should be parsed");
    check(torrentforge::to_string(peers[0]) == "165.232.41.73:51556",
          "The first compact peer should retain its address and network-order port");
    check(torrentforge::to_string(peers[1]) == "71.224.0.29:51414",
          "The second compact peer should retain its address and network-order port");

    const auto expect_error = [&check](const std::string& malformed) {
        try {
            static_cast<void>(torrentforge::parse_tracker_response(malformed));
            check(false, "Invalid tracker responses should be rejected");
        } catch (const std::exception&) {
        }
    };
    expect_error("le");
    expect_error("de");
    expect_error("d5:peersi1ee");
    expect_error("d5:peers5:abcdee");
    expect_error("d14:failure reason4:nopee");

    const auto generated_peer_id = torrentforge::generate_peer_id();
    check(generated_peer_id.size() == 20, "Generated peer IDs should contain exactly 20 bytes");
    check(std::string_view{reinterpret_cast<const char*>(generated_peer_id.data()), 8} ==
              "-TF0001-",
          "Generated peer IDs should identify TorrentForge");

    return failures == 0 ? 0 : 1;
}
