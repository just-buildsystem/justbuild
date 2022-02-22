#ifndef INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_GIT_CAS_HPP
#define INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_GIT_CAS_HPP

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>

#include "src/buildtool/file_system/object_type.hpp"

extern "C" {
using git_odb = struct git_odb;
}

class GitCAS;
using GitCASPtr = std::shared_ptr<GitCAS const>;

/// \brief Git CAS that maintains its own libgit2 global state.
class GitCAS {
  public:
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

  private:
    static inline std::mutex repo_mutex_{};
    git_odb* odb_{nullptr};
    bool initialized_{false};

    [[nodiscard]] auto OpenODB(std::filesystem::path const& repo_path) noexcept
        -> bool;
};

#endif  // INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_GIT_CAS_HPP
