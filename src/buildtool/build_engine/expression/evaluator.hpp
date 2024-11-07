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

#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_EXPRESSION_EVALUATOR_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_EXPRESSION_EVALUATOR_HPP

#include <cstddef>
#include <exception>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "src/buildtool/build_engine/expression/expression_ptr.hpp"
#include "src/buildtool/build_engine/expression/function_map.hpp"

class Evaluator {
    struct ConfigData {
        std::size_t expression_log_limit{kDefaultExpressionLogLimit};
    };

  public:
    /// \brief Set the limit for the size of logging a single expression
    /// in an error message.
    static void SetExpressionLogLimit(std::size_t width) {
        Config().expression_log_limit = width;
    }

    static auto GetExpressionLogLimit() -> std::size_t {
        return Config().expression_log_limit;
    }

    class EvaluationError : public std::exception {
      public:
        explicit EvaluationError(std::string msg,
                                 bool while_eval = false,
                                 bool user_context = false,
                                 std::vector<ExpressionPtr> involved_objetcs =
                                     std::vector<ExpressionPtr>{}) noexcept
            : msg_{std::move(msg)},
              while_eval_{while_eval},
              user_context_{user_context},
              involved_objects_{std::move(std::move(involved_objetcs))} {
            if (not while_eval_) {
                msg_ = (user_context_ ? "UserError: " : "EvaluationError: ") +
                       msg_;
            }
        }
        [[nodiscard]] auto what() const noexcept -> char const* final {
            return msg_.c_str();
        }

        [[nodiscard]] auto WhileEvaluation() const -> bool {
            return while_eval_;
        }

        [[nodiscard]] auto UserContext() const -> bool { return user_context_; }

        [[nodiscard]] auto InvolvedObjects() const& noexcept
            -> std::vector<ExpressionPtr> const& {
            return involved_objects_;
        }

        [[nodiscard]] auto InvolvedObjects() && -> std::vector<ExpressionPtr> {
            return involved_objects_;
        }

        [[nodiscard]] static auto WhileEvaluating(ExpressionPtr const& expr,
                                                  Configuration const& env,
                                                  std::exception const& ex)
            -> EvaluationError;

        [[nodiscard]] static auto WhileEval(ExpressionPtr const& expr,
                                            Configuration const& env,
                                            EvaluationError const& ex)
            -> EvaluationError;

        [[nodiscard]] static auto WhileEvaluating(const std::string& where,
                                                  std::exception const& ex)
            -> Evaluator::EvaluationError;

        [[nodiscard]] static auto WhileEval(const std::string& where,
                                            EvaluationError const& ex)
            -> Evaluator::EvaluationError;

      private:
        std::string msg_;
        bool while_eval_;
        bool user_context_;
        std::vector<ExpressionPtr> involved_objects_;
    };

    // Exception-free evaluation of expression
    [[nodiscard]] static auto EvaluateExpression(
        ExpressionPtr const& expr,
        Configuration const& env,
        FunctionMapPtr const& provider_functions,
        std::function<void(std::string const&)> const& logger,
        std::function<std::string(ExpressionPtr)> const& annotate_object =
            [](auto const& /*unused*/) { return std::string{}; },
        std::function<void(void)> const& note_user_context =
            []() {}) noexcept -> ExpressionPtr;

    constexpr static std::size_t kDefaultExpressionLogLimit = 320;

  private:
    [[nodiscard]] static auto Evaluate(ExpressionPtr const& expr,
                                       Configuration const& env,
                                       FunctionMapPtr const& functions)
        -> ExpressionPtr;
    [[nodiscard]] static auto Config() noexcept -> ConfigData& {
        static ConfigData instance{};
        return instance;
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_EXPRESSION_EVALUATOR_HPP
