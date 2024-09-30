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

#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_USER_RULE_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_USER_RULE_HPP

#include <algorithm>
#include <cstddef>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>  // std::move
#include <vector>

#include "fmt/core.h"
#include "gsl/gsl"
#include "src/buildtool/build_engine/base_maps/entity_name.hpp"
#include "src/buildtool/build_engine/base_maps/expression_function.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/utils/cpp/concepts.hpp"
#include "src/utils/cpp/gsl.hpp"

namespace BuildMaps::Base {

// Get duplicates from containers.
// NOTE: Requires all input containers to be sorted!
// kTriangular=true  Performs triangular compare, everyone with everyone.
// kTriangular=false Performs linear compare, first with each of the rest.
template <bool kTriangular,
          InputIterableContainer T_Container,
          InputIterableContainer... T_Rest,
          OutputIterableContainer T_Result =
              std::unordered_set<typename T_Container::value_type>>
[[nodiscard]] static inline auto GetDuplicates(T_Container const& first,
                                               T_Rest const&... rest)
    -> T_Result;

template <InputIterableStringContainer T_Container>
[[nodiscard]] static inline auto JoinContainer(T_Container const& c,
                                               std::string const& sep)
    -> std::string;

class UserRule {
  public:
    using Ptr = std::shared_ptr<UserRule>;
    using implicit_t = std::unordered_map<std::string, std::vector<EntityName>>;
    using implicit_exp_t = std::unordered_map<std::string, ExpressionPtr>;
    using config_trans_t =
        std::unordered_map<std::string, ExpressionFunctionPtr>;

    struct AnonymousDefinition {
        std::string target;
        std::string provider;
        ExpressionPtr rule_map;
    };
    using anonymous_defs_t =
        std::unordered_map<std::string, AnonymousDefinition>;

    [[nodiscard]] static auto Create(
        std::vector<std::string> target_fields,
        std::vector<std::string> string_fields,
        std::vector<std::string> config_fields,
        implicit_t const& implicit_targets,
        anonymous_defs_t anonymous_defs,
        std::vector<std::string> const& config_vars,
        std::vector<std::string> const& tainted,
        config_trans_t config_transitions,
        ExpressionFunctionPtr const& expr,
        std::function<void(std::string const&)> const& logger) -> Ptr {

        auto implicit_fields = std::vector<std::string>{};
        implicit_fields.reserve(implicit_targets.size());
        std::transform(implicit_targets.begin(),
                       implicit_targets.end(),
                       std::back_inserter(implicit_fields),
                       [](auto const& el) { return el.first; });
        std::sort(implicit_fields.begin(), implicit_fields.end());

        auto anonymous_fields = std::vector<std::string>{};
        anonymous_fields.reserve(anonymous_defs.size());
        std::transform(anonymous_defs.begin(),
                       anonymous_defs.end(),
                       std::back_inserter(anonymous_fields),
                       [](auto const& el) { return el.first; });
        std::sort(anonymous_fields.begin(), anonymous_fields.end());

        std::sort(target_fields.begin(), target_fields.end());
        std::sort(string_fields.begin(), string_fields.end());
        std::sort(config_fields.begin(), config_fields.end());

        auto dups = GetDuplicates</*kTriangular=*/false>(kReservedKeywords,
                                                         target_fields,
                                                         string_fields,
                                                         config_fields,
                                                         implicit_fields,
                                                         anonymous_fields);
        if (not dups.empty()) {
            logger(
                fmt::format("User-defined fields cannot be any of the reserved "
                            "fields [{}]",
                            JoinContainer(kReservedKeywords, ",")));
            return nullptr;
        }

        dups = GetDuplicates</*kTriangular=*/true>(target_fields,
                                                   string_fields,
                                                   config_fields,
                                                   implicit_fields,
                                                   anonymous_fields);

        if (not dups.empty()) {
            logger(
                fmt::format("A field can have only one type, but the following "
                            "have more: [{}]",
                            JoinContainer(dups, ",")));
            return nullptr;
        }

        auto transition_targets = std::vector<std::string>{};
        transition_targets.reserve(config_transitions.size());
        std::transform(config_transitions.begin(),
                       config_transitions.end(),
                       std::back_inserter(transition_targets),
                       [](auto const& el) { return el.first; });
        std::sort(transition_targets.begin(), transition_targets.end());

        dups = GetDuplicates</*kTriangular=*/false>(transition_targets,
                                                    target_fields,
                                                    implicit_fields,
                                                    anonymous_fields);
        if (dups != decltype(dups){transition_targets.begin(),
                                   transition_targets.end()}) {
            logger(
                fmt::format("Config transitions has to be a map from target "
                            "fields to transition expressions, but found [{}]",
                            JoinContainer(transition_targets, ",")));
            return nullptr;
        }

        auto const setter = [&config_transitions](auto const field) {
            config_transitions.emplace(
                field,
                ExpressionFunction::kEmptyTransition);  // wont overwrite
        };
        config_transitions.reserve(target_fields.size() +
                                   implicit_fields.size() +
                                   anonymous_fields.size());
        std::for_each(target_fields.begin(), target_fields.end(), setter);
        std::for_each(implicit_fields.begin(), implicit_fields.end(), setter);
        std::for_each(anonymous_fields.begin(), anonymous_fields.end(), setter);

        implicit_exp_t implicit_target_exp;
        implicit_target_exp.reserve(implicit_targets.size());
        for (auto const& [target_name, target_entity_vec] : implicit_targets) {
            std::vector<ExpressionPtr> target_exps;
            target_exps.reserve(target_entity_vec.size());
            for (auto const& target_entity : target_entity_vec) {
                target_exps.emplace_back(target_entity);
            }
            implicit_target_exp.emplace(target_name, target_exps);
        }

        return std::make_shared<UserRule>(
            std::move(target_fields),
            std::move(string_fields),
            std::move(config_fields),
            implicit_targets,
            std::move(implicit_target_exp),
            std::move(anonymous_defs),
            config_vars,
            std::set<std::string>{tainted.begin(), tainted.end()},
            std::move(config_transitions),
            expr);
    }

