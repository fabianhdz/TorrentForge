#include "torrentforge/application.hpp"
#include "torrentforge/bencode.hpp"

#include <exception>
#include <ostream>
#include <string>

namespace torrentforge {

int run(std::span<const std::string_view> arguments, std::ostream& output,
        std::ostream& errors) {
    if (arguments.empty() || (arguments.size() == 1 && arguments.front() == "--help")) {
        output << "TorrentForge\n"
                  "Usage: torrentforge [--help]\n"
                  "       torrentforge decode <encoded_value>\n"
                  "Decodes bencoded strings as JSON.\n";
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

    errors << "TorrentForge: unsupported arguments. Use --help for usage.\n";
    return 1;
}

} // namespace torrentforge
