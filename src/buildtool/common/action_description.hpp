#ifndef INCLUDED_SRC_BUILDTOOL_COMMON_ACTION_DESCRIPTION_HPP
#define INCLUDED_SRC_BUILDTOOL_COMMON_ACTION_DESCRIPTION_HPP

#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "nlohmann/json.hpp"
#include "src/buildtool/common/action.hpp"
#include "src/buildtool/common/artifact_description.hpp"

class ActionDescription {
  public:
    using outputs_t = std::vector<std::string>;
    using inputs_t = std::unordered_map<std::string, ArtifactDescription>;

    ActionDescription(outputs_t output_files,
                      outputs_t output_dirs,
                      Action action,
                      inputs_t inputs)
        : output_files_{std::move(output_files)},
          output_dirs_{std::move(output_dirs)},
          action_{std::move(action)},
          inputs_{std::move(inputs)} {}

    [[nodiscard]] static auto FromJson(std::string const& id,
                                       nlohmann::json const& desc) noexcept
        -> std::optional<ActionDescription> {
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
                auto artifact = ArtifactDescription::FromJson(input_desc);
                if (not artifact) {
                    return std::nullopt;
                }
                inputs.emplace(path, std::move(*artifact));
            }
            std::optional<std::string> may_fail{};
            bool no_cache{};
            auto may_fail_it = desc.find("may_fail");
            if (may_fail_it != desc.end()) {
                if (not may_fail_it->is_string()) {
                    Logger::Log(LogLevel::Error,
                                "may_fail has to be a boolean");
                    return std::nullopt;
                }
                may_fail = *may_fail_it;
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

            return ActionDescription{
                std::move(*outputs),
                std::move(*output_dirs),
                Action{id, std::move(*command), env, may_fail, no_cache},
                inputs};
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

    [[nodiscard]] auto ToJson() const noexcept -> nlohmann::json {
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
