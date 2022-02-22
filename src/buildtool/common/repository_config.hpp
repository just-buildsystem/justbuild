#ifndef INCLUDED_SRC_BUILDTOOL_COMMON_REPOSITORY_CONFIG_HPP
#define INCLUDED_SRC_BUILDTOOL_COMMON_REPOSITORY_CONFIG_HPP

#include <filesystem>
#include <string>
#include <unordered_map>

#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/file_system/git_cas.hpp"

class RepositoryConfig {
  public:
    struct RepositoryInfo {
        FileRoot workspace_root;
        FileRoot target_root{workspace_root};
        FileRoot rule_root{target_root};
        FileRoot expression_root{rule_root};
        std::unordered_map<std::string, std::string> name_mapping{};
        std::string target_file_name{"TARGETS"};
        std::string rule_file_name{"RULES"};
        std::string expression_file_name{"EXPRESSIONS"};
    };

    [[nodiscard]] static auto Instance() noexcept -> RepositoryConfig& {
        static RepositoryConfig instance{};
        return instance;
    }

    void SetInfo(std::string const& repo, RepositoryInfo&& info) {
        infos_.emplace(repo, std::move(info));
    }

    [[nodiscard]] auto SetGitCAS(
        std::filesystem::path const& repo_path) noexcept {
        git_cas_ = GitCAS::Open(repo_path);
        return static_cast<bool>(git_cas_);
    }

    [[nodiscard]] auto Info(std::string const& repo) const noexcept
        -> RepositoryInfo const* {
        auto it = infos_.find(repo);
        if (it != infos_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    [[nodiscard]] auto ReadBlobFromGitCAS(std::string const& hex_id)
        const noexcept -> std::optional<std::string> {
        return git_cas_ ? git_cas_->ReadObject(hex_id, /*is_hex_id=*/true)
                        : std::nullopt;
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

    // used for testing
    void Reset() {
        infos_.clear();
        git_cas_.reset();
    }

  private:
    std::unordered_map<std::string, RepositoryInfo> infos_;
    GitCASPtr git_cas_;

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
};

#endif  // INCLUDED_SRC_BUILDTOOL_COMMON_REPOSITORY_CONFIG_HPP
