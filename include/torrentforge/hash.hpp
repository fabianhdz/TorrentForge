#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace torrentforge {

inline constexpr std::size_t sha1_digest_size = 20;
using Sha1Digest = std::array<unsigned char, sha1_digest_size>;

Sha1Digest sha1(std::string_view input);
std::string to_hex(const Sha1Digest& digest);

} // namespace torrentforge
