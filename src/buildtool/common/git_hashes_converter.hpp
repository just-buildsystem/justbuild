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

#ifndef INCLUDED_SRC_BUILDTOOL_COMMON_GIT_HASHES_CONVERTER_HPP
#define INCLUDED_SRC_BUILDTOOL_COMMON_GIT_HASHES_CONVERTER_HPP

#include <functional>
#include <mutex>  //std::unique_lock
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>

#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

class GitHashesConverter final {
    using git_hash = std::string;
    using compat_hash = std::string;
    using git_repo = std::string;
    using GitToCompatibleMap = std::unordered_map<git_hash, compat_hash>;
    using CompatibleToGitMap =
        std::unordered_map<compat_hash, std::pair<git_hash, git_repo>>;

  public:
    [[nodiscard]] static auto Instance() noexcept -> GitHashesConverter& {
        static GitHashesConverter instance;
        return instance;
    }

    [[nodiscard]] auto RegisterGitEntry(std::string const& git_hash,
                                        std::string const& data,
                                        std::string const& repo)
        -> compat_hash {
        {
            std::shared_lock lock{mutex_};
            auto it = git_to_compatible_.find(git_hash);
            if (it != git_to_compatible_.end()) {
                return it->second;
            }
        }
        // This is only used in compatible mode.
        HashFunction const hash_function{HashFunction::Type::PlainSHA256};
        auto compatible_hash = hash_function.PlainHashData(data).HexString();
        std::unique_lock lock{mutex_};
        git_to_compatible_[git_hash] = compatible_hash;
        compatible_to_git_[compatible_hash] = {git_hash, repo};
        return compatible_hash;
    }

    [[nodiscard]] auto GetGitEntry(std::string const& compatible_hash)
        -> std::optional<std::pair<git_hash const&, git_repo const&>> {
        std::shared_lock lock{mutex_};
        auto it = compatible_to_git_.find(compatible_hash);
        if (it != compatible_to_git_.end()) {
            return it->second;
        }
        Logger::Log(LogLevel::Warning,
                    "Unable to get the git-sha1 code associated to {}",
                    compatible_hash);
        return std::nullopt;
    }

  private:
    explicit GitHashesConverter() noexcept = default;

    GitToCompatibleMap git_to_compatible_;
    CompatibleToGitMap compatible_to_git_;
    std::shared_mutex mutex_;
};

#endif  // INCLUDED_SRC_BUILDTOOL_COMMON_GIT_HASHES_CONVERTER_HPP