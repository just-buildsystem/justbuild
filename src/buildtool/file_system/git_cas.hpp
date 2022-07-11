#ifndef INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_GIT_CAS_HPP
#define INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_GIT_CAS_HPP

#include <filesystem>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "src/buildtool/file_system/object_type.hpp"

extern "C" {
using git_odb = struct git_odb;
}

class GitCAS;
using GitCASPtr = std::shared_ptr<GitCAS const>;

/// \brief Git CAS that maintains its own libgit2 global state.
class GitCAS {
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

    static auto Open(std::filesystem::path const& repo_path) noexcept
        -> GitCASPtr;

    GitCAS() noexcept;
    ~GitCAS() noexcept;

    // prohibit moves and copies
    GitCAS(GitCAS const&) = delete;
    GitCAS(GitCAS&& other) = delete;
    auto operator=(GitCAS const&) = delete;
    auto operator=(GitCAS&& other) = delete;

    /// \brief Read object from CAS.
    /// \param id         The object id.
    /// \param is_hex_id  Specify whether `id` is hex string or raw.
    [[nodiscard]] auto ReadObject(std::string const& id,
                                  bool is_hex_id = false) const noexcept
        -> std::optional<std::string>;

    /// \brief Read object header from CAS.
    /// \param id         The object id.
    /// \param is_hex_id  Specify whether `id` is hex string or raw.
    // Use with care. Quote from git2/odb.h:138:
    //    Note that most backends do not support reading only the header of an
    //    object, so the whole object will be read and then the header will be
    //    returned.
    [[nodiscard]] auto ReadHeader(std::string const& id,
                                  bool is_hex_id = false) const noexcept
        -> std::optional<std::pair<std::size_t, ObjectType>>;

    /// \brief Read entries from tree in CAS.
    /// Reading a tree must be backed by an object database. Therefore, a valid
    /// instance of this class is required.
    /// \param id         The object id.
    /// \param is_hex_id  Specify whether `id` is hex string or raw.
    [[nodiscard]] auto ReadTree(std::string const& id,
                                bool is_hex_id = false) const noexcept
        -> std::optional<tree_entries_t>;

    /// \brief Create a flat tree from entries and store tree in CAS.
    /// Creating a tree must be backed by an object database. Therefore, a valid
    /// instance of this class is required. Furthermore, all entries must be
    /// available in the underlying object database and object types must
    /// correctly reflect the type of the object found in the database.
    /// \param entries  The entries to create the tree from.
    /// \returns The raw object id as string, if successful.
    [[nodiscard]] auto CreateTree(GitCAS::tree_entries_t const& entries)
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
        GitCAS::tree_entries_t const& entries) noexcept
        -> std::optional<std::pair<std::string, std::string>>;

  private:
    git_odb* odb_{nullptr};
    bool initialized_{false};

    [[nodiscard]] auto OpenODB(std::filesystem::path const& repo_path) noexcept
        -> bool;
};

#endif  // INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_GIT_CAS_HPP
