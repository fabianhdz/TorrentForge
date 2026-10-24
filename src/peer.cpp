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
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

namespace torrentforge {

namespace {

constexpr std::string_view protocol_name = "BitTorrent protocol";
constexpr auto socket_timeout = std::chrono::seconds{10};

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
                                "Unable to send peer handshake");
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
            throw std::runtime_error("Peer closed before completing its handshake");
        }
        throw std::system_error(errno, std::generic_category(),
                                "Unable to receive peer handshake");
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

PeerHandshake exchange_peer_handshake(std::string_view endpoint,
                                      const PeerHandshake& local_handshake) {
    Socket connection;
    for (int attempt = 0; attempt < 3; ++attempt) {
        try {
            connection = connect_to_peer(endpoint);
            break;
        } catch (const std::system_error& error) {
            if (error.code().value() != ECONNREFUSED || attempt == 2) {
                throw;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{50});
        }
    }

    const PeerHandshakeBytes request = serialize_peer_handshake(local_handshake);
    const Deadline deadline = std::chrono::steady_clock::now() + socket_timeout;
    send_all(connection.get(), request, deadline);

    PeerHandshakeBytes response{};
    receive_all(connection.get(), response, deadline);
    return parse_peer_handshake(response, local_handshake.info_hash);
}

} // namespace torrentforge
