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

#ifndef INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_FILE_SYSTEM_MANAGER_HPP
#define INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_FILE_SYSTEM_MANAGER_HPP

#include <array>
#include <chrono>
#include <cstdio>  // for std::fopen
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <unordered_set>

#include <fcntl.h>

#ifdef __unix__
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "gsl/gsl"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/system/system.hpp"

namespace detail {
static inline consteval auto BitWidth(int max_val) -> int {
    constexpr int kBitsPerByte = 8;
    int i = sizeof(max_val) * kBitsPerByte;
    while ((i-- > 0) and (((max_val >> i) & 0x01) == 0x00)) {  // NOLINT
    }
    return i + 1;
}
}  // namespace detail

/// \brief Implements primitive file system functionality.
/// Catches all exceptions for use with exception-free callers.
class FileSystemManager {
  public:
    using ReadDirEntryFunc =
        std::function<bool(std::filesystem::path const&, ObjectType type)>;

    using UseDirEntryFunc =
        std::function<bool(std::filesystem::path const&, bool /*is_tree*/)>;

    class DirectoryAnchor {
        friend class FileSystemManager;

      public:
        DirectoryAnchor(DirectoryAnchor const&) = delete;
        auto operator=(DirectoryAnchor const&) -> DirectoryAnchor& = delete;
        auto operator=(DirectoryAnchor&&) -> DirectoryAnchor& = delete;
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
        std::filesystem::path const& link_path,
        LogLevel log_failure_at = LogLevel::Error) noexcept -> bool {
        try {
            std::filesystem::create_hard_link(file_path, link_path);
            return std::filesystem::is_regular_file(link_path);
        } catch (std::exception const& e) {
            Logger::Log(log_failure_at,
                        "hard linking {} to {}\n{}",
                        file_path.string(),
                        link_path.string(),
                        e.what());
            return false;
        }
    }

    template <ObjectType kType, bool kSetEpochTime = false>
    requires(IsFileObject(kType))
        [[nodiscard]] static auto CreateFileHardlinkAs(
            std::filesystem::path const& file_path,
            std::filesystem::path const& link_path,
            LogLevel log_failure_at = LogLevel::Error) noexcept -> bool {
        // Set permissions first (permissions are a property of the file) so
        // that the created link has the correct permissions as soon as the link
        // creation is finished.
        return SetFilePermissions(file_path, IsExecutableObject(kType)) and
               (not kSetEpochTime or SetEpochTime(file_path)) and
               CreateFileHardlink(file_path, link_path, log_failure_at);
    }

