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

#include "src/buildtool/build_engine/target_map/utils.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <unordered_set>
#include <utility>  // std::move
#include <vector>

#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/crypto/hasher.hpp"
#include "src/utils/cpp/path.hpp"
#include "src/utils/cpp/path_hash.hpp"

auto BuildMaps::Target::Utils::obtainTargetByName(
    const SubExprEvaluator& eval,
    const ExpressionPtr& expr,
    const Configuration& env,
    const Base::EntityName& current,
    const gsl::not_null<const RepositoryConfig*>& repo_config,
    std::unordered_map<BuildMaps::Target::ConfiguredTarget,
                       AnalysedTargetPtr> const& deps_by_transition)
    -> AnalysedTargetPtr {
    auto const& empty_map_exp = Expression::kEmptyMapExpr;
    auto reference = eval(expr["dep"], env);
    std::string error{};
    auto target = BuildMaps::Base::ParseEntityNameFromExpression(
        reference,
        current,
        repo_config,
        [&error](std::string const& parse_err) { error = parse_err; });
    if (not target) {
        throw Evaluator::EvaluationError{
            fmt::format("Parsing target name {} failed with:\n{}",
                        reference->ToString(),
                        error)};
    }
    auto transition = eval(expr->Get("transition", empty_map_exp), env);
    auto it = deps_by_transition.find(BuildMaps::Target::ConfiguredTarget{
        .target = *target, .config = Configuration{transition}});
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
        .target = reference->Name(), .config = Configuration{transition}});
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

auto BuildMaps::Target::Utils::artifacts_tree(const ExpressionPtr& map)
    -> std::variant<std::string, ExpressionPtr> {
    auto result = Expression::map_t::underlying_map_t{};
    for (auto const& [key, artifact] : map->Map()) {
        auto location = ToNormalPath(std::filesystem::path{key}).string();
        if (auto it = result.find(location);
            it != result.end() && !(it->second == artifact)) {
            return location;
        }
        result.emplace(std::move(location), artifact);
    }
    return ExpressionPtr{Expression::map_t{result}};
}

auto BuildMaps::Target::Utils::tree_conflict(const ExpressionPtr& map)
    -> std::optional<std::string> {
    // Work around the fact that std::hash<std::filesystem::path> is missing
    // in some libraries
    struct PathHash {
        auto operator()(std::filesystem::path const& p) const noexcept
            -> std::size_t {
            return std::hash<std::filesystem::path>{}(p);
        }
    };
    std::unordered_set<std::filesystem::path, PathHash> blocked{};
    blocked.reserve(map->Map().size());

    for (auto const& [path, artifact] : map->Map()) {
        if (path == "." and map->Map().size() > 1) {
            return ".";
        }
        auto p = std::filesystem::path{path};
        if (p.is_absolute()) {
            return p.string();
        }
        if (*p.begin() == "..") {
            return p.string();
        }
        auto insert_result = blocked.insert(p);
        if (not insert_result.second) {
            return p.string();  // duplicate path
        }
        for (p = p.parent_path(); not p.empty(); p = p.parent_path()) {
            if (blocked.contains(p)) {
                // Another artifact at a parent path position
                return p.string();
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
auto hash_vector(HashFunction hash_function,
                 std::vector<std::string> const& vec) -> std::string {
    auto hasher = hash_function.MakeHasher();
    for (auto const& s : vec) {
        hasher.Update(hash_function.PlainHashData(s).Bytes());
    }
    return std::move(hasher).Finalize().Bytes();
}
}  // namespace

auto BuildMaps::Target::Utils::createAction(
    const ActionDescription::outputs_t& output_files,
    const ActionDescription::outputs_t& output_dirs,
    std::vector<std::string> command,
    const ExpressionPtr& env,
    std::optional<std::string> may_fail,
    bool no_cache,
    double timeout_scale,
    const ExpressionPtr& execution_properties_exp,
    const ExpressionPtr& inputs_exp) -> ActionDescription::Ptr {
    // The type of HashFunction is irrelevant here. It is used for
    // identification and quick comparison of descriptions. SHA256 is used.
    HashFunction hash_function{HashFunction::Type::PlainSHA256};
    auto hasher = hash_function.MakeHasher();

    hasher.Update(hash_vector(hash_function, output_files));
    hasher.Update(hash_vector(hash_function, output_dirs));
    hasher.Update(hash_vector(hash_function, command));
    hasher.Update(env->ToHash());
    hasher.Update(hash_vector(hash_function,
                              may_fail ? std::vector<std::string>{*may_fail}
                                       : std::vector<std::string>{}));
    hasher.Update(no_cache ? std::string{"N"} : std::string{"Y"});
    hasher.Update(fmt::format("{:+24a}", timeout_scale));
    hasher.Update(execution_properties_exp->ToHash());
    hasher.Update(inputs_exp->ToHash());

    auto action_id = std::move(hasher).Finalize().HexString();

    std::map<std::string, std::string> env_vars{};
    for (auto const& [env_var, env_value] : env->Map()) {
        env_vars.emplace(env_var, env_value->String());
    }
    std::map<std::string, std::string> execution_properties{};
    for (auto const& [prop_name, prop_value] :
         execution_properties_exp->Map()) {
        execution_properties.emplace(prop_name, prop_value->String());
    }
    ActionDescription::inputs_t inputs;
    inputs.reserve(inputs_exp->Map().size());
    for (auto const& [input_path, artifact] : inputs_exp->Map()) {
        inputs.emplace(input_path, artifact->Artifact());
    }
    return std::make_shared<ActionDescription>(output_files,
                                               output_dirs,
                                               Action{std::move(action_id),
                                                      std::move(command),
                                                      std::move(env_vars),
                                                      std::move(may_fail),
                                                      no_cache,
                                                      timeout_scale,
                                                      execution_properties},
                                               std::move(inputs));
}
