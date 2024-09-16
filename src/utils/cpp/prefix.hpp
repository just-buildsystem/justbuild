// Copyright 2024 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_UTILS_CPP_PREFIX_HPP
#define INCLUDED_SRC_UTILS_CPP_PREFIX_HPP

#include <algorithm>
#include <sstream>
#include <string>

[[nodiscard]] static inline auto PrefixLines(const std::string& text,
                                             const std::string& prefix = "  ")
    -> std::string {
    std::stringstream out{};
    std::istringstream in{text};
    for (std::string line{}; std::getline(in, line);) {
        out << prefix << line << "\n";
    }
    return out.str();
}

#endif  // INCLUDED_SRC_UTILS_CPP_PREFIX_HPP
