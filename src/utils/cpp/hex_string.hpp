#ifndef INCLUDED_SRC_UTILS_CPP_HEX_STRING_HPP
#define INCLUDED_SRC_UTILS_CPP_HEX_STRING_HPP

#include <iomanip>
#include <sstream>
#include <string>

[[nodiscard]] static inline auto ToHexString(std::string const& bytes)
    -> std::string {
    std::ostringstream ss{};
    ss << std::hex << std::setfill('0');
    for (auto const& b : bytes) {
        ss << std::setw(2)
           << static_cast<int>(static_cast<unsigned char const>(b));
    }
    return ss.str();
}

#endif  // INCLUDED_SRC_UTILS_CPP_HEX_STRING_HPP
