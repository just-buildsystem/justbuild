// Copyright 2023 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDTOOL_STORAGE_CONFIG_HPP
#define INCLUDED_SRC_BUILDTOOL_STORAGE_CONFIG_HPP

#include <cstddef>
#include <filesystem>
#include <string>

#include "gsl/gsl"
#include "src/buildtool/compatibility/compatibility.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/gsl.hpp"
#include "src/utils/cpp/tmp_dir.hpp"

class StorageConfig;

struct GenerationConfig final {
    gsl::not_null<StorageConfig const*> const storage_config;
    std::filesystem::path const cas_f;
    std::filesystem::path const cas_x;
    std::filesystem::path const cas_t;
    std::filesystem::path const cas_large_f;
    std::filesystem::path const cas_large_t;
    std::filesystem::path const action_cache;
    std::filesystem::path const target_cache;
};

/// \brief Global storage configuration.
class StorageConfig {
  public:
    static inline auto const kDefaultBuildRoot =
        FileSystemManager::GetUserHome() / ".cache" / "just";

    [[nodiscard]] static auto Instance() noexcept -> StorageConfig& {
        static StorageConfig config;
        return config;
    }

    [[nodiscard]] auto SetBuildRoot(std::filesystem::path const& dir) noexcept
        -> bool {
        if (FileSystemManager::IsRelativePath(dir)) {
            Logger::Log(LogLevel::Error,
                        "Build root must be absolute path but got '{}'.",
                        dir.string());
            return false;
        }
        build_root_ = dir;
        return true;
    }

    /// \brief Specifies the number of storage generations.
    auto SetNumGenerations(std::size_t num_generations) noexcept -> void {
        num_generations_ = num_generations;
    }

    /// \brief Number of storage generations.
    [[nodiscard]] auto NumGenerations() const noexcept -> std::size_t {
        return num_generations_;
    }

    /// \brief Build directory, defaults to user directory if not set
    [[nodiscard]] auto BuildRoot() const noexcept
        -> std::filesystem::path const& {
        return build_root_;
    }

    /// \brief Root directory of all storage generations.
    [[nodiscard]] auto CacheRoot() const noexcept -> std::filesystem::path {
        return BuildRoot() / "protocol-dependent";
    }

    /// \brief Directory for the git repository storing various roots
    [[nodiscard]] auto GitRoot() const noexcept -> std::filesystem::path {
        return BuildRoot() / "git";
    }

    /// \brief Root directory of specific storage generation
    [[nodiscard]] auto GenerationCacheRoot(std::size_t index) const noexcept
        -> std::filesystem::path {
        ExpectsAudit(index < num_generations_);
        auto generation = std::string{"generation-"} + std::to_string(index);
        return CacheRoot() / generation;
    }

    /// \brief Root directory for all ephemeral directories, i.e., directories
    /// that can (and should) be removed immediately by garbage collection.
    [[nodiscard]] auto EphemeralRoot() const noexcept -> std::filesystem::path {
        return GenerationCacheRoot(0) / "ephemeral";
    }

    /// \brief Root directory for local action executions; individual actions
    /// create a working directory below this root.
    [[nodiscard]] auto ExecutionRoot() const noexcept -> std::filesystem::path {
        return EphemeralRoot() / "exec_root";
    }

    /// \brief Create a tmp directory with controlled lifetime for specific
    /// operations (archive, zip, file, distdir checkouts; fetch; update).
    [[nodiscard]] auto CreateTypedTmpDir(std::string const& type) const noexcept
        -> TmpDirPtr {
        // try to create parent dir
        auto parent_path = EphemeralRoot() / "tmp-workspaces" / type;
        return TmpDir::Create(parent_path);
    }

    [[nodiscard]] auto CreateGenerationConfig(
        std::size_t generation) const noexcept -> GenerationConfig {
        bool const compatible = Compatibility::IsCompatible();
        auto const cache_root = GenerationCacheRoot(generation);
        auto const cache_dir =
            UpdatePathForCompatibility(cache_root, compatible);

        return GenerationConfig{
            .storage_config = this,
            .cas_f = cache_dir / "casf",
            .cas_x = cache_dir / "casx",
            .cas_t = cache_dir / (compatible ? "casf" : "cast"),
            .cas_large_f = cache_dir / "cas-large-f",
            .cas_large_t =
                cache_dir / (compatible ? "cas-large-f" : "cas-large-t"),
            .action_cache = cache_dir / "ac",
            .target_cache = cache_dir / "tc"};
    };

  private:
    // Build root directory. All the storage dirs are subdirs of build_root.
    // By default, build_root is set to $HOME/.cache/just.
    // If the user uses --local-build-root PATH,
    // then build_root will be set to PATH.
    std::filesystem::path build_root_{kDefaultBuildRoot};

    // Number of total storage generations (default: two generations).
    std::size_t num_generations_{2};

    // different folder for different caching protocol
    [[nodiscard]] static auto UpdatePathForCompatibility(
        std::filesystem::path const& dir,
        bool is_compatible) -> std::filesystem::path {
        return dir / (is_compatible ? "compatible-sha256" : "git-sha1");
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_STORAGE_CONFIG_HPP
