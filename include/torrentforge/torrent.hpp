#pragma once

#include "torrentforge/hash.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace torrentforge {

struct TorrentMetadata {
    std::string tracker_url;
    std::int64_t length;
    Sha1Digest info_hash;
};

TorrentMetadata parse_torrent_metadata(const std::string& encoded_torrent);
TorrentMetadata load_torrent_metadata(const std::filesystem::path& torrent_path);

} // namespace torrentforge
