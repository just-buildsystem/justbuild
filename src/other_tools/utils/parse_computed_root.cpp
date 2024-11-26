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

#include "src/other_tools/utils/parse_computed_root.hpp"

#include "fmt/core.h"
#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"

auto ComputedRootParser::Create(
    gsl::not_null<ExpressionPtr const*> const& repository) noexcept
    -> std::optional<ComputedRootParser> {
    if (not repository->IsNotNull() or not(*repository)->IsMap()) {
        return std::nullopt;
    }
    try {
        auto const type = (*repository)->Get("type", Expression::none_t{});
        if (type.IsNotNull() and type->IsString() and
            type->String() == "computed") {
            return ComputedRootParser{repository};
        }
    } catch (...) {
        return std::nullopt;
    }
    return std::nullopt;
}

auto ComputedRootParser::GetTargetRepository() const
    -> expected<std::string, std::string> {
    auto const repo = repository_->Get("repo", Expression::none_t{});
    if (not repo.IsNotNull()) {
        return unexpected<std::string>{"Mandatory key \"repo\" is missing"};
    }
    if (not repo->IsString()) {
        return unexpected{fmt::format("Unsupported value {} for key \"repo\".",
                                      repo->ToString())};
    }
    return repo->String();
}

auto ComputedRootParser::GetResult() const
    -> expected<FileRoot::ComputedRoot, std::string> {
    auto const repo = GetTargetRepository();
    if (not repo) {
        return unexpected{repo.error()};
    }

    auto const target = repository_->Get("target", Expression::none_t{});
    if (not target.IsNotNull()) {
        return unexpected<std::string>{"Mandatory key \"target\" is missing"};
    }
    if (not target->IsList() or target->List().size() != 2) {
        return unexpected{fmt::format("Unsupported value {} for key \"target\"",
                                      target->ToString())};
    }
    auto const& target_list = target->List();
    auto const target_module = target_list.at(0);
    auto const target_name = target_list.at(1);
    if (not target_module->IsString() or not target_name->IsString()) {
        return unexpected{fmt::format(
            "Unsupported format {} for key \"target\"", target->ToString())};
    }

    auto const config = repository_->Get("config", Expression::none_t{});
    if (config.IsNotNull() and not config->IsMap()) {
        return unexpected{fmt::format("Unsupported value {} for key \"config\"",
                                      config->ToString())};
    }
    return FileRoot::ComputedRoot{.repository = *repo,
                                  .target_module = target_module->String(),
                                  .target_name = target_module->String(),
                                  .config = config.IsNotNull()
                                                ? config->ToJson()
                                                : nlohmann::json::object()};
}
