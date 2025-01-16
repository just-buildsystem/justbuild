// Copyright 2024 Huawei Cloud Computing Technology Co., Ltd.
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

#include "src/other_tools/utils/parse_precomputed_root.hpp"

#include <utility>

#include "fmt/core.h"
#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"

namespace {
[[nodiscard]] auto ParseComputedRoot(ExpressionPtr const& repository)
    -> expected<ComputedRoot, std::string> {
    auto const repo = repository->Get("repo", Expression::none_t{});
    if (not repo.IsNotNull()) {
        return unexpected<std::string>{"Mandatory key \"repo\" is missing"};
    }
    if (not repo->IsString()) {
        return unexpected{fmt::format("Unsupported value for key \"repo\":\n{}",
                                      repo->ToString())};
    }

    auto const target = repository->Get("target", Expression::none_t{});
    if (not target.IsNotNull()) {
        return unexpected<std::string>{"Mandatory key \"target\" is missing"};
    }
    if (not target->IsList() or target->List().size() != 2) {
        return unexpected{fmt::format(
            "Unsupported value for key \"target\":\n{}", target->ToString())};
    }
    auto const& target_list = target->List();
    auto const target_module = target_list.at(0);
    auto const target_name = target_list.at(1);
    if (not target_module->IsString() or not target_name->IsString()) {
        return unexpected{fmt::format(
            "Unsupported format for key \"target\":\n{}", target->ToString())};
    }

    auto const config = repository->Get("config", Expression::none_t{});
    if (config.IsNotNull() and not config->IsMap()) {
        return unexpected{fmt::format(
            "Unsupported value for key \"config\":\n{}", config->ToString())};
    }
    return ComputedRoot{.repository = repo->String(),
                        .target_module = target_module->String(),
                        .target_name = target_module->String(),
                        .config = config.IsNotNull()
                                      ? config->ToJson()
                                      : nlohmann::json::object()};
}

[[nodiscard]] auto ParseTreeStructureRoot(ExpressionPtr const& repository)
    -> expected<TreeStructureRoot, std::string> {
    auto const repo = repository->Get("repo", Expression::none_t{});
    if (not repo.IsNotNull()) {
        return unexpected<std::string>{"Mandatory key \"repo\" is missing"};
    }
    return TreeStructureRoot{.repository = repo->String(), .absent = false};
}
}  // namespace

auto ParsePrecomputedRoot(ExpressionPtr const& repository)
    -> expected<PrecomputedRoot, std::string> {
    if (not repository.IsNotNull() or not repository->IsMap()) {
        return unexpected<std::string>{"Repository has an incorrect format"};
    }

    auto const type = repository->Get("type", Expression::none_t());
    if (not type.IsNotNull()) {
        return unexpected<std::string>{"Mandatory key \"type\" is missing"};
    }
    if (not type->IsString()) {
        return unexpected{fmt::format("Unsupported value for key \"type\":\n{}",
                                      type->ToString())};
    }

    auto const& type_marker = type->String();
    if (type_marker == ComputedRoot::kMarker) {
        auto computed = ParseComputedRoot(repository);
        if (not computed) {
            return unexpected{std::move(computed).error()};
        }
        return PrecomputedRoot{*std::move(computed)};
    }
    if (type_marker == TreeStructureRoot::kMarker) {
        auto tree_structure = ParseTreeStructureRoot(repository);
        if (not tree_structure) {
            return unexpected{std::move(tree_structure).error()};
        }
        return PrecomputedRoot{*std::move(tree_structure)};
    }
    return unexpected{
        fmt::format("Unknown type {} of precomputed repository", type_marker)};
}
