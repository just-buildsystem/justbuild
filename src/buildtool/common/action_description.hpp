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

#ifndef INCLUDED_SRC_BUILDTOOL_COMMON_ACTION_DESCRIPTION_HPP
#define INCLUDED_SRC_BUILDTOOL_COMMON_ACTION_DESCRIPTION_HPP

#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>  // std::move
#include <vector>

#include "nlohmann/json.hpp"
#include "src/buildtool/common/action.hpp"
#include "src/buildtool/common/artifact_description.hpp"
#include "src/buildtool/common/identifier.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/json.hpp"

class ActionDescription {
  public:
    using outputs_t = std::vector<std::string>;
    using inputs_t = std::unordered_map<std::string, ArtifactDescription>;
    using Ptr = std::shared_ptr<ActionDescription>;

    ActionDescription(outputs_t output_files,
                      outputs_t output_dirs,
                      Action action,
                      inputs_t inputs)
        : output_files_{std::move(output_files)},
          output_dirs_{std::move(output_dirs)},
          action_{std::move(action)},
          inputs_{std::move(inputs)} {}

    [[nodiscard]] static auto FromJson(HashFunction::Type hash_type,
                                       std::string const& id,
                                       nlohmann::json const& desc) noexcept
        -> std::optional<ActionDescription::Ptr> {
        try {
            auto outputs =
                ExtractValueAs<std::vector<std::string>>(desc, "output");
            auto output_dirs =
                ExtractValueAs<std::vector<std::string>>(desc, "output_dirs");
            auto command =
                ExtractValueAs<std::vector<std::string>>(desc, "command");

            if ((not outputs.has_value() or outputs->empty()) and
                (not output_dirs.has_value() or output_dirs->empty())) {
                Logger::Log(
                    LogLevel::Error,
                    "Action description for action \"{}\" incomplete: values "
                    "for either \"output\" or \"output_dir\" must be non-empty "
                    "array.",
                    id);
                return std::nullopt;
            }

            if (not command.has_value() or command->empty()) {
                Logger::Log(
                    LogLevel::Error,
                    "Action description for action \"{}\" incomplete: values "
                    "for \"command\" must be non-empty array.",
                    id);
                return std::nullopt;
            }

            if (not outputs) {
                outputs = std::vector<std::string>{};
            }
            if (not output_dirs) {
                output_dirs = std::vector<std::string>{};
            }

            std::string cwd{};
            auto cwd_it = desc.find("cwd");
            if (cwd_it != desc.end()) {
                if (cwd_it->is_string()) {
                    cwd = *cwd_it;
                }
                else {
                    Logger::Log(LogLevel::Error,
                                "cwd, if given, has to be a string");
                    return std::nullopt;
                }
            }

            auto optional_key_value_reader =
                [](nlohmann::json const& action_desc,
                   std::string const& key) -> nlohmann::json {
                auto it = action_desc.find(key);
                if (it == action_desc.end()) {
                    return nlohmann::json::object();
                }
                return *it;
            };
            auto const input = optional_key_value_reader(desc, "input");
            auto const env = optional_key_value_reader(desc, "env");

            if (not(input.is_object() and env.is_object())) {
                Logger::Log(
                    LogLevel::Error,
                    "Action description for action \"{}\" type error: values "
                    "for \"input\" and \"env\" must be objects.",
                    id);
                return std::nullopt;
            }

            inputs_t inputs{};
            for (auto const& [path, input_desc] : input.items()) {
                auto artifact =
                    ArtifactDescription::FromJson(hash_type, input_desc);
                if (not artifact) {
                    return std::nullopt;
                }
                inputs.emplace(path, std::move(*artifact));
            }
            std::optional<std::string> may_fail{};
            bool no_cache{};
            auto may_fail_it = desc.find("may_fail");
            if (may_fail_it != desc.end()) {
                if (may_fail_it->is_null()) {
                    may_fail = std::nullopt;
                }
                else if (may_fail_it->is_string()) {
                    may_fail = *may_fail_it;
                }
                else {
                    Logger::Log(LogLevel::Error,
                                "may_fail has to be a null or a string");
                    return std::nullopt;
                }
            }
            auto no_cache_it = desc.find("no_cache");
            if (no_cache_it != desc.end()) {
                if (not no_cache_it->is_boolean()) {
                    Logger::Log(LogLevel::Error,
                                "no_cache has to be a boolean");
                    return std::nullopt;
                }
                no_cache = *no_cache_it;
            }

            double timeout_scale{1.0};
            auto timeout_scale_it = desc.find("timeout scaling");
            if (timeout_scale_it != desc.end()) {
                if (not timeout_scale_it->is_number()) {
                    Logger::Log(LogLevel::Error,
                                "timeout scaling has to be a number");
                }
                timeout_scale = *timeout_scale_it;
            }
            auto const execution_properties =
                optional_key_value_reader(desc, "execution properties");
            if (not execution_properties.is_object()) {
                Logger::Log(LogLevel::Error,
                            "Execution properties have to a map");
                return std::nullopt;
            }

            return std::make_shared<ActionDescription>(
                std::move(*outputs),
                std::move(*output_dirs),
                Action{id,
                       std::move(*command),
                       cwd,
                       env,
                       may_fail,
                       no_cache,
                       timeout_scale,
                       execution_properties},
                inputs);
        } catch (std::exception const& ex) {
            Logger::Log(
                LogLevel::Error,
                "Failed to parse action description from JSON with error:\n{}",
                ex.what());
        }
        return std::nullopt;
    }

