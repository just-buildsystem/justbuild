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

#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>  // std::move
#include <vector>

#include "gsl/gsl"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/file_system/git_cas.hpp"
#include "src/buildtool/file_system/git_types.hpp"
#include "src/buildtool/file_system/git_utils.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/utils/cpp/expected.hpp"

extern "C" {
struct git_repository;
struct git_config;
struct git_strarray;
}

/// \brief Git repository logic.
/// Models both a real repository, owning the underlying ODB
/// (non-thread-safe), as well as a "fake" repository, which only wraps an
/// existing ODB, allowing thread-safe operations.
class GitRepo {
  public:
    // Stores the data for defining a single Git tree entry, which consists of
    // a name (flat basename) and an object type (file/executable/tree).
    struct TreeEntry {
        TreeEntry(std::string n, ObjectType t) : name{std::move(n)}, type{t} {}
        std::string name;
        ObjectType type;
        [[nodiscard]] auto operator==(TreeEntry const& other) const noexcept
            -> bool {
            return name == other.name and type == other.type;
        }
    };

    // Tree entries by raw id. The same id might refer to multiple entries.
    // Note that sharding by id is used as this format enables a more efficient
    // internal implementation for creating trees.
    using tree_entries_t =
        std::unordered_map<std::string, std::vector<TreeEntry>>;

    // Stores the info of an object read by its path.
    struct TreeEntryInfo {
        std::string id;
        ObjectType type;
        // if type is symlink, read it in advance
        std::optional<std::string> symlink_content{std::nullopt};
    };

    // Checks whether a list of symlinks given by their hashes are
    // non-upwards, based on content read from an actual backend.
    using SymlinksCheckFunc =
        std::function<bool(std::vector<ArtifactDigest> const&)>;

    GitRepo() = delete;  // no default ctor
    ~GitRepo() noexcept = default;

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
    /// \param check_symlinks   Function to check non-upwardness condition.
    /// \param is_hex_id  Specify whether `id` is hex string or raw.
    /// \param ignore_special   If set, treat symlinks as absent.
    [[nodiscard]] auto ReadTree(
        std::string const& id,
        gsl::not_null<SymlinksCheckFunc> const& check_symlinks,
        bool is_hex_id = false,
        bool ignore_special = false) const noexcept
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
    /// \param check_symlinks   Function to check non-upwardness condition.
    /// \param is_hex_id  Specify whether `id` is hex string or raw.
    /// \returns The tree entries.
    [[nodiscard]] static auto ReadTreeData(
        std::string const& data,
        std::string const& id,
        gsl::not_null<SymlinksCheckFunc> const& check_symlinks,
        bool is_hex_id = false) noexcept -> std::optional<tree_entries_t>;

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

    using anon_logger_t = std::function<void(std::string const&, bool)>;
    using anon_logger_ptr = std::shared_ptr<anon_logger_t>;

    /// \brief Create tree from entries at given directory and commit it with
    /// given message. The given path need not be a subdirectory of the
    /// repository root path, but the caller must guarantee its entries are
    /// readable.
    /// Only possible with real repository and thus non-thread-safe.
    /// \returns The commit hash, or nullopt if failure. It guarantees the
    /// logger is called exactly once with fatal if failure.
    [[nodiscard]] auto CommitDirectory(std::filesystem::path const& dir,
                                       std::string const& message,
                                       anon_logger_ptr const& logger) noexcept
        -> std::optional<std::string>;

    /// \brief Create annotated tag for given commit.
    /// Only possible with real repository and thus non-thread-safe.
    /// Returns the tag on success.
    /// It guarantees the logger is called exactly once with fatal if failure.
    [[nodiscard]] auto KeepTag(std::string const& commit,
                               std::string const& message,
                               anon_logger_ptr const& logger) noexcept
        -> std::optional<std::string>;

    /// \brief Retrieves the commit of the HEAD reference.
    /// Only possible with real repository and thus non-thread-safe.
    /// Returns the commit hash as a string, or nullopt if failure.
    /// It guarantees the logger is called exactly once with fatal if failure.
    [[nodiscard]] auto GetHeadCommit(anon_logger_ptr const& logger) noexcept
        -> std::optional<std::string>;

