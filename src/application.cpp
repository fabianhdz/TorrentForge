#include "torrentforge/application.hpp"
#include "torrentforge/bencode.hpp"
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
                  "Decodes bencoded values and reads torrent metadata.\n";
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
                   << "Length: " << metadata.length << '\n';
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
