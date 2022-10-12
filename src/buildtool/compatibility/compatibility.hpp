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

#ifndef INCLUDED_SRC_BUILDTOOL_COMPATIBILITY_COMPATIBILITY_HPP
#define INCLUDED_SRC_BUILDTOOL_COMPATIBILITY_COMPATIBILITY_HPP
#include <shared_mutex>
#include <unordered_map>
#include <utility>

#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/logging/logger.hpp"
class Compatibility {
    using git_hash = std::string;
    using compat_hash = std::string;
    using git_repo = std::string;
    using GitToCompatibleMap = std::unordered_map<git_hash, compat_hash>;
    using CompatibleToGitMap =
        std::unordered_map<compat_hash, std::pair<git_hash, git_repo>>;

  public:
    [[nodiscard]] static auto Instance() noexcept -> Compatibility& {
        static Compatibility instance{};
        return instance;
    }
    [[nodiscard]] static auto IsCompatible() noexcept -> bool {
        return Instance().compatible_;
    }
    static void SetCompatible() noexcept { Instance().compatible_ = true; }

    [[nodiscard]] static auto RegisterGitEntry(std::string const& git_hash,
                                               std::string const& data,
                                               std::string const& repo)
        -> compat_hash {

        {
            auto& git_to_compatible = Instance().git_to_compatible_;
            std::shared_lock lock_{Instance().mutex_};
            auto it = git_to_compatible.find(git_hash);
            if (it != git_to_compatible.end()) {
                return it->second;
            }
        }
        // This is only used in compatible mode. Therefore, the default hash
        // function produces the compatible hash.
        auto compatible_hash = HashFunction::ComputeHash(data).HexString();
        std::unique_lock lock_{Instance().mutex_};
        Instance().git_to_compatible_[git_hash] = compatible_hash;
        Instance().compatible_to_git_[compatible_hash] = {git_hash, repo};
        return compatible_hash;
    }

    [[nodiscard]] static auto GetGitEntry(std::string const& compatible_hash)
        -> std::optional<std::pair<git_hash const&, git_repo const&>> {
        auto const& compatible_to_git = Instance().compatible_to_git_;
        std::shared_lock lock_{Instance().mutex_};
        auto it = compatible_to_git.find(compatible_hash);
        if (it != compatible_to_git.end()) {
            return it->second;
        }
        Logger::Log(
            LogLevel::Warning,
            fmt::format("Unable to get the git-sha1 code associated to {}",
                        compatible_hash));
        return std::nullopt;
    }

  private:
    GitToCompatibleMap git_to_compatible_{};
    CompatibleToGitMap compatible_to_git_{};
    bool compatible_{false};
    std::shared_mutex mutex_;
};
#endif  // INCLUDED_SRC_BUILDTOOL_COMPATIBILITY_COMPATIBILITY_HPP
