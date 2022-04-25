#include "src/buildtool/build_engine/expression/evaluator.hpp"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <sstream>
#include <string>
#include <unordered_set>

#include "fmt/core.h"
#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/buildtool/build_engine/expression/function_map.hpp"

namespace {

using namespace std::string_literals;
using number_t = Expression::number_t;
using list_t = Expression::list_t;
using map_t = Expression::map_t;

auto ValueIsTrue(ExpressionPtr const& val) -> bool {
    if (val->IsNone()) {
        return false;
    }
    if (val->IsBool()) {
        return *val != false;
    }
    if (val->IsNumber()) {
        return *val != number_t{0};
    }
    if (val->IsString()) {
        return *val != ""s;
    }
    if (val->IsList()) {
        return not val->List().empty();
    }
    if (val->IsMap()) {
        return not val->Map().empty();
    }
    return true;
}

auto Flatten(ExpressionPtr const& expr) -> ExpressionPtr {
    if (not expr->IsList()) {
        throw Evaluator::EvaluationError{fmt::format(
            "Flatten expects list but instead got: {}.", expr->ToString())};
    }
    if (expr->List().empty()) {
        return expr;
    }
    auto const& list = expr->List();
    size_t size{};
    std::for_each(list.begin(), list.end(), [&](auto const& l) {
        if (not l->IsList()) {
            throw Evaluator::EvaluationError{
                fmt::format("Non-list entry found for argument in flatten: {}.",
                            l->ToString())};
        }
        size += l->List().size();
    });
    auto result = Expression::list_t{};
    result.reserve(size);
    std::for_each(list.begin(), list.end(), [&](auto const& l) {
        std::copy(
            l->List().begin(), l->List().end(), std::back_inserter(result));
    });
    return ExpressionPtr{result};
}

auto All(ExpressionPtr const& list) -> ExpressionPtr {
    for (auto const& c : list->List()) {
        if (not ValueIsTrue(c)) {
            return ExpressionPtr{false};
        }
    }
    return ExpressionPtr{true};
}

auto Any(ExpressionPtr const& list) -> ExpressionPtr {
    for (auto const& c : list->List()) {
        if (ValueIsTrue(c)) {
            return ExpressionPtr{true};
        }
    }
    return ExpressionPtr{false};
}

// logical AND with short-circuit evaluation
auto LogicalAnd(SubExprEvaluator&& eval,
                ExpressionPtr const& expr,
                Configuration const& env) -> ExpressionPtr {
    if (auto const list = expr->At("$1")) {
        auto const& l = list->get();
        if (not l->IsList()) {
            throw Evaluator::EvaluationError{
                fmt::format("Non-list entry found for argument in and: {}.",
                            l->ToString())};
        }
        for (auto const& c : l->List()) {
            if (not ValueIsTrue(eval(c, env))) {
                return ExpressionPtr{false};
            }
        }
    }
    return ExpressionPtr{true};
}

// logical OR with short-circuit evaluation
auto LogicalOr(SubExprEvaluator&& eval,
               ExpressionPtr const& expr,
               Configuration const& env) -> ExpressionPtr {
    if (auto const list = expr->At("$1")) {
        auto const& l = list->get();
        if (not l->IsList()) {
            throw Evaluator::EvaluationError{fmt::format(
                "Non-list entry found for argument in or: {}.", l->ToString())};
        }
        for (auto const& c : l->List()) {
            if (ValueIsTrue(eval(c, env))) {
                return ExpressionPtr{true};
            }
        }
    }
    return ExpressionPtr{false};
}

auto Keys(ExpressionPtr const& d) -> ExpressionPtr {
    auto const& m = d->Map();
    auto result = Expression::list_t{};
    result.reserve(m.size());
    std::for_each(m.begin(), m.end(), [&](auto const& item) {
        result.emplace_back(ExpressionPtr{item.first});
    });
    return ExpressionPtr{result};
}

auto Values(ExpressionPtr const& d) -> ExpressionPtr {
    return ExpressionPtr{d->Map().Values()};
}

auto NubRight(ExpressionPtr const& expr) -> ExpressionPtr {
    if (not expr->IsList()) {
        throw Evaluator::EvaluationError{fmt::format(
            "nub_right expects list but instead got: {}.", expr->ToString())};
    }
    if (expr->List().empty()) {
        return expr;
    }
    auto const& list = expr->List();
    auto reverse_result = Expression::list_t{};
    reverse_result.reserve(list.size());
    auto seen = std::unordered_set<ExpressionPtr>{};
    seen.reserve(list.size());
    std::for_each(list.rbegin(), list.rend(), [&](auto const& l) {
        if (not seen.contains(l)) {
            reverse_result.push_back(l);
            seen.insert(l);
        }
    });
    std::reverse(reverse_result.begin(), reverse_result.end());
    return ExpressionPtr{reverse_result};
}

auto Range(ExpressionPtr const& expr) -> ExpressionPtr {
    size_t len = 0;
    if (expr->IsNumber() && expr->Number() > 0.0) {
        len = static_cast<size_t>(std::lround(expr->Number()));
    }
    if (expr->IsString()) {
        len = static_cast<size_t>(std::atol(expr->String().c_str()));
    }
    auto result = Expression::list_t{};
    result.reserve(len);
    for (size_t i = 0; i < len; i++) {
        result.emplace_back(ExpressionPtr{fmt::format("{}", i)});
    }
    return ExpressionPtr{result};
}

auto ChangeEndingTo(ExpressionPtr const& name, ExpressionPtr const& ending)
    -> ExpressionPtr {
    std::filesystem::path path{name->String()};
    return ExpressionPtr{(path.parent_path() / path.stem()).string() +
                         ending->String()};
}

auto BaseName(ExpressionPtr const& name) -> ExpressionPtr {
    std::filesystem::path path{name->String()};
    return ExpressionPtr{path.filename().string()};
}

auto ShellQuote(std::string arg) -> std::string {
    auto start_pos = size_t{};
    std::string from{"'"};
    std::string to{"'\\''"};
    while ((start_pos = arg.find(from, start_pos)) != std::string::npos) {
        arg.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
    return fmt::format("'{}'", arg);
}

template <bool kDoQuote = false>
auto Join(ExpressionPtr const& expr, std::string const& sep) -> ExpressionPtr {
    if (expr->IsString()) {
        auto string = expr->String();
        if constexpr (kDoQuote) {
            string = ShellQuote(std::move(string));
        }
        return ExpressionPtr{std::move(string)};
    }
    if (expr->IsList()) {
        auto const& list = expr->List();
        int insert_sep{};
        std::stringstream ss{};
        std::for_each(list.begin(), list.end(), [&](auto const& e) {
            ss << (insert_sep++ ? sep : "");
            auto string = e->String();
            if constexpr (kDoQuote) {
                string = ShellQuote(std::move(string));
            }
            ss << std::move(string);
        });
        return ExpressionPtr{ss.str()};
    }
    throw Evaluator::EvaluationError{fmt::format(
        "Join expects string or list but got: {}.", expr->ToString())};
}

template <bool kDisjoint = false>
// NOLINTNEXTLINE(misc-no-recursion)
auto Union(Expression::list_t const& dicts, size_t from, size_t to)
    -> ExpressionPtr {
    if (to <= from) {
        return Expression::kEmptyMap;
    }
    if (to == from + 1) {
        return dicts[from];
    }
    size_t mid = from + (to - from) / 2;
    auto left = Union(dicts, from, mid);
    auto right = Union(dicts, mid, to);
    if (left->Map().empty()) {
        return right;
    }
    if (right->Map().empty()) {
        return left;
    }
    if constexpr (kDisjoint) {
        auto dup = left->Map().FindConflictingDuplicate(right->Map());
        if (dup) {
            throw Evaluator::EvaluationError{
                fmt::format("Map union not essentially disjoint as claimed, "
                            "duplicate key '{}'.",
                            dup->get())};
        }
    }
    return ExpressionPtr{Expression::map_t{left, right}};
}

template <bool kDisjoint = false>
auto Union(ExpressionPtr const& expr) -> ExpressionPtr {
    if (not expr->IsList()) {
        throw Evaluator::EvaluationError{fmt::format(
            "Union expects list of maps but got: {}.", expr->ToString())};
    }
    auto const& list = expr->List();
    if (list.empty()) {
        return Expression::kEmptyMap;
    }
    return Union<kDisjoint>(list, 0, list.size());
}

auto ConcatTargetName(ExpressionPtr const& expr, ExpressionPtr const& append)
    -> ExpressionPtr {
    if (expr->IsString()) {
        return ExpressionPtr{expr->String() + append->String()};
    }
    if (expr->IsList()) {
        auto list = Expression::list_t{};
        auto not_last = expr->List().size();
        bool all_string = true;
        std::for_each(
            expr->List().begin(), expr->List().end(), [&](auto const& e) {
                all_string = all_string and e->IsString();
                if (all_string) {
                    list.emplace_back(ExpressionPtr{
                        e->String() + (--not_last ? "" : append->String())});
                }
            });
        if (all_string) {
            return ExpressionPtr{list};
        }
    }
    throw Evaluator::EvaluationError{fmt::format(
        "Unsupported expression for concat: {}.", expr->ToString())};
}

auto EvalArgument(ExpressionPtr const& expr,
                  std::string const& argument,
                  const SubExprEvaluator& eval,
                  Configuration const& env) -> ExpressionPtr {
    try {
        return eval(expr[argument], env);
    } catch (Evaluator::EvaluationError const& ex) {
        throw Evaluator::EvaluationError::WhileEval(
            fmt::format("Evaluating argument {}:", argument), ex);
    } catch (std::exception const& ex) {
        throw Evaluator::EvaluationError::WhileEvaluating(
            fmt::format("Evaluating argument {}:", argument), ex);
    }
}

auto UnaryExpr(std::function<ExpressionPtr(ExpressionPtr const&)> const& f)
    -> std::function<ExpressionPtr(SubExprEvaluator&&,
                                   ExpressionPtr const&,
                                   Configuration const&)> {
    return [f](auto&& eval, auto const& expr, auto const& env) {
        auto argument = EvalArgument(expr, "$1", eval, env);
        try {
            return f(argument);
        } catch (Evaluator::EvaluationError const& ex) {
            throw Evaluator::EvaluationError::WhileEval(
                fmt::format("Having evaluted the argument to {}:",
                            argument->ToString()),
                ex);
        } catch (std::exception const& ex) {
            throw Evaluator::EvaluationError::WhileEvaluating(
                fmt::format("Having evaluted the argument to {}:",
                            argument->ToString()),
                ex);
        }
    };
}

auto AndExpr(SubExprEvaluator&& eval,
             ExpressionPtr const& expr,
             Configuration const& env) -> ExpressionPtr {
    if (auto const conds = expr->At("$1")) {
        return conds->get()->IsList()
                   ? LogicalAnd(std::move(eval), expr, env)
                   : UnaryExpr(All)(std::move(eval), expr, env);
    }
    return ExpressionPtr{true};
}

auto OrExpr(SubExprEvaluator&& eval,
            ExpressionPtr const& expr,
            Configuration const& env) -> ExpressionPtr {
    if (auto const conds = expr->At("$1")) {
        return conds->get()->IsList()
                   ? LogicalOr(std::move(eval), expr, env)
                   : UnaryExpr(Any)(std::move(eval), expr, env);
    }
    return ExpressionPtr{false};
}

auto VarExpr(SubExprEvaluator&& eval,
             ExpressionPtr const& expr,
             Configuration const& env) -> ExpressionPtr {
    auto result = env[expr["name"]];
    if (result->IsNone()) {
        return eval(expr->Get("default", Expression::none_t{}), env);
    }
    return result;
}

auto IfExpr(SubExprEvaluator&& eval,
            ExpressionPtr const& expr,
            Configuration const& env) -> ExpressionPtr {
    if (ValueIsTrue(EvalArgument(expr, "cond", eval, env))) {
        return EvalArgument(expr, "then", eval, env);
    }
    return eval(expr->Get("else", list_t{}), env);
}

auto CondExpr(SubExprEvaluator&& eval,
              ExpressionPtr const& expr,
              Configuration const& env) -> ExpressionPtr {
    auto const& cond = expr->At("cond");
    if (cond) {
        if (not cond->get()->IsList()) {
            throw Evaluator::EvaluationError{fmt::format(
                "cond in cond has to be a list of pairs, but found {}",
                cond->get()->ToString())};
        }
        for (const auto& pair : cond->get()->List()) {
            if (not pair->IsList() or pair->List().size() != 2) {
                throw Evaluator::EvaluationError{
                    fmt::format("cond in cond has to be a list of pairs, "
                                "but found entry {}",
                                pair->ToString())};
            }
            if (ValueIsTrue(eval(pair->List()[0], env))) {
                return eval(pair->List()[1], env);
            }
        }
    }
    return eval(expr->Get("default", list_t{}), env);
}

auto CaseExpr(SubExprEvaluator&& eval,
              ExpressionPtr const& expr,
              Configuration const& env) -> ExpressionPtr {
    auto const& cases = expr->At("case");
    if (cases) {
        if (not cases->get()->IsMap()) {
            throw Evaluator::EvaluationError{fmt::format(
                "case in case has to be a map of expressions, but found {}",
                cases->get()->ToString())};
        }
        auto const& e = expr->At("expr");
        if (not e) {
            throw Evaluator::EvaluationError{"missing expr in case"};
        }
        auto const& key = eval(e->get(), env);
        if (not key->IsString()) {
            throw Evaluator::EvaluationError{fmt::format(
                "expr in case must evaluate to string, but found {}",
                key->ToString())};
        }
        if (auto const& val = cases->get()->At(key->String())) {
            return eval(val->get(), env);
        }
    }
    return eval(expr->Get("default", list_t{}), env);
}

auto SeqCaseExpr(SubExprEvaluator&& eval,
                 ExpressionPtr const& expr,
                 Configuration const& env) -> ExpressionPtr {
    auto const& cases = expr->At("case");
    if (cases) {
        if (not cases->get()->IsList()) {
            throw Evaluator::EvaluationError{fmt::format(
                "case in case* has to be a list of pairs, but found {}",
                cases->get()->ToString())};
        }
        auto const& e = expr->At("expr");
        if (not e) {
            throw Evaluator::EvaluationError{"missing expr in case"};
        }
        auto const& cmp = eval(e->get(), env);
        for (const auto& pair : cases->get()->List()) {
            if (not pair->IsList() or pair->List().size() != 2) {
                throw Evaluator::EvaluationError{
                    fmt::format("case in case* has to be a list of pairs, "
                                "but found entry {}",
                                pair->ToString())};
            }
            if (cmp == eval(pair->List()[0], env)) {
                return eval(pair->List()[1], env);
            }
        }
    }
    return eval(expr->Get("default", list_t{}), env);
}

auto EqualExpr(SubExprEvaluator&& eval,
               ExpressionPtr const& expr,
               Configuration const& env) -> ExpressionPtr {
    return ExpressionPtr{EvalArgument(expr, "$1", eval, env) ==
                         EvalArgument(expr, "$2", eval, env)};
}

auto ChangeEndingExpr(SubExprEvaluator&& eval,
                      ExpressionPtr const& expr,
                      Configuration const& env) -> ExpressionPtr {
    auto name = eval(expr->Get("$1", ""s), env);
    auto ending = eval(expr->Get("ending", ""s), env);
    return ChangeEndingTo(name, ending);
}

auto JoinExpr(SubExprEvaluator&& eval,
              ExpressionPtr const& expr,
              Configuration const& env) -> ExpressionPtr {
    auto list = eval(expr->Get("$1", list_t{}), env);
    auto separator = eval(expr->Get("separator", ""s), env);
    return Join(list, separator->String());
}

auto JoinCmdExpr(SubExprEvaluator&& eval,
                 ExpressionPtr const& expr,
                 Configuration const& env) -> ExpressionPtr {
    auto const& list = eval(expr->Get("$1", list_t{}), env);
    return Join</*kDoQuote=*/true>(list, " ");
}

auto JsonEncodeExpr(SubExprEvaluator&& eval,
                    ExpressionPtr const& expr,
                    Configuration const& env) -> ExpressionPtr {
    auto const& value = eval(expr->Get("$1", list_t{}), env);
    return ExpressionPtr{
        value->ToJson(Expression::JsonMode::NullForNonJson).dump()};
}

auto EscapeCharsExpr(SubExprEvaluator&& eval,
                     ExpressionPtr const& expr,
                     Configuration const& env) -> ExpressionPtr {
    auto string = eval(expr->Get("$1", ""s), env);
    auto chars = eval(expr->Get("chars", ""s), env);
    auto escape_prefix = eval(expr->Get("escape_prefix", "\\"s), env);
    std::stringstream ss{};
    std::for_each(
        string->String().begin(), string->String().end(), [&](auto const& c) {
            auto do_escape = chars->String().find(c) != std::string::npos;
            ss << (do_escape ? escape_prefix->String() : "") << c;
        });
    return ExpressionPtr{ss.str()};
}

auto LookupExpr(SubExprEvaluator&& eval,
                ExpressionPtr const& expr,
                Configuration const& env) -> ExpressionPtr {
    auto k = eval(expr["key"], env);
    auto d = eval(expr["map"], env);
    if (not k->IsString()) {
        throw Evaluator::EvaluationError{fmt::format(
            "Key expected to be string but found {}.", k->ToString())};
    }
    if (not d->IsMap()) {
        throw Evaluator::EvaluationError{fmt::format(
            "Map expected to be mapping but found {}.", d->ToString())};
    }
    auto lookup = Expression::kNone;
    if (d->Map().contains(k->String())) {
        lookup = d->Map().at(k->String());
    }
    if (lookup->IsNone()) {
        lookup = eval(expr->Get("default", Expression::none_t()), env);
    }
    return lookup;
}

auto EmptyMapExpr(SubExprEvaluator&& /*eval*/,
                  ExpressionPtr const& /*expr*/,
                  Configuration const&
                  /*env*/) -> ExpressionPtr {
    return Expression::kEmptyMap;
}

auto SingletonMapExpr(SubExprEvaluator&& eval,
                      ExpressionPtr const& expr,
                      Configuration const& env) -> ExpressionPtr {
    auto key = EvalArgument(expr, "key", eval, env);
    auto value = EvalArgument(expr, "value", eval, env);
    return ExpressionPtr{Expression::map_t{key->String(), value}};
}

auto ToSubdirExpr(SubExprEvaluator&& eval,
                  ExpressionPtr const& expr,
                  Configuration const& env) -> ExpressionPtr {
    auto d = eval(expr["$1"], env);
    auto s = eval(expr->Get("subdir", "."s), env);
    auto flat = ValueIsTrue(eval(expr->Get("flat", false), env));
    std::filesystem::path subdir{s->String()};
    auto result = Expression::map_t::underlying_map_t{};
    if (flat) {
        for (auto const& el : d->Map()) {
            std::filesystem::path k{el.first};
            auto new_path = subdir / k.filename();
            if (result.contains(new_path) && !(result[new_path] == el.second)) {
                // Check if the user specifed an error message for that case,
                // otherwise just generate a generic error message.
                auto msg_expr = expr->Map().Find("msg");
                if (not msg_expr) {
                    throw Evaluator::EvaluationError{fmt::format(
                        "Flat staging of {} to subdir {} conflicts on path {}",
                        d->ToString(),
                        subdir.string(),
                        new_path.string())};
                }
                std::string msg;
                try {
                    auto msg_val = eval(msg_expr->get(), env);
                    msg = msg_val->ToString();
                } catch (std::exception const&) {
                    msg =
                        "[non evaluating term] " + msg_expr->get()->ToString();
                }
                std::stringstream ss{};
                ss << msg << std::endl;
                ss << "Reason: flat staging to subdir " << subdir.string()
                   << " conflicts on path " << new_path.string() << std::endl;
                ss << "Map to flatly stage was " << d->ToString() << std::endl;
                throw Evaluator::EvaluationError(ss.str(), false, true);
            }
            result[new_path] = el.second;
        }
    }
    else {
        for (auto const& el : d->Map()) {
            result[(subdir / el.first).string()] = el.second;
        }
    }
    return ExpressionPtr{Expression::map_t{result}};
}

auto ForeachExpr(SubExprEvaluator&& eval,
                 ExpressionPtr const& expr,
                 Configuration const& env) -> ExpressionPtr {
    auto range_list = eval(expr->Get("range", list_t{}), env);
    if (range_list->List().empty()) {
        return Expression::kEmptyList;
    }
    auto const& var = expr->Get("var", "_"s);
    auto const& body = expr->Get("body", list_t{});
    auto result = Expression::list_t{};
    result.reserve(range_list->List().size());
    std::transform(range_list->List().begin(),
                   range_list->List().end(),
                   std::back_inserter(result),
                   [&](auto const& x) {
                       return eval(body, env.Update(var->String(), x));
                   });
    return ExpressionPtr{result};
}

auto ForeachMapExpr(SubExprEvaluator&& eval,
                    ExpressionPtr const& expr,
                    Configuration const& env) -> ExpressionPtr {
    auto range_map = eval(expr->Get("range", Expression::kEmptyMapExpr), env);
    if (range_map->Map().empty()) {
        return Expression::kEmptyList;
    }
    auto const& var = expr->Get("var_key", "_"s);
    auto const& var_val = expr->Get("var_val", "$_"s);
    auto const& body = expr->Get("body", list_t{});
    auto result = Expression::list_t{};
    result.reserve(range_map->Map().size());
    std::transform(range_map->Map().begin(),
                   range_map->Map().end(),
                   std::back_inserter(result),
                   [&](auto const& it) {
                       return eval(body,
                                   env.Update(var->String(), it.first)
                                       .Update(var_val->String(), it.second));
                   });
    return ExpressionPtr{result};
}

auto FoldLeftExpr(SubExprEvaluator&& eval,
                  ExpressionPtr const& expr,
                  Configuration const& env) -> ExpressionPtr {
    auto const& var = expr->Get("var", "_"s);
    auto const& accum_var = expr->Get("accum_var", "$1"s);
    auto range_list = eval(expr["range"], env);
    auto val = eval(expr->Get("start", list_t{}), env);
    auto const& body = expr->Get("body", list_t{});
    for (auto const& x : range_list->List()) {
        val = eval(
            body, env.Update({{var->String(), x}, {accum_var->String(), val}}));
    }
    return val;
}

auto LetExpr(SubExprEvaluator&& eval,
             ExpressionPtr const& expr,
             Configuration const& env) -> ExpressionPtr {
    auto const& bindings = expr->At("bindings");
    auto new_env = env;
    if (bindings) {
        if (not bindings->get()->IsList()) {
            throw Evaluator::EvaluationError{fmt::format(
                "bindings in let* has to be a list of pairs, but found {}",
                bindings->get()->ToString())};
        }
        int pos = -1;
        for (const auto& binding : bindings->get()->List()) {
            ++pos;
            if (not binding->IsList() or binding->List().size() != 2) {
                throw Evaluator::EvaluationError{
                    fmt::format("bindings in let* has to be a list of pairs, "
                                "but found entry {}",
                                binding->ToString())};
            }
            auto const& x_exp = binding[0];
            if (not x_exp->IsString()) {
                throw Evaluator::EvaluationError{
                    fmt::format("variable names in let* have to be strings, "
                                "but found binding entry {}",
                                binding->ToString())};
            }
            ExpressionPtr val;
            try {
                val = eval(binding[1], new_env);
            } catch (Evaluator::EvaluationError const& ex) {
                throw Evaluator::EvaluationError::WhileEval(
                    fmt::format("Evaluating entry {} in bindings, binding {}:",
                                pos,
                                x_exp->ToString()),
                    ex);
            } catch (std::exception const& ex) {
                throw Evaluator::EvaluationError::WhileEvaluating(
                    fmt::format("Evaluating entry {} in bindings, binding {}:",
                                pos,
                                x_exp->ToString()),
                    ex);
            }
            new_env = new_env.Update(x_exp->String(), val);
        }
    }
    auto const& body = expr->Get("body", map_t{});
    try {
        return eval(body, new_env);
    } catch (Evaluator::EvaluationError const& ex) {
        throw Evaluator::EvaluationError::WhileEval("Evaluating the body:", ex);
    } catch (std::exception const& ex) {
        throw Evaluator::EvaluationError::WhileEvaluating(
            "Evaluating the body:", ex);
    }
}

auto ConcatTargetNameExpr(SubExprEvaluator&& eval,
                          ExpressionPtr const& expr,
                          Configuration const& env) -> ExpressionPtr {
    auto p1 = eval(expr->Get("$1", ""s), env);
    auto p2 = eval(expr->Get("$2", ""s), env);
    return ConcatTargetName(p1, Join(p2, ""));
}

auto ContextExpr(SubExprEvaluator&& eval,
                 ExpressionPtr const& expr,
                 Configuration const& env) -> ExpressionPtr {
    try {
        return eval(expr->Get("$1", Expression::kNone), env);
    } catch (std::exception const& ex) {
        auto msg_expr = expr->Get("msg", map_t{});
        std::string context{};
        try {
            auto msg_val = eval(msg_expr, env);
            context = msg_val->ToString();
        } catch (std::exception const&) {
            context = "[non evaluating term] " + msg_expr->ToString();
        }
        std::stringstream ss{};
        ss << "In Context " << context << std::endl;
        ss << ex.what();
        throw Evaluator::EvaluationError(ss.str(), true, true);
    }
}

auto DisjointUnionExpr(SubExprEvaluator&& eval,
                       ExpressionPtr const& expr,
                       Configuration const& env) -> ExpressionPtr {
    auto argument = EvalArgument(expr, "$1", eval, env);
    try {
        return Union</*kDisjoint=*/true>(argument);
    } catch (std::exception const& ex) {
        auto msg_expr = expr->Map().Find("msg");
        if (not msg_expr) {
            throw Evaluator::EvaluationError::WhileEvaluating(
                fmt::format("Having evaluted the argument to {}:",
                            argument->ToString()),
                ex);
        }
        std::string msg;
        try {
            auto msg_val = eval(msg_expr->get(), env);
            msg = msg_val->ToString();
        } catch (std::exception const&) {
            msg = "[non evaluating term] " + msg_expr->get()->ToString();
        }
        std::stringstream ss{};
        ss << msg << std::endl;
        ss << "Reason: " << ex.what() << std::endl;
        ss << "The argument of the union was " << argument->ToString();
        throw Evaluator::EvaluationError(ss.str(), false, true);
    }
}

auto FailExpr(SubExprEvaluator&& eval,
              ExpressionPtr const& expr,
              Configuration const& env) -> ExpressionPtr {
    auto msg = eval(expr->Get("msg", Expression::kNone), env);
    throw Evaluator::EvaluationError(
        msg->ToString(), false, /* user error*/ true);
}

auto AssertNonEmptyExpr(SubExprEvaluator&& eval,
                        ExpressionPtr const& expr,
                        Configuration const& env) -> ExpressionPtr {
    auto val = eval(expr["$1"], env);
    if ((val->IsString() and (not val->String().empty())) or
        (val->IsList() and (not val->List().empty())) or
        (val->IsMap() and (not val->Map().empty()))) {
        return val;
    }
    auto msg_expr = expr->Get("msg", Expression::kNone);
    std::string msg;
    try {
        auto msg_val = eval(msg_expr, env);
        msg = msg_val->ToString();
    } catch (std::exception const&) {
        msg = "[non evaluating term] " + msg_expr->ToString();
    }
    std::stringstream ss{};
    ss << msg << std::endl;
    ss << "Expected non-empty value but found: " << val->ToString();
    throw Evaluator::EvaluationError(ss.str(), false, true);
}

auto const kBuiltInFunctions =
    FunctionMap::MakePtr({{"var", VarExpr},
                          {"if", IfExpr},
                          {"cond", CondExpr},
                          {"case", CaseExpr},
                          {"case*", SeqCaseExpr},
                          {"fail", FailExpr},
                          {"assert_non_empty", AssertNonEmptyExpr},
                          {"context", ContextExpr},
                          {"==", EqualExpr},
                          {"and", AndExpr},
                          {"or", OrExpr},
                          {"++", UnaryExpr(Flatten)},
                          {"nub_right", UnaryExpr(NubRight)},
                          {"range", UnaryExpr(Range)},
                          {"change_ending", ChangeEndingExpr},
                          {"basename", UnaryExpr(BaseName)},
                          {"join", JoinExpr},
                          {"join_cmd", JoinCmdExpr},
                          {"json_encode", JsonEncodeExpr},
                          {"escape_chars", EscapeCharsExpr},
                          {"keys", UnaryExpr(Keys)},
                          {"values", UnaryExpr(Values)},
                          {"lookup", LookupExpr},
                          {"empty_map", EmptyMapExpr},
                          {"singleton_map", SingletonMapExpr},
                          {"disjoint_map_union", DisjointUnionExpr},
                          {"map_union", UnaryExpr([](auto const& exp) {
                               return Union</*kDisjoint=*/false>(exp);
                           })},
                          {"to_subdir", ToSubdirExpr},
                          {"foreach", ForeachExpr},
                          {"foreach_map", ForeachMapExpr},
                          {"foldl", FoldLeftExpr},
                          {"let*", LetExpr},
                          {"concat_target_name", ConcatTargetNameExpr}});

}  // namespace

auto Evaluator::EvaluationError::WhileEvaluating(ExpressionPtr const& expr,
                                                 Configuration const& env,
                                                 std::exception const& ex)
    -> Evaluator::EvaluationError {
    std::stringstream ss{};
    ss << "* ";
    if (expr->IsMap() and expr->Map().contains("type") and
        expr["type"]->IsString()) {
        ss << expr["type"]->ToString() << "-expression ";
    }
    ss << expr->ToString() << std::endl;
    ss << "  environment " << std::endl;
    ss << env.Enumerate("  - ", kLineWidth) << std::endl;
    ss << ex.what();
    return EvaluationError{ss.str(), true /* while_eval */};
}

auto Evaluator::EvaluationError::WhileEval(ExpressionPtr const& expr,
                                           Configuration const& env,
                                           Evaluator::EvaluationError const& ex)
    -> Evaluator::EvaluationError {
    if (ex.UserContext()) {
        return ex;
    }
    return Evaluator::EvaluationError::WhileEvaluating(expr, env, ex);
}

auto Evaluator::EvaluationError::WhileEvaluating(const std::string& where,
                                                 std::exception const& ex)
    -> Evaluator::EvaluationError {
    std::stringstream ss{};
    ss << where << std::endl;
    ss << ex.what();
    return EvaluationError{ss.str(), true /* while_eval */};
}

auto Evaluator::EvaluationError::WhileEval(const std::string& where,
                                           Evaluator::EvaluationError const& ex)
    -> Evaluator::EvaluationError {
    if (ex.UserContext()) {
        return ex;
    }
    return Evaluator::EvaluationError::WhileEvaluating(where, ex);
}

auto Evaluator::EvaluateExpression(
    ExpressionPtr const& expr,
    Configuration const& env,
    FunctionMapPtr const& provider_functions,
    std::function<void(std::string const&)> const& logger,
    std::function<void(void)> const& note_user_context) noexcept
    -> ExpressionPtr {
    std::stringstream ss{};
    try {
        return Evaluate(
            expr,
            env,
            FunctionMap::MakePtr(kBuiltInFunctions, provider_functions));
    } catch (EvaluationError const& ex) {
        if (ex.UserContext()) {
            try {
                note_user_context();
            } catch (...) {
                // should not throw
            }
        }
        else {
            if (ex.WhileEvaluation()) {
                ss << "Expression evaluation traceback (most recent call last):"
                   << std::endl;
            }
        }
        ss << ex.what();
    } catch (std::exception const& ex) {
        ss << ex.what();
    }
    try {
        logger(ss.str());
    } catch (...) {
        // should not throw
    }
    return ExpressionPtr{nullptr};
}

// NOLINTNEXTLINE(misc-no-recursion)
auto Evaluator::Evaluate(ExpressionPtr const& expr,
                         Configuration const& env,
                         FunctionMapPtr const& functions) -> ExpressionPtr {
    try {
        if (expr->IsList()) {
            if (expr->List().empty()) {
                return expr;
            }
            auto list = Expression::list_t{};
            std::transform(
                expr->List().cbegin(),
                expr->List().cend(),
                std::back_inserter(list),
                // NOLINTNEXTLINE(misc-no-recursion)
                [&](auto const& e) { return Evaluate(e, env, functions); });
            return ExpressionPtr{list};
        }
        if (not expr->IsMap()) {
            return expr;
        }
        if (not expr->Map().contains("type")) {
            throw EvaluationError{fmt::format(
                "Object without keyword 'type': {}", expr->ToString())};
        }
        auto const& type = expr["type"]->String();
        auto func = functions->Find(type);
        if (func) {
            return func->get()(
                [&functions](auto const& subexpr, auto const& subenv) {
                    return Evaluator::Evaluate(subexpr, subenv, functions);
                },
                expr,
                env);
        }
        throw EvaluationError{
            fmt::format("Unknown syntactical construct {}", type)};
    } catch (EvaluationError const& ex) {
        throw EvaluationError::WhileEval(expr, env, ex);
    } catch (std::exception const& ex) {
        throw EvaluationError::WhileEvaluating(expr, env, ex);
    }
}
