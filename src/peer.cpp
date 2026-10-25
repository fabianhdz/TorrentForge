#include "torrentforge/peer.hpp"

#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace torrentforge {

namespace {

constexpr std::string_view protocol_name = "BitTorrent protocol";
constexpr auto socket_timeout = std::chrono::seconds{10};
constexpr auto message_timeout = std::chrono::seconds{30};
constexpr std::uint32_t maximum_peer_message_size = 4 * 1024 * 1024;

class Socket {
public:
    explicit Socket(int descriptor = -1) : descriptor_{descriptor} {}

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    Socket(Socket&& other) noexcept : descriptor_{std::exchange(other.descriptor_, -1)} {}

    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) {
            if (descriptor_ >= 0) {
                ::close(descriptor_);
            }
            descriptor_ = std::exchange(other.descriptor_, -1);
        }
        return *this;
    }

    ~Socket() {
        if (descriptor_ >= 0) {
            ::close(descriptor_);
        }
    }

    [[nodiscard]] int get() const {
        return descriptor_;
    }

    [[nodiscard]] int release() {
        return std::exchange(descriptor_, -1);
    }

private:
    int descriptor_;
};

struct AddressInfoDeleter {
    void operator()(addrinfo* addresses) const {
        freeaddrinfo(addresses);
    }
};

using AddressInfo = std::unique_ptr<addrinfo, AddressInfoDeleter>;
using Deadline = std::chrono::steady_clock::time_point;

std::pair<std::string, std::string> split_endpoint(std::string_view endpoint) {
    std::string_view host;
    std::string_view port_text;

    if (endpoint.starts_with('[')) {
        const std::size_t closing_bracket = endpoint.find(']');
        if (closing_bracket == std::string_view::npos || closing_bracket + 1 >= endpoint.size() ||
            endpoint[closing_bracket + 1] != ':') {
            throw std::runtime_error("Peer endpoint must use [address]:port");
        }
        host = endpoint.substr(1, closing_bracket - 1);
        port_text = endpoint.substr(closing_bracket + 2);
    } else {
        const std::size_t separator = endpoint.rfind(':');
        if (separator == std::string_view::npos || endpoint.find(':') != separator) {
            throw std::runtime_error("Peer endpoint must use address:port");
        }
        host = endpoint.substr(0, separator);
        port_text = endpoint.substr(separator + 1);
    }

    unsigned int port = 0;
    const auto [end, error] =
        std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
    if (host.empty() || port_text.empty() || error != std::errc{} ||
        end != port_text.data() + port_text.size() || port == 0 || port > 65535) {
        throw std::runtime_error("Peer endpoint contains an invalid address or port");
    }

    return {std::string{host}, std::to_string(port)};
}

void make_nonblocking(int descriptor) {
    const int flags = fcntl(descriptor, F_GETFL, 0);
    if (flags < 0 || fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) < 0) {
        throw std::system_error(errno, std::generic_category(),
                                "Unable to configure peer socket");
    }
}

void wait_for_socket(int descriptor, short events, Deadline deadline) {
    while (true) {
        const auto remaining = deadline - std::chrono::steady_clock::now();
        if (remaining <= Deadline::duration::zero()) {
            throw std::system_error(ETIMEDOUT, std::generic_category(),
                                    "Peer connection timed out");
        }

        const auto remaining_ms = std::chrono::ceil<std::chrono::milliseconds>(remaining).count();
        const int timeout_ms =
            remaining_ms > INT_MAX ? INT_MAX : static_cast<int>(remaining_ms);
        pollfd event{
            .fd = descriptor,
            .events = events,
            .revents = 0,
        };
        const int result = poll(&event, 1, timeout_ms);
        if (result > 0) {
            if ((event.revents & events) != 0) {
                return;
            }
            throw std::runtime_error("Peer closed the connection unexpectedly");
        }
        if (result == 0) {
            throw std::system_error(ETIMEDOUT, std::generic_category(),
                                    "Peer connection timed out");
        }
        if (errno != EINTR) {
            throw std::system_error(errno, std::generic_category(),
                                    "Unable to wait for peer socket");
        }
    }
}

