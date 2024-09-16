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

#ifndef INCLUDED_SRC_UTILS_CPP_PATH_REBASE_HPP
#define INCLUDED_SRC_UTILS_CPP_PATH_REBASE_HPP

#include <filesystem>
#include <string>
#include <vector>

[[nodiscard]] static inline auto RebasePathStringRelativeTo(
    const std::string& base,
    const std::string& path) -> std::string {
    return std::filesystem::path(path).lexically_relative(base).string();
}

[[nodiscard]] static inline auto RebasePathStringsRelativeTo(
    const std::string& base,
    const std::vector<std::string>& paths) -> std::vector<std::string> {
    std::vector<std::string> result{};
    result.reserve(paths.size());
    for (auto const& path : paths) {
        result.emplace_back(RebasePathStringRelativeTo(base, path));
    }
    return result;
}

#endif  // INCLUDED_SRC_UTILS_CPP_PATH_REBASE_HPP
