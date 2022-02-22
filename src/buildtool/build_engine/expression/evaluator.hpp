#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_EXPRESSION_EVALUATOR_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_EXPRESSION_EVALUATOR_HPP

#include <exception>
#include <string>

#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/build_engine/expression/function_map.hpp"

class Configuration;

class Evaluator {
  public:
    class EvaluationError : public std::exception {
      public:
        explicit EvaluationError(std::string const& msg,
                                 bool while_eval = false,
                                 bool user_context = false) noexcept
            : msg_{(while_eval ? ""
                               : (user_context ? "UserError: "
                                               : "EvaluationError: ")) +
                   msg},
              while_eval_{while_eval},
              user_context_{user_context} {}
        [[nodiscard]] auto what() const noexcept -> char const* final {
            return msg_.c_str();
        }

        [[nodiscard]] auto WhileEvaluation() const -> bool {
            return while_eval_;
        }

        [[nodiscard]] auto UserContext() const -> bool { return user_context_; }

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
    };

    // Exception-free evaluation of expression
    [[nodiscard]] static auto EvaluateExpression(
        ExpressionPtr const& expr,
        Configuration const& env,
        FunctionMapPtr const& provider_functions,
        std::function<void(std::string const&)> const& logger,
        std::function<void(void)> const& note_user_context = []() {}) noexcept
        -> ExpressionPtr;

  private:
    constexpr static std::size_t kLineWidth = 80;
    [[nodiscard]] static auto Evaluate(ExpressionPtr const& expr,
                                       Configuration const& env,
                                       FunctionMapPtr const& functions)
        -> ExpressionPtr;
};

#endif  // INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_EXPRESSION_EVALUATOR_HPP
