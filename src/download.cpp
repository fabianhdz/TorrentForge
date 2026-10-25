#include "torrentforge/download.hpp"

#include "torrentforge/hash.hpp"
#include "torrentforge/peer.hpp"
#include "torrentforge/tracker.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace torrentforge {

namespace {

constexpr std::size_t block_size = 16 * 1024;
constexpr std::size_t maximum_pending_requests = 5;
constexpr std::size_t maximum_ignored_messages = 4096;

void append_uint32(std::vector<unsigned char>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<unsigned char>(value >> 24));
    bytes.push_back(static_cast<unsigned char>(value >> 16));
    bytes.push_back(static_cast<unsigned char>(value >> 8));
    bytes.push_back(static_cast<unsigned char>(value));
}

std::uint32_t read_uint32(std::span<const unsigned char> bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
           static_cast<std::uint32_t>(bytes[offset + 3]);
}

PeerMessage make_request(std::uint32_t piece_index, std::uint32_t begin,
                         std::uint32_t length) {
    std::vector<unsigned char> payload;
    payload.reserve(12);
    append_uint32(payload, piece_index);
    append_uint32(payload, begin);
    append_uint32(payload, length);
    return PeerMessage{
        .id = PeerMessageId::request,
        .payload = std::move(payload),
    };
}

void wait_until_ready(PeerConnection& connection) {
    bool received_bitfield = false;
    bool unchoked = false;
    std::size_t ignored_messages = 0;

    while (!received_bitfield) {
        const PeerMessage message = connection.receive_message();
        if (message.id == PeerMessageId::bitfield) {
            received_bitfield = true;
        } else if (message.id == PeerMessageId::unchoke) {
            unchoked = true;
        } else if (message.id == PeerMessageId::choke) {
            unchoked = false;
        } else if (++ignored_messages > maximum_ignored_messages) {
            throw std::runtime_error("Peer did not send its bitfield");
        }
    }

    connection.send_message(PeerMessage{
        .id = PeerMessageId::interested,
        .payload = {},
    });

    while (!unchoked) {
        const PeerMessage message = connection.receive_message();
        if (message.id == PeerMessageId::unchoke) {
            unchoked = true;
        } else if (message.id == PeerMessageId::choke) {
            unchoked = false;
        } else if (++ignored_messages > maximum_ignored_messages) {
            throw std::runtime_error("Peer did not unchoke the client");
        }
    }
}

std::vector<unsigned char> receive_piece(PeerConnection& connection,
                                         std::uint32_t piece_index,
                                         std::size_t piece_size) {
    const std::size_t block_count = (piece_size + block_size - 1) / block_size;
    std::vector<unsigned char> piece(piece_size);
    std::vector<unsigned char> block_states(block_count, 0);
    std::size_t next_block = 0;
    std::size_t pending_requests = 0;
    std::size_t received_blocks = 0;
    std::size_t ignored_messages = 0;
    bool choked = false;

    const auto send_requests = [&] {
        while (!choked && pending_requests < maximum_pending_requests &&
               next_block < block_count) {
            const std::size_t begin = next_block * block_size;
            const std::size_t length = std::min(block_size, piece_size - begin);
            connection.send_message(make_request(piece_index, static_cast<std::uint32_t>(begin),
                                                 static_cast<std::uint32_t>(length)));
            block_states[next_block] = 1;
            ++next_block;
            ++pending_requests;
        }
    };

    send_requests();
    while (received_blocks < block_count) {
        const PeerMessage message = connection.receive_message();
        if (message.id == PeerMessageId::choke) {
            choked = true;
            continue;
        }
        if (message.id == PeerMessageId::unchoke) {
            choked = false;
            send_requests();
            continue;
        }
        if (message.id != PeerMessageId::piece) {
            if (++ignored_messages > maximum_ignored_messages) {
                throw std::runtime_error("Peer sent too many unrelated messages");
            }
            continue;
        }
        if (message.payload.size() < 8) {
            throw std::runtime_error("Peer sent a truncated piece message");
        }

        const std::span<const unsigned char> payload{message.payload};
        const std::uint32_t received_piece_index = read_uint32(payload, 0);
        const std::uint32_t begin = read_uint32(payload, 4);
        if (received_piece_index != piece_index || begin % block_size != 0) {
            throw std::runtime_error("Peer sent a block for an unexpected piece or offset");
        }

        const std::size_t block_index = begin / block_size;
        if (block_index >= block_count || block_states[block_index] != 1) {
            throw std::runtime_error("Peer sent an unrequested or duplicate block");
        }

        const std::size_t expected_length =
            std::min(block_size, piece_size - static_cast<std::size_t>(begin));
        const std::size_t received_length = message.payload.size() - 8;
        if (received_length != expected_length) {
            throw std::runtime_error("Peer sent a block with an unexpected length");
        }

        std::copy(message.payload.begin() + 8, message.payload.end(), piece.begin() + begin);
        block_states[block_index] = 2;
        --pending_requests;
        ++received_blocks;
        send_requests();
    }

    return piece;
}

