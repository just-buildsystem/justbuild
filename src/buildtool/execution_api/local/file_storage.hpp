#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_FILE_STORAGE_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_FILE_STORAGE_HPP

#include <filesystem>
#include <string>

#include "src/buildtool/execution_api/common/execution_common.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"

enum class StoreMode {
    // First thread to write conflicting file wins.
    FirstWins,
    // Last thread to write conflicting file wins, effectively overwriting
    // existing entries. NOTE: This might cause races if hard linking from
    // stored files due to an issue with the interaction of rename(2) and
    // link(2) (see: https://stackoverflow.com/q/69076026/1107763).
    LastWins
};

template <ObjectType kType = ObjectType::File,
          StoreMode kMode = StoreMode::FirstWins>
class FileStorage {
  public:
    explicit FileStorage(std::filesystem::path storage_root) noexcept
        : storage_root_{std::move(storage_root)} {}

    /// \brief Add file to storage.
    /// \returns true if file exists afterward.
    [[nodiscard]] auto AddFromFile(std::string const& id,
                                   std::filesystem::path const& source_path,
                                   bool is_owner = false) const noexcept
        -> bool {
        return AtomicAddFromFile(id, source_path, is_owner);
    }

    /// \brief Add bytes to storage.
    /// \returns true if file exists afterward.
    [[nodiscard]] auto AddFromBytes(std::string const& id,
                                    std::string const& bytes) const noexcept
        -> bool {
        return AtomicAddFromBytes(id, bytes);
    }

    [[nodiscard]] auto GetPath(std::string const& name) const noexcept
        -> std::filesystem::path {
        return storage_root_ / name;
    }

  private:
    std::filesystem::path const storage_root_{};
    static constexpr bool fd_less_{kType == ObjectType::Executable};

    /// \brief Add file to storage from file path via link or copy and rename.
    /// If a race-condition occurs, the winning thread will be the one
    /// performing the link/rename operation first or last, depending on kMode
    /// being set to FirstWins or LastWins, respectively. All threads will
    /// signal success.
    /// \returns true if file exists afterward.
    [[nodiscard]] auto AtomicAddFromFile(std::string const& id,
                                         std::filesystem::path const& path,
                                         bool is_owner) const noexcept -> bool {
        auto file_path = storage_root_ / id;
        if ((kMode == StoreMode::LastWins or
             not FileSystemManager::Exists(file_path)) and
            FileSystemManager::CreateDirectory(file_path.parent_path())) {
            auto direct_create = kMode == StoreMode::FirstWins and is_owner;
            auto create_directly = [&]() {
                // Entry does not exist and we are owner of the file (e.g., file
                // generated in the execution directory). Try to hard link it
                // directly or check its existence if it was created by now.
                return FileSystemManager::CreateFileHardlinkAs<kType>(
                           path, file_path) or
                       FileSystemManager::IsFile(file_path);
            };
            auto create_and_stage = [&]() {
                // Entry exists and we need to overwrite it, or we are not owner
                // of the file. Create the file in a process/thread-local
                // temporary path and stage it.
                auto unique_path = CreateUniquePath(file_path);
                return unique_path and
                       CreateFileFromPath(*unique_path, path, is_owner) and
                       StageFile(*unique_path, file_path);
            };
            if (direct_create ? create_directly() : create_and_stage()) {
                Logger::Log(
                    LogLevel::Trace, "created entry {}.", file_path.string());
                return true;
            }
        }
        return FileSystemManager::IsFile(file_path);
    }

    /// \brief Add file to storage from bytes via write and atomic rename.
    /// If a race-condition occurs, the winning thread will be the one
    /// performing the rename operation first or last, depending on kMode being
    /// set to FirstWins or LastWins, respectively. All threads will signal
    /// success.
    /// \returns true if file exists afterward.
    [[nodiscard]] auto AtomicAddFromBytes(
        std::string const& id,
        std::string const& bytes) const noexcept -> bool {
        auto file_path = storage_root_ / id;
        if (kMode == StoreMode::LastWins or
            not FileSystemManager::Exists(file_path)) {
            auto unique_path = CreateUniquePath(file_path);
            if (unique_path and
                FileSystemManager::CreateDirectory(file_path.parent_path()) and
                CreateFileFromBytes(*unique_path, bytes) and
                StageFile(*unique_path, file_path)) {
                Logger::Log(
                    LogLevel::Trace, "created entry {}.", file_path.string());
                return true;
            }
        }
        return FileSystemManager::IsFile(file_path);
    }

    /// \brief Create file from file path.
    [[nodiscard]] static auto CreateFileFromPath(
        std::filesystem::path const& file_path,
        std::filesystem::path const& other_path,
        bool is_owner) noexcept -> bool {
        // For files owned by us (e.g., generated files from the execution
        // directory), prefer faster creation of hard links instead of a copy.
        // Copy executables without opening any writeable file descriptors in
        // this process to avoid those from being inherited by child processes.
        return (is_owner and FileSystemManager::CreateFileHardlinkAs<kType>(
                                 other_path, file_path)) or
               FileSystemManager::CopyFileAs<kType>(
                   other_path, file_path, fd_less_);
    }

    /// \brief Create file from bytes.
    [[nodiscard]] static auto CreateFileFromBytes(
        std::filesystem::path const& file_path,
        std::string const& bytes) noexcept -> bool {
        // Write executables without opening any writeable file descriptors in
        // this process to avoid those from being inherited by child processes.
        return FileSystemManager::WriteFileAs<kType>(
            bytes, file_path, fd_less_);
    }

    /// \brief Stage file from source path to target path.
    [[nodiscard]] static auto StageFile(
        std::filesystem::path const& src_path,
        std::filesystem::path const& dst_path) noexcept -> bool {
        switch (kMode) {
            case StoreMode::FirstWins:
                // try rename source or delete it if the target already exists
                return FileSystemManager::Rename(
                           src_path, dst_path, /*no_clobber=*/true) or
                       (FileSystemManager::IsFile(dst_path) and
                        FileSystemManager::RemoveFile(src_path));
            case StoreMode::LastWins:
                return FileSystemManager::Rename(src_path, dst_path);
        }
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_FILE_STORAGE_HPP
