
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
                auto func_it = json_values[0]->find(id.name);
                if (func_it == json_values[0]->end()) {
                    (*logger)(fmt::format("Cannot find expression {} in {}",
                                          id.name,
                                          id.module),
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
                    std::move(logger));
            },
            [logger, id](auto msg, auto fatal) {
                (*logger)(fmt::format("While reading expression file in {}: {}",
                                      id.module,
                                      msg),
                          fatal);
            });
    };
    return ExpressionFunctionMap{expr_func_creator, jobs};
}

}  // namespace BuildMaps::Base
