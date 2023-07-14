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

#ifndef INCLUDED_SRC_UTILS_CPP_PATH_HPP
#define INCLUDED_SRC_UTILS_CPP_PATH_HPP

#include <filesystem>
#include <sstream>

[[nodiscard]] static inline auto ToNormalPath(
    std::filesystem::path const& p) noexcept -> std::filesystem::path {
    auto n = p.lexically_normal();
    if (not n.has_filename()) {
        n = n.parent_path();
    }
    if (n.empty()) {
        return std::filesystem::path{"."};
    }
    return n;
}

/// \brief Perform a non-upwards condition check on the given path.
/// A path is non-upwards if it is relative and it never references any other
/// path on a higher level in the directory tree than itself.
[[nodiscard]] static auto PathIsNonUpwards(
    std::filesystem::path const& path) noexcept -> bool {
    if (path.is_absolute()) {
        return false;
    }
    // check non-upwards condition
    return *path.lexically_normal().begin() != "..";
}

#endif
