#ifndef INCLUDED_SRC_UTILS_CPP_HEX_STRING_HPP
#define INCLUDED_SRC_UTILS_CPP_HEX_STRING_HPP

#include <iomanip>
#include <iostream>
#include <optional>
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

[[nodiscard]] static inline auto FromHexString(std::string const& hexstring)
    -> std::optional<std::string> {
    try {
        const size_t kHexBase = 16;
        std::stringstream ss{};
        for (size_t i = 0; i < hexstring.length(); i += 2) {
            unsigned char c =
                std::stoul(hexstring.substr(i, 2), nullptr, kHexBase);
            ss << c;
        }
        return ss.str();
    } catch (...) {
        return std::nullopt;
    }
}

#endif  // INCLUDED_SRC_UTILS_CPP_HEX_STRING_HPP
