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

#include <exception>
#include <optional>
#include <string>
#include <utility>  // std::move
#include <vector>

#include "fmt/core.h"
#include "nlohmann/json.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/expected.hpp"

/// \brief Store local execution configuration.
struct LocalExecutionConfig final {
    class Builder;

    // Launcher to be prepended to action's command before executed.
    // Default: ["env", "--"]
    std::vector<std::string> const launcher = {"env", "--"};
};

class LocalExecutionConfig::Builder final {
  public:
    auto SetLauncher(std::vector<std::string> launcher) noexcept -> Builder& {
        launcher_ = std::move(launcher);
        return *this;
    }

    /// \brief Finalize building and create LocalExecutionConfig.
    /// \return LocalExecutionConfig on success, an error string on failure.
    [[nodiscard]] auto Build() const noexcept
        -> expected<LocalExecutionConfig, std::string> {
        // To not duplicate default arguments in builder, create a default
        // config and copy arguments from there.
        LocalExecutionConfig const default_config;
        auto launcher = default_config.launcher;
        if (launcher_.has_value()) {
            try {
                launcher = *launcher_;
            } catch (std::exception const& ex) {
                return unexpected{
                    fmt::format("Failed to set launcher:\n{}", ex.what())};
            }
        }

        return LocalExecutionConfig{.launcher = std::move(launcher)};
    }

  private:
    std::optional<std::vector<std::string>> launcher_;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_CONFIG_HPP
