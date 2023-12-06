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

#ifndef BOOTSTRAP_BUILD_TOOL
#include <atomic>
#include <condition_variable>
#include <thread>
#endif  // BOOTSTRAP_BUILD_TOOL

#include "src/buildtool/build_engine/base_maps/directory_map.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name.hpp"
#include "src/buildtool/build_engine/base_maps/expression_map.hpp"
#include "src/buildtool/build_engine/base_maps/rule_map.hpp"
#include "src/buildtool/build_engine/base_maps/source_map.hpp"
#include "src/buildtool/build_engine/base_maps/targets_file_map.hpp"
#include "src/buildtool/build_engine/target_map/absent_target_map.hpp"
#include "src/buildtool/build_engine/target_map/target_map.hpp"
#include "src/buildtool/multithreading/async_map_consumer.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#ifndef BOOTSTRAP_BUILD_TOOL
#include "src/buildtool/serve_api/progress_reporting/progress_reporter.hpp"
#endif  // BOOTSTRAP_BUILD_TOOL

namespace {

namespace Base = BuildMaps::Base;
namespace Target = BuildMaps::Target;

template <HasToString K, typename V>
[[nodiscard]] auto DetectAndReportCycle(std::string const& name,
                                        AsyncMapConsumer<K, V> const& map)
    -> bool {
    using namespace std::string_literals;
    auto cycle = map.DetectCycle();
    if (cycle) {
        bool found{false};
        std::ostringstream oss{};
        oss << fmt::format("Cycle detected in {}:", name) << std::endl;
        for (auto const& k : *cycle) {
            auto match = (k == cycle->back());
            auto prefix{match   ? found ? "`-- "s : ".-> "s
                        : found ? "|   "s
                                : "    "s};
            oss << prefix << k.ToString() << std::endl;
            found = found or match;
        }
        Logger::Log(LogLevel::Error, "{}", oss.str());
        return true;
    }
    return false;
}

template <HasToString K, typename V>
void DetectAndReportPending(std::string const& name,
                            AsyncMapConsumer<K, V> const& map) {
    using namespace std::string_literals;
    auto keys = map.GetPendingKeys();
    if (not keys.empty()) {
        std::ostringstream oss{};
        oss << fmt::format("Internal error, failed to evaluate pending {}:",
                           name)
            << std::endl;
        for (auto const& k : keys) {
            oss << "  " << k.ToString() << std::endl;
        }
        Logger::Log(LogLevel::Error, "{}", oss.str());
    }
}

[[nodiscard]] auto GetActionNumber(const AnalysedTarget& target, int number)
    -> std::optional<ActionDescription::Ptr> {
    auto const& actions = target.Actions();
    if (number >= 0) {
        if (actions.size() > static_cast<std::size_t>(number)) {
            return actions[static_cast<std::size_t>(number)];
        }
    }
    else {
        if (((static_cast<int64_t>(actions.size())) +
             static_cast<int64_t>(number)) >= 0) {
            return actions[static_cast<std::size_t>(
                (static_cast<int64_t>(actions.size())) +
                static_cast<int64_t>(number))];
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

    auto provides_exp = Expression::FromJson(provides);
    return std::make_shared<AnalysedTarget const>(
        TargetResult{.artifact_stage = inputs_exp,
                     .provides = provides_exp,
                     .runfiles = Expression::kEmptyMap},
        std::vector<ActionDescription::Ptr>{action},
        target->Blobs(),
        target->Trees(),
        target->Vars(),
        target->Tainted(),
        target->GraphInformation());
}

}  // namespace

[[nodiscard]] auto AnalyseTarget(
    const Target::ConfiguredTarget& id,
    gsl::not_null<Target::ResultTargetMap*> const& result_map,
    gsl::not_null<RepositoryConfig*> const& repo_config,
    std::size_t jobs,
    std::optional<std::string> const& request_action_input)
    -> std::optional<AnalysisResult> {
    auto directory_entries = Base::CreateDirectoryEntriesMap(repo_config, jobs);
    auto expressions_file_map =
        Base::CreateExpressionFileMap(repo_config, jobs);
    auto rule_file_map = Base::CreateRuleFileMap(repo_config, jobs);
    auto targets_file_map = Base::CreateTargetsFileMap(repo_config, jobs);
    auto expr_map =
        Base::CreateExpressionMap(&expressions_file_map, repo_config, jobs);
    auto rule_map =
        Base::CreateRuleMap(&rule_file_map, &expr_map, repo_config, jobs);
    auto source_targets =
        Base::CreateSourceTargetMap(&directory_entries, repo_config, jobs);
    auto absent_target_map =
        Target::CreateAbsentTargetMap(result_map, repo_config, jobs);
    auto target_map = Target::CreateTargetMap(&source_targets,
                                              &targets_file_map,
                                              &rule_map,
                                              &directory_entries,
                                              &absent_target_map,
                                              result_map,
                                              repo_config,
                                              jobs);
    Logger::Log(LogLevel::Info, "Requested target is {}", id.ToString());
    AnalysedTargetPtr target{};

#ifndef BOOTSTRAP_BUILD_TOOL
    std::atomic<bool> done{false};
    std::condition_variable cv{};
    auto reporter = ServeServiceProgressReporter::Reporter();
    auto observer =
        std::thread([reporter, &done, &cv]() { reporter(&done, &cv); });
#endif  // BOOTSTRAP_BUILD_TOOL

    bool failed{false};
    {
        TaskSystem ts{jobs};
        target_map.ConsumeAfterKeysReady(
            &ts,
            {id},
            [&target](auto values) { target = *values[0]; },
            [&failed](auto const& msg, bool fatal) {
                Logger::Log(fatal ? LogLevel::Error : LogLevel::Warning,
                            "While processing targets:\n{}",
                            msg);
                failed = failed or fatal;
            });
    }

#ifndef BOOTSTRAP_BUILD_TOOL
    // close progress observer
    done = true;
    cv.notify_all();
    observer.join();
#endif  // BOOTSTRAP_BUILD_TOOL

    if (failed) {
        return std::nullopt;
    }

    if (not target) {
        Logger::Log(
            LogLevel::Error, "Failed to analyse target: {}", id.ToString());
        if (not(DetectAndReportCycle("expression imports", expr_map) or
                DetectAndReportCycle("target dependencies", target_map))) {
            DetectAndReportPending("expressions", expr_map);
            DetectAndReportPending("targets", expr_map);
        }
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
            auto action = result_map->GetAction(action_id);
            if (action) {
                Logger::Log(LogLevel::Info,
                            "Request is input of action %{}",
                            action_id);
                target = SwitchToActionInput(target, *action);
                modified = fmt::format("%{}", action_id);
            }
            else {
                Logger::Log(LogLevel::Error,
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
                Logger::Log(
                    LogLevel::Info, "Request is input of action #{}", number);
                target = SwitchToActionInput(target, *action);
                modified = fmt::format("#{}", number);
            }
            else {
                Logger::Log(LogLevel::Error,
                            "Action #{} out of range for the requested target",
                            number);
                return std::nullopt;
            }
        }
        else {
            auto action = result_map->GetAction(*request_action_input);
            if (action) {
                Logger::Log(LogLevel::Info,
                            "Request is input of action %{}",
                            *request_action_input);
                target = SwitchToActionInput(target, *action);
                modified = fmt::format("%{}", *request_action_input);
            }
            else {
                auto number = std::atoi(request_action_input->c_str());
                auto action = GetActionNumber(*target, number);
                if (action) {
                    Logger::Log(LogLevel::Info,
                                "Request is input of action #{}",
                                number);
                    target = SwitchToActionInput(target, *action);
                    modified = fmt::format("#{}", number);
                }
                else {
                    Logger::Log(
                        LogLevel::Error,
                        "Action #{} out of range for the requested target",
                        number);
                    return std::nullopt;
                }
            }
        }
    }
    return AnalysisResult{.id = id, .target = target, .modified = modified};
}