std::vector<unsigned char> download_from_peer(const TorrentMetadata& metadata,
                                              std::size_t piece_index,
                                              std::size_t piece_size,
                                              const PeerId& peer_id,
                                              const PeerEndpoint& endpoint) {
    PeerConnection connection{to_string(endpoint)};
    const PeerHandshake local_handshake{
        .reserved = {},
        .info_hash = metadata.info_hash,
        .peer_id = peer_id,
    };
    static_cast<void>(connection.exchange_handshake(local_handshake));
    wait_until_ready(connection);

    std::vector<unsigned char> piece =
        receive_piece(connection, static_cast<std::uint32_t>(piece_index), piece_size);
    const std::string_view piece_bytes{reinterpret_cast<const char*>(piece.data()), piece.size()};
    if (sha1(piece_bytes) != metadata.piece_hashes[piece_index]) {
        throw std::runtime_error("Downloaded piece failed SHA-1 verification");
    }
    return piece;
}

struct TemporaryOutput {
    std::filesystem::path path;
    bool published = false;

    ~TemporaryOutput() {
        if (!published) {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    }
};

} // namespace

std::size_t torrent_piece_size(const TorrentMetadata& metadata, std::size_t piece_index) {
    if (metadata.piece_length <= 0 || metadata.length < 0) {
        throw std::runtime_error("Torrent contains invalid piece sizing metadata");
    }
    if (piece_index >= metadata.piece_hashes.size()) {
        throw std::out_of_range("Piece index is outside the torrent");
    }

    const std::uint64_t piece_length = static_cast<std::uint64_t>(metadata.piece_length);
    if (piece_index > std::numeric_limits<std::uint64_t>::max() / piece_length) {
        throw std::runtime_error("Piece offset is out of range");
    }
    const std::uint64_t piece_begin = static_cast<std::uint64_t>(piece_index) * piece_length;
    const std::uint64_t file_length = static_cast<std::uint64_t>(metadata.length);
    if (piece_begin >= file_length) {
        throw std::runtime_error("Piece begins beyond the end of the torrent");
    }

    const std::uint64_t size = std::min(piece_length, file_length - piece_begin);
    if (size > std::numeric_limits<std::size_t>::max() ||
        size > std::numeric_limits<std::uint32_t>::max() ||
        piece_index > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("Piece is too large for the peer protocol");
    }
    return static_cast<std::size_t>(size);
}

std::vector<unsigned char> download_piece(const TorrentMetadata& metadata,
                                          std::size_t piece_index,
                                          const PeerId& peer_id) {
    const std::size_t piece_size = torrent_piece_size(metadata, piece_index);
    const std::vector<PeerEndpoint> peers = discover_peers(metadata, peer_id);
    if (peers.empty()) {
        throw std::runtime_error("Tracker returned no peers");
    }

    std::exception_ptr last_error;
    for (const PeerEndpoint& peer : peers) {
        try {
            return download_from_peer(metadata, piece_index, piece_size, peer_id, peer);
        } catch (...) {
            last_error = std::current_exception();
        }
    }

    if (last_error) {
        std::rethrow_exception(last_error);
    }
    throw std::runtime_error("Unable to download piece from any peer");
}

void write_piece_file(const std::filesystem::path& output_path,
                      std::span<const unsigned char> contents) {
    if (contents.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
        throw std::runtime_error("Piece output is too large to write");
    }
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryOutput temporary{
        .path = output_path.string() + ".torrentforge-part-" + std::to_string(suffix),
    };

    {
        std::ofstream output{temporary.path, std::ios::binary | std::ios::trunc};
        if (!output) {
            throw std::runtime_error("Unable to open piece output file: " +
                                     output_path.string());
        }
        if (!contents.empty()) {
            output.write(reinterpret_cast<const char*>(contents.data()),
                         static_cast<std::streamsize>(contents.size()));
        }
        output.close();
        if (!output) {
            throw std::runtime_error("Unable to write piece output file: " +
                                     output_path.string());
        }
    }

    std::error_code rename_error;
    std::filesystem::rename(temporary.path, output_path, rename_error);
    if (rename_error) {
        throw std::runtime_error("Unable to publish piece output file: " +
                                 rename_error.message());
    }
    temporary.published = true;
}

} // namespace torrentforge
