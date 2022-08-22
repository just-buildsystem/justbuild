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

#ifndef INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_GIT_REPO_HPP
#define INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_GIT_REPO_HPP

#include "src/buildtool/file_system/git_cas.hpp"
#include "src/buildtool/multithreading/async_map_consumer.hpp"

extern "C" {
using git_repository = struct git_repository;
}

/// \brief Git repository logic.
/// Models both a real repository, owning the underlying ODB
/// (non-thread-safe), as well as a "fake" repository, which only wraps an
/// existing ODB, allowing thread-safe operations.
class GitRepo {
  public:
    GitRepo() = delete;  // no default ctor

    // allow only move, no copy
    GitRepo(GitRepo const&) = delete;
    GitRepo(GitRepo&&) noexcept;
    auto operator=(GitRepo const&) = delete;
    auto operator=(GitRepo&& other) noexcept -> GitRepo&;

    /// \brief Factory to wrap existing open CAS in a "fake" repository.
    [[nodiscard]] static auto Open(GitCASPtr git_cas) noexcept
        -> std::optional<GitRepo>;

    /// \brief Factory to open existing real repository at given location.
    [[nodiscard]] static auto Open(
        std::filesystem::path const& repo_path) noexcept
        -> std::optional<GitRepo>;

    /// \brief Factory to initialize and open new real repository at location.
    /// Returns nullopt if repository init fails even after repeated tries.
    [[nodiscard]] static auto InitAndOpen(
        std::filesystem::path const& repo_path,
        bool is_bare) noexcept -> std::optional<GitRepo>;

    [[nodiscard]] auto GetGitCAS() const noexcept -> GitCASPtr;

    [[nodiscard]] auto IsRepoFake() const noexcept -> bool;

    ~GitRepo() noexcept;

  private:
    GitCASPtr git_cas_{nullptr};
    git_repository* repo_{nullptr};
    // default to real repo, as that is non-thread-safe
    bool is_repo_fake_{false};

    /// \brief Open "fake" repository wrapper for existing CAS.
    explicit GitRepo(GitCASPtr git_cas) noexcept;
    /// \brief Open real repository at given location.
    explicit GitRepo(std::filesystem::path const& repo_path) noexcept;
};

#endif  // INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_GIT_REPO_HPP