#include "torrentforge/download.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct TemporaryFile {
    std::filesystem::path path;

    ~TemporaryFile() {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
};

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
        .tracker_url = "http://tracker.example/announce",
        .length = 40000,
        .info_hash = {},
        .piece_length = 16384,
        .piece_hashes = std::vector<torrentforge::Sha1Digest>(3),
    };
    check(torrentforge::torrent_piece_size(metadata, 0) == 16384,
          "A regular piece should use the torrent piece length");
    check(torrentforge::torrent_piece_size(metadata, 1) == 16384,
          "Every complete piece should use the torrent piece length");
    check(torrentforge::torrent_piece_size(metadata, 2) == 7232,
          "The final piece should use the remaining file length");

    try {
        static_cast<void>(torrentforge::torrent_piece_size(metadata, 3));
        check(false, "An out-of-range piece index should be rejected");
    } catch (const std::out_of_range&) {
    }

    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryFile output{
        std::filesystem::temp_directory_path() /
        ("torrentforge-piece-" + std::to_string(suffix) + ".bin")};
    const std::vector<unsigned char> contents{0x00, 0xff, 0x80, 0x41, 0x00};
    torrentforge::write_piece_file(output.path, contents);

    std::ifstream input{output.path, std::ios::binary};
    const std::string saved{std::istreambuf_iterator<char>{input},
                            std::istreambuf_iterator<char>{}};
    check(saved.size() == contents.size(), "The piece file should preserve its binary length");
    check(std::equal(saved.begin(), saved.end(), contents.begin(),
                     [](char left, unsigned char right) {
                         return static_cast<unsigned char>(left) == right;
                     }),
          "The piece file should preserve every binary byte");

    TemporaryFile empty_output{
        std::filesystem::temp_directory_path() /
        ("torrentforge-empty-" + std::to_string(suffix) + ".bin")};
    {
        std::ofstream previous_contents{empty_output.path, std::ios::binary};
        previous_contents << "old contents";
    }
    const torrentforge::TorrentMetadata empty_metadata{
        .tracker_url = "http://tracker.example/announce",
        .length = 0,
        .info_hash = {},
        .piece_length = 16384,
        .piece_hashes = {},
    };
    torrentforge::download_file(empty_metadata, torrentforge::PeerId{}, empty_output.path);
    check(std::filesystem::file_size(empty_output.path) == 0,
          "Downloading an empty torrent should atomically replace the output with an empty file");

    torrentforge::TorrentMetadata invalid_metadata = metadata;
    invalid_metadata.piece_hashes.pop_back();
    TemporaryFile invalid_output{
        std::filesystem::temp_directory_path() /
        ("torrentforge-invalid-" + std::to_string(suffix) + ".bin")};
    try {
        torrentforge::download_file(invalid_metadata, torrentforge::PeerId{},
                                    invalid_output.path);
        check(false, "A mismatched piece count should be rejected before downloading");
    } catch (const std::runtime_error&) {
    }
    check(!std::filesystem::exists(invalid_output.path),
          "Invalid metadata should not create an output file");

    return failures == 0 ? 0 : 1;
}
