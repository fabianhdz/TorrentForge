# TorrentForge

**TorrentForge is a BitTorrent client built from scratch in modern C++23, designed to explore peer-to-peer networking, torrent metadata parsing, protocol implementation, and piece-based file downloads.**

The application decodes bencoded strings and prints their JSON representation.
It also provides usage output and diagnostics for unsupported arguments. It
does not yet decode other bencoded types, parse metadata, connect to peers,
or download files.

## Requirements

- GCC with C++23 support (GCC 13 or newer recommended)
- CMake 3.20 or newer, including CTest
- Make or Ninja
- GDB, optionally, for debugging

JSON output uses the vendored, header-only nlohmann/json 3.11.2 library.
No package installation or network download is needed to build it. See
[third-party notices](third_party/README.md) for dependency and reused-code licenses.

## Build and run

```sh
cmake -S . -B build
cmake --build build
./build/torrentforge
./build/torrentforge --help
./build/torrentforge decode '5:hello'
```

The last command prints `"hello"` followed by a newline. `0:` produces `""`.
JSON output escapes quotes, backslashes, and control characters.

To select GCC explicitly, add `-DCMAKE_CXX_COMPILER=g++` when configuring a fresh
build directory. Unsupported arguments produce an error on stderr and exit code 1.
Startup, help, and successful decoding exit with code 0. Decode requires one
input value; decoding or JSON serialization errors are reported on stderr
with exit code 1.

The initial decoder has limited input validation: it does not strictly validate
length prefixes, detect truncated strings, or reject trailing bytes. JSON text
output requires valid UTF-8. See [architecture](docs/architecture.md) for the
current parsing behavior.

## Tests

```sh
ctest --test-dir build --output-on-failure
```

CTest runs application and string-decoder unit tests and an executable smoke test. The unit tests
use the standard library and remain active in release builds. To omit tests,
configure with `-DBUILD_TESTING=OFF`.

## Debugging

Install GDB using your operating system's package manager, then:

```sh
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug
gdb --args ./build-debug/torrentforge --help
```

In GDB, use `break main`, `run`, `next`, `step`, and `backtrace` to inspect execution.

AddressSanitizer and UndefinedBehaviorSanitizer are optional:

```sh
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DTORRENTFORGE_ENABLE_SANITIZERS=ON
cmake --build build-sanitize
ctest --test-dir build-sanitize --output-on-failure
```

## Architecture

```text
include/torrentforge/application.hpp  Application entry interface
include/torrentforge/bencode.hpp      Bencode decoder interface
src/application.cpp                  Usage and argument handling
src/bencode.cpp                       String decoding
src/main.cpp                         Process entry and stream wiring
tests/                               Unit tests and CTest registration
third_party/                         Vendored JSON header and license notices
docs/architecture.md                 Component boundaries and interfaces
```

The `torrentforge_core` static library holds application logic; `torrentforge`
provides the process entry point. Tests call the same core through explicit
output streams. See [architecture](docs/architecture.md) for details.

## Planned functionality

Bencoded integers, lists, and dictionaries; torrent metadata parsing;
info hashes and piece hashes; tracker communication and peer discovery; peer
handshakes; piece downloads and complete file downloads. None of these features
is implemented yet.