Socket connect_to_peer(std::string_view endpoint) {
    const auto [host, service] = split_endpoint(endpoint);
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;

    addrinfo* raw_addresses = nullptr;
    const int lookup_result = getaddrinfo(host.c_str(), service.c_str(), &hints, &raw_addresses);
    if (lookup_result != 0) {
        throw std::runtime_error("Unable to parse peer address: " +
                                 std::string{gai_strerror(lookup_result)});
    }
    AddressInfo addresses{raw_addresses};

    int last_error = ECONNREFUSED;
    for (const addrinfo* address = addresses.get(); address != nullptr;
         address = address->ai_next) {
        Socket candidate{socket(address->ai_family, address->ai_socktype, address->ai_protocol)};
        if (candidate.get() < 0) {
            last_error = errno;
            continue;
        }

        try {
            make_nonblocking(candidate.get());
            if (connect(candidate.get(), address->ai_addr, address->ai_addrlen) == 0) {
                return candidate;
            }
            if (errno != EINPROGRESS) {
                last_error = errno;
                continue;
            }

            wait_for_socket(candidate.get(), POLLOUT,
                            std::chrono::steady_clock::now() + socket_timeout);
            socklen_t error_size = sizeof(last_error);
            if (getsockopt(candidate.get(), SOL_SOCKET, SO_ERROR, &last_error, &error_size) < 0) {
                last_error = errno;
                continue;
            }
            if (last_error == 0) {
                return candidate;
            }
        } catch (const std::system_error& error) {
            last_error = error.code().value();
        } catch (const std::runtime_error&) {
            last_error = ECONNRESET;
        }
    }

    throw std::system_error(last_error, std::generic_category(), "Unable to connect to peer");
}

Socket connect_with_retries(std::string_view endpoint) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        try {
            return connect_to_peer(endpoint);
        } catch (const std::system_error& error) {
            if (error.code().value() != ECONNREFUSED || attempt == 2) {
                throw;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{50});
        }
    }
    throw std::runtime_error("Unable to connect to peer");
}

std::uint32_t read_uint32(std::span<const unsigned char> bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
           static_cast<std::uint32_t>(bytes[offset + 3]);
}

void append_uint32(std::vector<unsigned char>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<unsigned char>(value >> 24));
    bytes.push_back(static_cast<unsigned char>(value >> 16));
    bytes.push_back(static_cast<unsigned char>(value >> 8));
    bytes.push_back(static_cast<unsigned char>(value));
}

void send_all(int descriptor, std::span<const unsigned char> bytes, Deadline deadline) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count =
            send(descriptor, bytes.data() + offset, bytes.size() - offset, MSG_NOSIGNAL);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            wait_for_socket(descriptor, POLLOUT, deadline);
            continue;
        }
        throw std::system_error(count == 0 ? EPIPE : errno, std::generic_category(),
                                "Unable to send peer data");
    }
}

void receive_all(int descriptor, std::span<unsigned char> bytes, Deadline deadline) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count = recv(descriptor, bytes.data() + offset, bytes.size() - offset, 0);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            wait_for_socket(descriptor, POLLIN, deadline);
            continue;
        }
        if (count == 0) {
            throw std::runtime_error("Peer closed the connection unexpectedly");
        }
        throw std::system_error(errno, std::generic_category(),
                                "Unable to receive peer data");
    }
}

} // namespace

PeerHandshakeBytes serialize_peer_handshake(const PeerHandshake& handshake) {
    PeerHandshakeBytes message{};
    message[0] = static_cast<unsigned char>(protocol_name.size());
    std::copy(protocol_name.begin(), protocol_name.end(), message.begin() + 1);
    std::copy(handshake.reserved.begin(), handshake.reserved.end(), message.begin() + 20);
    std::copy(handshake.info_hash.begin(), handshake.info_hash.end(), message.begin() + 28);
    std::copy(handshake.peer_id.begin(), handshake.peer_id.end(), message.begin() + 48);
    return message;
}

PeerHandshake parse_peer_handshake(std::span<const unsigned char> message,
                                   const Sha1Digest& expected_info_hash) {
    if (message.size() != peer_handshake_size) {
        throw std::runtime_error("Peer handshake must contain exactly 68 bytes");
    }
    if (message[0] != protocol_name.size() ||
        !std::equal(protocol_name.begin(), protocol_name.end(), message.begin() + 1)) {
        throw std::runtime_error("Peer handshake contains an invalid protocol name");
    }

    PeerHandshake handshake{};
    std::copy_n(message.begin() + 20, handshake.reserved.size(), handshake.reserved.begin());
    std::copy_n(message.begin() + 28, handshake.info_hash.size(), handshake.info_hash.begin());
    std::copy_n(message.begin() + 48, handshake.peer_id.size(), handshake.peer_id.begin());
    if (handshake.info_hash != expected_info_hash) {
        throw std::runtime_error("Peer handshake info hash does not match the torrent");
    }
    return handshake;
}