    [[nodiscard]] auto Id() const noexcept -> ActionIdentifier {
        return action_.Id();
    }

    [[nodiscard]] auto ToJson() const -> nlohmann::json {
        auto json = nlohmann::json{{"command", action_.Command()}};
        if (not output_files_.empty()) {
            json["output"] = output_files_;
        }
        if (not output_dirs_.empty()) {
            json["output_dirs"] = output_dirs_;
        }
        if (not inputs_.empty()) {
            auto inputs = nlohmann::json::object();
            for (auto const& [path, artifact] : inputs_) {
                inputs[path] = artifact.ToJson();
            }
            json["input"] = inputs;
        }
        if (not action_.Env().empty()) {
            json["env"] = action_.Env();
        }
        if (action_.MayFail()) {
            json["may_fail"] = *action_.MayFail();
        }
        if (action_.NoCache()) {
            json["no_cache"] = true;
        }
        if (action_.TimeoutScale() != 1.0) {
            json["timeout scaling"] = action_.TimeoutScale();
        }
        if (not action_.Cwd().empty()) {
            json["cwd"] = action_.Cwd();
        }
        if (not action_.ExecutionProperties().empty()) {
            json["execution properties"] = action_.ExecutionProperties();
        }
        return json;
    }

    [[nodiscard]] auto OutputFiles() const& noexcept -> outputs_t const& {
        return output_files_;
    }

    [[nodiscard]] auto OutputFiles() && noexcept -> outputs_t {
        return std::move(output_files_);
    }

    [[nodiscard]] auto OutputDirs() const& noexcept -> outputs_t const& {
        return output_dirs_;
    }

    [[nodiscard]] auto OutputDirs() && noexcept -> outputs_t {
        return std::move(output_dirs_);
    }

    [[nodiscard]] auto GraphAction() const& noexcept -> Action const& {
        return action_;
    }

    [[nodiscard]] auto GraphAction() && noexcept -> Action {
        return std::move(action_);
    }

    [[nodiscard]] auto Inputs() const& noexcept -> inputs_t const& {
        return inputs_;
    }

    [[nodiscard]] auto Inputs() && noexcept -> inputs_t {
        return std::move(inputs_);
    }

  private:
    outputs_t output_files_;
    outputs_t output_dirs_;
    Action action_;
    inputs_t inputs_;
};

#endif  // INCLUDED_SRC_BUILDTOOL_COMMON_ACTION_DESCRIPTION_HPP
