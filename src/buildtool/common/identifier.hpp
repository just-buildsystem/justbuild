#ifndef INCLUDED_SRC_BUILDTOOL_COMMON_IDENTIFIER_HPP
#define INCLUDED_SRC_BUILDTOOL_COMMON_IDENTIFIER_HPP

#include <iomanip>
#include <sstream>
#include <string>

// Global artifact identifier (not the CAS hash)
using ArtifactIdentifier = std::string;

// Global action identifier
using ActionIdentifier = std::string;

static inline auto IdentifierToString(const std::string& id) -> std::string {
    std::ostringstream encoded{};
    encoded << std::hex << std::setfill('0');
    for (auto const& b : id) {
        encoded << std::setw(2)
                << static_cast<int>(
                       static_cast<std::make_unsigned_t<
                           std::remove_reference_t<decltype(b)>>>(b));
    }
    return encoded.str();
}
#endif  // INCLUDED_SRC_BUILDTOOL_COMMON_IDENTIFIER_HPP
