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

#include "src/buildtool/main/analyse.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "fmt/core.h"
#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/base_maps/directory_map.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name_data.hpp"
#include "src/buildtool/build_engine/base_maps/expression_map.hpp"
#include "src/buildtool/build_engine/base_maps/rule_map.hpp"
#include "src/buildtool/build_engine/base_maps/source_map.hpp"
#include "src/buildtool/build_engine/base_maps/targets_file_map.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/build_engine/expression/expression_ptr.hpp"
#include "src/buildtool/build_engine/expression/target_result.hpp"
#include "src/buildtool/build_engine/target_map/absent_target_map.hpp"
#include "src/buildtool/build_engine/target_map/target_map.hpp"
#include "src/buildtool/common/action.hpp"
#include "src/buildtool/common/action_description.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/multithreading/async_map_utils.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/buildtool/progress_reporting/base_progress_reporter.hpp"
#include "src/buildtool/progress_reporting/exports_progress_reporter.hpp"
#include "src/buildtool/storage/storage.hpp"

namespace {

namespace Base = BuildMaps::Base;
namespace Target = BuildMaps::Target;

[[nodiscard]] auto GetActionNumber(const AnalysedTarget& target, int number)
    -> std::optional<ActionDescription::Ptr> {
    auto const& actions = target.Actions();
    if (number >= 0) {
        if (actions.size() > static_cast<std::size_t>(number)) {
            return actions[static_cast<std::size_t>(number)];
        }
    }
    else {
        if (((static_cast<std::int64_t>(actions.size())) +
             static_cast<std::int64_t>(number)) >= 0) {
            return actions[static_cast<std::size_t>(
                (static_cast<std::int64_t>(actions.size())) +
                static_cast<std::int64_t>(number))];
        }
    }
    return std::nullopt;
}

[[nodiscard]] auto SwitchToActionInput(const AnalysedTargetPtr& target,
                                       const ActionDescription::Ptr& action)
    -> AnalysedTargetPtr {
    auto inputs = Expression::map_t::underlying_map_t{};
    for (auto const& [k, v] : action->Inputs()) {
        inputs[k] = ExpressionPtr{Expression{v}};
    }
    auto inputs_exp = ExpressionPtr{Expression::map_t{inputs}};
    auto provides = nlohmann::json::object();
    provides["cmd"] = action->GraphAction().Command();
    provides["env"] = action->GraphAction().Env();
    provides["output"] = action->OutputFiles();
    provides["output_dirs"] = action->OutputDirs();
    if (action->GraphAction().MayFail()) {
        provides["may_fail"] = *(action->GraphAction().MayFail());
    }
    if (action->GraphAction().NoCache()) {
        provides["no_cache"] = true;
    }
    if (action->GraphAction().TimeoutScale() != 1.0) {
        provides["timeout scaling"] = action->GraphAction().TimeoutScale();
    }
    if (not action->GraphAction().Cwd().empty()) {
        provides["cwd"] = action->GraphAction().Cwd();
    }
    if (not action->GraphAction().ExecutionProperties().empty()) {
        provides["execution properties"] =
            action->GraphAction().ExecutionProperties();
    }

    auto provides_exp = Expression::FromJson(provides);
    return std::make_shared<AnalysedTarget const>(
        TargetResult{.artifact_stage = inputs_exp,
                     .provides = provides_exp,
                     .runfiles = Expression::kEmptyMap},
        std::vector<ActionDescription::Ptr>{action},
        target->Blobs(),
        target->Trees(),
        target->TreeOverlays(),
        target->Vars(),
        target->Tainted(),
        target->ImpliedExport(),
        target->GraphInformation());
}

}  // namespace