    template <bool kSetEpochTime = false>
    [[nodiscard]] static auto CreateFileHardlinkAs(
        std::filesystem::path const& file_path,
        std::filesystem::path const& link_path,
        ObjectType output_type) noexcept -> bool {
        switch (output_type) {
            case ObjectType::File:
                return CreateFileHardlinkAs<ObjectType::File, kSetEpochTime>(
                    file_path, link_path);
            case ObjectType::Executable:
                return CreateFileHardlinkAs<ObjectType::Executable,
                                            kSetEpochTime>(file_path,
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

    /// \brief Copy file
    /// If argument fd_less is given, the copy will be performed in a child
    /// process to prevent polluting the parent with open writable file
    /// descriptors (which might be inherited by other children that keep them
    /// open and can cause EBUSY errors).
    [[nodiscard]] static auto CopyFile(
        std::filesystem::path const& src,
        std::filesystem::path const& dst,
        bool fd_less = false,
        std::filesystem::copy_options opt =
            std::filesystem::copy_options::overwrite_existing) noexcept
        -> bool {
        if (fd_less) {
            auto const* src_cstr = src.c_str();
            auto const* dst_cstr = dst.c_str();

            pid_t pid = ::fork();
            if (pid == -1) {
                Logger::Log(
                    LogLevel::Error,
                    "Failed to copy file: cannot fork a child process.");
                return false;
            }

            if (pid == 0) {
                // In the child process, use low-level copies to avoid mallocs,
                // which removes the risk of deadlocks on certain combinations
                // of C++ standard library and libc.
                System::ExitWithoutCleanup(LowLevel::CopyFile(
                    src_cstr,
                    dst_cstr,
                    opt == std::filesystem::copy_options::skip_existing));
            }

            int status{};
            ::waitpid(pid, &status, 0);

            // NOLINTNEXTLINE(hicpp-signed-bitwise)
            int retval = WEXITSTATUS(status);

            if (retval != 0) {
                Logger::Log(LogLevel::Error,
                            "Failed copying file {} to {} with: {}",
                            src.string(),
                            dst.string(),
                            LowLevel::ErrorToString(retval));
                return false;
            }
            return true;
        }
        return CopyFileImpl(src, dst, opt);
    }

    template <ObjectType kType,
              bool kSetEpochTime = false,
              bool kSetWritable = false>
    requires(IsFileObject(kType)) [[nodiscard]] static auto CopyFileAs(
        std::filesystem::path const& src,
        std::filesystem::path const& dst,
        bool fd_less = false,
        std::filesystem::copy_options opt =
            std::filesystem::copy_options::overwrite_existing) noexcept
        -> bool {
        return CopyFile(src, dst, fd_less, opt) and
               SetFilePermissions<kSetWritable>(dst,
                                                IsExecutableObject(kType)) and
               (not kSetEpochTime or SetEpochTime(dst));
    }

    template <bool kSetEpochTime = false, bool kSetWritable = false>
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
                return CopyFileAs<ObjectType::File,
                                  kSetEpochTime,
                                  kSetWritable>(src, dst, fd_less, opt);
            case ObjectType::Executable:
                return CopyFileAs<ObjectType::Executable,
                                  kSetEpochTime,
                                  kSetWritable>(src, dst, fd_less, opt);
            case ObjectType::Tree:
                break;
        }

        return false;
    }

    [[nodiscard]] static auto CopyDirectoryImpl(
        std::filesystem::path const& src,
        std::filesystem::path const& dst,
        bool recursively = false) noexcept -> bool {
        try {
            // also checks existence
            if (not IsDirectory(src)) {
                Logger::Log(LogLevel::Error,
                            "source {} does not exist or is not a directory",
                            src.string());
                return false;
            }
            // if dst does not exist, it is created, so only check if path
            // exists but is something else
            if (Exists(dst) and not IsDirectory(dst)) {
                Logger::Log(LogLevel::Error,
                            "destination {} exists but it is not a directory",
                            dst.string());
                return false;
            }
            std::filesystem::copy(src,
                                  dst,
                                  recursively
                                      ? std::filesystem::copy_options::recursive
                                      : std::filesystem::copy_options::none);
            return true;
        } catch (std::exception const& e) {
            Logger::Log(LogLevel::Error,
                        "copying directory from {} to {}:\n{}",
                        src.string(),
                        dst.string(),
                        e.what());
            return false;
        }
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
            auto const status = std::filesystem::symlink_status(path);
            return std::filesystem::exists(status);
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
            auto const status = std::filesystem::symlink_status(file);
            if (!std::filesystem::is_regular_file(status)) {
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
            auto const status = std::filesystem::symlink_status(dir);
            return std::filesystem::is_directory(status);
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
    /// \returns true if path corresponds to an executable object, false
    /// otherwise
    [[nodiscard]] static auto IsExecutable(
        std::filesystem::path const& path) noexcept -> bool {
        try {
            auto const status = std::filesystem::symlink_status(path);
            return std::filesystem::is_regular_file(status) and
                   HasExecPermissions(status);
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
        try {
            auto const status = std::filesystem::symlink_status(path);
            if (std::filesystem::is_regular_file(status)) {
                if (HasExecPermissions(status)) {
                    return ObjectType::Executable;
                }
                return ObjectType::File;
            }
            if (std::filesystem::is_directory(status)) {
                return ObjectType::Tree;
            }
            if (std::filesystem::exists(status)) {
                Logger::Log(LogLevel::Debug,
                            "object type for {} is not supported yet.",
                            path.string());
            }
            else {
                Logger::Log(LogLevel::Trace,
                            "non-existing object path {}.",
                            path.string());
            }
        } catch (std::exception const& e) {
            Logger::Log(LogLevel::Error,
                        "checking type of path {} failed with:\n{}",
                        path.string(),
                        e.what());
        }
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
                ObjectType type{};
                auto const status = entry.symlink_status();
                if (std::filesystem::is_regular_file(status)) {
                    if (HasExecPermissions(status)) {
                        type = ObjectType::Executable;
                    }
                    else {
                        type = ObjectType::File;
                    }
                }
                else if (std::filesystem::is_directory(status)) {
                    type = ObjectType::Tree;
                }
                else {
                    Logger::Log(LogLevel::Error,
                                "unsupported type for dir entry {}",
                                entry.path().string());
                    return false;
                }
                if (not read_entry(entry.path().filename(), type)) {
                    return false;
                }
            }
        } catch (std::exception const& ex) {
            Logger::Log(
                LogLevel::Error, "reading directory {} failed", dir.string());
            return false;
        }
        return true;
    }

    /// \brief Read all entries recursively in a filesystem directory tree.
    /// \param dir root directory to traverse
    /// \param use_entry callback to call with found valid entries
    /// \param ignored_subdirs directory names to be ignored wherever found in
    ///                        the directory tree of dir.
    [[nodiscard]] static auto ReadDirectoryEntriesRecursive(
        std::filesystem::path const& dir,
        UseDirEntryFunc const& use_entry,
        std::unordered_set<std::string> const& ignored_subdirs = {}) noexcept
        -> bool {
        try {
            // constructor of this iterator points to end by default;
            for (auto it = std::filesystem::recursive_directory_iterator(dir);
                 it != std::filesystem::recursive_directory_iterator();
                 ++it) {
                // check for ignored subdirs
                if (std::filesystem::is_directory(it->symlink_status()) and
                    ignored_subdirs.contains(*--it->path().end())) {
                    it.disable_recursion_pending();
                    continue;
                }
                // use the entry
                if (not use_entry(
                        it->path().lexically_relative(dir),
                        std::filesystem::is_directory(it->symlink_status()))) {
                    return false;
                }
            }
        } catch (std::exception const& ex) {
            Logger::Log(LogLevel::Error,
                        "reading directory {} recursively failed",
                        dir.string());
            return false;
        }
        return true;
    }

    /// \brief Write file
    /// If argument fd_less is given, the write will be performed in a child
    /// process to prevent polluting the parent with open writable file
    /// descriptors (which might be inherited by other children that keep them
    /// open and can cause EBUSY errors).
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
            auto const* file_cstr = file.c_str();
            auto const* content_cstr = content.c_str();
            auto content_size = content.size();

            pid_t pid = ::fork();
            if (pid == -1) {
                Logger::Log(
                    LogLevel::Error,
                    "Failed to write file: cannot fork a child process.");
                return false;
            }

            if (pid == 0) {
                // In the child process, use low-level writes to avoid mallocs,
                // which removes the risk of deadlocks on certain combinations
                // of C++ standard library and libc.
                System::ExitWithoutCleanup(
                    LowLevel::WriteFile(content_cstr, content_size, file_cstr));
            }

            int status{};
            ::waitpid(pid, &status, 0);

            // NOLINTNEXTLINE(hicpp-signed-bitwise)
            int retval = WEXITSTATUS(status);

            if (retval != 0) {
                Logger::Log(LogLevel::Error,
                            "Failed writing file {} with: {}",
                            file.string(),
                            LowLevel::ErrorToString(retval));
                return false;
            }
            return true;
        }
        return WriteFileImpl(content, file);
    }

