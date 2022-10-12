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

#include "src/buildtool/build_engine/expression/target_node.hpp"

#include "src/buildtool/build_engine/expression/expression.hpp"

auto TargetNode::Abstract::IsCacheable() const noexcept -> bool {
    return target_fields->IsCacheable();
}

auto TargetNode::ToJson() const -> nlohmann::json {
    if (IsValue()) {
        return {{"type", "VALUE_NODE"}, {"result", GetValue()->ToJson()}};
    }
    auto const& data = GetAbstract();
    return {{"type", "ABSTRACT_NODE"},
            {"node_type", data.node_type},
            {"string_fields", data.string_fields->ToJson()},
            {"target_fields",
             data.target_fields->ToJson(
                 Expression::JsonMode::SerializeAllButNodes)}};
}
