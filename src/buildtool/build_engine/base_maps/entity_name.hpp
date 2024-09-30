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

#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_ENTITY_NAME_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_ENTITY_NAME_HPP

#include <cstddef>
#include <filesystem>
#include <optional>
#include <utility>

#include "gsl/gsl"
#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name_data.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/utils/cpp/hash_combine.hpp"

namespace BuildMaps::Base {

static inline auto IsString(const nlohmann::json& x) noexcept -> bool {
    return x.is_string();
}

static inline auto IsString(const ExpressionPtr& x) noexcept -> bool {
    return x->IsString();
}

// for compatibility with ExpressionPtr
static inline auto GetList(const nlohmann::json& x) noexcept
    -> nlohmann::json const& {
    return x;
}

static inline auto GetList(const ExpressionPtr& x) noexcept
    -> Expression::list_t const& {
    return x->Value<Expression::list_t>()->get();
}

static inline auto IsList(const nlohmann::json& x) noexcept -> bool {
    return x.is_array();
}

static inline auto IsList(const ExpressionPtr& x) noexcept -> bool {
    return x->IsList();
}

template <typename T>
auto Size(const T& x) noexcept -> std::size_t {
    return x.size();
}

static inline auto GetString(const nlohmann::json& x) -> std::string {
    return x.template get<std::string>();
}

static inline auto GetString(const ExpressionPtr& x) -> const std::string& {
    return x->String();
}

static inline auto ToString(const nlohmann::json& x) -> std::string {
    return x.dump();
}

static inline auto ToString(const ExpressionPtr& x) noexcept -> std::string {
    return x->ToString();
}

static inline auto IsNone(const nlohmann::json& x) noexcept -> bool {
    return x.is_null();
}

static inline auto IsNone(const ExpressionPtr& x) noexcept -> bool {
    return x->IsNone();
}

template <typename T>
// IsList(list) == true and Size(list) == 2
[[nodiscard]] inline auto ParseEntityName_2(T const& list,
                                            EntityName const& current) noexcept
    -> std::optional<EntityName> {
    try {
        if (IsString(list[0]) and IsString(list[1])) {
            return EntityName{current.GetNamedTarget().repository,
                              GetString(list[0]),
                              GetString(list[1])};
        }
        return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

template <typename T>
// IsList(list) == true
[[nodiscard]] inline auto ParseEntityNameFSReference(
    std::string const& s0,  // list[0]
    T const& list,
    std::size_t const list_size,
    EntityName const& current,
    std::optional<std::function<void(std::string const&)>> logger =
        std::nullopt) noexcept -> std::optional<EntityName> {
    try {
        auto get_ref_type = [](std::string const& s) -> ReferenceType {
            if (s == EntityName::kFileLocationMarker) {
                return ReferenceType::kFile;
            }
            if (s == EntityName::kGlobMarker) {
                return ReferenceType::kGlob;
            }
            if (s == EntityName::kSymlinkLocationMarker) {
                return ReferenceType::kSymlink;
            }
            return ReferenceType::kTree;
        };
        auto const ref_type = get_ref_type(s0);
        if (list_size == 3) {
            if (IsString(list[2])) {
                auto const& name = GetString(list[2]);
                auto const& x = current.GetNamedTarget();
                if (IsNone(list[1])) {
                    return EntityName{x.repository, x.module, name, ref_type};
                }
                if (IsString(list[1])) {
                    auto const& middle = GetString(list[1]);
                    if (middle == "." or middle == x.module) {
                        return EntityName{
                            x.repository, x.module, name, ref_type};
                    }
                }
                if (logger) {
                    (*logger)(
                        fmt::format("Invalid module name {} for file reference",
                                    ToString(list[1])));
                }
            }
        }
        return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}
template <typename T>
// IsList(list) == true
// list[0] == kRelativeLocationMarker
[[nodiscard]] inline auto ParseEntityNameRelative(
    T const& list,
    std::size_t const list_size,
    EntityName const& current,
    std::optional<std::function<void(std::string const&)>> logger =
        std::nullopt) noexcept -> std::optional<EntityName> {
    try {
        if (list_size == 3 and IsString(list[1]) and IsString(list[2])) {
            auto const& relmodule = GetString(list[1]);
            auto const& name = GetString(list[2]);

            std::filesystem::path m{current.GetNamedTarget().module};
            auto const& module = (m / relmodule).lexically_normal().string();
            if (module.compare(0, 3, "../") != 0) {
                return EntityName{
                    current.GetNamedTarget().repository, module, name};
            }
            if (logger) {
                (*logger)(fmt::format(
                    "Relative module name {} is outside of workspace",
                    relmodule));
            }
        }
        return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

template <typename T>
// IsList(list) == true
// list[0] == EntityName::kLocationMarker
[[nodiscard]] inline auto ParseEntityNameLocation(
    T const& list,
    std::size_t const list_size,
    EntityName const& current,
    gsl::not_null<const RepositoryConfig*> const& repo_config,
    std::optional<std::function<void(std::string const&)>> logger =
        std::nullopt) noexcept -> std::optional<EntityName> {
    try {
        if (list_size == 4 and IsString(list[1]) and IsString(list[2]) and
            IsString(list[3])) {
            auto const& local_repo_name = GetString(list[1]);
            auto const& module = GetString(list[2]);
            auto const& target = GetString(list[3]);
            auto const* repo_name = repo_config->GlobalName(
                current.GetNamedTarget().repository, local_repo_name);
            if (repo_name != nullptr) {
                return EntityName{*repo_name, module, target};
            }
            if (logger) {
                (*logger)(fmt::format("Cannot resolve repository name {}",
                                      local_repo_name));
            }
        }
        return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

template <typename T>
// IsList(list) == true and Size(list) >= 3
[[nodiscard]] inline auto ParseEntityName_3(
    T const& list,
    std::size_t const list_size,
    EntityName const& current,
    gsl::not_null<const RepositoryConfig*> const& repo_config,
    std::optional<std::function<void(std::string const&)>> logger =
        std::nullopt) noexcept -> std::optional<EntityName> {
    try {
        // the first entry of the list must be a string
        if (IsString(list[0])) {
            auto const& s0 = GetString(list[0]);
            if (s0 == EntityName::kRelativeLocationMarker) {
                return ParseEntityNameRelative(
                    list, list_size, current, logger);
            }
            if (s0 == EntityName::kLocationMarker) {
                return ParseEntityNameLocation(
                    list, list_size, current, repo_config, logger);
            }
            if (s0 == EntityName::kAnonymousMarker and logger) {
                (*logger)(fmt::format(
                    "Parsing anonymous target is not "
                    "supported. Identifiers of anonymous targets should be "
                    "obtained as FIELD value of anonymous fields"));
            }
            else if (s0 == EntityName::kFileLocationMarker or
                     s0 == EntityName::kTreeLocationMarker or
                     s0 == EntityName::kGlobMarker or
                     s0 == EntityName::kSymlinkLocationMarker) {
                return ParseEntityNameFSReference(
                    s0, list, list_size, current, logger);
            }
        }
        return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

template <typename T>
[[nodiscard]] inline auto ParseEntityName(
    T const& source,
    EntityName const& current,
    gsl::not_null<const RepositoryConfig*> const& repo_config,
    std::optional<std::function<void(std::string const&)>> logger =
        std::nullopt) noexcept -> std::optional<EntityName> {
    try {
        std::optional<EntityName> res = std::nullopt;
        if (IsString(source)) {
            const auto& x = current.GetNamedTarget();
            return EntityName{x.repository, x.module, GetString(source)};
        }
        if (IsList(source)) {
            auto const& list = GetList(source);
            const auto list_size = Size(list);
            if (list_size == 2) {
                res = ParseEntityName_2(list, current);
            }
            else if (list_size >= 3) {
                res = ParseEntityName_3(
                    list, list_size, current, repo_config, logger);
            }
        }
        if (logger and (res == std::nullopt)) {
            (*logger)(fmt::format("Syntactically invalid entity name: {}.",
                                  ToString(source)));
        }
        return res;
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] inline auto ParseEntityNameFromJson(
    nlohmann::json const& json,
    EntityName const& current,
    gsl::not_null<const RepositoryConfig*> const& repo_config,
    std::optional<std::function<void(std::string const&)>> logger =
        std::nullopt) noexcept -> std::optional<EntityName> {
    return ParseEntityName(json, current, repo_config, std::move(logger));
}

[[nodiscard]] inline auto ParseEntityNameFromExpression(
    ExpressionPtr const& expr,
    EntityName const& current,
    gsl::not_null<const RepositoryConfig*> const& repo_config,
    std::optional<std::function<void(std::string const&)>> logger =
        std::nullopt) noexcept -> std::optional<EntityName> {
    return ParseEntityName(expr, current, repo_config, std::move(logger));
}

}  // namespace BuildMaps::Base

#endif  // INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_ENTITY_NAME_HPP
