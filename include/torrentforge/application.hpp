#pragma once

#include <iosfwd>
#include <span>
#include <string_view>

namespace torrentforge {

// Arguments exclude the executable name. Output streams remain caller-owned.
int run(std::span<const std::string_view> arguments, std::ostream& output,
        std::ostream& errors);

} // namespace torrentforge
