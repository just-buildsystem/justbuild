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

#ifndef INCLUDED_SRC_BUILDTOOL_COMMON_IDENTIFIER_HPP
#define INCLUDED_SRC_BUILDTOOL_COMMON_IDENTIFIER_HPP

#include <iomanip>
#include <sstream>
#include <string>
#include <type_traits>

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
