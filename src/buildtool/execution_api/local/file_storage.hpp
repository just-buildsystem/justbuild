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
    [[nodiscard]] auto AddFromFile(
        std::string const& id,
        std::filesystem::path const& source_path) const noexcept -> bool {
        return AtomicAdd(id, source_path);
    }

    /// \brief Add bytes to storage.
    /// \returns true if file exists afterward.
    [[nodiscard]] auto AddFromBytes(std::string const& id,
                                    std::string const& bytes) const noexcept
        -> bool {
        return AtomicAdd(id, bytes);
    }

    [[nodiscard]] auto GetPath(std::string const& name) const noexcept
        -> std::filesystem::path {
        return storage_root_ / name;
    }

  private:
    std::filesystem::path const storage_root_{};

    /// \brief Add file to storage via copy and atomic rename.
    /// If a race-condition occurs, the winning thread will be the one
    /// performing the rename operation first or last, depending on kMode being
    /// set to FirstWins or LastWins, respectively. All threads will signal
    /// success.
    /// \returns true if file exists afterward.
    template <class T>
    [[nodiscard]] auto AtomicAdd(std::string const& id,
                                 T const& data) const noexcept -> bool {
        auto file_path = storage_root_ / id;
        if (kMode == StoreMode::LastWins or
            not FileSystemManager::Exists(file_path)) {
            auto unique_path = CreateUniquePath(file_path);
            if (unique_path and
                FileSystemManager::CreateDirectory(file_path.parent_path()) and
                CreateFileFromData(*unique_path, data) and
                StageFile(*unique_path, file_path)) {
                Logger::Log(
                    LogLevel::Trace, "created entry {}.", file_path.string());
                return true;
            }
        }
        return FileSystemManager::IsFile(file_path);
    }

    /// \brief Create file from file path.
    [[nodiscard]] static auto CreateFileFromData(
        std::filesystem::path const& file_path,
        std::filesystem::path const& other_path) noexcept -> bool {
        return FileSystemManager::CopyFileAs<kType>(other_path, file_path);
    }

    /// \brief Create file from bytes.
    [[nodiscard]] static auto CreateFileFromData(
        std::filesystem::path const& file_path,
        std::string const& bytes) noexcept -> bool {
        return FileSystemManager::WriteFileAs<kType>(bytes, file_path);
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
