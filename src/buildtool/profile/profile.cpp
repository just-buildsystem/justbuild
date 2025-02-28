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

#include <fstream>

void Profile::Write(int exit_code) {
    if (not output_file_) {
        return;
    }

    if (not actions_.empty()) {
        auto actions = nlohmann::json::object();
        for (auto const& [k, v] : actions_) {
            auto entry = nlohmann::json::object();
            entry["cached"] = v.cached;
            actions[k] = entry;
        }
        profile_["actions"] = actions;
    }

    profile_["exit code"] = exit_code;

    std::ofstream os(*output_file_);
    os << profile_.dump(2) << std::endl;
}

void Profile::SetTarget(nlohmann::json target) {
    profile_["target"] = std::move(target);
}

void Profile::SetConfiguration(nlohmann::json configuration) {
    profile_["configuration"] = std::move(configuration);
}

void Profile::NoteActionCompleted(std::string const& id,
                                  IExecutionResponse::Ptr const& response) {
    std::unique_lock lock{mutex_};
    actions_[id] = ActionData{.cached = response->IsCached()};
}
