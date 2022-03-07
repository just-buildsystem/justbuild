#ifndef INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_FILE_SYSTEM_MANAGER_HPP
#define INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_FILE_SYSTEM_MANAGER_HPP

#include <cstdio>  // for std::fopen
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>

#ifdef __unix__
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "gsl-lite/gsl-lite.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/system/system.hpp"

/// \brief Implements primitive file system functionality.
/// Catches all exceptions for use with exception-free callers.
class FileSystemManager {
  public:
    using ReadDirEntryFunc =
        std::function<bool(std::filesystem::path const&, ObjectType type)>;

    class DirectoryAnchor {
        friend class FileSystemManager;

      public:
        DirectoryAnchor(DirectoryAnchor const&) = delete;
        auto operator=(DirectoryAnchor const&) -> DirectoryAnchor& = delete;
        auto operator=(DirectoryAnchor &&) -> DirectoryAnchor& = delete;
        ~DirectoryAnchor() noexcept {
            if (!kRestorePath.empty()) {
                try {
                    std::filesystem::current_path(kRestorePath);
                } catch (std::exception const& e) {
                    Logger::Log(LogLevel::Error, e.what());
                }
            }
        }
        [[nodiscard]] auto GetRestorePath() const noexcept
            -> std::filesystem::path const& {
            return kRestorePath;
        }

      private:
        std::filesystem::path const kRestorePath{};

        DirectoryAnchor()
            : kRestorePath{FileSystemManager::GetCurrentDirectory()} {}
        DirectoryAnchor(DirectoryAnchor&&) = default;
    };

    [[nodiscard]] static auto GetCurrentDirectory() noexcept
        -> std::filesystem::path {
        try {
            return std::filesystem::current_path();
        } catch (std::exception const& e) {
            Logger::Log(LogLevel::Error, e.what());
            return std::filesystem::path{};
        }
    }

    [[nodiscard]] static auto ChangeDirectory(
        std::filesystem::path const& dir) noexcept -> DirectoryAnchor {
        DirectoryAnchor anchor{};
        try {
            std::filesystem::current_path(dir);
        } catch (std::exception const& e) {
            Logger::Log(LogLevel::Error,
                        "changing directory to {} from anchor {}:\n{}",
                        dir.string(),
                        anchor.GetRestorePath().string(),
                        e.what());
        }
        return anchor;
    }

    /// \brief Returns true if the directory was created or existed before.
    [[nodiscard]] static auto CreateDirectory(
        std::filesystem::path const& dir) noexcept -> bool {
        return CreateDirectoryImpl(dir) != CreationStatus::Failed;
    }

    /// \brief Returns true if the directory was created by this call.
    [[nodiscard]] static auto CreateDirectoryExclusive(
        std::filesystem::path const& dir) noexcept -> bool {
        return CreateDirectoryImpl(dir) == CreationStatus::Created;
    }

    /// \brief Returns true if the file was created or existed before.
    [[nodiscard]] static auto CreateFile(
        std::filesystem::path const& file) noexcept -> bool {
        return CreateFileImpl(file) != CreationStatus::Failed;
    }

    /// \brief Returns true if the file was created by this call.
    [[nodiscard]] static auto CreateFileExclusive(
        std::filesystem::path const& file) noexcept -> bool {
        return CreateFileImpl(file) == CreationStatus::Created;
    }

    [[nodiscard]] static auto CreateFileHardlink(
        std::filesystem::path const& file_path,
        std::filesystem::path const& link_path) noexcept -> bool {
        try {
            std::filesystem::create_hard_link(file_path, link_path);
            return std::filesystem::is_regular_file(link_path);
        } catch (std::exception const& e) {
            Logger::Log(LogLevel::Error,
                        "hard linking {} to {}\n{}",
                        file_path.string(),
                        link_path.string(),
                        e.what());
            return false;
        }
    }

    template <ObjectType kType>
    requires(IsFileObject(kType))
        [[nodiscard]] static auto CreateFileHardlinkAs(
            std::filesystem::path const& file_path,
            std::filesystem::path const& link_path) noexcept -> bool {
        // Set permissions first (permissions are a property of the file) so
        // that the created link has the correct permissions as soon as the link
        // creation is finished.
        return SetFilePermissions(file_path, IsExecutableObject(kType)) and
               CreateFileHardlink(file_path, link_path);
    }

