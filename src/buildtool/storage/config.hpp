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

#include <cstddef>
#include <exception>
#include <filesystem>
#include <string>

#include "gsl/gsl"
#include "nlohmann/json.hpp"
#include "src/buildtool/compatibility/compatibility.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/gsl.hpp"
#include "src/utils/cpp/tmp_dir.hpp"

/// \brief Global storage configuration.
class StorageConfig {
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

    [[nodiscard]] static auto Instance() noexcept -> StorageConfig& {
        static StorageConfig config;
        return config;
    }

    [[nodiscard]] static auto SetBuildRoot(
        std::filesystem::path const& dir) noexcept -> bool {
        if (FileSystemManager::IsRelativePath(dir)) {
            Logger::Log(LogLevel::Error,
                        "Build root must be absolute path but got '{}'.",
                        dir.string());
            return false;
        }
        Instance().build_root_ = dir;
        return true;
    }

    /// \brief Specifies the number of storage generations.
    static auto SetNumGenerations(std::size_t num_generations) noexcept
        -> void {
        Instance().num_generations_ = num_generations;
    }

    /// \brief Number of storage generations.
    [[nodiscard]] static auto NumGenerations() noexcept -> std::size_t {
        return Instance().num_generations_;
    }

    /// \brief Build directory, defaults to user directory if not set
    [[nodiscard]] static auto BuildRoot() noexcept -> std::filesystem::path {
        return Instance().build_root_;
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
        ExpectsAudit(index < Instance().num_generations_);
        auto generation = std::string{"generation-"} + std::to_string(index);
        return CacheRoot() / generation;
    }

    /// \brief Storage directory of specific generation and protocol type.
    [[nodiscard]] static auto GenerationCacheDir(
        std::size_t index,
        bool is_compatible = Compatibility::IsCompatible()) noexcept
        -> std::filesystem::path {
        return UpdatePathForCompatibility(GenerationCacheRoot(index),
                                          is_compatible);
    }

    /// \brief String representation of the used execution backend.
    [[nodiscard]] static auto ExecutionBackendDescription() noexcept
        -> std::string {
        auto address = RemoteExecutionConfig::RemoteAddress();
        auto properties = RemoteExecutionConfig::PlatformProperties();
        auto dispatch = RemoteExecutionConfig::DispatchList();
        auto description = nlohmann::json{
            {"remote_address", address ? address->ToJson() : nlohmann::json{}},
            {"platform_properties", properties}};
        if (!dispatch.empty()) {
            try {
                // only add the dispatch list, if not empty, so that keys remain
                // not only more readable, but also backwards compatible with
                // earlier versions.
                auto dispatch_list = nlohmann::json::array();
                for (auto const& [props, endpoint] : dispatch) {
                    auto entry = nlohmann::json::array();
                    entry.push_back(nlohmann::json(props));
                    entry.push_back(endpoint.ToJson());
                    dispatch_list.push_back(entry);
                }
                description["endpoint dispatch list"] = dispatch_list;
            } catch (std::exception const& e) {
                Logger::Log(LogLevel::Error,
                            "Failed to serialize endpoint dispatch list: {}",
                            e.what());
            }
        }
        try {
            // json::dump with json::error_handler_t::replace will not throw an
            // exception if invalid UTF-8 sequences are detected in the input.
            // Instead, it will replace them with the UTF-8 replacement
            // character, but still it needs to be inside a try-catch clause to
            // ensure the noexcept modifier of the enclosing function.
            return description.dump(
                2, ' ', false, nlohmann::json::error_handler_t::replace);
        } catch (...) {
            return "";
        }
    }

    /// \brief Root directory for all ephemeral directories, i.e., directories
    /// that can (and should) be removed immediately by garbage collection.
    [[nodiscard]] static auto EphemeralRoot() noexcept
        -> std::filesystem::path {
        return GenerationCacheRoot(0) / "ephemeral";
    }

    /// \brief Root directory for local action executions; individual actions
    /// create a working directory below this root.
    [[nodiscard]] static auto ExecutionRoot() noexcept
        -> std::filesystem::path {
        return EphemeralRoot() / "exec_root";
    }

    /// \brief Create a tmp directory with controlled lifetime for specific
    /// operations (archive, zip, file, distdir checkouts; fetch; update).
    [[nodiscard]] static auto CreateTypedTmpDir(
        std::string const& type) noexcept -> TmpDirPtr {
        // try to create parent dir
        auto parent_path = EphemeralRoot() / "tmp-workspaces" / type;
        return TmpDir::Create(parent_path);
    }

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
