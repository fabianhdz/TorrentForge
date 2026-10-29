#pragma once

#include "torrentforge/peer_id.hpp"
#include "torrentforge/torrent.hpp"

#include <cstddef>
#include <filesystem>
#include <span>
#include <vector>

namespace torrentforge {

std::size_t torrent_piece_size(const TorrentMetadata& metadata, std::size_t piece_index);
std::vector<unsigned char> download_piece(const TorrentMetadata& metadata,
                                          std::size_t piece_index,
                                          const PeerId& peer_id);
void download_file(const TorrentMetadata& metadata, const PeerId& peer_id,
                   const std::filesystem::path& output_path);
void write_piece_file(const std::filesystem::path& output_path,
                      std::span<const unsigned char> contents);

} // namespace torrentforge
