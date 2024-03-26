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

#ifndef INCLUDED_SRC_OTHER_TOOLS_FILE_LOCKING_HPP
#define INCLUDED_SRC_OTHER_TOOLS_FILE_LOCKING_HPP

#include <filesystem>
#include <memory>
#include <optional>
#include <utility>  // std::move

#include "gsl/gsl"

/* \brief Thread- and process-safe file locking mechanism for paths.
 * User guarantees write access in the parent directory of the path given, as
 * the lock will be placed there and missing tree directories will be created.
 */
class LockFile {
  public:
    // no default ctor
    LockFile() = delete;

    /// \brief Destroy the LockFile object.
    /// It only closes the open file descriptor, but does not explicitly unlock.
    ~LockFile() noexcept;

    // no copies, only moves
    LockFile(LockFile const&) = delete;
    LockFile(LockFile&& other) noexcept;
    auto operator=(LockFile const&) = delete;
    auto operator=(LockFile&& other) noexcept -> LockFile&;

    /// \brief Tries to acquire a lock file with the given name.
    /// Missing directories will be created if write permission exists.
    /// Returns the lock file object on success, nullopt on failure.
    [[nodiscard]] static auto Acquire(std::filesystem::path const& fspath,
                                      bool is_shared) noexcept
        -> std::optional<LockFile>;

  private:
    gsl::owner<FILE*> file_handle_{nullptr};
    std::filesystem::path lock_file_{};

    /// \brief Private ctor. Instances are only created by Acquire method.
    explicit LockFile(gsl::owner<FILE*> file_handle,
                      std::filesystem::path lock_file) noexcept
        : file_handle_{file_handle}, lock_file_{std::move(lock_file)} {};

    [[nodiscard]] static auto GetLockFilePath(
        std::filesystem::path const& fspath) noexcept
        -> std::optional<std::filesystem::path>;
};

#endif  // FILE_LOCKING