    template <ObjectType kType,
              bool kSetEpochTime = false,
              bool kSetWritable = false>
    requires(IsFileObject(kType))
        [[nodiscard]] static auto WriteFileAs(std::string const& content,
                                              std::filesystem::path const& file,
                                              bool fd_less = false) noexcept
        -> bool {
        return WriteFile(content, file, fd_less) and
               SetFilePermissions<kSetWritable>(file,
                                                IsExecutableObject(kType)) and
               (not kSetEpochTime or SetEpochTime(file));
    }

    template <bool kSetEpochTime = false, bool kSetWritable = false>
    [[nodiscard]] static auto WriteFileAs(std::string const& content,
                                          std::filesystem::path const& file,
                                          ObjectType output_type,
                                          bool fd_less = false) noexcept
        -> bool {
        switch (output_type) {
            case ObjectType::File:
                return WriteFileAs<ObjectType::File,
                                   kSetEpochTime,
                                   kSetWritable>(content, file, fd_less);
            case ObjectType::Executable:
                return WriteFileAs<ObjectType::Executable,
                                   kSetEpochTime,
                                   kSetWritable>(content, file, fd_less);
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
    /// By default, we set to 0444 for non-executables and set to 0555 for
    /// executables. When we install or install-cas, we add the owner write
    /// permission to allow for, e.g., overwriting if we re-install the same
    /// target after a recompilation
    template <bool kSetWritable = false>
    static auto SetFilePermissions(std::filesystem::path const& path,
                                   bool is_executable) noexcept -> bool {
        try {
            using std::filesystem::perms;
            perms p{(kSetWritable ? perms::owner_write : perms::none) |
                    perms::owner_read | perms::group_read | perms::others_read};
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

    static auto SetEpochTime(std::filesystem::path const& file_path) noexcept
        -> bool {
        static auto const kPosixEpochTime =
            System::GetPosixEpoch<std::chrono::file_clock>();
        try {
            std::filesystem::last_write_time(file_path, kPosixEpochTime);
            return true;
        } catch (std::exception const& e) {
            Logger::Log(LogLevel::Error, e.what());
            return false;
        }
    }

    static auto HasExecPermissions(
        std::filesystem::file_status const& status) noexcept -> bool {
        try {
            namespace fs = std::filesystem;
            static constexpr auto exec_flags = fs::perms::owner_exec bitor
                                               fs::perms::group_exec bitor
                                               fs::perms::others_exec;
            auto exec_perms = status.permissions() bitand exec_flags;
            return exec_perms != fs::perms::none;
        } catch (std::exception const& e) {
            Logger::Log(LogLevel::Error,
                        "checking for executable permissions failed with:\n{}",
                        e.what());
        }
        return false;
    }

    /// \brief Low-level copy and write operations.
    /// Those do not perform malloc operations, which removes the risk of
    /// deadlocks on certain combinations of C++ standard library and libc.
    /// Non-zero return values indicate errors, which can be decoded using
    /// \ref ErrorToString.
    class LowLevel {
        static constexpr ssize_t kDefaultChunkSize = 1024 * 32;
        static constexpr int kWriteFlags =
            O_WRONLY | O_CREAT | O_TRUNC | O_SYNC;  // NOLINT
        static constexpr int kWritePerms =          // 644
            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;  // NOLINT

      public:
        template <ssize_t kChunkSize = kDefaultChunkSize>
        [[nodiscard]] static auto CopyFile(char const* src,
                                           char const* dst,
                                           bool skip_existing) noexcept -> int {
            // NOLINTNEXTLINE(hicpp-signed-bitwise)
            auto write_flags = kWriteFlags | (skip_existing ? O_EXCL : 0);
            auto out = FdOpener{dst, write_flags, kWritePerms};
            if (out.fd == -1) {
                if (skip_existing and errno == EEXIST) {
                    return 0;
                }
                return PackError(ERROR_OPEN_OUTPUT, errno);
            }

            auto in = FdOpener{src, O_RDONLY};
            if (in.fd == -1) {
                return PackError(ERROR_OPEN_INPUT, errno);
            }

            ssize_t len{};
            std::array<std::uint8_t, kChunkSize> buf{};
            while ((len = read(in.fd, buf.data(), buf.size())) > 0) {
                ssize_t wlen{};
                ssize_t written_len{};
                while (written_len < len &&
                       (wlen = write(out.fd,
                                     buf.data() + written_len,  // NOLINT
                                     len - written_len)) > 0) {
                    written_len += wlen;
                }
                if (wlen < 0) {
                    return PackError(ERROR_WRITE_OUTPUT, errno);
                }
            }
            if (len < 0) {
                return PackError(ERROR_READ_INPUT, errno);
            }
            return 0;
        }

        template <ssize_t kChunkSize = kDefaultChunkSize>
        [[nodiscard]] static auto WriteFile(char const* content,
                                            ssize_t size,
                                            char const* file) noexcept -> int {
            auto out = FdOpener{file, kWriteFlags, kWritePerms};
            if (out.fd == -1) {
                return PackError(ERROR_OPEN_OUTPUT, errno);
            }
            ssize_t pos{};
            while (pos < size) {
                auto const write_len = std::min(kChunkSize, size - pos);
                auto len = write(out.fd, content + pos, write_len);  // NOLINT
                if (len < 0) {
                    return PackError(ERROR_WRITE_OUTPUT, errno);
                }
                pos += len;
            }
            return 0;
        }

        static auto ErrorToString(int retval) -> std::string {
            if (retval == 0) {
                return "no error";
            }
            if ((retval & kSignalBit) == kSignalBit) {  // NOLINT
                return fmt::format(
                    "exceptional termination with return code {}", retval);
            }
            static auto strcode = [](int code) -> std::string {
                switch (code) {
                    case ERROR_OPEN_INPUT:
                        return "open() input file";
                    case ERROR_OPEN_OUTPUT:
                        return "open() output file";
                    case ERROR_READ_INPUT:
                        return "read() input file";
                    case ERROR_WRITE_OUTPUT:
                        return "write() output file";
                    default:
                        return "unknown operation";
                }
            };
            auto const [code, err] = UnpackError(retval);
            return fmt::format("{} failed with:\n{}: {} (probably)",
                               strcode(code),
                               err,
                               strerror(err));
        }

      private:
        enum ErrorCodes {
            ERROR_READ_INPUT,    // read() input file failed
            ERROR_OPEN_INPUT,    // open() input file failed
            ERROR_OPEN_OUTPUT,   // open() output file failed
            ERROR_WRITE_OUTPUT,  // write() output file failed
            LAST_ERROR_CODE      // marker for first unused error code
        };

        static constexpr int kSignalBit = 0x80;
        static constexpr int kAvailableBits = 7;  // 8 bits - 1 signal bit
        static constexpr int kCodeWidth = detail::BitWidth(LAST_ERROR_CODE - 1);
        static constexpr int kCodeMask = (1 << kCodeWidth) - 1;  // NOLINT
        static constexpr int kErrnoWidth = kAvailableBits - kCodeWidth;
        static constexpr int kErrnoMask = (1 << kErrnoWidth) - 1;  // NOLINT

        // Open file descriptor and close on destruction.
        struct FdOpener {
            int fd;
            FdOpener(char const* path, int flags, int perms = 0)
                : fd{open(path, flags, perms)} {}  // NOLINT
            FdOpener(FdOpener const&) = delete;
            FdOpener(FdOpener&&) = delete;
            auto operator=(FdOpener const&) = delete;
            auto operator=(FdOpener&&) = delete;
            ~FdOpener() {
                if (fd != -1) {
                    close(fd);
                }
            }
        };

        // encode to 8 bits with format <signal-bit><errcode><errno>
        static auto PackError(int code, int err) -> int {
            err &= kErrnoMask;  // NOLINT
            if (code == 0 and err == 0) {
                err = kErrnoMask;
            }
            return (code << kErrnoWidth) | err;  // NOLINT
        }

        static auto UnpackError(int retval) -> std::pair<int, int> {
            int code = (retval >> kErrnoWidth) & kCodeMask;  // NOLINT
            int err = retval & kErrnoMask;                   // NOLINT
            if (err == kErrnoMask) {
                err = 0;
            }
            return {code, err};
        }

    };  // class LowLevel
};      // class FileSystemManager

#endif  // INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_FILE_SYSTEM_MANAGER_HPP
