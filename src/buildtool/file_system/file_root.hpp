#ifndef INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_FILE_ROOT_HPP
#define INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_FILE_ROOT_HPP

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_set>
#include <variant>

#include "gsl-lite/gsl-lite.hpp"
#include "src/buildtool/common/artifact_description.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/git_tree.hpp"

class FileRoot {
    using fs_root_t = std::filesystem::path;
    struct git_root_t {
        gsl::not_null<GitCASPtr> cas;
        gsl::not_null<GitTreePtr> tree;
    };
    using root_t = std::variant<fs_root_t, git_root_t>;

  public:
    class DirectoryEntries {
        using names_t = std::unordered_set<std::string>;
        using tree_t = gsl::not_null<GitTree const*>;
        using entries_t = std::variant<std::monostate, names_t, tree_t>;

      public:
        DirectoryEntries() noexcept = default;
        explicit DirectoryEntries(names_t names) noexcept
            : data_{std::move(names)} {}
        explicit DirectoryEntries(tree_t git_tree) noexcept
            : data_{std::move(git_tree)} {}
        [[nodiscard]] auto Contains(std::string const& name) const noexcept
            -> bool {
            if (std::holds_alternative<tree_t>(data_)) {
                return static_cast<bool>(
                    std::get<tree_t>(data_)->LookupEntryByName(name));
            }
            if (std::holds_alternative<names_t>(data_)) {
                return std::get<names_t>(data_).contains(name);
            }
            return false;
        }
        [[nodiscard]] auto Empty() const noexcept -> bool {
            if (std::holds_alternative<tree_t>(data_)) {
                try {
                    auto const& tree = std::get<tree_t>(data_);
                    return tree->begin() == tree->end();
                } catch (...) {
                    return false;
                }
            }
            if (std::holds_alternative<names_t>(data_)) {
                return std::get<names_t>(data_).empty();
            }
            return true;
        }

      private:
        entries_t data_{};
    };

    FileRoot() noexcept = default;
    explicit FileRoot(std::filesystem::path root) noexcept
        : root_{std::move(root)} {}
    FileRoot(gsl::not_null<GitCASPtr> cas,
             gsl::not_null<GitTreePtr> tree) noexcept
        : root_{git_root_t{std::move(cas), std::move(tree)}} {}

    [[nodiscard]] static auto FromGit(std::filesystem::path const& repo_path,
                                      std::string const& git_tree_id) noexcept
        -> std::optional<FileRoot> {
        if (auto cas = GitCAS::Open(repo_path)) {
            if (auto tree = GitTree::Read(cas, git_tree_id)) {
                try {
                    return FileRoot{
                        cas, std::make_shared<GitTree const>(std::move(*tree))};
                } catch (...) {
                }
            }
        }
        return std::nullopt;
    }

    // Indicates that subsequent calls to `Exists()`, `IsFile()`,
    // `IsDirectory()`, and `FileType()` on contents of the same directory will
    // be served without any additional file system lookups.
    [[nodiscard]] auto HasFastDirectoryLookup() const noexcept -> bool {
        return std::holds_alternative<git_root_t>(root_);
    }

    [[nodiscard]] auto Exists(std::filesystem::path const& path) const noexcept
        -> bool {
        if (std::holds_alternative<git_root_t>(root_)) {
            if (path == ".") {
                return true;
            }
            return static_cast<bool>(
                std::get<git_root_t>(root_).tree->LookupEntryByPath(path));
        }
        return FileSystemManager::Exists(std::get<fs_root_t>(root_) / path);
    }

    [[nodiscard]] auto IsFile(
        std::filesystem::path const& file_path) const noexcept -> bool {
        if (std::holds_alternative<git_root_t>(root_)) {
            if (auto entry =
                    std::get<git_root_t>(root_).tree->LookupEntryByPath(
                        file_path)) {
                return entry->IsBlob();
            }
            return false;
        }
        return FileSystemManager::IsFile(std::get<fs_root_t>(root_) /
                                         file_path);
    }

