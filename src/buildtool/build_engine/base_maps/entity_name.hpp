#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_ENTITY_NAME_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_ENTITY_NAME_HPP

#include <filesystem>
#include <optional>
#include <utility>

#include "gsl-lite/gsl-lite.hpp"
#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name_data.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/utils/cpp/hash_combine.hpp"

namespace BuildMaps::Base {

[[nodiscard]] inline auto ParseEntityNameFromJson(
    nlohmann::json const& json,
    EntityName const& current,
    std::optional<std::function<void(std::string const&)>> logger =
        std::nullopt) noexcept -> std::optional<EntityName> {
    try {
        if (json.is_string()) {
            return EntityName{current.repository,
                              current.module,
                              json.template get<std::string>()};
        }
        if (json.is_array() and json.size() == 2 and json[0].is_string() and
            json[1].is_string()) {
            return EntityName{current.repository,
                              json[0].template get<std::string>(),
                              json[1].template get<std::string>()};
        }
        if (json.is_array() and json.size() == 3 and json[0].is_string() and
            json[0].template get<std::string>() ==
                EntityName::kFileLocationMarker and
            json[2].is_string()) {
            auto name = json[2].template get<std::string>();
            if (json[1].is_null()) {
                return EntityName{
                    current.repository, current.module, name, true};
            }
            if (json[1].is_string()) {
                auto middle = json[1].template get<std::string>();
                if (middle == "." or middle == current.module) {
                    return EntityName{
                        current.repository, current.module, name, true};
                }
            }
            if (logger) {
                (*logger)(
                    fmt::format("Invalid module name {} for file reference",
                                json[1].dump()));
            }
        }
        else if (json.is_array() and json.size() == 3 and
                 json[0].is_string() and
                 json[0].template get<std::string>() ==
                     EntityName::kRelativeLocationMarker and
                 json[1].is_string() and json[2].is_string()) {
            auto relmodule = json[1].template get<std::string>();
            auto name = json[2].template get<std::string>();

            std::filesystem::path m{current.module};
            auto module = (m / relmodule).lexically_normal().string();
            if (module.compare(0, 3, "../") != 0) {
                return EntityName{current.repository, module, name};
            }
            if (logger) {
                (*logger)(fmt::format(
                    "Relative module name {} is outside of workspace",
                    relmodule));
            }
        }
        else if (json.is_array() and json.size() == 3 and
                 json[0].is_string() and
                 json[0].template get<std::string>() ==
                     EntityName::kAnonymousMarker) {
            if (logger) {
                (*logger)(fmt::format(
                    "Parsing anonymous target from JSON is not supported."));
            }
        }
        else if (json.is_array() and json.size() == 4 and
                 json[0].is_string() and
                 json[0].template get<std::string>() ==
                     EntityName::kLocationMarker and
                 json[1].is_string() and json[2].is_string() and
                 json[3].is_string()) {
            auto local_repo_name = json[1].template get<std::string>();
            auto module = json[2].template get<std::string>();
            auto target = json[3].template get<std::string>();
            auto const* repo_name = RepositoryConfig::Instance().GlobalName(
                current.repository, local_repo_name);
            if (repo_name != nullptr) {
                return EntityName{*repo_name, module, target};
            }
            if (logger) {
                (*logger)(fmt::format("Cannot resolve repository name {}",
                                      local_repo_name));
            }
        }
        else if (logger) {
            (*logger)(fmt::format("Syntactically invalid entity name: {}.",
                                  json.dump()));
        }
    } catch (...) {
    }
    return std::nullopt;
}

[[nodiscard]] inline auto ParseEntityNameFromExpression(
    ExpressionPtr const& expr,
    EntityName const& current,
    std::optional<std::function<void(std::string const&)>> logger =
        std::nullopt) noexcept -> std::optional<EntityName> {
    try {
        if (expr) {
            if (expr->IsString()) {
                return EntityName{current.repository,
                                  current.module,
                                  expr->Value<std::string>()->get()};
            }
            if (expr->IsList()) {
                auto const& list = expr->Value<Expression::list_t>()->get();
                if (list.size() == 2 and list[0]->IsString() and
                    list[1]->IsString()) {
                    return EntityName{current.repository,
                                      list[0]->Value<std::string>()->get(),
                                      list[1]->Value<std::string>()->get()};
                }
                if (list.size() == 3 and list[0]->IsString() and
                    list[0]->String() == EntityName::kFileLocationMarker and
                    list[2]->IsString()) {
                    auto name = list[2]->Value<std::string>()->get();
                    if (list[1]->IsNone()) {
                        return EntityName{
                            current.repository, current.module, name, true};
                    }
                    if (list[1]->IsString() and
                        (list[1]->String() == "." or
                         list[1]->String() == current.module)) {
                        return EntityName{
                            current.repository, current.module, name, true};
                    }
                    if (logger) {
                        (*logger)(fmt::format(
                            "Invalid module name {} for file reference",
                            list[1]->ToString()));
                    }
                }
                else if (list.size() == 3 and list[0]->IsString() and
                         list[0]->String() ==
                             EntityName::kRelativeLocationMarker and
                         list[1]->IsString() and list[2]->IsString()) {
                    std::filesystem::path m{current.module};
                    auto module =
                        (m / (list[1]->String())).lexically_normal().string();
                    if (module.compare(0, 3, "../") != 0) {
                        return EntityName{
                            current.repository, module, list[2]->String()};
                    }
                    if (logger) {
                        (*logger)(fmt::format(
                            "Relative module name {} is outside of workspace",
                            list[1]->String()));
                    }
                }
                else if (list.size() == 3 and list[0]->IsString() and
                         list[0]->String() ==
                             EntityName::kRelativeLocationMarker and
                         list[1]->IsMap() and list[2]->IsNode()) {
                    return EntityName{AnonymousTarget{list[1], list[2]}};
                }
                else if (list.size() == 4 and list[0]->IsString() and
                         list[0]->String() == EntityName::kLocationMarker and
                         list[1]->IsString() and list[2]->IsString() and
                         list[3]->IsString()) {
                    auto const* repo_name =
                        RepositoryConfig::Instance().GlobalName(
                            current.repository, list[1]->String());
                    if (repo_name != nullptr) {
                        return EntityName{
                            *repo_name, list[2]->String(), list[3]->String()};
                    }
                    if (logger) {
                        (*logger)(
                            fmt::format("Cannot resolve repository name {}",
                                        list[1]->String()));
                    }
                }
                else if (logger) {
                    (*logger)(
                        fmt::format("Syntactically invalid entity name: {}.",
                                    expr->ToString()));
                }
            }
        }
    } catch (...) {
    }
    return std::nullopt;
}

}  // namespace BuildMaps::Base

#endif
