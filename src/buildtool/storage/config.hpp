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

#ifdef __unix__
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#else
#error "Non-unix is not supported yet"
#endif

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <fmt/core.h>
#include <nlohmann/json.hpp>

#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/compatibility/compatibility.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/gsl.hpp"

#include <gsl/gsl>

/// \brief Global storage configuration.
class StorageConfig {
    struct ConfigData {
        // Build root directory. All the storage dirs are subdirs of build_root.
        // By default, build_root is set to $HOME/.cache/just.
        // If the user uses --local-build-root PATH,
        // then build_root will be set to PATH.
        std::filesystem::path build_root{kDefaultBuildRoot};

        // Number of total storage generations (default: two generations).
        std::size_t num_generations{2};
    };

  public:
    /// \brief Determine user home directory
    [[nodiscard]] static auto GetUserHome() noexcept -> std::filesystem::path {
        char const* root{nullptr};

#ifdef __unix__
        root = std::getenv("HOME");
        if (root == nullptr) {
            root = getpwuid(getuid())->pw_dir;
        }
#endif

        if (root == nullptr) {
            Logger::Log(LogLevel::Error,
                        "Cannot determine user home directory.");
            std::exit(EXIT_FAILURE);
        }

        return root;
    }

    static inline auto const kDefaultBuildRoot =
        GetUserHome() / ".cache" / "just";

    [[nodiscard]] static auto SetBuildRoot(
        std::filesystem::path const& dir) noexcept -> bool {
        if (FileSystemManager::IsRelativePath(dir)) {
            Logger::Log(LogLevel::Error,
                        "Build root must be absolute path but got '{}'.",
                        dir.string());
            return false;
        }
        Data().build_root = dir;
        return true;
    }

    /// \brief Specifies the number of storage generations.
    static auto SetNumGenerations(std::size_t num_generations) noexcept
        -> void {
        Data().num_generations = num_generations;
    }

    /// \brief Number of storage generations.
    [[nodiscard]] static auto NumGenerations() noexcept -> std::size_t {
        return Data().num_generations;
    }

    /// \brief Build directory, defaults to user directory if not set
    [[nodiscard]] static auto BuildRoot() noexcept -> std::filesystem::path {
        return Data().build_root;
    }

    /// \brief Root directory of all storage generations.
    [[nodiscard]] static auto CacheRoot() noexcept -> std::filesystem::path {
        return BuildRoot() / "protocol-dependent";
    }

    /// \brief Directory for the git repository storing various roots
    [[nodiscard]] static auto GitRoot() noexcept -> std::filesystem::path {
        return BuildRoot() / "git";
    }

    /// \brief Root directory of specific storage generation for compatible and
    /// non-compatible protocol types.
    [[nodiscard]] static auto GenerationCacheRoot(std::size_t index) noexcept
        -> std::filesystem::path {
        ExpectsAudit(index < Data().num_generations);
        auto generation = std::string{"generation-"} + std::to_string(index);
        return CacheRoot() / generation;
    }

    /// \brief Storage directory of specific generation and protocol type.
    [[nodiscard]] static auto GenerationCacheDir(std::size_t index) noexcept
        -> std::filesystem::path {
        return UpdatePathForCompatibility(GenerationCacheRoot(index));
    }

    /// \brief String representation of the used execution backend.
    [[nodiscard]] static auto ExecutionBackendDescription() noexcept
        -> std::string {
        auto address = RemoteExecutionConfig::RemoteAddress();
        auto properties = RemoteExecutionConfig::PlatformProperties();
        try {
            // json::dump with json::error_handler_t::replace will not throw an
            // exception if invalid UTF-8 sequences are detected in the input.
            // Instead, it will replace them with the UTF-8 replacement
            // character, but still it needs to be inside a try-catch clause to
            // ensure the noexcept modifier of the enclosing function.
            return nlohmann::json{
                {"remote_address",
                 address ? nlohmann::json{fmt::format(
                               "{}:{}", address->host, address->port)}
                         : nlohmann::json{}},
                {"platform_properties", properties}}
                .dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
        } catch (...) {
            return "";
        }
    }

    /// \brief Root directory for local action executions; individual actions
    /// create a working directory below this root.
    [[nodiscard]] static auto ExecutionRoot() noexcept
        -> std::filesystem::path {
        return GenerationCacheRoot(0) / "exec_root";
    }

  private:
    [[nodiscard]] static auto Data() noexcept -> ConfigData& {
        static ConfigData instance{};
        return instance;
    }

    // different folder for different caching protocol
    [[nodiscard]] static auto UpdatePathForCompatibility(
        std::filesystem::path const& dir) -> std::filesystem::path {
        return dir / (Compatibility::IsCompatible() ? "compatible-sha256"
                                                    : "git-sha1");
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_STORAGE_CONFIG_HPP
