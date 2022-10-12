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

#ifndef INCLUDED_SRC_BUILDTOOL_BUILDENGINE_EXPRESSION_TARGET_RESULT_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILDENGINE_EXPRESSION_TARGET_RESULT_HPP

#include <unordered_map>

#include <nlohmann/json.hpp>

#include "src/buildtool/build_engine/expression/expression_ptr.hpp"
#include "src/buildtool/common/artifact_description.hpp"
#include "src/utils/cpp/hash_combine.hpp"

struct TargetResult {
    ExpressionPtr artifact_stage{};
    ExpressionPtr provides{};
    ExpressionPtr runfiles{};
    bool is_cacheable{provides.IsCacheable()};

    [[nodiscard]] static auto FromJson(nlohmann::json const& json) noexcept
        -> std::optional<TargetResult>;

    [[nodiscard]] auto ToJson() const -> nlohmann::json;

    [[nodiscard]] auto ReplaceNonKnownAndToJson(
        std::unordered_map<ArtifactDescription, Artifact::ObjectInfo> const&
            replacements) const noexcept -> std::optional<nlohmann::json>;

    [[nodiscard]] auto operator==(TargetResult const& other) const noexcept
        -> bool {
        return artifact_stage == other.artifact_stage and
               provides == other.provides and runfiles == other.runfiles;
    }
};

namespace std {
template <>
struct hash<TargetResult> {
    [[nodiscard]] auto operator()(TargetResult const& r) noexcept
        -> std::size_t {
        auto seed = std::hash<ExpressionPtr>{}(r.artifact_stage);
        hash_combine(&seed, std::hash<ExpressionPtr>{}(r.provides));
        hash_combine(&seed, std::hash<ExpressionPtr>{}(r.runfiles));
        return seed;
    }
};
}  // namespace std

#endif  // INCLUDED_SRC_BUILDTOOL_BUILDENGINE_EXPRESSION_TARGET_RESULT_HPP
