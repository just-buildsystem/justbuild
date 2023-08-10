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

#include <unordered_map>

#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/log_config.hpp"
#include "src/buildtool/logging/log_sink_cmdline.hpp"
#include "src/other_tools/utils/archive_ops.hpp"
#include "src/utils/cpp/tmp_dir.hpp"

namespace {

void SetupDefaultLogging() {
    LogConfig::SetLogLimit(LogLevel::Progress);
    LogConfig::SetSinks({LogSinkCmdLine::CreateFactory()});
}

using file_t = std::pair</*content*/ std::string, ObjectType>;
using filetree_t = std::unordered_map<std::string, file_t>;
/*
Structure of content tree:

+--root
    +--bar
    +--foo
    +--foo_l
    +--baz_l
    +--baz
        +--bar
        +--foo
        +--foo_l

foo_l is symlink "baz/foo_l"        [non-upwards, pointing to file]
baz_l is symlink "baz"              [non-upwards, pointing to tree]
baz/foo_l is symlink "../foo_l"     [upwards & confined, pointing to symlink]
*/
auto const kExpected =
    filetree_t{{"root", {"", ObjectType::Tree}},
               {"root/foo", {"foo", ObjectType::File}},
               {"root/bar", {"bar", ObjectType::File}},
               {"root/baz", {"", ObjectType::Tree}},
               {"root/baz_l", {"baz", ObjectType::Symlink}},
               {"root/foo_l", {"foo", ObjectType::Symlink}},
               {"root/baz/foo", {"foo", ObjectType::File}},
               {"root/baz/bar", {"bar", ObjectType::File}},
               {"root/baz/foo_l", {"../foo_l", ObjectType::Symlink}}};

void create_files(std::filesystem::path const& destDir = ".") {
    for (auto const& [path, file] : kExpected) {
        auto const& [content, type] = file;
        switch (type) {
            case ObjectType::File: {
                if (not FileSystemManager::WriteFile(content, destDir / path)) {
                    Logger::Log(LogLevel::Error,
                                "Could not create test file at path {}",
                                (destDir / path).string());
                    std::exit(1);
                };
            } break;
            case ObjectType::Tree: {
                if (not FileSystemManager::CreateDirectory(destDir / path)) {
                    Logger::Log(LogLevel::Error,
                                "Could not create test dir at path {}",
                                (destDir / path).string());
                    std::exit(1);
                };
            } break;
            case ObjectType::Symlink: {
                if (not FileSystemManager::CreateSymlink(content,
                                                         destDir / path)) {
                    Logger::Log(LogLevel::Error,
                                "Could not create test symlink at path {}",
                                (destDir / path).string());
                    std::exit(1);
                };
            } break;
            default: {
                Logger::Log(LogLevel::Error,
                            "File system failure in creating test archive");
                std::exit(1);
            }
        }
    }
}

}  // namespace

/// \brief This code will create a zip and a tar.gz archives to be used in
/// tests. The archives are created in a tmp directory and then posted in the
/// current directory. Caller must guarantee write rights in current directory.
auto main() -> int {
    // setup logging
    SetupDefaultLogging();
    // make tmp dir
    auto curr_path = FileSystemManager::GetCurrentDirectory();
    auto tmp_dir = TmpDir::Create(curr_path);
    if (not tmp_dir) {
        Logger::Log(LogLevel::Error,
                    "Could not create tmp dir for test archives at path {}",
                    curr_path.string());
        return 1;
    }
    // create the content tree
    create_files(tmp_dir->GetPath());
    // create the archives
    // 1. move to correct directory
    {
        auto anchor = FileSystemManager::ChangeDirectory(tmp_dir->GetPath());
        // 2. create the archives wrt to current directory
        std::optional<std::string> res{std::nullopt};
        res =
            ArchiveOps::CreateArchive(ArchiveType::Zip, "zip_repo.zip", "root");
        if (res) {
            Logger::Log(LogLevel::Error,
                        "Creating test zip archive failed with:\n{}",
                        *res);
            return 1;
        }
        res = ArchiveOps::CreateArchive(
            ArchiveType::TarGz, "tgz_repo.tar.gz", "root");
        if (res) {
            Logger::Log(LogLevel::Error,
                        "Creating test tar.gz archive failed with:\n{}",
                        *res);
            return 1;
        }
    }  // anchor released
    // 3. move archives to their correct location
    if (not FileSystemManager::Rename(tmp_dir->GetPath() / "zip_repo.zip",
                                      "zip_repo.zip")) {
        Logger::Log(LogLevel::Error, "Renaming zip archive failed");
        return 1;
    }
    if (not FileSystemManager::Rename(tmp_dir->GetPath() / "tgz_repo.tar.gz",
                                      "tgz_repo.tar.gz")) {
        Logger::Log(LogLevel::Error, "Renaming tar.gz archive failed");
        return 1;
    }
    // Done! Tmp dir will be cleaned automatically
    return 0;
}
