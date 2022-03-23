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

#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/logger.hpp"

/// \brief Store global build system configuration.
class LocalExecutionConfig {
    struct ConfigData {
        // User root directory (Unix default: /home/${USER})
        std::filesystem::path user_root{};

        // Build root directory (default: empty)
        std::filesystem::path build_root{};

        // Disk cache directory (default: empty)
        std::filesystem::path disk_cache{};

        // Launcher to be prepended to action's command before executed.
        // Default: ["env", "--"]
        std::vector<std::string> launcher{"env", "--"};

        // Persistent build directory option
        bool keep_build_dir{false};
    };

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
        return true;
    }

    [[nodiscard]] static auto SetDiskCache(
        std::filesystem::path const& dir) noexcept -> bool {
        if (FileSystemManager::IsRelativePath(dir)) {
            Logger::Log(LogLevel::Error,
                        "Disk cache must be absolute path but got '{}'.",
                        dir.string());
            return false;
        }
        Data().disk_cache = dir;
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

    [[nodiscard]] static auto SetKeepBuildDir(bool is_persistent) noexcept
        -> bool {
        Data().keep_build_dir = is_persistent;
        return true;
    }

    /// \brief User directory.
    [[nodiscard]] static auto GetUserDir() noexcept -> std::filesystem::path {
        auto& user_root = Data().user_root;
        if (user_root.empty()) {
            user_root = GetUserRoot() / ".cache" / "just";
        }
        return user_root;
    }

    /// \brief Build directory, defaults to user directory if not set
    [[nodiscard]] static auto GetBuildDir() noexcept -> std::filesystem::path {
        auto& build_root = Data().build_root;
        if (build_root.empty()) {
            return GetUserDir();
        }
        return build_root;
    }

    /// \brief Cache directory, defaults to user directory if not set
    [[nodiscard]] static auto GetCacheDir() noexcept -> std::filesystem::path {
        auto& disk_cache = Data().disk_cache;
        if (disk_cache.empty()) {
            return GetBuildDir();
        }
        return disk_cache;
    }

    [[nodiscard]] static auto GetLauncher() noexcept
        -> std::vector<std::string> {
        return Data().launcher;
    }

    [[nodiscard]] static auto KeepBuildDir() noexcept -> bool {
        return Data().keep_build_dir;
    }

  private:
    [[nodiscard]] static auto Data() noexcept -> ConfigData& {
        static ConfigData instance{};
        return instance;
    }

    /// \brief Determine user root directory
    [[nodiscard]] static auto GetUserRoot() noexcept -> std::filesystem::path {
        char const* root{nullptr};

#ifdef __unix__
        root = std::getenv("HOME");
        if (root == nullptr) {
            root = getpwuid(getuid())->pw_dir;
        }
#endif

        if (root == nullptr) {
            Logger::Log(LogLevel::Error, "Cannot determine user directory.");
            std::exit(EXIT_FAILURE);
        }

        return root;
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_CONFIG_HPP
