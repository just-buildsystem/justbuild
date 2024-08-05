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

#include "src/utils/cpp/file_locking.hpp"

#include <cerrno>   // for errno
#include <cstring>  // for strerror()

#ifdef __unix__
#include <sys/file.h>
#else
#error "Non-unix is not supported yet"
#endif

#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/path.hpp"

auto LockFile::Acquire(std::filesystem::path const& fspath,
                       bool is_shared) noexcept -> std::optional<LockFile> {
    static std::mutex lock_mutex{};
    try {
        // ensure thread-safety
        std::unique_lock lock{lock_mutex};
        // get path to lock file
        auto lock_file = GetLockFilePath(fspath);
        if (not lock_file) {
            return std::nullopt;
        }
        // touch lock file
        if (not FileSystemManager::CreateFile(*lock_file)) {
            Logger::Log(LogLevel::Error,
                        "LockFile: could not create file {}",
                        lock_file->string());
            return std::nullopt;
        }
        // get open file descriptor
        gsl::owner<FILE*> file_handle = std::fopen(lock_file->c_str(), "r");
        if (file_handle == nullptr) {
            Logger::Log(LogLevel::Error,
                        "LockFile: could not open descriptor for file {}",
                        lock_file->string());
            return std::nullopt;
        }
        // attach flock
        auto err = flock(fileno(file_handle), is_shared ? LOCK_SH : LOCK_EX);
        if (err != 0) {
            Logger::Log(LogLevel::Error,
                        "LockFile: applying lock to file {} failed with:\n{}",
                        lock_file->string(),
                        strerror(errno));
            fclose(file_handle);
            return std::nullopt;
        }
        // lock file has been acquired
        return LockFile(file_handle, *lock_file);
    } catch (std::exception const& ex) {
        Logger::Log(
            LogLevel::Error,
            "LockFile: acquiring file lock for path {} failed with:\n{}",
            fspath.string(),
            ex.what());
        return std::nullopt;
    }
}

LockFile::~LockFile() noexcept {
    if (file_handle_ != nullptr) {
        // close open file descriptor
        fclose(file_handle_);
        file_handle_ = nullptr;
    }
}

auto LockFile::GetLockFilePath(std::filesystem::path const& fspath) noexcept
    -> std::optional<std::filesystem::path> {
    try {
        // bring to normal form
        auto filename = ToNormalPath(fspath);
        if (not filename.is_absolute()) {
            try {
                filename = std::filesystem::absolute(fspath);
            } catch (std::exception const& e) {
                Logger::Log(LogLevel::Error,
                            "Failed to determine absolute path for lock file "
                            "name {}: {}",
                            fspath.string(),
                            e.what());
                return std::nullopt;
            }
        }
        auto parent = filename.parent_path();
        // create parent folder
        if (not FileSystemManager::CreateDirectory(parent)) {
            return std::nullopt;
        }
        // return lock file name
        return filename;
    } catch (std::exception const& ex) {
        Logger::Log(
            LogLevel::Error,
            "LockFile: defining lock file name for path {} failed with:\n{}",
            fspath.string(),
            ex.what());
        return std::nullopt;
    }
}

LockFile::LockFile(LockFile&& other) noexcept
    : file_handle_{other.file_handle_},
      lock_file_{std::move(other.lock_file_)} {
    other.file_handle_ = nullptr;
}

auto LockFile::operator=(LockFile&& other) noexcept -> LockFile& {
    file_handle_ = other.file_handle_;
    other.file_handle_ = nullptr;
    lock_file_ = std::move(other.lock_file_);
    return *this;
}
