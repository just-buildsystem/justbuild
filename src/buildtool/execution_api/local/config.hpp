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
  public:
    [[nodiscard]] static auto SetBuildRoot(
        std::filesystem::path const& dir) noexcept -> bool {
        if (FileSystemManager::IsRelativePath(dir)) {
            Logger::Log(LogLevel::Error,
                        "Build root must be absolute path but got '{}'.",
                        dir.string());
            return false;
        }
        build_root_ = dir;
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
        disk_cache_ = dir;
        return true;
    }

    [[nodiscard]] static auto SetLauncher(
        std::vector<std::string> const& launcher) noexcept -> bool {
        try {
            launcher_ = launcher;
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
        keep_build_dir_ = is_persistent;
        return true;
    }

    /// \brief User directory.
    [[nodiscard]] static auto GetUserDir() noexcept -> std::filesystem::path {
        if (user_root_.empty()) {
            user_root_ = GetUserRoot() / ".cache" / "just";
        }
        return user_root_;
    }

    /// \brief Build directory, defaults to user directory if not set
    [[nodiscard]] static auto GetBuildDir() noexcept -> std::filesystem::path {
        if (build_root_.empty()) {
            return GetUserDir();
        }
        return build_root_;
    }

    /// \brief Cache directory, defaults to user directory if not set
    [[nodiscard]] static auto GetCacheDir() noexcept -> std::filesystem::path {
        if (disk_cache_.empty()) {
            return GetBuildDir();
        }
        return disk_cache_;
    }

    [[nodiscard]] static auto GetLauncher() noexcept
        -> std::vector<std::string> {
        return launcher_;
    }

    [[nodiscard]] static auto KeepBuildDir() noexcept -> bool {
        return keep_build_dir_;
    }

  private:
    // User root directory (Unix default: /home/${USER})
    static inline std::filesystem::path user_root_{};

    // Build root directory (default: empty)
    static inline std::filesystem::path build_root_{};

    // Disk cache directory (default: empty)
    static inline std::filesystem::path disk_cache_{};

    // Launcher to be prepended to action's command before executed.
    // Default: ["env", "--"]
    static inline std::vector<std::string> launcher_{"env", "--"};

    // Persistent build directory option
    static inline bool keep_build_dir_{false};

    /// \brief Determine user root directory
    [[nodiscard]] static inline auto GetUserRoot() noexcept
        -> std::filesystem::path {
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
