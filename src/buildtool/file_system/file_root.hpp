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

#ifndef INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_FILE_ROOT_HPP
#define INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_FILE_ROOT_HPP

#include <filesystem>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

#include "gsl/gsl"
#include "nlohmann/json.hpp"
#include "src/buildtool/common/artifact_description.hpp"
#include "src/buildtool/compatibility/compatibility.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/git_tree.hpp"
#include "src/utils/cpp/concepts.hpp"

/// FilteredIterator is an helper class to allow for iteration over
/// directory-only or file-only entries stored inside the class
/// DirectoryEntries. Internally, DirectoryEntries holds a
/// map<string,ObjectType> or map<string, GitTree*>. While iterating, we are
/// just interested in the name of the entry (i.e., the string).
/// To decide which entries retain, the FilteredIterator requires a predicate.
template <StrMapConstForwardIterator I>
// I is a forward iterator
// I::value_type is a std::pair<const std::string, *>
class FilteredIterator {
  public:
    using value_type = std::string const;
    using pointer = value_type*;
    using reference = value_type&;
    using difference_type = std::ptrdiff_t;
    using iteratori_category = std::forward_iterator_tag;
    using predicate_t = std::function<bool(typename I::reference)>;

    FilteredIterator() noexcept = default;
    // [first, last) is a valid sequence
    FilteredIterator(I first, I last, predicate_t p) noexcept
        : iterator_{std::find_if(first, last, p)},
          end_{std::move(last)},
          p{std::move(p)} {}

    auto operator*() const noexcept -> reference { return iterator_->first; }

    auto operator++() noexcept -> FilteredIterator& {
        ++iterator_;
        iterator_ = std::find_if(iterator_, end_, p);
        return *this;
    }

    [[nodiscard]] auto begin() noexcept -> FilteredIterator& { return *this; }
    [[nodiscard]] auto end() const noexcept -> FilteredIterator {
        return FilteredIterator{end_, end_, p};
    }

    [[nodiscard]] friend auto operator==(FilteredIterator const& x,
                                         FilteredIterator const& y) noexcept
        -> bool {
        return x.iterator_ == y.iterator_;
    }

    [[nodiscard]] friend auto operator!=(FilteredIterator const& x,
                                         FilteredIterator const& y) noexcept
        -> bool {
        return not(x == y);
    }

  private:
    I iterator_{};
    const I end_{};
    predicate_t p{};
};

class FileRoot {
    using fs_root_t = std::filesystem::path;
    struct git_root_t {
        gsl::not_null<GitCASPtr> cas;
        gsl::not_null<GitTreePtr> tree;
    };
    using root_t = std::variant<fs_root_t, git_root_t>;

  public:
    static constexpr auto kGitTreeMarker = "git tree";

    class DirectoryEntries {
        friend class FileRoot;

      public:
        using pairs_t = std::unordered_map<std::string, ObjectType>;
        using tree_t = gsl::not_null<GitTree const*>;
        using entries_t = std::variant<tree_t, pairs_t>;

        using fs_iterator_type = typename pairs_t::const_iterator;
        using fs_iterator = FilteredIterator<fs_iterator_type>;

        using git_iterator_type = GitTree::entries_t::const_iterator;
        using git_iterator = FilteredIterator<git_iterator_type>;

      private:
        /// Iterator has two FilteredIterators, one for iterating over pairs_t
        /// and one for tree_t. Each FilteredIterator is constructed with a
        /// proper predicate, allowing for iteration on file-only or
        /// directory-only entries
        class Iterator {
          public:
            using value_type = std::string const;
            using pointer = value_type*;
            using reference = value_type&;
            using difference_type = std::ptrdiff_t;
            using iteratori_category = std::forward_iterator_tag;
            explicit Iterator(fs_iterator fs_it) : it_{std::move(fs_it)} {}
            explicit Iterator(git_iterator git_it) : it_{std::move(git_it)} {}

            auto operator*() const noexcept -> reference {
                if (std::holds_alternative<fs_iterator>(it_)) {
                    return *std::get<fs_iterator>(it_);
                }
                return *std::get<git_iterator>(it_);
            }

            [[nodiscard]] auto begin() noexcept -> Iterator& { return *this; }
            [[nodiscard]] auto end() const noexcept -> Iterator {
                if (std::holds_alternative<fs_iterator>(it_)) {
                    return Iterator{std::get<fs_iterator>(it_).end()};
                }
                return Iterator{std::get<git_iterator>(it_).end()};
            }
            auto operator++() noexcept -> Iterator& {
                if (std::holds_alternative<fs_iterator>(it_)) {
                    ++(std::get<fs_iterator>(it_));
                }
                else {
                    ++(std::get<git_iterator>(it_));
                }
                return *this;
            }

            friend auto operator==(Iterator const& x,
                                   Iterator const& y) noexcept -> bool {
                return x.it_ == y.it_;
            }
            friend auto operator!=(Iterator const& x,
                                   Iterator const& y) noexcept -> bool {
                return not(x == y);
            }

          private:
            std::variant<fs_iterator, git_iterator> it_;
        };

      public:
        explicit DirectoryEntries(pairs_t pairs) noexcept
            : data_{std::move(pairs)} {}

        explicit DirectoryEntries(tree_t const& git_tree) noexcept
            : data_{git_tree} {}

