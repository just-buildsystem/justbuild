// Copyright 2025 Huawei Cloud Computing Technology Co., Ltd.
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

#include "src/buildtool/profile/profile.hpp"

#include <filesystem>
#include <fstream>

#include "gsl/gsl"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/cli.hpp"
#include "src/buildtool/common/remote/remote_common.hpp"
#include "src/utils/cpp/expected.hpp"

void Profile::Write(int exit_code) {
    profile_["stop time"] = std::time(nullptr);
    if (not actions_.empty()) {
        auto actions = nlohmann::json::object();
        for (auto const& [k, v] : actions_) {
            auto entry = nlohmann::json::object();
            entry["cached"] = v.cached;
            if (not v.cached) {
                entry["duration"] = v.duration;
            }
            if (v.exit_code != 0) {
                entry["exit code"] = v.exit_code;
            }
            entry["artifacts"] = v.artifacts;
            if (v.out) {
                entry["stdout"] = *v.out;
            }
            if (v.err) {
                entry["stderr"] = *v.err;
            }
            actions[k] = entry;
        }
        profile_["actions"] = actions;
    }

    profile_["exit code"] = exit_code;
    if (not analysis_errors_.empty()) {
        profile_["analysis errors"] = analysis_errors_;
    }

    std::ofstream os(*output_file_);
    os << profile_.dump(2) << std::endl;
}

void Profile::SetTarget(nlohmann::json target) {
    profile_["target"] = std::move(target);
}

void Profile::SetConfiguration(nlohmann::json configuration) {
    profile_["configuration"] = std::move(configuration);
}

void Profile::SetCLI(CommandLineArguments const& cli) {
    switch (cli.cmd) {
        case SubCommand::kDescribe:
            profile_["subcommand"] = "describe";
            break;
        case SubCommand::kAnalyse:
            profile_["subcommand"] = "analyse";
            break;
        case SubCommand::kBuild:
            profile_["subcommand"] = "build";
            break;
        case SubCommand::kInstall:
            profile_["subcommand"] = "install";
            break;
        case SubCommand::kRebuild:
            profile_["subcommand"] = "rebuild";
            break;
        default:
            // We only log information on the commands that support profiling.
            return;
    }
    if (cli.analysis.target) {
        if (cli.analysis.target->is_array()) {
            profile_["subcommand args"] = *cli.analysis.target;
        }
        else {
            auto args = nlohmann::json::array();
            args.push_back(*cli.analysis.target);
            profile_["subcommand args"] = args;
        }
    }
    else {
        profile_["subcommand args"] = nlohmann::json::array();
    }
}

void Profile::NoteActionCompleted(std::string const& id,
                                  IExecutionResponse::Ptr const& response,
                                  std::string const& cwd) {
    if (not response) {
        return;
    }
    std::unique_lock lock{mutex_};
    auto artifacts = response->Artifacts();
    std::optional<std::string> out = std::nullopt;
    std::optional<std::string> err = std::nullopt;
    if (response->HasStdOut()) {
        auto action_out = response->StdOutDigest();
        if (action_out) {
            out = action_out->hash();
        }
    }
    if (response->HasStdErr()) {
        auto action_err = response->StdErrDigest();
        if (action_err) {
            err = action_err->hash();
        }
    }
    if (not artifacts) {
        actions_[id] = ActionData{
            .cached = response->IsCached(),
            .duration = response->ExecutionDuration(),
            .exit_code = response->ExitCode(),
            .out = out,
            .err = err,
            .artifacts = std::unordered_map<std::string, std::string>()};
    }
    else {
        actions_[id] = ActionData{
            .cached = response->IsCached(),
            .duration = response->ExecutionDuration(),
            .exit_code = response->ExitCode(),
            .out = out,
            .err = err,
            .artifacts = std::unordered_map<std::string, std::string>(
                (*artifacts)->size())};
        if (cwd.empty()) {
            // the typical case of empty cwd, avoid unnecessary calls
            for (auto const& [k, v] : **artifacts) {
                actions_[id].artifacts.emplace(k, v.digest.hash());
            }
        }
        else {
            std::filesystem::path base{cwd};
            for (auto const& [k, v] : **artifacts) {
                actions_[id].artifacts.emplace(
                    (base / k).lexically_normal().string(), v.digest.hash());
            }
        }
    }
}

void Profile::NoteAnalysisError(std::string const& error_message) {
    std::unique_lock lock{mutex_};
    analysis_errors_.emplace_back(error_message);
}

void Profile::SetRemoteExecutionConfig(RemoteExecutionConfig const& config) {
    auto remote = nlohmann::json::object();
    if (config.remote_address) {
        remote["address"] = config.remote_address->ToJson();
    }
    remote["properties"] = config.platform_properties;
    auto dispatch = nlohmann::json::array();
    for (auto const& dispatch_entry : config.dispatch) {
        auto entry = nlohmann::json::array();
        entry.emplace_back(dispatch_entry.first);
        entry.emplace_back(dispatch_entry.second.ToJson());
        dispatch.emplace_back(entry);
    }
    remote["dispatch"] = dispatch;
    profile_["remote"] = remote;
}

void Profile::StartBuild() {
    profile_["build start time"] = std::time(nullptr);
}
