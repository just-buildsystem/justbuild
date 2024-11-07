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

#include "src/buildtool/build_engine/analysed_target/target_graph_information.hpp"

#include <memory>

auto TargetGraphInformation::NodeString() const noexcept
    -> std::optional<std::string> {
    if (node_) {
        return node_->ToString();
    }
    return std::nullopt;
}

namespace {
auto NodesToString(std::vector<BuildMaps::Target::ConfiguredTargetPtr> const&
                       nodes) -> std::vector<std::string> {
    std::vector<std::string> result{};
    result.reserve(nodes.size());
    for (auto const& n : nodes) {
        if (n) {
            result.emplace_back(n->ToString());
        }
    }
    return result;
}

}  // namespace

auto TargetGraphInformation::DepsToJson() const -> nlohmann::json {
    auto result = nlohmann::json::object();
    result["declared"] = NodesToString(direct_);
    result["implicit"] = NodesToString(implicit_);
    result["anonymous"] = NodesToString(anonymous_);
    return result;
}
