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

    } catch (...) {
    }
    return std::nullopt;
}

template <typename T>
// IsList(list) == true and Size(list) >= 3
// list[0] == kFileLocationMarker
[[nodiscard]] inline auto ParseEntityNameFile(
    T const& list,
    EntityName const& current,
    std::optional<std::function<void(std::string const&)>> logger =
        std::nullopt) noexcept -> std::optional<EntityName> {
    try {
        if (IsString(list[2])) {
            const auto& name = GetString(list[2]);
            const auto& x = current.GetNamedTarget();
            if (IsNone(list[1])) {
                return EntityName{
                    x.repository, x.module, name, ReferenceType::kFile};
            }
            if (IsString(list[1])) {
                const auto& middle = GetString(list[1]);
                if (middle == "." or middle == x.module) {
                    return EntityName{
                        x.repository, x.module, name, ReferenceType::kFile};
                }
            }
            if (logger) {
                (*logger)(
                    fmt::format("Invalid module name {} for file reference",
                                ToString(list[1])));
            }
        }
    } catch (...) {
    }
    return std::nullopt;
}
template <typename T>
// IsList(list) == true and Size(list) >= 3
// list[0] == kRelativeLocationMarker
[[nodiscard]] inline auto ParseEntityNameRelative(
    T const& list,
    EntityName const& current,
    std::optional<std::function<void(std::string const&)>> logger =
        std::nullopt) noexcept -> std::optional<EntityName> {
    try {
        if (IsString(list[1]) and IsString(list[2])) {
            const auto& relmodule = GetString(list[1]);
            const auto& name = GetString(list[2]);

            std::filesystem::path m{current.GetNamedTarget().module};
            const auto& module = (m / relmodule).lexically_normal().string();
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
    } catch (...) {
    }
    return std::nullopt;
}

template <typename T>
// IsList(list) == true and Size(list) >= 3
// list[0] == EntityName::kLocationMarker
[[nodiscard]] inline auto ParseEntityNameLocation(
    T const& list,
    EntityName const& current,
    std::optional<std::function<void(std::string const&)>> logger =
        std::nullopt) noexcept -> std::optional<EntityName> {
    try {
        if (IsString(list[1]) and IsString(list[2]) and IsString(list[3])) {
            const auto& local_repo_name = GetString(list[1]);
            const auto& module = GetString(list[2]);
            const auto& target = GetString(list[3]);
            auto const* repo_name = RepositoryConfig::Instance().GlobalName(
                current.GetNamedTarget().repository, local_repo_name);
            if (repo_name != nullptr) {
                return EntityName{*repo_name, module, target};
            }
            if (logger) {
                (*logger)(fmt::format("Cannot resolve repository name {}",
                                      local_repo_name));
            }
        }

    } catch (...) {
    }
    return std::nullopt;
}

template <typename T>
// IsList(list) == true and Size(list) >= 3
[[nodiscard]] inline auto ParseEntityName_3(
    T const& list,
    EntityName const& current,
    std::optional<std::function<void(std::string const&)>> logger =
        std::nullopt) noexcept -> std::optional<EntityName> {
    try {
        // the first entry of the list must be a string
        if (IsString(list[0])) {
            const auto& s0 = GetString(list[0]);
            if (s0 == EntityName::kFileLocationMarker) {
                return ParseEntityNameFile(list, current, logger);
            }
            if (s0 == EntityName::kRelativeLocationMarker) {
                return ParseEntityNameRelative(list, current, logger);
            }
            if (s0 == EntityName::kLocationMarker) {
                return ParseEntityNameLocation(list, current, logger);
            }
            if (s0 == EntityName::kAnonymousMarker and logger) {
                (*logger)(fmt::format(
                    "Parsing anonymous target is not "
                    "supported. Identifiers of anonymous targets should be "
                    "obtained as FIELD value of anonymous fields"));
            }
        }
    } catch (...) {
    }
    return std::nullopt;
}

template <typename T>
[[nodiscard]] inline auto ParseEntityName(
    T const& source,
    EntityName const& current,
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
                res = ParseEntityName_3(list, current, logger);
            }
        }
        if (logger and (res == std::nullopt)) {
            (*logger)(fmt::format("Syntactically invalid entity name: {}.",
                                  ToString(source)));
        }
        return res;
    } catch (...) {
    }
    return std::nullopt;
}

[[nodiscard]] inline auto ParseEntityNameFromJson(
    nlohmann::json const& json,
    EntityName const& current,
    std::optional<std::function<void(std::string const&)>> logger =
        std::nullopt) noexcept -> std::optional<EntityName> {
    return ParseEntityName(json, current, std::move(logger));
}

[[nodiscard]] inline auto ParseEntityNameFromExpression(
    ExpressionPtr const& expr,
    EntityName const& current,
    std::optional<std::function<void(std::string const&)>> logger =
        std::nullopt) noexcept -> std::optional<EntityName> {
    return ParseEntityName(expr, current, std::move(logger));
}

}  // namespace BuildMaps::Base

#endif
