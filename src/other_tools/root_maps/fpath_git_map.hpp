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

#ifndef INCLUDED_SRC_OTHER_TOOLS_ROOT_MAPS_FPATH_GIT_MAP_HPP
#define INCLUDED_SRC_OTHER_TOOLS_ROOT_MAPS_FPATH_GIT_MAP_HPP

#include "nlohmann/json.hpp"
#include "src/other_tools/ops_maps/import_to_git_map.hpp"

/// \brief Maps the path to a repo on the file system to its Git tree WS root.
using FilePathGitMap = AsyncMapConsumer<std::filesystem::path, nlohmann::json>;

namespace std {
template <>
struct hash<std::filesystem::path> {
    [[nodiscard]] auto operator()(
        std::filesystem::path const& ct) const noexcept -> std::size_t {
        return std::filesystem::hash_value(ct);
    }
};
}  // namespace std

[[nodiscard]] auto CreateFilePathGitMap(
    std::optional<std::string> const& current_subcmd,
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    gsl::not_null<ImportToGitMap*> const& import_to_git_map,
    std::size_t jobs) -> FilePathGitMap;

#endif  // INCLUDED_SRC_OTHER_TOOLS_ROOT_MAPS_FPATH_GIT_MAP_HPP