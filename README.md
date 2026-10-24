# TorrentForge

TorrentForge is a BitTorrent client written in modern C++23 for working with
torrent metadata, peer-to-peer networking, and piece-based file transfers.

## Requirements

- GCC 13 or newer
- CMake 3.20 or newer
- Make or Ninja
- OpenSSL development headers and libraries
- libcurl 7.85 or newer, including development headers

## Build

```sh
cmake -S . -B build
cmake --build build
```

## Run

```sh
./build/torrentforge
./build/torrentforge --help
./build/torrentforge decode 'l5:helloi52ee'
./build/torrentforge info path/to/file.torrent
./build/torrentforge peers path/to/file.torrent
./build/torrentforge handshake path/to/file.torrent 203.0.113.10:6881
```

## Test

```sh
ctest --test-dir build --output-on-failure
```

## Sanitizers

AddressSanitizer and UndefinedBehaviorSanitizer can be enabled for development:

```sh
cmake -S . -B build-sanitize \
  -DCMAKE_BUILD_TYPE=Debug \
  -DTORRENTFORGE_ENABLE_SANITIZERS=ON
cmake --build build-sanitize
ctest --test-dir build-sanitize --output-on-failure
```

Third-party components and their licenses are documented in
[third_party/README.md](third_party/README.md).

Inspired by the [CodeCrafters Build Your Own BitTorrent challenge](https://codecrafters.io/challenges/bittorrent).