    UserRule(std::vector<std::string> target_fields,
             std::vector<std::string> string_fields,
             std::vector<std::string> config_fields,
             implicit_t implicit_targets,
             implicit_exp_t implicit_target_exp,
             anonymous_defs_t anonymous_defs,
             std::vector<std::string> config_vars,
             std::set<std::string> tainted,
             config_trans_t config_transitions,
             ExpressionFunctionPtr expr) noexcept
        : target_fields_{std::move(target_fields)},
          string_fields_{std::move(string_fields)},
          config_fields_{std::move(config_fields)},
          implicit_targets_{std::move(implicit_targets)},
          implicit_target_exp_{std::move(implicit_target_exp)},
          anonymous_defs_{std::move(anonymous_defs)},
          config_vars_{std::move(config_vars)},
          tainted_{std::move(tainted)},
          config_transitions_{std::move(config_transitions)},
          expr_{std::move(expr)} {}

    [[nodiscard]] auto TargetFields() const& noexcept
        -> std::vector<std::string> const& {
        return target_fields_;
    }

    [[nodiscard]] auto TargetFields() && noexcept -> std::vector<std::string> {
        return std::move(target_fields_);
    }

    [[nodiscard]] auto StringFields() const& noexcept
        -> std::vector<std::string> const& {
        return string_fields_;
    }

    [[nodiscard]] auto StringFields() && noexcept -> std::vector<std::string> {
        return std::move(string_fields_);
    }

    [[nodiscard]] auto ConfigFields() const& noexcept
        -> std::vector<std::string> const& {
        return config_fields_;
    }

    [[nodiscard]] auto ConfigFields() && noexcept -> std::vector<std::string> {
        return std::move(config_fields_);
    }

    [[nodiscard]] auto ImplicitTargets() const& noexcept -> implicit_t const& {
        return implicit_targets_;
    }

    [[nodiscard]] auto ImplicitTargets() && noexcept -> implicit_t {
        return std::move(implicit_targets_);
    }

    [[nodiscard]] auto ImplicitTargetExps() const& noexcept
        -> implicit_exp_t const& {
        return implicit_target_exp_;
    }

    [[nodiscard]] auto ExpectedFields() const& noexcept
        -> std::unordered_set<std::string> const& {
        return expected_entries_;
    }

    [[nodiscard]] auto ConfigVars() const& noexcept
        -> std::vector<std::string> const& {
        return config_vars_;
    }

    [[nodiscard]] auto ConfigVars() && noexcept -> std::vector<std::string> {
        return std::move(config_vars_);
    }

    [[nodiscard]] auto Tainted() const& noexcept
        -> std::set<std::string> const& {
        return tainted_;
    }

    [[nodiscard]] auto Tainted() && noexcept -> std::set<std::string> {
        return std::move(tainted_);
    }

    [[nodiscard]] auto ConfigTransitions() const& noexcept
        -> config_trans_t const& {
        return config_transitions_;
    }

    [[nodiscard]] auto ConfigTransitions() && noexcept -> config_trans_t {
        return std::move(config_transitions_);
    }

    [[nodiscard]] auto Expression() const& noexcept
        -> ExpressionFunctionPtr const& {
        return expr_;
    }

    [[nodiscard]] auto Expression() && noexcept -> ExpressionFunctionPtr {
        return std::move(expr_);
    }

