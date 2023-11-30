// Copyright 2023 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_UTILS_CPP_VERIFY_HASH_HPP
#define INCLUDED_SRC_UTILS_CPP_VERIFY_HASH_HPP
#include <algorithm>
#include <cctype>
#include <optional>
#include <string>

#include "fmt/core.h"

/// \brief Check if the passed string \p s is a hash.
/// This function is mainly used to check that the hash of a Digest received
/// over the wire is a real hash, to prevent a malicious attack.
[[nodiscard]] static inline auto IsAHash(std::string const& s) noexcept
    -> std::optional<std::string> {
    if (!std::all_of(s.begin(), s.end(), [](unsigned char c) {
            return std::isxdigit(c);
        })) {
        return fmt::format("Invalid hash {}", s);
    }
    return std::nullopt;
}
#endif
