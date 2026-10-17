#include "torrentforge/torrent.hpp"

#include "torrentforge/bencode.hpp"
#include "torrentforge/hash.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace torrentforge {

namespace {

const nlohmann::json& require_member(const nlohmann::json& object, const char* name) {
    const auto member = object.find(name);
    if (member == object.end()) {
        throw std::runtime_error(std::string{"Torrent metadata is missing '"} + name + "'");
    }
    return *member;
}

std::vector<Sha1Digest> parse_piece_hashes(std::string_view packed_hashes) {
    if (packed_hashes.size() % sha1_digest_size != 0) {
        throw std::runtime_error(
            "Torrent metadata 'pieces' length must be a multiple of 20 bytes");
    }

    std::vector<Sha1Digest> piece_hashes;
    piece_hashes.reserve(packed_hashes.size() / sha1_digest_size);

    for (std::size_t offset = 0; offset < packed_hashes.size(); offset += sha1_digest_size) {
        Sha1Digest piece_hash{};
        std::memcpy(piece_hash.data(), packed_hashes.data() + offset, piece_hash.size());
        piece_hashes.push_back(piece_hash);
    }

    return piece_hashes;
}

} // namespace

TorrentMetadata parse_torrent_metadata(const std::string& encoded_torrent) {
    const nlohmann::json root = decode_bencoded_value(encoded_torrent);
    if (!root.is_object()) {
        throw std::runtime_error("Torrent metadata must be a dictionary");
    }

    const nlohmann::json& announce = require_member(root, "announce");
    if (!announce.is_string()) {
        throw std::runtime_error("Torrent metadata 'announce' must be a string");
    }

    const nlohmann::json& info = require_member(root, "info");
    if (!info.is_object()) {
        throw std::runtime_error("Torrent metadata 'info' must be a dictionary");
    }

    const nlohmann::json& length = require_member(info, "length");
    if (!length.is_number_integer()) {
        throw std::runtime_error("Torrent metadata 'length' must be an integer");
    }

    const std::int64_t file_length = length.get<std::int64_t>();
    if (file_length < 0) {
        throw std::runtime_error("Torrent metadata 'length' cannot be negative");
    }

    const nlohmann::json& piece_length_value = require_member(info, "piece length");
    if (!piece_length_value.is_number_integer()) {
        throw std::runtime_error("Torrent metadata 'piece length' must be an integer");
    }

    const std::int64_t piece_length = piece_length_value.get<std::int64_t>();
    if (piece_length <= 0) {
        throw std::runtime_error("Torrent metadata 'piece length' must be positive");
    }

    const nlohmann::json& pieces = require_member(info, "pieces");
    if (!pieces.is_string()) {
        throw std::runtime_error("Torrent metadata 'pieces' must be a string");
    }

    std::vector<Sha1Digest> piece_hashes =
        parse_piece_hashes(pieces.get_ref<const nlohmann::json::string_t&>());
    const std::uint64_t expected_piece_count =
        file_length == 0
            ? 0
            : 1 + (static_cast<std::uint64_t>(file_length) - 1) /
                      static_cast<std::uint64_t>(piece_length);
    if (piece_hashes.size() != expected_piece_count) {
        throw std::runtime_error(
            "Torrent metadata piece hash count does not match the file length");
    }

    return TorrentMetadata{
        .tracker_url = announce.get<std::string>(),
        .length = file_length,
        .info_hash = sha1(encode_bencoded_value(info)),
        .piece_length = piece_length,
        .piece_hashes = std::move(piece_hashes),
    };
}

TorrentMetadata load_torrent_metadata(const std::filesystem::path& torrent_path) {
    std::ifstream input{torrent_path, std::ios::binary};
    if (!input) {
        throw std::runtime_error("Unable to open torrent file: " + torrent_path.string());
    }

    const std::string encoded_torrent{std::istreambuf_iterator<char>{input},
                                      std::istreambuf_iterator<char>{}};
    if (input.bad()) {
        throw std::runtime_error("Unable to read torrent file: " + torrent_path.string());
    }

    return parse_torrent_metadata(encoded_torrent);
}

} // namespace torrentforge