std::vector<unsigned char> serialize_peer_message(const PeerMessage& message) {
    if (!message.id.has_value() && !message.payload.empty()) {
        throw std::runtime_error("A keep-alive peer message cannot contain a payload");
    }

    if (message.id.has_value() && message.payload.size() >= maximum_peer_message_size) {
        throw std::runtime_error("Peer message exceeds the maximum supported size");
    }
    const std::size_t body_size = message.id.has_value() ? message.payload.size() + 1 : 0;

    std::vector<unsigned char> encoded;
    encoded.reserve(4 + body_size);
    append_uint32(encoded, static_cast<std::uint32_t>(body_size));
    if (message.id.has_value()) {
        encoded.push_back(static_cast<unsigned char>(*message.id));
        encoded.insert(encoded.end(), message.payload.begin(), message.payload.end());
    }
    return encoded;
}

PeerMessage parse_peer_message(std::span<const unsigned char> message) {
    if (message.size() < 4) {
        throw std::runtime_error("Peer message is missing its length prefix");
    }

    const std::uint32_t body_size = read_uint32(message, 0);
    if (body_size > maximum_peer_message_size) {
        throw std::runtime_error("Peer message exceeds the maximum supported size");
    }
    if (message.size() - 4 != body_size) {
        throw std::runtime_error("Peer message length does not match its payload");
    }
    if (body_size == 0) {
        return PeerMessage{.id = std::nullopt, .payload = {}};
    }

    return PeerMessage{
        .id = static_cast<PeerMessageId>(message[4]),
        .payload = std::vector<unsigned char>{message.begin() + 5, message.end()},
    };
}

PeerConnection::PeerConnection(std::string_view endpoint)
    : descriptor_{connect_with_retries(endpoint).release()} {}

PeerConnection::~PeerConnection() {
    if (descriptor_ >= 0) {
        ::close(descriptor_);
    }
}

PeerConnection::PeerConnection(PeerConnection&& other) noexcept
    : descriptor_{std::exchange(other.descriptor_, -1)} {}

PeerConnection& PeerConnection::operator=(PeerConnection&& other) noexcept {
    if (this != &other) {
        if (descriptor_ >= 0) {
            ::close(descriptor_);
        }
        descriptor_ = std::exchange(other.descriptor_, -1);
    }
    return *this;
}

PeerHandshake PeerConnection::exchange_handshake(const PeerHandshake& local_handshake) {
    const PeerHandshakeBytes request = serialize_peer_handshake(local_handshake);
    const Deadline deadline = std::chrono::steady_clock::now() + socket_timeout;
    send_all(descriptor_, request, deadline);

    PeerHandshakeBytes response{};
    receive_all(descriptor_, response, deadline);
    return parse_peer_handshake(response, local_handshake.info_hash);
}

void PeerConnection::send_message(const PeerMessage& message) {
    const std::vector<unsigned char> encoded = serialize_peer_message(message);
    send_all(descriptor_, encoded, std::chrono::steady_clock::now() + message_timeout);
}

PeerMessage PeerConnection::receive_message() {
    const Deadline deadline = std::chrono::steady_clock::now() + message_timeout;
    std::array<unsigned char, 4> prefix{};
    receive_all(descriptor_, prefix, deadline);

    const std::uint32_t body_size = read_uint32(prefix, 0);
    if (body_size > maximum_peer_message_size) {
        throw std::runtime_error("Peer message exceeds the maximum supported size");
    }

    std::vector<unsigned char> encoded(prefix.begin(), prefix.end());
    encoded.resize(4 + body_size);
    if (body_size != 0) {
        receive_all(descriptor_, std::span<unsigned char>{encoded}.subspan(4), deadline);
    }
    return parse_peer_message(encoded);
}

PeerHandshake exchange_peer_handshake(std::string_view endpoint,
                                      const PeerHandshake& local_handshake) {
    PeerConnection connection{endpoint};
    return connection.exchange_handshake(local_handshake);
}

} // namespace torrentforge