    [[nodiscard]] static auto CreateFileHardlinkAs(
        std::filesystem::path const& file_path,
        std::filesystem::path const& link_path,
        ObjectType output_type) noexcept -> bool {
        switch (output_type) {
            case ObjectType::File:
                return CreateFileHardlinkAs<ObjectType::File>(file_path,
                                                              link_path);
            case ObjectType::Executable:
                return CreateFileHardlinkAs<ObjectType::Executable>(file_path,
                                                                    link_path);
            case ObjectType::Tree:
                return false;
        }
    }

    [[nodiscard]] static auto Rename(std::filesystem::path const& src,
                                     std::filesystem::path const& dst,
                                     bool no_clobber = false) noexcept -> bool {
        if (no_clobber) {
#ifdef __unix__
            return link(src.c_str(), dst.c_str()) == 0 and
                   unlink(src.c_str()) == 0;
#else
#error "Non-unix is not supported yet"
#endif
        }
        try {
            std::filesystem::rename(src, dst);
            return true;
        } catch (std::exception const& e) {
            Logger::Log(LogLevel::Error, e.what());
            return false;
        }
    }

    [[nodiscard]] static auto CopyFile(
        std::filesystem::path const& src,
        std::filesystem::path const& dst,
        bool fd_less = false,
        std::filesystem::copy_options opt =
            std::filesystem::copy_options::overwrite_existing) noexcept
        -> bool {
        if (fd_less) {
            pid_t pid = ::fork();
            if (pid == -1) {
                Logger::Log(
                    LogLevel::Error,
                    "Failed to copy file: cannot fork a child process.");
                return false;
            }

            if (pid == 0) {
                // Disable logging errors in child to avoid the use of mutexes.
                System::ExitWithoutCleanup(
                    CopyFileImpl</*kLogError=*/false>(src, dst, opt)
                        ? EXIT_SUCCESS
                        : EXIT_FAILURE);
            }

            int status{};
            ::waitpid(pid, &status, 0);

            // NOLINTNEXTLINE(hicpp-signed-bitwise)
            if (WEXITSTATUS(status) != EXIT_SUCCESS) {
                Logger::Log(LogLevel::Error,
                            "Failed copying file {} to {}",
                            src.string(),
                            dst.string());
                return false;
            }
            return true;
        }
        return CopyFileImpl(src, dst, opt);
    }

    template <ObjectType kType>
    requires(IsFileObject(kType)) [[nodiscard]] static auto CopyFileAs(
        std::filesystem::path const& src,
        std::filesystem::path const& dst,
        bool fd_less = false,
        std::filesystem::copy_options opt =
            std::filesystem::copy_options::overwrite_existing) noexcept
        -> bool {
        return CopyFile(src, dst, fd_less, opt) and
               SetFilePermissions(dst, IsExecutableObject(kType));
    }

    [[nodiscard]] static auto CopyFileAs(
        std::filesystem::path const& src,
        std::filesystem::path const& dst,
        ObjectType type,
        bool fd_less = false,
        std::filesystem::copy_options opt =
            std::filesystem::copy_options::overwrite_existing) noexcept
        -> bool {
        switch (type) {
            case ObjectType::File:
                return CopyFileAs<ObjectType::File>(src, dst, fd_less, opt);
            case ObjectType::Executable:
                return CopyFileAs<ObjectType::Executable>(
                    src, dst, fd_less, opt);
            case ObjectType::Tree:
                break;
        }

        return false;
    }

    [[nodiscard]] static auto RemoveFile(
        std::filesystem::path const& file) noexcept -> bool {
        try {
            if (!std::filesystem::exists(file)) {
                return true;
            }
            if (!IsFile(file)) {
                return false;
            }
            return std::filesystem::remove(file);
        } catch (std::exception const& e) {
            Logger::Log(LogLevel::Error,
                        "removing file from {}:\n{}",
                        file.string(),
                        e.what());
            return false;
        }
    }

