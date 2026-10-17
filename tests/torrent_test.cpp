#include "torrentforge/torrent.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

std::string bencoded_string(std::string_view value) {
    return std::to_string(value.size()) + ':' + std::string{value};
}

std::string make_piece_hashes() {
    std::string piece_hashes(6 * torrentforge::sha1_digest_size, '\0');
    for (std::size_t index = 0; index < piece_hashes.size(); ++index) {
        piece_hashes[index] = static_cast<char>((index * 37) % 256);
    }
    return piece_hashes;
}

std::string make_torrent(std::int64_t file_length, std::int64_t piece_length,
                         std::string_view piece_hashes) {
    const std::string tracker = "http://tracker.example/announce";

    return "d" + bencoded_string("announce") + bencoded_string(tracker) +
           bencoded_string("info") + "d" + bencoded_string("length") + "i" +
           std::to_string(file_length) + "e" +
           bencoded_string("name") + bencoded_string("sample.txt") +
           bencoded_string("piece length") + "i" + std::to_string(piece_length) + "e" +
           bencoded_string("pieces") + bencoded_string(piece_hashes) + "ee";
}

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

    const std::string packed_piece_hashes = make_piece_hashes();
    const std::string encoded = make_torrent(92063, 16384, packed_piece_hashes);
    const auto parsed = torrentforge::parse_torrent_metadata(encoded);
    check(parsed.tracker_url == "http://tracker.example/announce",
          "The announce URL should be read from the root dictionary");
    check(parsed.length == 92063, "The file length should be read from the info dictionary");
    check(torrentforge::to_hex(parsed.info_hash) ==
              "9c4fa26ed7ad5c3fc824b04bb19a6ed1a69381d7",
          "The info hash should cover the complete bencoded info dictionary");
    check(parsed.piece_length == 16384,
          "The piece length should be read from the info dictionary");
    check(parsed.piece_hashes.size() == 6,
          "The packed pieces string should be split into 20-byte hashes");
    check(torrentforge::to_hex(parsed.piece_hashes.front()) ==
              "00254a6f94b9de03284d7297bce1062b50759abf",
          "Piece hashes should preserve leading zero bytes");
    check(torrentforge::to_hex(parsed.piece_hashes.back()) ==
              "7499bee3082d52779cc1e60b30557a9fc4e90e33",
          "Every packed piece hash should be retained");

    const auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryFile temporary{
        std::filesystem::temp_directory_path() /
        ("torrentforge-metadata-" + std::to_string(unique_suffix) + ".torrent")};
    {
        std::ofstream output{temporary.path, std::ios::binary};
        output.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
        check(static_cast<bool>(output), "The test torrent should be written successfully");
    }

    const auto loaded = torrentforge::load_torrent_metadata(temporary.path);
    check(loaded.tracker_url == parsed.tracker_url && loaded.length == parsed.length &&
              loaded.info_hash == parsed.info_hash &&
              loaded.piece_length == parsed.piece_length &&
              loaded.piece_hashes == parsed.piece_hashes,
          "Loading a torrent file should preserve its parsed metadata");

    const auto expect_error = [&check](const std::string& malformed) {
        try {
            static_cast<void>(torrentforge::parse_torrent_metadata(malformed));
            check(false, "Invalid torrent metadata should be rejected");
        } catch (const std::exception&) {
        }
    };

    expect_error("le");
    expect_error("d4:infod6:lengthi1eee");
    expect_error("d8:announcei1e4:infod6:lengthi1eee");
    expect_error("d8:announce3:url4:infolee");
    expect_error("d8:announce3:url4:infodee");
    expect_error("d8:announce3:url4:infod6:length1:1ee");
    expect_error("d8:announce3:url4:infod6:lengthi-1eee");
    expect_error(make_torrent(1, 0, std::string(20, '\0')));
    expect_error(make_torrent(1, 1, std::string(19, '\0')));
    expect_error(make_torrent(2, 1, std::string(20, '\0')));

    try {
        static_cast<void>(torrentforge::load_torrent_metadata(temporary.path.string() + ".missing"));
        check(false, "A missing torrent file should be rejected");
    } catch (const std::runtime_error&) {
    }

    return failures == 0 ? 0 : 1;
}
