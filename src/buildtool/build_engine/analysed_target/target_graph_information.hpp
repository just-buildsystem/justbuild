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

#ifndef INCLUDED_SRC_BUILDTOOL_BUILDENGINE_ANALYSED_TARGET_TARGET_GRAPH_INFORMATION_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILDENGINE_ANALYSED_TARGET_TARGET_GRAPH_INFORMATION_HPP

#include <optional>
#include <string>
#include <utility>  // std::move
#include <vector>

#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/target_map/configured_target.hpp"

class TargetGraphInformation {
  public:
    explicit TargetGraphInformation(
        BuildMaps::Target::ConfiguredTargetPtr node,
        std::vector<BuildMaps::Target::ConfiguredTargetPtr> direct,
        std::vector<BuildMaps::Target::ConfiguredTargetPtr> implicit,
        std::vector<BuildMaps::Target::ConfiguredTargetPtr> anonymous)
        : node_{std::move(node)},
          direct_{std::move(direct)},
          implicit_{std::move(implicit)},
          anonymous_{std::move(anonymous)} {}

    static const TargetGraphInformation kSource;

    [[nodiscard]] auto Node() const noexcept
        -> BuildMaps::Target::ConfiguredTargetPtr {
        return node_;
    }

    [[nodiscard]] auto NodeString() const noexcept
        -> std::optional<std::string>;
    [[nodiscard]] auto DepsToJson() const -> nlohmann::json;

  private:
    BuildMaps::Target::ConfiguredTargetPtr node_;
    std::vector<BuildMaps::Target::ConfiguredTargetPtr> direct_;
    std::vector<BuildMaps::Target::ConfiguredTargetPtr> implicit_;
    std::vector<BuildMaps::Target::ConfiguredTargetPtr> anonymous_;
};

inline const TargetGraphInformation TargetGraphInformation::kSource =
    TargetGraphInformation{nullptr, {}, {}, {}};
#endif  // INCLUDED_SRC_BUILDTOOL_BUILDENGINE_ANALYSED_TARGET_TARGET_GRAPH_INFORMATION_HPP
