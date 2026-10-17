#include "torrentforge/hash.hpp"

#include <openssl/evp.h>

#include <stdexcept>
#include <string>
#include <string_view>

namespace torrentforge {

Sha1Digest sha1(std::string_view input) {
    Sha1Digest digest{};
    unsigned int digest_length = 0;

    if (EVP_Digest(input.data(), input.size(), digest.data(), &digest_length, EVP_sha1(),
                   nullptr) != 1 ||
        digest_length != digest.size()) {
        throw std::runtime_error("Unable to calculate SHA-1 digest");
    }

    return digest;
}

std::string to_hex(const Sha1Digest& digest) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result(digest.size() * 2, '0');

    for (std::size_t index = 0; index < digest.size(); ++index) {
        result[index * 2] = digits[digest[index] >> 4];         // Upper four bits
        result[index * 2 + 1] = digits[digest[index] & 0x0f];   // Lower four bits
    }

    return result;
}

} // namespace torrentforge