    [[nodiscard]] static auto RemoveDirectory(std::filesystem::path const& dir,
                                              bool recursively = false) noexcept
        -> bool {
        try {
            if (!std::filesystem::exists(dir)) {
                return true;
            }
            if (recursively) {
                return (std::filesystem::remove_all(dir) !=
                        static_cast<uintmax_t>(-1));
            }
            return std::filesystem::remove(dir);
        } catch (std::exception const& e) {
            Logger::Log(LogLevel::Error,
                        "removing directory {}:\n{}",
                        dir.string(),
                        e.what());
            return false;
        }
    }

    [[nodiscard]] static auto ResolveSymlinks(
        gsl::not_null<std::filesystem::path*> const& path) noexcept -> bool {
        try {
            while (std::filesystem::is_symlink(*path)) {
                *path = std::filesystem::read_symlink(*path);
            }
        } catch (std::exception const& e) {
            Logger::Log(LogLevel::Error, e.what());
            return false;
        }

        return true;
    }

    [[nodiscard]] static auto Exists(std::filesystem::path const& path) noexcept
        -> bool {
        try {
            return std::filesystem::exists(path);
        } catch (std::exception const& e) {
            Logger::Log(LogLevel::Error,
                        "checking for existence of path{}:\n{}",
                        path.string(),
                        e.what());
            return false;
        }

        return true;
    }

    [[nodiscard]] static auto IsFile(std::filesystem::path const& file) noexcept
        -> bool {
        try {
            if (!std::filesystem::is_regular_file(file)) {
                return false;
            }
        } catch (std::exception const& e) {
            Logger::Log(LogLevel::Error,
                        "checking if path {} corresponds to a file:\n{}",
                        file.string(),
                        e.what());
            return false;
        }

        return true;
    }

    [[nodiscard]] static auto IsDirectory(
        std::filesystem::path const& dir) noexcept -> bool {
        try {
            return std::filesystem::is_directory(dir);
        } catch (std::exception const& e) {
            Logger::Log(LogLevel::Error,
                        "checking if path {} corresponds to a directory:\n{}",
                        dir.string(),
                        e.what());
            return false;
        }

        return true;
    }

    /// \brief Checks whether a path corresponds to an executable or not.
    /// \param[in]  path Path to check
    /// \param[in]  is_file_known   (Optional) If true, we assume that the path
    /// corresponds to a file, if false, we check if it's a file or not first.
    /// Default value is false
    /// \returns true if path corresponds to an executable object, false
    /// otherwise
    [[nodiscard]] static auto IsExecutable(std::filesystem::path const& path,
                                           bool is_file_known = false) noexcept
        -> bool {
        if (not is_file_known and not IsFile(path)) {
            return false;
        }

        try {
            namespace fs = std::filesystem;
            auto exec_flags = fs::perms::owner_exec bitor
                              fs::perms::group_exec bitor
                              fs::perms::others_exec;
            auto exec_perms = fs::status(path).permissions() bitand exec_flags;
            if (exec_perms == fs::perms::none) {
                return false;
            }
        } catch (std::exception const& e) {
            Logger::Log(LogLevel::Error,
                        "checking if path {} corresponds to an executable:\n{}",
                        path.string(),
                        e.what());
            return false;
        }

        return true;
    }

    /// \brief Gets type of object in path according to file system
    [[nodiscard]] static auto Type(std::filesystem::path const& path) noexcept
        -> std::optional<ObjectType> {
        if (IsFile(path)) {
            if (IsExecutable(path, true)) {
                return ObjectType::Executable;
            }
            return ObjectType::File;
        }
        if (IsDirectory(path)) {
            return ObjectType::Tree;
        }
        Logger::Log(LogLevel::Debug,
                    "object type for {} not supported yet.",
                    path.string());
        return std::nullopt;
    }

    [[nodiscard]] static auto ReadFile(
        std::filesystem::path const& file) noexcept
        -> std::optional<std::string> {
        auto const type = Type(file);
        if (not type) {
            Logger::Log(LogLevel::Debug,
                        "{} can not be read because it is not a file.",
                        file.string());
            return std::nullopt;
        }
        return ReadFile(file, *type);
    }

