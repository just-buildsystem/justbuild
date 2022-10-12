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

#ifndef INCLUDED_SRC_TEST_UTILS_HERMETICITY_LOCAL_HPP
#define INCLUDED_SRC_TEST_UTILS_HERMETICITY_LOCAL_HPP

#include "src/buildtool/common/statistics.hpp"
#include "src/buildtool/execution_api/local/config.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/logger.hpp"

class HermeticLocalTestFixture {
  public:
    HermeticLocalTestFixture() noexcept {
        static int id{};
        Statistics::Instance().Reset();
        CreateAndSetCleanDiskCache(id++);
    }

  private:
    static void CreateAndSetCleanDiskCache(int case_id) {
        auto test_dir = FileSystemManager::GetCurrentDirectory();
        auto case_dir = test_dir / "tmp" / ("case_" + std::to_string(case_id));

        if (FileSystemManager::RemoveDirectory(case_dir, true) and
            FileSystemManager::CreateDirectoryExclusive(case_dir) and
            LocalExecutionConfig::SetBuildRoot(case_dir)) {
            Logger::Log(LogLevel::Debug,
                        "created test-local cache dir {}",
                        case_dir.string());
        }
        else {
            Logger::Log(LogLevel::Error,
                        "failed to create a test-local cache dir {}",
                        case_dir.string());
            std::exit(EXIT_FAILURE);
        }
    }
};

#endif  // INCLUDED_SRC_TEST_UTILS_HERMETICITY_LOCAL_HPP
