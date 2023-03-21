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

#ifndef INCLUDED_SRC_OTHER_TOOLS_OPS_MAPS_GIT_UPDATE_MAP_HPP
#define INCLUDED_SRC_OTHER_TOOLS_OPS_MAPS_GIT_UPDATE_MAP_HPP

#include "src/buildtool/multithreading/async_map_consumer.hpp"
#include "src/other_tools/git_operations/git_repo_remote.hpp"
#include "src/utils/cpp/hash_combine.hpp"

using StringPair = std::pair<std::string, std::string>;

/// \brief Maps a pair of repository url and branch to an updated commit hash.
using GitUpdateMap = AsyncMapConsumer<StringPair, std::string>;

namespace std {
template <>
struct hash<StringPair> {
    [[nodiscard]] auto operator()(StringPair const& ct) const noexcept
        -> std::size_t {
        size_t seed{};
        hash_combine<std::string>(&seed, ct.first);
        hash_combine<std::string>(&seed, ct.second);
        return seed;
    }
};
}  // namespace std

[[nodiscard]] auto CreateGitUpdateMap(GitCASPtr const& git_cas,
                                      std::vector<std::string> const& launcher,
                                      std::size_t jobs) -> GitUpdateMap;

#endif  // INCLUDED_SRC_OTHER_TOOLS_OPS_MAPS_GIT_UPDATE_MAP_HPP