    [[nodiscard]] static auto ReadFile(std::filesystem::path const& file,
                                       ObjectType type) noexcept
        -> std::optional<std::string> {
        if (not IsFileObject(type)) {
            Logger::Log(LogLevel::Debug,
                        "{} can not be read because it is not a file.",
                        file.string());
            return std::nullopt;
        }
        try {
            std::string chunk{};
            std::string content{};
            chunk.resize(kChunkSize);
            std::ifstream file_reader(file.string(), std::ios::binary);
            if (file_reader.is_open()) {
                auto ssize = gsl::narrow<std::streamsize>(chunk.size());
                do {
                    file_reader.read(chunk.data(), ssize);
                    auto count = file_reader.gcount();
                    if (count == ssize) {
                        content += chunk;
                    }
                    else {
                        content +=
                            chunk.substr(0, gsl::narrow<std::size_t>(count));
                    }
                } while (file_reader.good());
                file_reader.close();
                return content;
            }
            return std::nullopt;
        } catch (std::exception const& e) {
            Logger::Log(LogLevel::Error,
                        "reading file {}:\n{}",
                        file.string(),
                        e.what());
            return std::nullopt;
        }
    }

    [[nodiscard]] static auto ReadDirectory(
        std::filesystem::path const& dir,
        ReadDirEntryFunc const& read_entry) noexcept -> bool {
        try {
            for (auto const& entry : std::filesystem::directory_iterator{dir}) {
                std::optional<ObjectType> type{};
                if (entry.is_regular_file()) {
                    type = Type(entry.path());
                }
                if (entry.is_directory()) {
                    type = ObjectType::Tree;
                }
                if (not type) {
                    Logger::Log(LogLevel::Error,
                                "unsupported type for dir entry {}",
                                entry.path().string());
                    return false;
                }
                read_entry(entry.path().filename(), *type);
            }
        } catch (std::exception const& ex) {
            Logger::Log(
                LogLevel::Error, "reading directory {} failed", dir.string());
            return false;
        }
        return true;
    }

    [[nodiscard]] static auto WriteFile(std::string const& content,
                                        std::filesystem::path const& file,
                                        bool fd_less = false) noexcept -> bool {
        if (not CreateDirectory(file.parent_path())) {
            Logger::Log(LogLevel::Error,
                        "can not create directory {}",
                        file.parent_path().string());
            return false;
        }
        if (fd_less) {
            pid_t pid = ::fork();
            if (pid == -1) {
                Logger::Log(
                    LogLevel::Error,
                    "Failed to write file: cannot fork a child process.");
                return false;
            }

            if (pid == 0) {
                // Disable logging errors in child to avoid the use of mutexes.
                System::ExitWithoutCleanup(
                    WriteFileImpl</*kLogError=*/false>(content, file)
                        ? EXIT_SUCCESS
                        : EXIT_FAILURE);
            }

            int status{};
            ::waitpid(pid, &status, 0);

            // NOLINTNEXTLINE(hicpp-signed-bitwise)
            if (WEXITSTATUS(status) != EXIT_SUCCESS) {
                Logger::Log(
                    LogLevel::Error, "Failed writing file {}", file.string());
                return false;
            }
            return true;
        }
        return WriteFileImpl(content, file);
    }

    template <ObjectType kType>
    requires(IsFileObject(kType))
        [[nodiscard]] static auto WriteFileAs(std::string const& content,
                                              std::filesystem::path const& file,
                                              bool fd_less = false) noexcept
        -> bool {
        return WriteFile(content, file, fd_less) and
               SetFilePermissions(file, IsExecutableObject(kType));
    }

    [[nodiscard]] static auto WriteFileAs(std::string const& content,
                                          std::filesystem::path const& file,
                                          ObjectType output_type,
                                          bool fd_less = false) noexcept
        -> bool {
        switch (output_type) {
            case ObjectType::File:
                return WriteFileAs<ObjectType::File>(content, file, fd_less);
            case ObjectType::Executable:
                return WriteFileAs<ObjectType::Executable>(
                    content, file, fd_less);
            case ObjectType::Tree:
                return false;
        }
    }

    [[nodiscard]] static auto IsRelativePath(
        std::filesystem::path const& path) noexcept -> bool {
        try {
            return path.is_relative();
        } catch (std::exception const& e) {
            Logger::Log(LogLevel::Error, e.what());
            return false;
        }
    }

