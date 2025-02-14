// Copyright 2025 Huawei Cloud Computing Technology Co., Ltd.
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

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"

TEST_CASE("tmp_dir", "[tmp_dir]") {
    char const* const env_tmpdir = std::getenv("TEST_TMPDIR");
    REQUIRE(env_tmpdir != nullptr);
    auto const test_tempdir =
        std::filesystem::path{std::string{env_tmpdir}} / ".test_build_root";
    REQUIRE(FileSystemManager::CreateDirectory(test_tempdir));

    SECTION("simple") {
        // Create a temp directory
        auto temp_dir = TmpDir::Create(test_tempdir / "test_dir");
        REQUIRE(temp_dir != nullptr);

        // Create one more temp directory at the same location to ensure the
        // template gets populated, and a new directory gets created
        auto temp_dir_2 = TmpDir::Create(test_tempdir / "test_dir");
        REQUIRE(temp_dir_2 != nullptr);

        std::filesystem::path const temp_dir_path = temp_dir->GetPath();
        REQUIRE(FileSystemManager::Exists(temp_dir_path));
        {
            auto temp_dir_clone = temp_dir;
            temp_dir = nullptr;
            REQUIRE(FileSystemManager::Exists(temp_dir_path));
        }
        REQUIRE(not FileSystemManager::Exists(temp_dir_path));

        std::filesystem::path const temp_dir_path_2 = temp_dir_2->GetPath();
        REQUIRE(FileSystemManager::Exists(temp_dir_path_2));
        {
            auto temp_dir_clone = temp_dir_2;
            temp_dir_2 = nullptr;
            REQUIRE(FileSystemManager::Exists(temp_dir_path_2));
        }
        REQUIRE(not FileSystemManager::Exists(temp_dir_path_2));
    }
}
