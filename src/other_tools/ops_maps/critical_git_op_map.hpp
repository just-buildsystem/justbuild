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

#ifndef INCLUDED_SRC_OTHER_TOOLS_OPS_MAPS_CRITICAL_GIT_OP_MAP_HPP
#define INCLUDED_SRC_OTHER_TOOLS_OPS_MAPS_CRITICAL_GIT_OP_MAP_HPP

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "src/buildtool/multithreading/async_map_consumer.hpp"
#include "src/other_tools/git_operations/git_ops_types.hpp"
#include "src/utils/cpp/hash_combine.hpp"
#include "src/utils/cpp/path_hash.hpp"

using GitOpKeyMap = std::unordered_map<
    GitOpType,
    std::function<GitOpValue(GitOpParams const&,
                             AsyncMapConsumerLoggerPtr const&)>>;

struct GitOpKey {
    GitOpParams params{"", ""};               /* key (with exceptions) */
    GitOpType op_type{GitOpType::DEFAULT_OP}; /* key */

    [[nodiscard]] auto operation(GitOpParams const& params,
                                 AsyncMapConsumerLoggerPtr const& logger) const
        -> GitOpValue {
        return kMap.at(op_type)(params, logger);
    }

    [[nodiscard]] auto operator==(GitOpKey const& other) const -> bool {
        return params == other.params && op_type == other.op_type;
    }

  private:
    static GitOpKeyMap const kMap;
};

class CriticalGitOpGuard;
using CriticalGitOpGuardPtr = std::shared_ptr<CriticalGitOpGuard>;

using CriticalGitOpMap = AsyncMapConsumer<GitOpKey, GitOpValue>;

[[nodiscard]] auto CreateCriticalGitOpMap(
    CriticalGitOpGuardPtr const& crit_git_op_ptr) -> CriticalGitOpMap;

/// \brief Class ensuring thread safety in critical Git operations.
/// By always storing the most recent operation to be executed, a chain of
/// operations can be created such that no thread is left in a blocking state.
/// Each repo has its own key, so the caller has to ensure the target_path
/// parameter provided is non-empty.
class CriticalGitOpGuard {
  public:
    [[nodiscard]] auto FetchAndSetCriticalKey(GitOpKey const& new_key)
        -> std::optional<GitOpKey> {
        // making sure only one thread at a time processes this section
        std::scoped_lock<std::mutex> const lock(critical_key_mutex_);
        // try emplace a new value

        auto const canonical_path =
            std::filesystem::weakly_canonical(new_key.params.target_path);
        auto result = curr_critical_key_.try_emplace(canonical_path, new_key);
        // If the insertion happens, there are no keys to wait for. std::nullopt
        // is returned.
        if (result.second) {
            return std::nullopt;
        }
        // If insertion fails (the path is being processed), mark the path
        // occupied by the new key, but return the previous key to inform
        // the new task to wait for the previous operation to complete.
        return std::exchange(result.first->second, new_key);
    }

  private:
    std::unordered_map<std::filesystem::path, GitOpKey> curr_critical_key_;
    std::mutex critical_key_mutex_;
};

namespace std {
template <>
struct hash<GitOpParams> {
    [[nodiscard]] auto operator()(GitOpParams const& ct) const noexcept
        -> std::size_t {
        size_t seed{};
        hash_combine<std::filesystem::path>(&seed, ct.target_path);
        hash_combine<std::string>(&seed, ct.git_hash);
        return seed;
    }
};

template <>
struct hash<GitOpKey> {
    [[nodiscard]] auto operator()(GitOpKey const& ct) const noexcept
        -> std::size_t {
        size_t seed{};
        hash_combine<GitOpParams>(&seed, ct.params);
        hash_combine<>(&seed, ct.op_type);
        return seed;
    }
};
}  // namespace std

#endif  // INCLUDED_SRC_OTHER_TOOLS_OPS_MAPS_CRITICAL_GIT_OP_MAP_HPP