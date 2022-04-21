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
        // If the user uses --local_build_root PATH,
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

        // Persistent build directory option
        bool keep_build_dir{false};
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

    [[nodiscard]] static auto SetKeepBuildDir(bool is_persistent) noexcept
        -> bool {
        Data().keep_build_dir = is_persistent;
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
            cache_root =
                UpdatePathForCompatibility(BuildRoot() / "protocol-dependent");
        }
        return cache_root;
    }

    // CAS directory based on the type of the file.
    // Specialized just for regular File or Exectuable
    template <ObjectType kType,
              typename = std::enable_if<kType == ObjectType::File or
                                        kType == ObjectType::Executable>>
    [[nodiscard]] static inline auto CASDir() noexcept
        -> std::filesystem::path {
        static const std::string kSuffix = std::string{"cas"} + ToChar(kType);
        return CacheRoot() / kSuffix;
    }

    /// \brief Action cache directory
    [[nodiscard]] static auto ActionCacheDir() noexcept
        -> std::filesystem::path {
        return CacheRoot() / "ac";
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
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_CONFIG_HPP
