// Copyright 2022 Huawei Cloud Computing Technology Co., Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef INCLUDED_SRC_UTILS_CPP_HEX_STRING_HPP
#define INCLUDED_SRC_UTILS_CPP_HEX_STRING_HPP

#include <cstddef>
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
        const std::size_t kHexBase = 16;
        std::stringstream ss{};
        for (std::size_t i = 0; i < hexstring.length(); i += 2) {
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
