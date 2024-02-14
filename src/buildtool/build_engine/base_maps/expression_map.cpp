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

#include "src/buildtool/build_engine/base_maps/expression_map.hpp"

#include <optional>
#include <string>

#include "fmt/core.h"
#include "src/buildtool/build_engine/base_maps/field_reader.hpp"

namespace BuildMaps::Base {

auto CreateExpressionMap(gsl::not_null<ExpressionFileMap*> const& expr_file_map,
                         std::size_t jobs) -> ExpressionFunctionMap {
    auto expr_func_creator = [expr_file_map](auto ts,
                                             auto setter,
                                             auto logger,
                                             auto subcaller,
                                             auto const& id) {
        if (not id.IsDefinitionName()) {
            (*logger)(
                fmt::format("{} cannot name an expression", id.ToString()),
                true);
            return;
        }
        expr_file_map->ConsumeAfterKeysReady(
            ts,
            {id.ToModule()},
            [setter = std::move(setter),
             logger,
             subcaller = std::move(subcaller),
             id](auto json_values) {
                auto const& target_ = id.GetNamedTarget();
                auto func_it = json_values[0]->find(target_.name);
                if (func_it == json_values[0]->end()) {
                    (*logger)(fmt::format("Cannot find expression {}",
                                          EntityName(target_).ToString()),
                              true);
                    return;
                }

                auto reader = FieldReader::Create(
                    func_it.value(), id, "expression", logger);
                if (not reader) {
                    return;
                }

                auto expr = reader->ReadExpression("expression");
                if (not expr) {
                    return;
                }

                auto vars = reader->ReadStringList("vars");
                if (not vars) {
                    return;
                }

                auto import_aliases =
                    reader->ReadEntityAliasesObject("imports");
                if (not import_aliases) {
                    return;
                }
                auto [names, ids] = std::move(*import_aliases).Obtain();

                auto wrapped_logger = std::make_shared<AsyncMapConsumerLogger>(
                    [logger, id](auto msg, auto fatal) {
                        (*logger)(
                            fmt::format("While handling imports of {}:\n{}",
                                        id.ToString(),
                                        msg),
                            fatal);
                    });
                (*subcaller)(
                    std::move(ids),
                    [setter = std::move(setter),
                     vars = std::move(*vars),
                     names = std::move(names),
                     expr = std::move(expr)](auto const& expr_funcs) {
                        auto imports = ExpressionFunction::imports_t{};
                        imports.reserve(expr_funcs.size());
                        for (std::size_t i{}; i < expr_funcs.size(); ++i) {
                            imports.emplace(names[i], *expr_funcs[i]);
                        }
                        (*setter)(std::make_shared<ExpressionFunction>(
                            vars, imports, expr));
                    },
                    wrapped_logger);
            },
            [logger, id](auto msg, auto fatal) {
                (*logger)(
                    fmt::format("While reading expression file in {}:\n{}",
                                id.GetNamedTarget().module,
                                msg),
                    fatal);
            });
    };
    return ExpressionFunctionMap{expr_func_creator, jobs};
}

}  // namespace BuildMaps::Base
