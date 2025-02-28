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

#ifndef INCLUDED_SRC_BUILDTOOL_PROFILE_PROFILE_HPP
#define INCLUDED_SRC_BUILDTOOL_PROFILE_PROFILE_HPP

#include <optional>
#include <string>
#include <utility>

#include "nlohmann/json.hpp"

class Profile {
  public:
    explicit Profile(std::optional<std::string> output_file)
        : output_file_{std::move(output_file)} {
        profile_ = nlohmann::json::object();
    }

    void Write(int exit_code);
    void SetTarget(nlohmann::json target);
    void SetConfiguration(nlohmann::json configuration);

  private:
    std::optional<std::string> output_file_;
    nlohmann::json profile_;
};

#endif
