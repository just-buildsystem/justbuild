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

#ifndef INCLUDED_SRC_BUILDTOOL_COMMON_ACTION_HPP
#define INCLUDED_SRC_BUILDTOOL_COMMON_ACTION_HPP

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "src/buildtool/common/identifier.hpp"

class Action {
  public:
    using LocalPath = std::string;

    Action(std::string action_id,
           std::vector<std::string> command,
           std::map<std::string, std::string> env_vars,
           std::optional<std::string> may_fail,
           bool no_cache,
           double timeout_scale,
           std::map<std::string, std::string> execution_properties)
        : id_{std::move(action_id)},
          command_{std::move(command)},
          env_{std::move(env_vars)},
          may_fail_{std::move(may_fail)},
          no_cache_{no_cache},
          timeout_scale_{timeout_scale},
          execution_properties_{std::move(std::move(execution_properties))} {}

    Action(std::string action_id,
           std::vector<std::string> command,
           std::map<std::string, std::string> env_vars)
        : Action(std::move(action_id),
                 std::move(command),
                 std::move(env_vars),
                 std::nullopt,
                 false,
                 1.0,
                 std::map<std::string, std::string>{}) {}

    [[nodiscard]] auto Id() const noexcept -> ActionIdentifier { return id_; }

    [[nodiscard]] auto Command() && noexcept -> std::vector<std::string> {
        return std::move(command_);
    }

    [[nodiscard]] auto Command() const& noexcept
        -> std::vector<std::string> const& {
        return command_;
    }

    [[nodiscard]] auto Env() const& noexcept
        -> std::map<std::string, std::string> {
        return env_;
    }

    [[nodiscard]] auto Env() && noexcept -> std::map<std::string, std::string> {
        return std::move(env_);
    }

    [[nodiscard]] auto IsTreeAction() const -> bool { return is_tree_; }
    [[nodiscard]] auto MayFail() const -> std::optional<std::string> {
        return may_fail_;
    }
    [[nodiscard]] auto NoCache() const -> bool { return no_cache_; }
    [[nodiscard]] auto TimeoutScale() const -> double { return timeout_scale_; }

    [[nodiscard]] auto ExecutionProperties() const& noexcept
        -> std::map<std::string, std::string> {
        return execution_properties_;
    }

    [[nodiscard]] auto ExecutionProperties() && noexcept
        -> std::map<std::string, std::string> {
        return std::move(execution_properties_);
    }

    [[nodiscard]] static auto CreateTreeAction(ActionIdentifier const& id)
        -> Action {
        return Action{id};
    }

  private:
    ActionIdentifier id_{};
    std::vector<std::string> command_{};
    std::map<std::string, std::string> env_{};
    bool is_tree_{};
    std::optional<std::string> may_fail_{};
    bool no_cache_{};
    double timeout_scale_{};
    std::map<std::string, std::string> execution_properties_{};

    explicit Action(ActionIdentifier id) : id_{std::move(id)}, is_tree_{true} {}
};

#endif  // INCLUDED_SRC_BUILDTOOL_COMMON_ACTION_HPP
