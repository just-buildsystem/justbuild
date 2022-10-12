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

#ifndef INCLUDED_SRC_TEST_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_TEST_REPO_HPP
#define INCLUDED_SRC_TEST_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_TEST_REPO_HPP

#include <filesystem>

#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"

static auto const kBasePath =
    std::filesystem::path{"test/buildtool/build_engine/base_maps"};
static auto const kBundlePath = kBasePath / "data/test_repo.bundle";
static auto const kSrcTreeId =
    std::string{"a35c324c6cf79354f6fd8a3c962f9ce7db801915"};
static auto const kRuleTreeId =
    std::string{"c6dd902c9d4e7afa8b20eb04e58503e63ecab84d"};
static auto const kExprTreeId =
    std::string{"4946bd21d0a5b3e0c82d6944f3d47adaf1bb66f7"};
static auto const kJsonTreeId =
    std::string{"6982563dfc4dcdd1362792dbbc9d8243968d1ec9"};

[[nodiscard]] static inline auto GetTestDir() -> std::filesystem::path {
    auto* tmp_dir = std::getenv("TEST_TMPDIR");
    if (tmp_dir != nullptr) {
        return tmp_dir;
    }
    return FileSystemManager::GetCurrentDirectory() / kBasePath;
}

[[nodiscard]] static inline auto CreateTestRepo()
    -> std::optional<std::filesystem::path> {
    static std::atomic<int> counter{};
    auto repo_path =
        GetTestDir() / "test_repo" /
        std::filesystem::path{std::to_string(counter++)}.filename();
    auto cmd = fmt::format(
        "git clone --bare {} {}", kBundlePath.string(), repo_path.string());
    if (std::system(cmd.c_str()) == 0) {
        return repo_path;
    }
    return std::nullopt;
}

#endif  // INCLUDED_SRC_TEST_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_TEST_REPO_HPP
