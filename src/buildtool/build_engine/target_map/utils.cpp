#include "src/buildtool/build_engine/target_map/utils.hpp"

#include <algorithm>
#include <filesystem>
#include <vector>

auto BuildMaps::Target::Utils::obtainTargetByName(
    const SubExprEvaluator& eval,
    const ExpressionPtr& expr,
    const Configuration& env,
    const Base::EntityName& current,
    std::unordered_map<BuildMaps::Target::ConfiguredTarget,
                       AnalysedTargetPtr> const& deps_by_transition)
    -> AnalysedTargetPtr {
    auto const& empty_map_exp = Expression::kEmptyMapExpr;
    auto reference = eval(expr["dep"], env);
    std::string error{};
    auto target = BuildMaps::Base::ParseEntityNameFromExpression(
        reference, current, [&error](std::string const& parse_err) {
            error = parse_err;
        });
    if (not target) {
        throw Evaluator::EvaluationError{
            fmt::format("Parsing target name {} failed with:\n{}",
                        reference->ToString(),
                        error)};
    }
    auto transition = eval(expr->Get("transition", empty_map_exp), env);
    auto it = deps_by_transition.find(BuildMaps::Target::ConfiguredTarget{
        *target, Configuration{transition}});
    if (it == deps_by_transition.end()) {
        throw Evaluator::EvaluationError{fmt::format(
            "Reference to undeclared dependency {} in transition {}",
            reference->ToString(),
            transition->ToString())};
    }
    return it->second;
}

auto BuildMaps::Target::Utils::obtainTarget(
    const SubExprEvaluator& eval,
    const ExpressionPtr& expr,
    const Configuration& env,
    std::unordered_map<BuildMaps::Target::ConfiguredTarget,
                       AnalysedTargetPtr> const& deps_by_transition)
    -> AnalysedTargetPtr {
    auto const& empty_map_exp = Expression::kEmptyMapExpr;
    auto reference = eval(expr["dep"], env);
    if (not reference->IsName()) {
        throw Evaluator::EvaluationError{
            fmt::format("Not a target name: {}", reference->ToString())};
    }
    auto transition = eval(expr->Get("transition", empty_map_exp), env);
    auto it = deps_by_transition.find(BuildMaps::Target::ConfiguredTarget{
        reference->Name(), Configuration{transition}});
    if (it == deps_by_transition.end()) {
        throw Evaluator::EvaluationError{fmt::format(
            "Reference to undeclared dependency {} in transition {}",
            reference->ToString(),
            transition->ToString())};
    }
    return it->second;
}

auto BuildMaps::Target::Utils::keys_expr(const ExpressionPtr& map)
    -> ExpressionPtr {
    auto const& m = map->Map();
    auto result = Expression::list_t{};
    result.reserve(m.size());
    std::for_each(m.begin(), m.end(), [&](auto const& item) {
        result.emplace_back(ExpressionPtr{item.first});
    });
    return ExpressionPtr{result};
}

auto BuildMaps::Target::Utils::tree_conflict(const ExpressionPtr& map)
    -> std::optional<std::string> {
    std::vector<std::filesystem::path> trees{};
    for (auto const& [path, artifact] : map->Map()) {
        if (artifact->Artifact().IsTree()) {
            trees.emplace_back(std::filesystem::path{path});
        }
    }
    if (trees.empty()) {
        return std::nullopt;
    }
    for (auto const& [path, artifact] : map->Map()) {
        auto p = std::filesystem::path{path};
        for (auto const& treepath : trees) {
            if (not artifact->Artifact().IsTree()) {
                if (std::mismatch(treepath.begin(), treepath.end(), p.begin())
                        .first == treepath.end()) {
                    return path;
                }
            }
        }
    }
    return std::nullopt;
}

auto BuildMaps::Target::Utils::getTainted(
    std::set<std::string>* tainted,
    const Configuration& config,
    const ExpressionPtr& tainted_exp,
    const BuildMaps::Target::TargetMap::LoggerPtr& logger) -> bool {
    if (not tainted_exp) {
        return false;
    }
    auto tainted_val =
        tainted_exp.Evaluate(config, {}, [logger](auto const& msg) {
            (*logger)(fmt::format("While evaluating tainted:\n{}", msg), true);
        });
    if (not tainted_val) {
        return false;
    }
    if (not tainted_val->IsList()) {
        (*logger)(fmt::format("tainted should evaluate to a list of strings, "
                              "but got {}",
                              tainted_val->ToString()),
                  true);
        return false;
    }
    for (auto const& entry : tainted_val->List()) {
        if (not entry->IsString()) {
            (*logger)(fmt::format("tainted should evaluate to a list of "
                                  "strings, but got {}",
                                  tainted_val->ToString()),
                      true);
            return false;
        }
        tainted->insert(entry->String());
    }
    return true;
}

namespace {
auto hash_vector(std::vector<std::string> const& vec) -> std::string {
    auto hasher = HashGenerator{BuildMaps::Target::Utils::kActionHash}
                      .IncrementalHasher();
    for (auto const& s : vec) {
        hasher.Update(HashGenerator{BuildMaps::Target::Utils::kActionHash}
                          .Run(s)
                          .Bytes());
    }
    auto digest = std::move(hasher).Finalize();
    if (not digest) {
        Logger::Log(LogLevel::Error, "Failed to finalize hash.");
        std::terminate();
    }
    return digest->Bytes();
}
}  // namespace

auto BuildMaps::Target::Utils::createAction(
    ActionDescription::outputs_t output_files,
    ActionDescription::outputs_t output_dirs,
    std::vector<std::string> command,
    const ExpressionPtr& env,
    std::optional<std::string> may_fail,
    bool no_cache,
    const ExpressionPtr& inputs_exp) -> ActionDescription {
    auto hasher = HashGenerator{BuildMaps::Target::Utils::kActionHash}
                      .IncrementalHasher();
    hasher.Update(hash_vector(output_files));
    hasher.Update(hash_vector(output_dirs));
    hasher.Update(hash_vector(command));
    hasher.Update(env->ToHash());
    hasher.Update(hash_vector(may_fail ? std::vector<std::string>{*may_fail}
                                       : std::vector<std::string>{}));
    hasher.Update(no_cache ? std::string{"N"} : std::string{"Y"});
    hasher.Update(inputs_exp->ToHash());

    auto digest = std::move(hasher).Finalize();
    if (not digest) {
        Logger::Log(LogLevel::Error, "Failed to finalize hash.");
        std::terminate();
    }
    auto action_id = digest->HexString();

    std::map<std::string, std::string> env_vars{};
    for (auto const& [env_var, env_value] : env->Map()) {
        env_vars.emplace(env_var, env_value->String());
    }
    ActionDescription::inputs_t inputs;
    inputs.reserve(inputs_exp->Map().size());
    for (auto const& [input_path, artifact] : inputs_exp->Map()) {
        inputs.emplace(input_path, artifact->Artifact());
    }
    return ActionDescription{std::move(output_files),
                             std::move(output_dirs),
                             Action{std::move(action_id),
                                    std::move(command),
                                    std::move(env_vars),
                                    std::move(may_fail),
                                    no_cache},
                             std::move(inputs)};
}
