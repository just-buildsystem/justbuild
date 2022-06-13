#ifndef INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_GIT_TREE_HPP
#define INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_GIT_TREE_HPP

#include <filesystem>
#include <optional>
#include <unordered_map>

#include "gsl-lite/gsl-lite.hpp"
#include "src/buildtool/file_system/git_cas.hpp"
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
    [[nodiscard]] static auto Read(gsl::not_null<GitCASPtr> const& cas,
                                   std::string const& tree_id) noexcept
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

  private:
    gsl::not_null<GitCASPtr> cas_;
    entries_t entries_;
    std::string raw_id_;

    GitTree(gsl::not_null<GitCASPtr> cas,
            entries_t&& entries,
            std::string raw_id) noexcept
        : cas_{std::move(cas)},
          entries_{std::move(entries)},
          raw_id_{std::move(raw_id)} {}
};

class GitTreeEntry {
  public:
    GitTreeEntry(gsl::not_null<GitCASPtr> cas,
                 std::string raw_id,
                 ObjectType type) noexcept
        : cas_{std::move(cas)}, raw_id_{std::move(raw_id)}, type_{type} {}

    [[nodiscard]] auto IsBlob() const noexcept { return IsFileObject(type_); }
    [[nodiscard]] auto IsTree() const noexcept { return IsTreeObject(type_); }

    [[nodiscard]] auto Blob() const noexcept -> std::optional<std::string>;
    [[nodiscard]] auto Tree() && = delete;
    [[nodiscard]] auto Tree() const& noexcept -> std::optional<GitTree> const&;

    [[nodiscard]] auto Hash() const noexcept { return ToHexString(raw_id_); }
    [[nodiscard]] auto Type() const noexcept { return type_; }
    // Use with care. Implementation might read entire object to obtain size.
    // Consider using Blob()->size() instead.
    [[nodiscard]] auto Size() const noexcept -> std::optional<std::size_t>;

  private:
    gsl::not_null<GitCASPtr> cas_;
    std::string raw_id_;
    ObjectType type_;
    AtomicValue<std::optional<GitTree>> tree_cached_{};
};

using GitTreePtr = std::shared_ptr<GitTree const>;

#endif  // INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_GIT_TREE_HPP