        [[nodiscard]] auto ContainsFile(std::string const& name) const noexcept
            -> bool {
            try {
                if (std::holds_alternative<tree_t>(data_)) {
                    auto const& data = std::get<tree_t>(data_);
                    auto ptr = data->LookupEntryByName(name);
                    if (static_cast<bool>(ptr)) {
                        return ptr->IsBlob();
                    }
                    return false;
                }
                if (std::holds_alternative<pairs_t>(data_)) {
                    auto const& data = std::get<pairs_t>(data_);
                    auto it = data.find(name);
                    return (it != data.end() and IsFileObject(it->second));
                }
            } catch (...) {
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
            if (std::holds_alternative<pairs_t>(data_)) {
                return std::get<pairs_t>(data_).empty();
            }
            return true;
        }

        [[nodiscard]] auto AsKnownTree(std::string const& repository)
            const noexcept -> std::optional<ArtifactDescription> {
            if (Compatibility::IsCompatible()) {
                return std::nullopt;
            }
            if (std::holds_alternative<tree_t>(data_)) {
                try {
                    auto const& data = std::get<tree_t>(data_);
                    auto const& id = data->Hash();
                    auto const& size = data->Size();
                    if (size) {
                        return ArtifactDescription{
                            ArtifactDigest{id, *size, /*is_tree=*/true},
                            ObjectType::Tree,
                            repository};
                    }
                } catch (...) {
                    return std::nullopt;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] auto FilesIterator() const -> Iterator {
            if (std::holds_alternative<pairs_t>(data_)) {
                auto const& data = std::get<pairs_t>(data_);
                return Iterator{FilteredIterator{
                    data.begin(), data.end(), [](auto const& x) {
                        return IsFileObject(x.second);
                    }}};
            }
            // std::holds_alternative<tree_t>(data_) == true
            auto const& data = std::get<tree_t>(data_);
            return Iterator{FilteredIterator{
                data->begin(), data->end(), [](auto const& x) noexcept -> bool {
                    return x.second->IsBlob();
                }}};
        }

        [[nodiscard]] auto DirectoriesIterator() const -> Iterator {
            if (std::holds_alternative<pairs_t>(data_)) {
                auto const& data = std::get<pairs_t>(data_);
                return Iterator{FilteredIterator{
                    data.begin(), data.end(), [](auto const& x) {
                        return IsTreeObject(x.second);
                    }}};
            }

            // std::holds_alternative<tree_t>(data_) == true
            auto const& data = std::get<tree_t>(data_);
            return Iterator{FilteredIterator{
                data->begin(), data->end(), [](auto const& x) noexcept -> bool {
                    return x.second->IsTree();
                }}};
        }

      private:
        entries_t data_;
    };

    FileRoot() noexcept = default;
    explicit FileRoot(std::filesystem::path root) noexcept
        : root_{std::move(root)} {}
    FileRoot(gsl::not_null<GitCASPtr> const& cas,
             gsl::not_null<GitTreePtr> const& tree) noexcept
        : root_{git_root_t{cas, tree}} {}

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

    // Return a complete description of the content of this root, if that can be
    // done without any file-system access.
    [[nodiscard]] auto ContentDescription() const noexcept
        -> std::optional<nlohmann::json> {
        try {
            if (std::holds_alternative<git_root_t>(root_)) {
                nlohmann::json j;
                j.push_back(kGitTreeMarker);
                j.push_back(std::get<git_root_t>(root_).tree->Hash());
                return j;
            }
        } catch (...) {
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
                DirectoryEntries::pairs_t map{};
                if (FileSystemManager::ReadDirectory(
                        std::get<fs_root_t>(root_) / dir_path,
                        [&map](const auto& name, auto type) {
                            map.emplace(name.string(), type);
                            return true;
                        })) {
                    return DirectoryEntries{std::move(map)};
                }
            }
        } catch (std::exception const& ex) {
            Logger::Log(LogLevel::Error,
                        "reading directory {} failed with:\n{}",
                        dir_path.string(),
                        ex.what());
        }
        return DirectoryEntries{DirectoryEntries::pairs_t{}};
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

    [[nodiscard]] auto ReadTree(std::string const& tree_id) const noexcept
        -> std::optional<GitTree> {
        if (std::holds_alternative<git_root_t>(root_)) {
            try {
                auto const& cas = std::get<git_root_t>(root_).cas;
                return GitTree::Read(cas, tree_id);
            } catch (...) {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    // Create LOCAL or KNOWN artifact. Does not check existence for LOCAL.
    // `file_path` must reference a blob.
    [[nodiscard]] auto ToArtifactDescription(
        std::filesystem::path const& file_path,
        std::string const& repository) const noexcept
        -> std::optional<ArtifactDescription> {
        if (std::holds_alternative<git_root_t>(root_)) {
            if (auto entry =
                    std::get<git_root_t>(root_).tree->LookupEntryByPath(
                        file_path)) {
                if (entry->IsBlob()) {
                    if (Compatibility::IsCompatible()) {
                        auto compatible_hash = Compatibility::RegisterGitEntry(
                            entry->Hash(), *entry->Blob(), repository);
                        return ArtifactDescription{
                            ArtifactDigest{compatible_hash,
                                           *entry->Size(),
                                           /*is_tree=*/false},
                            entry->Type()};
                    }
                    return ArtifactDescription{
                        ArtifactDigest{
                            entry->Hash(), *entry->Size(), /*is_tree=*/false},
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
