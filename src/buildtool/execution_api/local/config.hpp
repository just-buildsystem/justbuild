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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_CONFIG_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_CONFIG_HPP

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "gsl/gsl"
#include "nlohmann/json.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

/// \brief Store global build system configuration.
class LocalExecutionConfig {
  public:
    [[nodiscard]] static auto SetLauncher(
        std::vector<std::string> const& launcher) noexcept -> bool {
        try {
            Instance().launcher_ = launcher;
        } catch (std::exception const& e) {
            Logger::Log(LogLevel::Error,
                        "when setting the local launcher\n{}",
                        e.what());
            return false;
        }
        return true;
    }

    [[nodiscard]] static auto GetLauncher() noexcept
        -> std::vector<std::string> {
        return Instance().launcher_;
    }

    [[nodiscard]] static auto Instance() noexcept -> LocalExecutionConfig& {
        static LocalExecutionConfig config;
        return config;
    }

  private:
    // Launcher to be prepended to action's command before executed.
    // Default: ["env", "--"]
    std::vector<std::string> launcher_ = {"env", "--"};
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_CONFIG_HPP
