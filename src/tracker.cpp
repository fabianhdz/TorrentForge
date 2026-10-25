#include "torrentforge/tracker.hpp"

#include "torrentforge/bencode.hpp"

#include <curl/curl.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace torrentforge {

namespace {

constexpr std::size_t maximum_tracker_response_size = 4 * 1024 * 1024;

struct CurlGlobalState {
    CurlGlobalState() {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
            throw std::runtime_error("Unable to initialize HTTP support");
        }
    }

    ~CurlGlobalState() {
        curl_global_cleanup();
    }
};

struct CurlEasyDeleter {
    void operator()(CURL* handle) const {
        curl_easy_cleanup(handle);
    }
};

using CurlEasyHandle = std::unique_ptr<CURL, CurlEasyDeleter>;

void initialize_curl() {
    static const CurlGlobalState global_state;
    static_cast<void>(global_state);
}

std::string percent_encode(const unsigned char* bytes, std::size_t size) {
    constexpr char hex_digits[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(size * 3);

    for (std::size_t index = 0; index < size; ++index) {
        const unsigned char byte = bytes[index];
        const bool unreserved =
            (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
            (byte >= '0' && byte <= '9') || byte == '-' || byte == '.' || byte == '_' ||
            byte == '~';
        if (unreserved) {
            encoded += static_cast<char>(byte);
        } else {
            encoded += '%';
            encoded += hex_digits[byte >> 4];
            encoded += hex_digits[byte & 0x0f];
        }
    }

    return encoded;
}

std::size_t receive_response(char* data, std::size_t element_size, std::size_t element_count,
                             void* destination) noexcept {
    if (element_size != 0 && element_count > std::numeric_limits<std::size_t>::max() / element_size) {
        return 0;
    }

    const std::size_t byte_count = element_size * element_count;
    auto& response = *static_cast<std::string*>(destination);
    if (response.size() > maximum_tracker_response_size ||
        byte_count > maximum_tracker_response_size - response.size()) {
        return 0;
    }

    try {
        response.append(data, byte_count);
    } catch (...) {
        return 0;
    }
    return byte_count;
}

void require_curl_option(CURLcode result) {
    if (result != CURLE_OK) {
        throw std::runtime_error("Unable to configure tracker request");
    }
}

std::string request_tracker(std::string_view url) {
    initialize_curl();
    CurlEasyHandle request{curl_easy_init()};
    if (!request) {
        throw std::runtime_error("Unable to create tracker request");
    }

    std::string response;
    std::array<char, CURL_ERROR_SIZE> error_buffer{};
    const std::string url_text{url};

    require_curl_option(curl_easy_setopt(request.get(), CURLOPT_URL, url_text.c_str()));
    require_curl_option(curl_easy_setopt(request.get(), CURLOPT_HTTPGET, 1L));
    require_curl_option(curl_easy_setopt(request.get(), CURLOPT_PROTOCOLS_STR, "http,https"));
    require_curl_option(curl_easy_setopt(request.get(), CURLOPT_REDIR_PROTOCOLS_STR, "http,https"));
    require_curl_option(curl_easy_setopt(request.get(), CURLOPT_FOLLOWLOCATION, 1L));
    require_curl_option(curl_easy_setopt(request.get(), CURLOPT_MAXREDIRS, 5L));
    require_curl_option(curl_easy_setopt(request.get(), CURLOPT_CONNECTTIMEOUT, 10L));
    require_curl_option(curl_easy_setopt(request.get(), CURLOPT_TIMEOUT, 30L));
    require_curl_option(curl_easy_setopt(request.get(), CURLOPT_NOSIGNAL, 1L));
    require_curl_option(curl_easy_setopt(request.get(), CURLOPT_FAILONERROR, 1L));
    require_curl_option(curl_easy_setopt(request.get(), CURLOPT_ACCEPT_ENCODING, ""));
    require_curl_option(curl_easy_setopt(request.get(), CURLOPT_USERAGENT, "TorrentForge/0.1"));
    require_curl_option(curl_easy_setopt(request.get(), CURLOPT_ERRORBUFFER, error_buffer.data()));
    require_curl_option(curl_easy_setopt(request.get(), CURLOPT_WRITEFUNCTION, receive_response));
    require_curl_option(curl_easy_setopt(request.get(), CURLOPT_WRITEDATA, &response));

    CURLcode result = CURLE_OK;
    for (int attempt = 0; attempt < 3; ++attempt) {
        response.clear();
        error_buffer.fill('\0');
        result = curl_easy_perform(request.get());
        if (result != CURLE_COULDNT_CONNECT || attempt == 2) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
    }

    if (result != CURLE_OK) {
        const std::string detail = error_buffer.front() == '\0'
                                       ? curl_easy_strerror(result)
                                       : error_buffer.data();
        throw std::runtime_error("Tracker request failed: " + detail);
    }

    return response;
}

} // namespace

std::string build_tracker_url(const TorrentMetadata& metadata, const PeerId& peer_id) {
    std::string url = metadata.tracker_url;
    std::string fragment;
    if (const std::size_t fragment_start = url.find('#'); fragment_start != std::string::npos) {
        fragment = url.substr(fragment_start);
        url.erase(fragment_start);
    }

    if (!url.contains('?')) {
        url += '?';
    } else if (!url.ends_with('?') && !url.ends_with('&')) {
        url += '&';
    }

    url += "info_hash=" + percent_encode(metadata.info_hash.data(), metadata.info_hash.size());
    url += "&peer_id=" + percent_encode(peer_id.data(), peer_id.size());
    url += "&port=6881&uploaded=0&downloaded=0&left=" + std::to_string(metadata.length);
    url += "&compact=1";
    url += fragment;
    return url;
}

std::vector<PeerEndpoint> parse_tracker_response(const std::string& encoded_response) {
    const nlohmann::json response = decode_bencoded_value(
        encoded_response, BencodeDecodeOptions{.require_sorted_dictionary_keys = false});
    if (!response.is_object()) {
        throw std::runtime_error("Tracker response must be a dictionary");
    }

    if (const auto failure = response.find("failure reason"); failure != response.end()) {
        if (failure->is_string()) {
            throw std::runtime_error("Tracker rejected request: " + failure->get<std::string>());
        }
        throw std::runtime_error("Tracker returned an invalid failure reason");
    }

    const auto peers_value = response.find("peers");
    if (peers_value == response.end() || !peers_value->is_string()) {
        throw std::runtime_error("Tracker response 'peers' must be a string");
    }

    const auto& packed_peers = peers_value->get_ref<const nlohmann::json::string_t&>();
    if (packed_peers.size() % 6 != 0) {
        throw std::runtime_error("Tracker response contains an incomplete compact peer");
    }

    std::vector<PeerEndpoint> peers;
    peers.reserve(packed_peers.size() / 6);
    for (std::size_t offset = 0; offset < packed_peers.size(); offset += 6) {
        const auto byte = [&packed_peers, offset](std::size_t index) {
            return static_cast<unsigned char>(packed_peers[offset + index]);
        };
        peers.push_back(PeerEndpoint{
            .address = {byte(0), byte(1), byte(2), byte(3)},
            .port = static_cast<std::uint16_t>((static_cast<std::uint16_t>(byte(4)) << 8) |
                                               static_cast<std::uint16_t>(byte(5))),
        });
    }
    return peers;
}

std::vector<PeerEndpoint> discover_peers(const TorrentMetadata& metadata,
                                         const PeerId& peer_id) {
    return parse_tracker_response(request_tracker(build_tracker_url(metadata, peer_id)));
}

std::string to_string(const PeerEndpoint& peer) {
    return std::to_string(peer.address[0]) + '.' + std::to_string(peer.address[1]) + '.' +
           std::to_string(peer.address[2]) + '.' + std::to_string(peer.address[3]) + ':' +
           std::to_string(peer.port);
}

} // namespace torrentforge
