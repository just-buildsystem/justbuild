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

#ifndef INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_GIT_TREE_HPP
#define INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_GIT_TREE_HPP

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>  // std::move

#include "gsl/gsl"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/multithreading/atomic_value.hpp"
#include "src/utils/cpp/hex_string.hpp"

class GitTreeEntry;
using GitTreeEntryPtr = std::shared_ptr<GitTreeEntry const>;

class GitTree {
    friend class GitTreeEntry;

  public:
    using entries_t =
        std::unordered_map<std::string, gsl::not_null<GitTreeEntryPtr>>;

    /// \brief Read tree with given id from Git repository.
    /// \param repo_path  Path to the Git repository.
    /// \param tree_id    Tree id as as hex string.
    [[nodiscard]] static auto Read(std::filesystem::path const& repo_path,
                                   std::string const& tree_id) noexcept
        -> std::optional<GitTree>;

    /// \brief Read tree with given id from CAS.
    /// \param cas      Git CAS that contains the tree id.
    /// \param tree_id  Tree id as as hex string.
    /// \param ignore_special   If set, treat symlinks as absent.
    /// NOTE: If ignore_special==true, the stored entries might differ from the
    /// actual tree, so the stored ID is set to empty to signal that it should
    /// not be used.
    [[nodiscard]] static auto Read(gsl::not_null<GitCASPtr> const& cas,
                                   std::string const& tree_id,
                                   bool ignore_special = false) noexcept
        -> std::optional<GitTree>;

    /// \brief Lookup by dir entry name. '.' and '..' are not allowed.
    [[nodiscard]] auto LookupEntryByName(std::string const& name) const noexcept
        -> GitTreeEntryPtr;

    /// \brief Lookup by relative path. '.' is not allowed.
    [[nodiscard]] auto LookupEntryByPath(
        std::filesystem::path const& path) const noexcept -> GitTreeEntryPtr;

    [[nodiscard]] auto begin() const noexcept { return entries_.begin(); }
    [[nodiscard]] auto end() const noexcept { return entries_.end(); }
    [[nodiscard]] auto Hash() const noexcept { return ToHexString(raw_id_); }
    [[nodiscard]] auto RawHash() const noexcept { return raw_id_; }
    [[nodiscard]] auto Size() const noexcept -> std::optional<std::size_t>;
    [[nodiscard]] auto RawData() const noexcept -> std::optional<std::string>;

  private:
    gsl::not_null<GitCASPtr> cas_;
    entries_t entries_;
    std::string raw_id_;
    // If set, ignore all fast tree lookups and always traverse
    bool ignore_special_;

    GitTree(gsl::not_null<GitCASPtr> const& cas,
            entries_t&& entries,
            std::string raw_id,
            bool ignore_special = false) noexcept
        : cas_{cas},
          entries_{std::move(entries)},
          raw_id_{std::move(raw_id)},
          ignore_special_{ignore_special} {}

    [[nodiscard]] static auto FromEntries(gsl::not_null<GitCASPtr> const& cas,
                                          GitRepo::tree_entries_t&& entries,
                                          std::string raw_id,
                                          bool ignore_special = false) noexcept
        -> std::optional<GitTree> {
        entries_t e{};
        e.reserve(entries.size());
        for (auto& [id, es] : entries) {
            for (auto& entry : es) {
                try {
                    e.emplace(
                        std::move(entry.name),
                        std::make_shared<GitTreeEntry>(cas, id, entry.type));
                } catch (...) {
                    return std::nullopt;
                }
            }
        }
        return GitTree(cas, std::move(e), std::move(raw_id), ignore_special);
    }
};

class GitTreeEntry {
  public:
    GitTreeEntry(gsl::not_null<GitCASPtr> const& cas,
                 std::string raw_id,
                 ObjectType type) noexcept
        : cas_{cas}, raw_id_{std::move(raw_id)}, type_{type} {}

    [[nodiscard]] auto IsBlob() const noexcept {
        return IsFileObject(type_) or IsSymlinkObject(type_);
    }
    [[nodiscard]] auto IsTree() const noexcept { return IsTreeObject(type_); }

    [[nodiscard]] auto Blob() const noexcept -> std::optional<std::string>;
    [[nodiscard]] auto Tree(bool) && = delete;
    [[nodiscard]] auto Tree(bool ignore_special = false) const& noexcept
        -> std::optional<GitTree> const&;

    [[nodiscard]] auto Hash() const noexcept { return ToHexString(raw_id_); }
    [[nodiscard]] auto Type() const noexcept { return type_; }
    // Use with care. Implementation might read entire object to obtain
    // size. Consider using Blob()->size() instead.
    [[nodiscard]] auto Size() const noexcept -> std::optional<std::size_t>;
    [[nodiscard]] auto RawData() const noexcept -> std::optional<std::string>;

  private:
    gsl::not_null<GitCASPtr> cas_;
    std::string raw_id_;
    ObjectType type_;
    AtomicValue<std::optional<GitTree>> tree_cached_{};
};

using GitTreePtr = std::shared_ptr<GitTree const>;

#endif  // INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_GIT_TREE_HPP
