#include "torrentforge/application.hpp"

#include <array>
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
        check(torrentforge::run(arguments, decoded, diagnostics) == 0, "String decoding should succeed");
        check(decoded.str() == expected, "Decoded output should be a JSON string followed by a newline");
        check(diagnostics.str().empty(), "Successful decoding should not write errors");
    };
    check_decode("5:hello", "\"hello\"\n");
    check_decode("0:", "\"\"\n");
    check_decode("5:a\"b\\c", "\"a\\\"b\\\\c\"\n");
    check_decode("3:a\nb", "\"a\\nb\"\n");

    for (const auto* encoded : {"", "5hello", "i42e", "1:\xff"}) {
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

    return failures == 0 ? 0 : 1;
}
