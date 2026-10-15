#include "torrentforge/application.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

int main() {
    int failures = 0;
    const auto check = [&failures](bool condition, std::string_view message) {
        if (!condition) {
            std::cerr << message << '\n';
            ++failures;
        }
    };

    std::ostringstream output;
    std::ostringstream errors;
    check(torrentforge::run({}, output, errors) == 0, "Starting without arguments should succeed");
    check(output.str().find("TorrentForge") != std::string::npos, "Startup should identify the application");
    check(errors.str().empty(), "Startup should not write errors");

    output.str("");
    const std::array help{std::string_view{"--help"}};
    check(torrentforge::run(help, output, errors) == 0, "Help should succeed");
    check(output.str().find("Usage:") != std::string::npos, "Help should describe usage");
    check(errors.str().empty(), "Help should not write errors");

    output.str("");
    const std::array unsupported{std::string_view{"unknown-command"}};
    check(torrentforge::run(unsupported, output, errors) != 0, "Unsupported arguments should fail");
    check(output.str().empty(), "Failures should not write normal output");
    check(!errors.str().empty(), "Failures should explain the error");

    const auto check_decode = [&check](std::string_view encoded, std::string_view expected) {
        std::ostringstream decoded;
        std::ostringstream diagnostics;
        const std::array arguments{std::string_view{"decode"}, encoded};
        check(torrentforge::run(arguments, decoded, diagnostics) == 0, "Decoding should succeed");
        check(decoded.str() == expected, "Decoded output should be JSON followed by a newline");
        check(diagnostics.str().empty(), "Successful decoding should not write errors");
    };
    check_decode("5:hello", "\"hello\"\n");
    check_decode("0:", "\"\"\n");
    check_decode("5:a\"b\\c", "\"a\\\"b\\\\c\"\n");
    check_decode("3:a\nb", "\"a\\nb\"\n");
    check_decode("i0e", "0\n");
    check_decode("i52e", "52\n");
    check_decode("i-52e", "-52\n");
    check_decode("i4294967300e", "4294967300\n");
    check_decode("le", "[]\n");
    check_decode("l5:helloi52ee", "[\"hello\",52]\n");
    check_decode("ll5:helloi52eee", "[[\"hello\",52]]\n");
    check_decode("lli4eei5ee", "[[4],5]\n");
    check_decode("de", "{}\n");
    check_decode("d3:foo3:bar5:helloi52ee", "{\"foo\":\"bar\",\"hello\":52}\n");
    check_decode("d5:innerd1:ai1e1:bleee", "{\"inner\":{\"a\":1,\"b\":[]}}\n");

    for (const auto* encoded : {"", "5hello", "ie", "i01e", "i9223372036854775808e",
                                "l", "li1e", "lxe", "d", "d3:fooe", "d1:bi1e1:ai2ee",
                                "1:a1:b", "1:\xff"}) {
        std::ostringstream decoded;
        std::ostringstream diagnostics;
        const std::array arguments{std::string_view{"decode"}, std::string_view{encoded}};
        check(torrentforge::run(arguments, decoded, diagnostics) == 1, "Decode errors should return exit code 1");
        check(decoded.str().empty(), "Decode errors should not produce partial output");
        check(!diagnostics.str().empty(), "Decode errors should produce a diagnostic");
    }

    output.str("");
    errors.str("");
    const std::array missing_value{std::string_view{"decode"}};
    check(torrentforge::run(missing_value, output, errors) == 1, "Decode requires an input value");
    check(output.str().empty(), "Missing input should not produce normal output");
    check(errors.str().find("Usage:") != std::string::npos, "Missing input should explain usage");

    errors.str("");
    const std::array extra_value{std::string_view{"decode"}, std::string_view{"0:"}, std::string_view{"extra"}};
    check(torrentforge::run(extra_value, output, errors) == 1, "Decode should reject extra arguments");
    check(output.str().empty(), "Extra arguments should not produce normal output");
    check(!errors.str().empty(), "Extra arguments should produce a diagnostic");

    const std::string tracker = "http://tracker.example/announce";
    const std::string torrent = "d8:announce" + std::to_string(tracker.size()) + ':' + tracker +
                                "4:infod6:lengthi92063eee";
    const auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path torrent_path =
        std::filesystem::temp_directory_path() /
        ("torrentforge-application-" + std::to_string(unique_suffix) + ".torrent");
    {
        std::ofstream torrent_file{torrent_path, std::ios::binary};
        torrent_file.write(torrent.data(), static_cast<std::streamsize>(torrent.size()));
    }

    output.str("");
    errors.str("");
    const std::string torrent_path_text = torrent_path.string();
    const std::array info_arguments{std::string_view{"info"},
                                    std::string_view{torrent_path_text}};
    check(torrentforge::run(info_arguments, output, errors) == 0,
          "Reading torrent metadata should succeed");
    check(output.str() == "Tracker URL: http://tracker.example/announce\nLength: 92063\n"
                          "Info Hash: 6c0ef2157656ae55b2e1d65fddbc0de1e3445ee1\n",
          "Torrent metadata should use the documented output format");
    check(errors.str().empty(), "Reading valid torrent metadata should not write errors");

    std::error_code ignored;
    std::filesystem::remove(torrent_path, ignored);

    output.str("");
    errors.str("");
    const std::array missing_info_argument{std::string_view{"info"}};
    check(torrentforge::run(missing_info_argument, output, errors) == 1,
          "Info requires a torrent file path");
    check(output.str().empty(), "A missing torrent path should not produce normal output");
    check(errors.str().find("Usage:") != std::string::npos,
          "A missing torrent path should explain usage");

    return failures == 0 ? 0 : 1;
}
