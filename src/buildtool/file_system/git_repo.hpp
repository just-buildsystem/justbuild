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
    // Stores the data for defining a single Git tree entry, which consists of
    // a name (flat basename) and an object type (file/executable/tree).
    struct tree_entry_t {
        tree_entry_t(std::string n, ObjectType t)
            : name{std::move(n)}, type{t} {}
        std::string name;
        ObjectType type;
        [[nodiscard]] auto operator==(tree_entry_t const& other) const noexcept
            -> bool {
            return name == other.name and type == other.type;
        }
    };

    // Tree entries by raw id. The same id might refer to multiple entries.
    // Note that sharding by id is used as this format enables a more efficient
    // internal implementation for creating trees.
    using tree_entries_t =
        std::unordered_map<std::string, std::vector<tree_entry_t>>;

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

    /// \brief Read entries from tree in CAS.
    /// Reading a tree must be backed by an object database. Therefore, a real
    /// repository is required.
    /// \param id         The object id.
    /// \param is_hex_id  Specify whether `id` is hex string or raw.
    [[nodiscard]] auto ReadTree(std::string const& id,
                                bool is_hex_id = false) const noexcept
        -> std::optional<tree_entries_t>;

    /// \brief Create a flat tree from entries and store tree in CAS.
    /// Creating a tree must be backed by an object database. Therefore, a real
    /// repository is required. Furthermore, all entries must be available in
    /// the underlying object database and object types must correctly reflect
    /// the type of the object found in the database.
    /// \param entries  The entries to create the tree from.
    /// \returns The raw object id as string, if successful.
    [[nodiscard]] auto CreateTree(GitRepo::tree_entries_t const& entries)
        const noexcept -> std::optional<std::string>;

    /// \brief Read entries from tree data (without object db).
    /// \param data       The tree object as plain data.
    /// \param id         The object id.
    /// \param is_hex_id  Specify whether `id` is hex string or raw.
    /// \returns The tree entries.
    [[nodiscard]] static auto ReadTreeData(std::string const& data,
                                           std::string const& id,
                                           bool is_hex_id = false) noexcept
        -> std::optional<tree_entries_t>;

    /// \brief Create a flat shallow (without objects in db) tree and return it.
    /// Creates a tree object from the entries without access to the actual
    /// blobs. Objects are not required to be available in the underlying object
    /// database. It is sufficient to provide the raw object id and and object
    /// type for every entry.
    /// \param entries  The entries to create the tree from.
    /// \returns A pair of raw object id and the tree object content.
    [[nodiscard]] static auto CreateShallowTree(
        GitRepo::tree_entries_t const& entries) noexcept
        -> std::optional<std::pair<std::string, std::string>>;

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