    [[nodiscard]] auto IsDirectory(
        std::filesystem::path const& dir_path) const noexcept -> bool {
        if (std::holds_alternative<git_root_t>(root_)) {
            if (dir_path == ".") {
                return true;
            }
            if (auto entry =
                    std::get<git_root_t>(root_).tree->LookupEntryByPath(
                        dir_path)) {
                return entry->IsTree();
            }
            return false;
        }
        return FileSystemManager::IsDirectory(std::get<fs_root_t>(root_) /
                                              dir_path);
    }

    [[nodiscard]] auto ReadFile(std::filesystem::path const& file_path)
        const noexcept -> std::optional<std::string> {
        if (std::holds_alternative<git_root_t>(root_)) {
            if (auto entry =
                    std::get<git_root_t>(root_).tree->LookupEntryByPath(
                        file_path)) {
                return entry->Blob();
            }
            return std::nullopt;
        }
        return FileSystemManager::ReadFile(std::get<fs_root_t>(root_) /
                                           file_path);
    }

    [[nodiscard]] auto ReadDirectory(std::filesystem::path const& dir_path)
        const noexcept -> DirectoryEntries {
        try {
            if (std::holds_alternative<git_root_t>(root_)) {
                auto const& tree = std::get<git_root_t>(root_).tree;
                if (dir_path == ".") {
                    return DirectoryEntries{&(*tree)};
                }
                if (auto entry = tree->LookupEntryByPath(dir_path)) {
                    if (auto const& found_tree = entry->Tree()) {
                        return DirectoryEntries{&(*found_tree)};
                    }
                }
            }
            else {
                std::unordered_set<std::string> names{};
                if (FileSystemManager::ReadDirectory(
                        std::get<fs_root_t>(root_) / dir_path,
                        [&names](auto name, auto /*type*/) {
                            names.emplace(name.string());
                            return true;
                        })) {
                    return DirectoryEntries{std::move(names)};
                }
            }
        } catch (std::exception const& ex) {
            Logger::Log(LogLevel::Error,
                        "reading directory {} failed with:\n{}",
                        dir_path.string(),
                        ex.what());
        }
        return {};
    }

    [[nodiscard]] auto FileType(std::filesystem::path const& file_path)
        const noexcept -> std::optional<ObjectType> {
        if (std::holds_alternative<git_root_t>(root_)) {
            if (auto entry =
                    std::get<git_root_t>(root_).tree->LookupEntryByPath(
                        file_path)) {
                if (entry->IsBlob()) {
                    return entry->Type();
                }
            }
            return std::nullopt;
        }
        auto type =
            FileSystemManager::Type(std::get<fs_root_t>(root_) / file_path);
        if (type and IsFileObject(*type)) {
            return type;
        }
        return std::nullopt;
    }

    [[nodiscard]] auto ReadBlob(std::string const& blob_id) const noexcept
        -> std::optional<std::string> {
        if (std::holds_alternative<git_root_t>(root_)) {
            return std::get<git_root_t>(root_).cas->ReadObject(
                blob_id, /*is_hex_id=*/true);
        }
        return std::nullopt;
    }

    // Create LOCAL or KNOWN artifact. Does not check existence for LOCAL.
    [[nodiscard]] auto ToArtifactDescription(
        std::filesystem::path const& file_path,
        std::string const& repository) const noexcept
        -> std::optional<ArtifactDescription> {
        if (std::holds_alternative<git_root_t>(root_)) {
            if (auto entry =
                    std::get<git_root_t>(root_).tree->LookupEntryByPath(
                        file_path)) {
                if (entry->IsBlob()) {
                    return ArtifactDescription{
                        ArtifactDigest{entry->Hash(), *entry->Size()},
                        entry->Type(),
                        repository};
                }
            }
            return std::nullopt;
        }
        return ArtifactDescription{file_path, repository};
    }

  private:
    root_t root_;
};

#endif  // INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_FILE_ROOT_HPP
