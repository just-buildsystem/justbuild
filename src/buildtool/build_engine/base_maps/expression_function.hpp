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

#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_EXPRESSION_FUNCTION_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_EXPRESSION_FUNCTION_HPP

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "fmt/core.h"
#include "gsl/gsl"
#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/buildtool/build_engine/expression/evaluator.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/gsl.hpp"

namespace BuildMaps::Base {

class ExpressionFunction {
  public:
    using Ptr = std::shared_ptr<ExpressionFunction>;
    using imports_t = std::unordered_map<std::string, gsl::not_null<Ptr>>;

    ExpressionFunction(std::vector<std::string> vars,
                       imports_t imports,
                       ExpressionPtr expr) noexcept
        : vars_{std::move(vars)},
          imports_{std::move(imports)},
          expr_{std::move(expr)} {}

    [[nodiscard]] auto Evaluate(
        Configuration const& env,
        FunctionMapPtr const& functions,
        std::function<void(std::string const&)> const& logger =
            [](std::string const& error) noexcept -> void {
            Logger::Log(LogLevel::Error, error);
        },
        std::function<void(void)> const& note_user_context =
            []() noexcept -> void {}) const noexcept -> ExpressionPtr {
        try {  // try-catch to silence clang-tidy's bugprone-exception-escape,
               // only imports_caller can throw but it is not called here.
            auto imports_caller = [this, &functions](
                                      SubExprEvaluator&& /*eval*/,
                                      ExpressionPtr const& expr,
                                      Configuration const& env) {
                auto name_expr = expr["name"];
                auto const& name = name_expr->String();
                auto it = imports_.find(name);
                if (it != imports_.end()) {
                    std::stringstream ss{};
                    bool user_context = false;
                    auto result = it->second->Evaluate(
                        env,
                        functions,
                        [&ss](auto const& msg) { ss << msg; },
                        [&user_context]() { user_context = true; }

                    );
                    if (result) {
                        return result;
                    }
                    if (user_context) {
                        throw Evaluator::EvaluationError(ss.str(), true, true);
                    }
                    throw Evaluator::EvaluationError(
                        fmt::format(
                            "This call to {} failed in the following way:\n{}",
                            name_expr->ToString(),
                            ss.str()),
                        true);
                }
                throw Evaluator::EvaluationError(
                    fmt::format("Unknown expression '{}'.", name));
            };
            auto newenv = env.Prune(vars_);
            return expr_.Evaluate(
                newenv,
                FunctionMap::MakePtr(
                    functions, "CALL_EXPRESSION", imports_caller),
                logger,
                note_user_context);
        } catch (...) {
            EnsuresAudit(false);  // ensure that the try-block never throws
            return ExpressionPtr{nullptr};
        }
    }

    inline static Ptr const kEmptyTransition =
        std::make_shared<ExpressionFunction>(
            std::vector<std::string>{},
            ExpressionFunction::imports_t{},
            Expression::FromJson(R"([{"type": "empty_map"}])"_json));

  private:
    std::vector<std::string> vars_{};
    imports_t imports_{};
    ExpressionPtr expr_{};
};

using ExpressionFunctionPtr = ExpressionFunction::Ptr;

}  // namespace BuildMaps::Base

#endif  // INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_EXPRESSION_FUNCTION_HPP