    [[nodiscard]] auto AnonymousDefinitions() const& noexcept
        -> anonymous_defs_t {
        return anonymous_defs_;
    }

    [[nodiscard]] auto AnonymousDefinitions() && noexcept -> anonymous_defs_t {
        return std::move(anonymous_defs_);
    }

  private:
    // NOTE: Must be sorted
    static inline std::vector<std::string> const kReservedKeywords{
        "arguments_config",
        "tainted",
        "type"};

    static auto ComputeExpectedEntries(std::vector<std::string> tfields,
                                       std::vector<std::string> sfields,
                                       std::vector<std::string> cfields)
        -> std::unordered_set<std::string> {
        std::size_t n = 0;
        n += tfields.size();
        n += sfields.size();
        n += cfields.size();
        n += kReservedKeywords.size();
        std::unordered_set<std::string> expected_entries{};
        expected_entries.reserve(n);
        expected_entries.insert(tfields.begin(), tfields.end());
        expected_entries.insert(sfields.begin(), sfields.end());
        expected_entries.insert(cfields.begin(), cfields.end());
        expected_entries.insert(kReservedKeywords.begin(),
                                kReservedKeywords.end());
        return expected_entries;
    }

    std::vector<std::string> target_fields_;
    std::vector<std::string> string_fields_;
    std::vector<std::string> config_fields_;
    implicit_t implicit_targets_;
    implicit_exp_t implicit_target_exp_;
    anonymous_defs_t anonymous_defs_;
    std::vector<std::string> config_vars_;
    std::set<std::string> tainted_;
    config_trans_t config_transitions_;
    ExpressionFunctionPtr expr_;
    std::unordered_set<std::string> expected_entries_{
        ComputeExpectedEntries(target_fields_, string_fields_, config_fields_)};
};

using UserRulePtr = UserRule::Ptr;

namespace detail {

template <HasSize T_Container, HasSize... T_Rest>
[[nodiscard]] static inline auto MaxSize(T_Container const& first,
                                         T_Rest const&... rest) -> std::size_t {
    if constexpr (sizeof...(rest) > 0) {
        return std::max(first.size(), MaxSize(rest...));
    }
    return first.size();
}

template <bool kTriangular,
          OutputIterableContainer T_Result,
          InputIterableContainer T_First,
          InputIterableContainer T_Second,
          InputIterableContainer... T_Rest>
static auto inline FindDuplicates(gsl::not_null<T_Result*> const& dups,
                                  T_First const& first,
                                  T_Second const& second,
                                  T_Rest const&... rest) -> void {
    ExpectsAudit(std::is_sorted(first.begin(), first.end()) and
                 std::is_sorted(second.begin(), second.end()));
    std::set_intersection(first.begin(),
                          first.end(),
                          second.begin(),
                          second.end(),
                          std::inserter(*dups, dups->begin()));
    if constexpr (sizeof...(rest) > 0) {
        // n comparisons with rest: first<->rest[0], ..., first<->rest[n]
        FindDuplicates</*kTriangular=*/false>(dups, first, rest...);
        if constexpr (kTriangular) {
            // do triangular compare of second with rest

            // NOLINTNEXTLINE(readability-suspicious-call-argument)
            FindDuplicates</*kTriangular=*/true>(dups, second, rest...);
        }
    }
}

}  // namespace detail

template <bool kTriangular,
          InputIterableContainer T_Container,
          InputIterableContainer... T_Rest,
          OutputIterableContainer T_Result>
[[nodiscard]] static inline auto GetDuplicates(T_Container const& first,
                                               T_Rest const&... rest)
    -> T_Result {
    auto dups = T_Result{};
    constexpr auto kNumContainers = 1 + sizeof...(rest);
    if constexpr (kNumContainers > 1) {
        std::size_t size{};
        if constexpr (kTriangular) {
            // worst case if all containers are of the same size
            size = kNumContainers * detail::MaxSize(first, rest...) / 2;
        }
        else {
            size = std::min(first.size(), detail::MaxSize(rest...));
        }
        dups.reserve(size);
        detail::FindDuplicates<kTriangular, T_Result>(&dups, first, rest...);
    }
    return dups;
}

template <InputIterableStringContainer T_Container>
[[nodiscard]] static inline auto JoinContainer(T_Container const& c,
                                               std::string const& sep)
    -> std::string {
    std::ostringstream oss{};
    std::size_t insert_sep{};
    for (auto const& i : c) {
        oss << (insert_sep++ ? sep.c_str() : "");
        oss << i;
    }
    return oss.str();
};

}  // namespace BuildMaps::Base

#endif  // INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_USER_RULE_HPP
