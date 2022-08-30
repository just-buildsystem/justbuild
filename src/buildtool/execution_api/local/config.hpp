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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_CONFIG_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_CONFIG_HPP

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

#include "src/buildtool/compatibility/compatibility.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/logger.hpp"

/// \brief Store global build system configuration.
class LocalExecutionConfig {
    struct ConfigData {

        // Build root directory. All the cache dirs are subdirs of build_root.
        // By default, build_root is set to $HOME/.cache/just.
        // If the user uses --local-build-root PATH,
        // then build_root will be set to PATH.
        std::filesystem::path build_root{};

        // cache_root points to one of the following
        // build_root/protocol-dependent/{git-sha1,compatible-sha256}
        // git-sha1 is the current default. If the user passes the flag
        // --compatible, then the subfolder compatible_sha256 is used
        std::filesystem::path cache_root{};

        // Launcher to be prepended to action's command before executed.
        // Default: ["env", "--"]
        std::vector<std::string> launcher{"env", "--"};
    };

    // different folder for different caching protocol
    [[nodiscard]] static auto UpdatePathForCompatibility(
        std::filesystem::path const& dir) -> std::filesystem::path {
        return dir / (Compatibility::IsCompatible() ? "compatible-sha256"
                                                    : "git-sha1");
    }

  public:
    [[nodiscard]] static auto SetBuildRoot(
        std::filesystem::path const& dir) noexcept -> bool {
        if (FileSystemManager::IsRelativePath(dir)) {
            Logger::Log(LogLevel::Error,
                        "Build root must be absolute path but got '{}'.",
                        dir.string());
            return false;
        }
        Data().build_root = dir;
        Data().cache_root = "";  // in case we re-set build_root, we are sure
                                 // that the cache path is recomputed as well
        return true;
    }

    [[nodiscard]] static auto SetLauncher(
        std::vector<std::string> const& launcher) noexcept -> bool {
        try {
            Data().launcher = launcher;
        } catch (std::exception const& e) {
            Logger::Log(LogLevel::Error,
                        "when setting the local launcher\n{}",
                        e.what());
            return false;
        }
        return true;
    }

    /// \brief User directory.
    [[nodiscard]] static auto GetUserDir() noexcept -> std::filesystem::path {
        return GetUserHome() / ".cache" / "just";
    }

    /// \brief Build directory, defaults to user directory if not set
    [[nodiscard]] static auto BuildRoot() noexcept -> std::filesystem::path {
        auto& build_root = Data().build_root;
        if (build_root.empty()) {
            build_root = GetUserDir();
        }
        return build_root;
    }

    [[nodiscard]] static auto CacheRoot() noexcept -> std::filesystem::path {
        auto& cache_root = Data().cache_root;
        if (cache_root.empty()) {
            cache_root = UpdatePathForCompatibility(
                BuildRoot() / "protocol-dependent" / "generation-0");
        }
        return cache_root;
    }

    // CAS directory based on the type of the file.
    template <ObjectType kType>
    [[nodiscard]] static inline auto CASDir() noexcept
        -> std::filesystem::path {
        char t = ToChar(kType);
        if constexpr (kType == ObjectType::Tree) {
            if (Compatibility::IsCompatible()) {
                t = ToChar(ObjectType::File);
            }
        }
        static const std::string kSuffix = std::string{"cas"} + t;
        return CacheRoot() / kSuffix;
    }

    /// \brief Action cache directory
    [[nodiscard]] static auto ActionCacheDir() noexcept
        -> std::filesystem::path {
        return CacheRoot() / "ac";
    }

    /// \brief Target cache directory
    [[nodiscard]] static auto TargetCacheDir() noexcept
        -> std::filesystem::path {
        return CacheRoot() / "tc";
    }

    [[nodiscard]] static auto GetLauncher() noexcept
        -> std::vector<std::string> {
        return Data().launcher;
    }

    /// \brief Determine user root directory
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

  private:
    [[nodiscard]] static auto Data() noexcept -> ConfigData& {
        static ConfigData instance{};
        return instance;
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_CONFIG_HPP
