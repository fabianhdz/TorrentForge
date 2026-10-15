#include "torrentforge/torrent.hpp"

#include "torrentforge/bencode.hpp"
#include "torrentforge/hash.hpp"

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace torrentforge {

namespace {

const nlohmann::json& require_member(const nlohmann::json& object, const char* name) {
    const auto member = object.find(name);
    if (member == object.end()) {
        throw std::runtime_error(std::string{"Torrent metadata is missing '"} + name + "'");
    }
    return *member;
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

    return TorrentMetadata{
        .tracker_url = announce.get<std::string>(),
        .length = file_length,
        .info_hash = sha1(encode_bencoded_value(info)),
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