    /// \brief Fetch from given local repository. It can either fetch a given
    /// named branch, or it can fetch with base refspecs.
    /// Only possible with real repository and thus non-thread-safe.
    /// If non-null, use given config snapshot to interact with config entries;
    /// otherwise, use a snapshot from the current repo and share pointer to it.
    /// Returns a success flag. It guarantees the logger is called exactly once
    /// with fatal if failure.
    [[nodiscard]] auto FetchFromPath(std::shared_ptr<git_config> cfg,
                                     std::string const& repo_path,
                                     std::optional<std::string> const& branch,
                                     anon_logger_ptr const& logger) noexcept
        -> bool;

    /// \brief Ensure given tree is kept alive via a tag. It is expected that
    /// the tree is part of the repository already.
    /// Only possible with real repository and thus non-thread-safe.
    /// Returns the tag on success.
    /// It guarantees the logger is called exactly once with fatal if failure.
    [[nodiscard]] auto KeepTree(std::string const& tree_id,
                                std::string const& message,
                                anon_logger_ptr const& logger) noexcept
        -> std::optional<std::string>;

    /// \brief Get the tree id of a subtree given the root commit
    /// Calling it from a fake repository allows thread-safe use.
    /// Returns the subtree hash on success or an unexpected error.
    [[nodiscard]] auto GetSubtreeFromCommit(
        std::string const& commit,
        std::string const& subdir,
        anon_logger_ptr const& logger) noexcept
        -> expected<std::string, GitLookupError>;

    /// \brief Get the tree id of a subtree given the root tree hash
    /// Calling it from a fake repository allows thread-safe use.
    /// Returns the subtree hash, as a string, or nullopt if failure.
    /// It guarantees the logger is called exactly once with fatal if failure.
    [[nodiscard]] auto GetSubtreeFromTree(
        std::string const& tree_id,
        std::string const& subdir,
        anon_logger_ptr const& logger) noexcept -> std::optional<std::string>;

    /// \brief Get the tree id of a subtree given a filesystem directory path.
    /// Calling it from a fake repository allows thread-safe use.
    /// Will search for the root of the repository containing the path,
    /// and deduce the subdir relationship in the Git tree.
    /// Requires for the HEAD commit to be already known.
    /// Returns the tree hash, as a string, or nullopt if failure.
    /// It guarantees the logger is called exactly once with fatal if failure.
    [[nodiscard]] auto GetSubtreeFromPath(
        std::filesystem::path const& fpath,
        std::string const& head_commit,
        anon_logger_ptr const& logger) noexcept -> std::optional<std::string>;

    /// \brief Check if given commit is part of the local repository.
    /// Calling it from a fake repository allows thread-safe use.
    /// Returns a status of commit presence, or nullopt if failure.
    /// It guarantees the logger is called exactly once with fatal if failure.
    [[nodiscard]] auto CheckCommitExists(std::string const& commit,
                                         anon_logger_ptr const& logger) noexcept
        -> std::optional<bool>;

    /// \brief Try to retrieve the root of the repository containing the
    /// given path, if the path is actually part of a repository.
    /// Returns the git folder if path is in a git repo, empty string if path is
    /// not in a git repo, or nullopt if failure.
    /// It guarantees the logger is called exactly once with fatal if failure.
    [[nodiscard]] static auto GetRepoRootFromPath(
        std::filesystem::path const& fpath,
        anon_logger_ptr const& logger) noexcept
        -> std::optional<std::filesystem::path>;

    /// \brief Check if given tree ID is present in the directory structure of
    /// the local repository.
    /// Calling it from a fake repository allows thread-safe use.
    /// Returns a status of tree presence, or nullopt if failure.
    /// It guarantees the logger is called exactly once with fatal if failure.
    [[nodiscard]] auto CheckTreeExists(std::string const& tree_id,
                                       anon_logger_ptr const& logger) noexcept
        -> std::optional<bool>;

    /// \brief Check if given blob ID is present in the directory structure of
    /// the local repository.
    /// Calling it from a fake repository allows thread-safe use.
    /// Returns a status of blob presence, or nullopt if failure.
    /// It guarantees the logger is called exactly once with fatal if failure.
    [[nodiscard]] auto CheckBlobExists(std::string const& blob_id,
                                       anon_logger_ptr const& logger) noexcept
        -> std::optional<bool>;

    /// \brief Check if given blob ID is present in the directory structure
    /// of the local repository and try to return it. Calling it from a fake
    /// repository allows thread-safe use. Returns a pair of a success flag,
    /// stating that no errors occurred while performing the libgit2 calls,
    /// and an optional string containing the content of the blob, if the
    /// blob is found. It guarantees the logger is called exactly once with
    /// fatal if failure.
    [[nodiscard]] auto TryReadBlob(std::string const& blob_id,
                                   anon_logger_ptr const& logger) noexcept
        -> std::pair<bool, std::optional<std::string>>;

