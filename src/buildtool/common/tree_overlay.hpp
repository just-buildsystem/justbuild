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

#ifndef INCLUDED_SRC_BUILDTOOL_COMMON_TREE_OVERLAY_HPP
#define INCLUDED_SRC_BUILDTOOL_COMMON_TREE_OVERLAY_HPP

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "fmt/core.h"
#include "nlohmann/json.hpp"
#include "src/buildtool/common/action.hpp"
#include "src/buildtool/common/action_description.hpp"
#include "src/buildtool/common/artifact_description.hpp"
#include "src/buildtool/crypto/hash_function.hpp"

class TreeOverlay {

  public:
    using to_overlay_t = std::vector<ArtifactDescription>;
    using Ptr = std::shared_ptr<TreeOverlay>;
    explicit TreeOverlay(to_overlay_t const& trees, bool disjoint)
        : id_{ComputeId(trees, disjoint)}, trees_{trees}, disjoint_{disjoint} {}

    [[nodiscard]] auto Id() const& -> std::string const& { return id_; }
    [[nodiscard]] auto Id() && -> std::string { return std::move(id_); }

    [[nodiscard]] auto ToJson() const -> nlohmann::json {
        return ComputeDescription(trees_, disjoint_);
    }

    [[nodiscard]] auto Inputs() const -> ActionDescription::inputs_t {
        return AsInputs(trees_);
    }

    [[nodiscard]] auto Action() const -> ActionDescription {
        return {{/*unused*/},
                {/*unused*/},
                Action::CreateTreeOverlayAction(id_, disjoint_),
                AsInputs(trees_)};
    }

    [[nodiscard]] auto Output() const -> ArtifactDescription {
        return ArtifactDescription::CreateTreeOverlay(id_);
    }

    [[nodiscard]] static auto FromJson(HashFunction::Type hash_type,
                                       std::string const& id,
                                       nlohmann::json const& json)
        -> std::optional<TreeOverlay::Ptr> {
        bool disjoint = json["disjoint"];
        auto trees = to_overlay_t{};
        for (auto const& entry : json["trees"]) {
            auto artifact_desc =
                ArtifactDescription::FromJson(hash_type, entry);
            if (not artifact_desc) {
                return std::nullopt;
            }
            trees.emplace_back(std::move(*artifact_desc));
        }
        return std::make_shared<TreeOverlay>(id, std::move(trees), disjoint);
    }

    TreeOverlay(std::string id, to_overlay_t&& trees, bool disjoint)
        : id_{std::move(id)}, trees_{std::move(trees)}, disjoint_{disjoint} {}

  private:
    std::string id_;
    to_overlay_t trees_;
    bool disjoint_;

    static auto ComputeDescription(to_overlay_t const& trees,
                                   bool disjoint) -> nlohmann::json {
        auto json = nlohmann::json::object();
        auto tree_descs = nlohmann::json::array();
        for (auto const& tree : trees) {
            tree_descs.emplace_back(tree.ToJson());
        }
        json["trees"] = tree_descs;
        json["disjoint"] = disjoint;
        return json;
    }

    static auto ComputeId(to_overlay_t const& trees,
                          bool disjoint) -> std::string {
        // The type of HashFunction is irrelevant here. It is used for
        // identification of trees. SHA256 is used.
        HashFunction const hash_function{HashFunction::Type::PlainSHA256};
        return hash_function
            .PlainHashData(ComputeDescription(trees, disjoint).dump())
            .HexString();
    }

    static auto AsInputs(to_overlay_t const& trees)
        -> ActionDescription::inputs_t {
        ActionDescription::inputs_t as_inputs{};
        for (size_t i = 0; i < trees.size(); i++) {
            as_inputs.emplace(fmt::format("{:010d}", i), trees[i]);
        }
        return as_inputs;
    }
};

#endif
