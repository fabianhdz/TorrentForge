#include "torrentforge/torrent.hpp"

#include <chrono>
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

std::string make_torrent() {
    const std::string tracker = "http://tracker.example/announce";
    std::string piece_hash(20, '\0');
    piece_hash.front() = static_cast<char>(0xff);
    piece_hash.back() = static_cast<char>(0x80);

    return "d" + bencoded_string("announce") + bencoded_string(tracker) +
           bencoded_string("info") + "d" + bencoded_string("length") + "i92063e" +
           bencoded_string("name") + bencoded_string("sample.txt") +
           bencoded_string("piece length") + "i16384e" + bencoded_string("pieces") +
           bencoded_string(piece_hash) + "ee";
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

    const std::string encoded = make_torrent();
    const auto parsed = torrentforge::parse_torrent_metadata(encoded);
    check(parsed.tracker_url == "http://tracker.example/announce",
          "The announce URL should be read from the root dictionary");
    check(parsed.length == 92063, "The file length should be read from the info dictionary");
    check(torrentforge::to_hex(parsed.info_hash) ==
              "dc68106bd79e0aaed6549b092ba775d5a75c0d33",
          "The info hash should cover the complete bencoded info dictionary");

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
              loaded.info_hash == parsed.info_hash,
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

    try {
        static_cast<void>(torrentforge::load_torrent_metadata(temporary.path.string() + ".missing"));
        check(false, "A missing torrent file should be rejected");
    } catch (const std::runtime_error&) {
    }

    return failures == 0 ? 0 : 1;
}