    /// \brief Write given content as a blob into underlying object database.
    /// Calling it from a fake repository allows thread-safe use.
    /// \returns Git ID of the written blob, or nullopt on errors.
    /// It guarantees the logger is called exactly once with fatal if failure.
    /// Use with care, especially for large objects.
    [[nodiscard]] auto WriteBlob(std::string const& content,
                                 anon_logger_ptr const& logger) noexcept
        -> std::optional<std::string>;

    /// \brief Get the object info related to a given path inside a Git tree.
    /// Unlike GetSubtreeFromTree, we here ignore errors and only return a value
    /// when all is successful.
    /// Calling it from a fake repository allows thread-safe use.
    [[nodiscard]] auto GetObjectByPathFromTree(
        std::string const& tree_id,
        std::string const& rel_path) noexcept -> std::optional<TreeEntryInfo>;

    /// \brief Fetch from given local repository via a temporary location. Uses
    /// tmp dir to fetch asynchronously using libgit2.
    /// Uses either a given branch, or fetches all (with base refspecs).
    /// Returns a success flag.
    /// It guarantees the logger is called exactly once with fatal if failure.
    [[nodiscard]] auto LocalFetchViaTmpRepo(
        StorageConfig const& storage_config,
        std::string const& repo_path,
        std::optional<std::string> const& branch,
        anon_logger_ptr const& logger) noexcept -> bool;

    /// \brief Get a snapshot of the repository configuration.
    /// Returns nullptr on errors.
    [[nodiscard]] auto GetConfigSnapshot() const noexcept
        -> std::shared_ptr<git_config>;

    /// \brief Import source directory to target git repository
    /// \param storage_config   Storage where the source must be imported to
    /// \param source_dir       Directory to import
    /// \param commit_message   Message of the commit
    /// \param tagging_lock     Mutex to protect critical git operations
    /// \return The tree id of the commited directory on success or an
    /// unexpected error as string on failure.
    [[nodiscard]] static auto ImportToGit(
        StorageConfig const& storage_config,
        std::filesystem::path const& source_dir,
        std::string const& commit_message,
        gsl::not_null<std::mutex*> const& tagging_lock) noexcept
        -> expected<std::string, std::string>;

    /// \brief Check that the given repository contains the given tree
    /// \returns Flag reflecting whether the tree is present in the repository
    /// or an error message on failure.
    [[nodiscard]] static auto IsTreeInRepo(std::filesystem::path const& repo,
                                           std::string const& tree_id) noexcept
        -> expected<bool, std::string>;

  private:
    GitCASPtr git_cas_;
    // default to real repo, as that is non-thread-safe
    bool is_repo_fake_;

  protected:
    /// \brief Open "fake" repository wrapper for existing CAS.
    explicit GitRepo(GitCASPtr git_cas) noexcept;
    /// \brief Open real repository at given location.
    explicit GitRepo(std::filesystem::path const& repo_path) noexcept;

    /// \brief Function type handling directory entries read from filesystem.
    /// \returns Success flag. Must guarantee that the logger is called exactly
    /// once with fatal if returning false.
    using StoreDirEntryFunc = std::function<
        bool(std::filesystem::path const&, ObjectType, anon_logger_ptr const&)>;

    /// \brief Helper function to read the entries of a filesystem subdirectory
    /// and store them to the ODB. It is a modified version of the same-named
    /// function from FileSystemManager which accepts a subdir and a specific
    /// logger instead of the default.
    /// It guarantees the logger is called exactly once with fatal if failure.
    [[nodiscard]] static auto ReadDirectory(
        std::filesystem::path const& dir,
        StoreDirEntryFunc const& read_and_store_entry,
        anon_logger_ptr const& logger) noexcept -> bool;

    /// \brief Create a tree from the content of a directory by recursively
    /// adding its entries to the object database.
    /// \return The raw id of the tree.
    /// It guarantees the logger is called exactly once with fatal if failure.
    [[nodiscard]] auto CreateTreeFromDirectory(
        std::filesystem::path const& dir,
        anon_logger_ptr const& logger) noexcept -> std::optional<std::string>;

    class GitStrArray final {
      public:
        void AddEntry(std::string entry);
        [[nodiscard]] auto Get() & noexcept -> git_strarray;

      private:
        std::vector<std::string> entries_;
        std::vector<char*> entry_pointers_;
    };
};

#endif  // INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_GIT_REPO_HPP
