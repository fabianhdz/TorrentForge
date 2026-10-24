#include "torrentforge/application.hpp"
#include "torrentforge/bencode.hpp"
#include "torrentforge/hash.hpp"
#include "torrentforge/peer.hpp"
#include "torrentforge/peer_id.hpp"
#include "torrentforge/tracker.hpp"
#include "torrentforge/torrent.hpp"

#include <exception>
#include <filesystem>
#include <ostream>
#include <string>

namespace torrentforge {

int run(std::span<const std::string_view> arguments, std::ostream& output,
        std::ostream& errors) {
    if (arguments.empty() || (arguments.size() == 1 && arguments.front() == "--help")) {
        output << "TorrentForge\n"
                  "Usage: torrentforge [--help]\n"
                  "       torrentforge decode <encoded_value>\n"
                  "       torrentforge info <torrent_file>\n"
                  "       torrentforge peers <torrent_file>\n"
                  "       torrentforge handshake <torrent_file> <peer>\n"
                  "Decodes metadata, discovers peers, and speaks the peer protocol.\n";
        return 0;
    }

    if (arguments.front() == "decode") {
        if (arguments.size() != 2) {
            errors << "Usage: torrentforge decode <encoded_value>\n";
            return 1;
        }
        try {
            const auto decoded_value = decode_bencoded_value(std::string{arguments[1]});
            output << decoded_value.dump() << '\n';
            return 0;
        } catch (const std::exception& error) {
            errors << "TorrentForge: " << error.what() << '\n';
            return 1;
        }
    }

    if (arguments.front() == "info") {
        if (arguments.size() != 2) {
            errors << "Usage: torrentforge info <torrent_file>\n";
            return 1;
        }
        try {
            const auto metadata =
                load_torrent_metadata(std::filesystem::path{std::string{arguments[1]}});
            output << "Tracker URL: " << metadata.tracker_url << '\n'
                   << "Length: " << metadata.length << '\n'
                   << "Info Hash: " << to_hex(metadata.info_hash) << '\n'
                   << "Piece Length: " << metadata.piece_length << '\n'
                   << "Piece Hashes:\n";
            for (const auto& piece_hash : metadata.piece_hashes) {
                output << to_hex(piece_hash) << '\n';
            }
            return 0;
        } catch (const std::exception& error) {
            errors << "TorrentForge: " << error.what() << '\n';
            return 1;
        }
    }

    if (arguments.front() == "peers") {
        if (arguments.size() != 2) {
            errors << "Usage: torrentforge peers <torrent_file>\n";
            return 1;
        }
        try {
            const auto metadata =
                load_torrent_metadata(std::filesystem::path{std::string{arguments[1]}});
            const auto peers = discover_peers(metadata, generate_peer_id());
            for (const auto& peer : peers) {
                output << to_string(peer) << '\n';
            }
            return 0;
        } catch (const std::exception& error) {
            errors << "TorrentForge: " << error.what() << '\n';
            return 1;
        }
    }

    if (arguments.front() == "handshake") {
        if (arguments.size() != 3) {
            errors << "Usage: torrentforge handshake <torrent_file> <peer_ip>:<peer_port>\n";
            return 1;
        }
        try {
            const auto metadata =
                load_torrent_metadata(std::filesystem::path{std::string{arguments[1]}});
            const PeerHandshake local_handshake{
                .reserved = {},
                .info_hash = metadata.info_hash,
                .peer_id = generate_peer_id(),
            };
            const PeerHandshake remote_handshake =
                exchange_peer_handshake(arguments[2], local_handshake);
            output << "Peer ID: " << to_hex(remote_handshake.peer_id) << '\n';
            return 0;
        } catch (const std::exception& error) {
            errors << "TorrentForge: " << error.what() << '\n';
            return 1;
        }
    }

    errors << "TorrentForge: unsupported arguments. Use --help for usage.\n";
    return 1;
}

} // namespace torrentforge
