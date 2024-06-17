// Copyright 2023 Huawei Cloud Computing Technology Co., Ltd.
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

#include "src/buildtool/execution_api/remote/config.hpp"

#include <fstream>
#include <utility>

#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

[[nodiscard]] auto RemoteExecutionConfig::SetRemoteExecutionDispatch(
    const std::filesystem::path& filename) noexcept -> bool {
    try {
        if (auto dispatch_info = FileSystemManager::ReadFile(filename)) {
            auto parsed = ParseDispatch(*dispatch_info);
            if (not parsed) {
                Logger::Log(LogLevel::Warning, std::move(parsed).error());
                return false;
            }
            Instance().dispatch_ = *std::move(parsed);
            return true;
        }
        Logger::Log(LogLevel::Warning,
                    "Failed to read json file {}",
                    filename.string());
    } catch (std::exception const& e) {
        Logger::Log(LogLevel::Warning,
                    "Failure assigning the endpoint configuration: {}",
                    e.what());
    }
    return false;
}