[[nodiscard]] auto AnalyseTarget(
    gsl::not_null<AnalyseContext*> const& context,
    const Target::ConfiguredTarget& id,
    std::size_t jobs,
    std::optional<std::string> const& request_action_input,
    Logger const* logger,
    BuildMaps::Target::ServeFailureLogReporter* serve_log,
    Profile* profile) -> std::optional<AnalysisResult> {
    // create async maps
    auto directory_entries =
        Base::CreateDirectoryEntriesMap(context->repo_config, jobs);
    auto expressions_file_map =
        Base::CreateExpressionFileMap(context->repo_config, jobs);
    auto rule_file_map = Base::CreateRuleFileMap(context->repo_config, jobs);
    auto targets_file_map =
        Base::CreateTargetsFileMap(context->repo_config, jobs);
    auto expr_map = Base::CreateExpressionMap(
        &expressions_file_map, context->repo_config, jobs);
    auto rule_map = Base::CreateRuleMap(
        &rule_file_map, &expr_map, context->repo_config, jobs);
    auto source_targets = Base::CreateSourceTargetMap(
        &directory_entries,
        context->repo_config,
        context->storage->GetHashFunction().GetType(),
        jobs);
    auto absent_target_variables_map =
        Target::CreateAbsentTargetVariablesMap(context, jobs);

    BuildMaps::Target::ResultTargetMap result_map{jobs};
    auto absent_target_map = Target::CreateAbsentTargetMap(
        context, &result_map, &absent_target_variables_map, jobs, serve_log);

    auto target_map = Target::CreateTargetMap(context,
                                              &source_targets,
                                              &targets_file_map,
                                              &rule_map,
                                              &directory_entries,
                                              &absent_target_map,
                                              &result_map,
                                              jobs);
    Logger::Log(
        logger, LogLevel::Info, "Requested target is {}", id.ToString());
    AnalysedTargetPtr target{};

    // we should only report served export targets if a serve endpoint exists
    bool const has_serve = context->serve != nullptr;
    auto reporter = ExportsProgressReporter::Reporter(
        context->statistics, context->progress, has_serve, logger);

    std::atomic<bool> done{false};
    std::condition_variable cv{};
    auto observer =
        std::thread([reporter, &done, &cv]() { reporter(&done, &cv); });

    bool failed{false};
    {
        TaskSystem ts{jobs};
        target_map.ConsumeAfterKeysReady(
            &ts,
            {id},
            [&target](auto values) { target = *values[0]; },
            [&failed, logger, profile](auto const& msg, bool fatal) {
                Logger::Log(logger,
                            fatal ? LogLevel::Error : LogLevel::Warning,
                            "While processing targets:\n{}",
                            msg);
                failed = failed or fatal;
                if (fatal and (profile != nullptr)) {
                    profile->NoteAnalysisError(msg);
                }
            });
    }

    // close analysis progress observer
    done = true;
    cv.notify_all();
    observer.join();

    if (failed) {
        return std::nullopt;
    }

    if (not target) {
        Logger::Log(logger,
                    LogLevel::Error,
                    "Failed to analyse target: {}",
                    id.ToString());
        if (auto error_msg = DetectAndReportCycle(
                "expression imports", expr_map, Base::kEntityNamePrinter)) {
            Logger::Log(logger, LogLevel::Error, *error_msg);
            return std::nullopt;
        }
        if (auto error_msg =
                DetectAndReportCycle("target dependencies",
                                     target_map,
                                     Target::kConfiguredTargetPrinter)) {
            Logger::Log(logger, LogLevel::Error, *error_msg);
            return std::nullopt;
        }
        DetectAndReportPending(
            "expressions", expr_map, Base::kEntityNamePrinter, logger);
        DetectAndReportPending(
            "rules", rule_map, Base::kEntityNamePrinter, logger);
        DetectAndReportPending(
            "targets", target_map, Target::kConfiguredTargetPrinter, logger);
        return std::nullopt;
    }

    // Clean up in parallel what is no longer needed
    {
        TaskSystem ts{jobs};
        target_map.Clear(&ts);
        source_targets.Clear(&ts);
        directory_entries.Clear(&ts);
        expressions_file_map.Clear(&ts);
        rule_file_map.Clear(&ts);
        targets_file_map.Clear(&ts);
        expr_map.Clear(&ts);
        rule_map.Clear(&ts);
    }
    std::optional<std::string> modified{};

    if (request_action_input) {
        if (request_action_input->starts_with("%")) {
            auto action_id = request_action_input->substr(1);
            auto action = result_map.GetAction(action_id);
            if (action) {
                Logger::Log(logger,
                            LogLevel::Info,
                            "Request is input of action %{}",
                            action_id);
                target = SwitchToActionInput(target, *action);
                modified = fmt::format("%{}", action_id);
            }
            else {
                Logger::Log(logger,
                            LogLevel::Error,
                            "Action {} not part of the action graph of the "
                            "requested target",
                            action_id);
                return std::nullopt;
            }
        }
        else if (request_action_input->starts_with("#")) {
            auto number = std::atoi(request_action_input->substr(1).c_str());
            auto action = GetActionNumber(*target, number);
            if (action) {
                Logger::Log(logger,
                            LogLevel::Info,
                            "Request is input of action #{}",
                            number);
                target = SwitchToActionInput(target, *action);
                modified = fmt::format("#{}", number);
            }
            else {
                Logger::Log(logger,
                            LogLevel::Error,
                            "Action #{} out of range for the requested target",
                            number);
                return std::nullopt;
            }
        }
        else {
            auto action = result_map.GetAction(*request_action_input);
            if (action) {
                Logger::Log(logger,
                            LogLevel::Info,
                            "Request is input of action %{}",
                            *request_action_input);
                target = SwitchToActionInput(target, *action);
                modified = fmt::format("%{}", *request_action_input);
            }
            else {
                auto number = std::atoi(request_action_input->c_str());
                auto action = GetActionNumber(*target, number);
                if (action) {
                    Logger::Log(logger,
                                LogLevel::Info,
                                "Request is input of action #{}",
                                number);
                    target = SwitchToActionInput(target, *action);
                    modified = fmt::format("#{}", number);
                }
                else {
                    Logger::Log(
                        logger,
                        LogLevel::Error,
                        "Action #{} out of range for the requested target",
                        number);
                    return std::nullopt;
                }
            }
        }
    }
    return AnalysisResult{.id = id,
                          .target = target,
                          .result_map = std::move(result_map),
                          .modified = modified};
}
