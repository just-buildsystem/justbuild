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

#ifndef INCLUDED_SRC_BUILDTOOL_COMMON_REPOSITORY_CONFIG_HPP
#define INCLUDED_SRC_BUILDTOOL_COMMON_REPOSITORY_CONFIG_HPP

#include <filesystem>
#include <map>
#include <string>
#include <unordered_map>

#include "src/buildtool/execution_api/local/local_cas.hpp"
#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/file_system/git_cas.hpp"
#include "src/buildtool/multithreading/atomic_value.hpp"

class RepositoryConfig {

  public:
    struct RepositoryInfo {
        FileRoot workspace_root;
        FileRoot target_root{workspace_root};
        FileRoot rule_root{target_root};
        FileRoot expression_root{rule_root};
        std::map<std::string, std::string> name_mapping{};
        std::string target_file_name{"TARGETS"};
        std::string rule_file_name{"RULES"};
        std::string expression_file_name{"EXPRESSIONS"};

        // Return base content description without bindings if all roots are
        // content fixed or return std::nullopt otherwise.
        [[nodiscard]] auto BaseContentDescription() const
            -> std::optional<nlohmann::json>;
    };

    [[nodiscard]] static auto Instance() noexcept -> RepositoryConfig& {
        static RepositoryConfig instance{};
        return instance;
    }

    void SetInfo(std::string const& repo, RepositoryInfo&& info) {
        repos_[repo].base_desc = info.BaseContentDescription();
        repos_[repo].info = std::move(info);
        repos_[repo].key.Reset();
    }

    [[nodiscard]] auto SetGitCAS(
        std::filesystem::path const& repo_path) noexcept {
        git_cas_ = GitCAS::Open(repo_path);
        return static_cast<bool>(git_cas_);
    }

    [[nodiscard]] auto Info(std::string const& repo) const noexcept
        -> RepositoryInfo const* {
        if (auto const* data = Data(repo)) {
            return &data->info;
        }
        return nullptr;
    }

    [[nodiscard]] auto ReadBlobFromGitCAS(std::string const& hex_id)
        const noexcept -> std::optional<std::string> {
        return git_cas_ ? git_cas_->ReadObject(hex_id, /*is_hex_id=*/true)
                        : std::nullopt;
    }

    [[nodiscard]] auto ReadTreeFromGitCAS(
        std::string const& hex_id) const noexcept -> std::optional<GitTree> {
        return git_cas_ ? GitTree::Read(git_cas_, hex_id) : std::nullopt;
    }

    [[nodiscard]] auto WorkspaceRoot(std::string const& repo) const noexcept
        -> FileRoot const* {
        return Get<FileRoot>(
            repo, [](auto const& info) { return &info.workspace_root; });
    }

    [[nodiscard]] auto TargetRoot(std::string const& repo) const noexcept
        -> FileRoot const* {
        return Get<FileRoot>(
            repo, [](auto const& info) { return &info.target_root; });
    }

    [[nodiscard]] auto RuleRoot(std::string const& repo) const
        -> FileRoot const* {
        return Get<FileRoot>(repo,
                             [](auto const& info) { return &info.rule_root; });
    }

    [[nodiscard]] auto ExpressionRoot(std::string const& repo) const noexcept
        -> FileRoot const* {
        return Get<FileRoot>(
            repo, [](auto const& info) { return &info.expression_root; });
    }

    [[nodiscard]] auto GlobalName(std::string const& repo,
                                  std::string const& local_name) const noexcept
        -> std::string const* {
        return Get<std::string>(
            repo, [&local_name](auto const& info) -> std::string const* {
                auto it = info.name_mapping.find(local_name);
                if (it != info.name_mapping.end()) {
                    return &it->second;
                }
                return nullptr;
            });
    }

    [[nodiscard]] auto TargetFileName(std::string const& repo) const noexcept
        -> std::string const* {
        return Get<std::string>(
            repo, [](auto const& info) { return &info.target_file_name; });
    }

    [[nodiscard]] auto RuleFileName(std::string const& repo) const noexcept
        -> std::string const* {
        return Get<std::string>(
            repo, [](auto const& info) { return &info.rule_file_name; });
    }

    [[nodiscard]] auto ExpressionFileName(
        std::string const& repo) const noexcept -> std::string const* {
        return Get<std::string>(
            repo, [](auto const& info) { return &info.expression_file_name; });
    }

    // Obtain repository's cache key if the repository is content fixed or
    // std::nullopt otherwise.
    [[nodiscard]] auto RepositoryKey(std::string const& repo) const noexcept
        -> std::optional<std::string>;

    // used for testing
    void Reset() {
        repos_.clear();
        git_cas_.reset();
        duplicates_.Reset();
    }

  private:
    using duplicates_t = std::unordered_map<std::string, std::string>;

    // All data we store per repository.
    struct RepositoryData {
        // Info structure (roots, file names, bindings)
        RepositoryInfo info{};
        // Base description if content-fixed
        std::optional<nlohmann::json> base_desc{};
        // Cache key if content-fixed
        AtomicValue<std::optional<std::string>> key{};
    };

    std::unordered_map<std::string, RepositoryData> repos_;
    GitCASPtr git_cas_;
    AtomicValue<duplicates_t> duplicates_{};

    template <class T>
    [[nodiscard]] auto Get(std::string const& repo,
                           std::function<T const*(RepositoryInfo const&)> const&
                               getter) const noexcept -> T const* {
        if (auto const* info = Info(repo)) {
            try {  // satisfy clang-tidy's bugprone-exception-escape
                return getter(*info);
            } catch (...) {
            }
        }
        return nullptr;
    }

    [[nodiscard]] auto Data(std::string const& repo) const noexcept
        -> RepositoryData const* {
        auto it = repos_.find(repo);
        if (it != repos_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    [[nodiscard]] auto DeduplicateRepo(std::string const& repo) const
        -> std::string const&;

    [[nodiscard]] auto BuildGraphForRepository(std::string const& repo) const
        -> std::optional<nlohmann::json>;

    [[nodiscard]] auto AddToGraphAndGetId(
        gsl::not_null<nlohmann::json*> const& graph,
        gsl::not_null<int*> const& id_counter,
        gsl::not_null<std::unordered_map<std::string, std::string>*> const&
            repo_ids,
        std::string const& repo) const -> std::optional<std::string>;
};

#endif  // INCLUDED_SRC_BUILDTOOL_COMMON_REPOSITORY_CONFIG_HPP
