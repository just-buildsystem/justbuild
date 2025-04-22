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

#include "src/utils/cpp/tmp_dir.hpp"

#ifdef __unix__
#include <unistd.h>
#else
#error "Non-unix is not supported yet"
#endif

#include <cstdlib>
#include <string_view>
#include <tuple>

#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

auto TmpDir::Create(std::filesystem::path const& prefix) noexcept -> Ptr {
    return CreateImpl(/*parent=*/nullptr, prefix);
}

auto TmpDir::CreateNestedDirectory(TmpDir::Ptr const& parent) noexcept
    -> TmpDir::Ptr {
    if (parent == nullptr) {
        return nullptr;
    }
    return CreateImpl(parent, parent->GetPath());
}

auto TmpDir::CreateFile(TmpDir::Ptr const& parent,
                        std::string const& file_name) noexcept -> TmpFile::Ptr {
    // Create a new tmp directory to guarantee uniqueness:
    auto temp_dir = CreateNestedDirectory(parent);
    if (temp_dir == nullptr) {
        return nullptr;
    }

    try {
        auto file_path =
            std::filesystem::weakly_canonical(temp_dir->GetPath() / file_name);
        if (not FileSystemManager::CreateFile(file_path)) {
            return nullptr;
        }

        return std::shared_ptr<TmpFile const>{
            new TmpFile(std::move(temp_dir), std::move(file_path))};
    } catch (...) {
        return nullptr;
    }
}

auto TmpDir::CreateImpl(TmpDir::Ptr parent,
                        std::filesystem::path const& path) noexcept -> Ptr {
    static constexpr std::string_view kDirTemplate = "tmp.XXXXXX";
    // make sure prefix folder exists
    if (not FileSystemManager::CreateDirectory(path)) {
        Logger::Log(LogLevel::Error,
                    "TmpDir: could not create prefix directory {}",
                    path.string());
        return nullptr;
    }

    std::string file_path;
    try {
        file_path = std::filesystem::weakly_canonical(path / kDirTemplate);
        // Create a temporary directory:
        if (mkdtemp(file_path.data()) == nullptr) {
            return nullptr;
        }
        return std::shared_ptr<TmpDir const>(
            new TmpDir(std::move(parent), file_path));
    } catch (...) {
        if (not file_path.empty()) {
            rmdir(file_path.c_str());
        }
    }
    return nullptr;
}

TmpDir::~TmpDir() noexcept {
    // try to remove the tmp dir and all its content
    std::ignore = FileSystemManager::RemoveDirectory(tmp_dir_);
}