    [[nodiscard]] static auto IsAbsolutePath(
        std::filesystem::path const& path) noexcept -> bool {
        try {
            return path.is_absolute();
        } catch (std::exception const& e) {
            Logger::Log(LogLevel::Error, e.what());
            return false;
        }
    }

  private:
    enum class CreationStatus { Created, Exists, Failed };

    static constexpr std::size_t kChunkSize{256};

    /// \brief Race condition free directory creation.
    /// Solves the TOCTOU issue.
    [[nodiscard]] static auto CreateDirectoryImpl(
        std::filesystem::path const& dir) noexcept -> CreationStatus {
        try {
            if (std::filesystem::is_directory(dir)) {
                return CreationStatus::Exists;
            }
            if (std::filesystem::create_directories(dir)) {
                return CreationStatus::Created;
            }
            // It could be that another thread has created the directory right
            // after the current thread checked if it existed. For that reason,
            // we try to create it and check if it exists if create_directories
            // was not successful.
            if (std::filesystem::is_directory(dir)) {
                return CreationStatus::Exists;
            }

            return CreationStatus::Failed;
        } catch (std::exception const& e) {
            Logger::Log(LogLevel::Error, e.what());
            return CreationStatus::Failed;
        }
    }

    /// \brief Race condition free file creation.
    /// Solves the TOCTOU issue via C11's std::fopen.
    [[nodiscard]] static auto CreateFileImpl(
        std::filesystem::path const& file) noexcept -> CreationStatus {
        try {
            if (std::filesystem::is_regular_file(file)) {
                return CreationStatus::Exists;
            }
            if (gsl::owner<FILE*> fp = std::fopen(file.c_str(), "wx")) {
                std::fclose(fp);
                return CreationStatus::Created;
            }
            // It could be that another thread has created the file right after
            // the current thread checked if it existed. For that reason, we try
            // to create it and check if it exists if fopen() with exclusive bit
            // was not successful.
            if (std::filesystem::is_regular_file(file)) {
                return CreationStatus::Exists;
            }
            return CreationStatus::Failed;
        } catch (std::exception const& e) {
            Logger::Log(LogLevel::Error, e.what());
            return CreationStatus::Failed;
        }
    }

    template <bool kLogError = true>
    [[nodiscard]] static auto CopyFileImpl(
        std::filesystem::path const& src,
        std::filesystem::path const& dst,
        std::filesystem::copy_options opt =
            std::filesystem::copy_options::overwrite_existing) noexcept
        -> bool {
        try {
            return std::filesystem::copy_file(src, dst, opt);
        } catch (std::exception const& e) {
            if constexpr (kLogError) {
                Logger::Log(LogLevel::Error,
                            "copying file from {} to {}:\n{}",
                            src.string(),
                            dst.string(),
                            e.what());
            }
            return false;
        }
    }

    template <bool kLogError = true>
    [[nodiscard]] static auto WriteFileImpl(
        std::string const& content,
        std::filesystem::path const& file) noexcept -> bool {
        try {
            std::ofstream writer{file};
            if (!writer.is_open()) {
                if constexpr (kLogError) {
                    Logger::Log(
                        LogLevel::Error, "can not open file {}", file.string());
                }
                return false;
            }
            writer << content;
            writer.close();
            return true;
        } catch (std::exception const& e) {
            if constexpr (kLogError) {
                Logger::Log(LogLevel::Error,
                            "writing to {}:\n{}",
                            file.string(),
                            e.what());
            }
            return false;
        }
    }

    /// \brief Set special permissions for files.
    /// Set to 0444 for non-executables and set to 0555 for executables.
    static auto SetFilePermissions(std::filesystem::path const& path,
                                   bool is_executable) noexcept -> bool {
        try {
            using std::filesystem::perms;
            perms p{perms::owner_read | perms::group_read | perms::others_read};
            if (is_executable) {
                p |= perms::owner_exec | perms::group_exec | perms::others_exec;
            }
            std::filesystem::permissions(path, p);
            return true;
        } catch (std::exception const& e) {
            Logger::Log(LogLevel::Error, e.what());
            return false;
        }
    }
};  // class FileSystemManager

#endif  // INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_FILE_SYSTEM_MANAGER_HPP
