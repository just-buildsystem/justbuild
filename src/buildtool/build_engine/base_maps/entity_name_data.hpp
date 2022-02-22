#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_ENTITY_NAME_DATA_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_ENTITY_NAME_DATA_HPP

#include <filesystem>
#include <optional>
#include <utility>

#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/base_maps/module_name.hpp"
#include "src/buildtool/build_engine/expression/expression_ptr.hpp"
#include "src/utils/cpp/hash_combine.hpp"
#include "src/utils/cpp/hex_string.hpp"

namespace BuildMaps::Base {

struct AnonymousTarget {
    ExpressionPtr rule_map;
    ExpressionPtr target_node;

    [[nodiscard]] auto operator==(AnonymousTarget const& other) const noexcept
        -> bool {
        return rule_map == other.rule_map && target_node == other.target_node;
    }
};

struct EntityName {
    static constexpr auto kLocationMarker = "@";
    static constexpr auto kFileLocationMarker = "FILE";
    static constexpr auto kRelativeLocationMarker = "./";
    static constexpr auto kAnonymousMarker = "#";

    std::string repository{};
    std::string module{};
    std::string name{};
    std::optional<AnonymousTarget> anonymous{};
    bool explicit_file_reference{};

    EntityName() = default;
    EntityName(std::string repository,
               const std::string& module,
               std::string name)
        : repository{std::move(repository)},
          module{normal_module_name(module)},
          name{std::move(name)} {}
    explicit EntityName(AnonymousTarget anonymous)
        : anonymous{std::move(anonymous)} {}

    static auto normal_module_name(const std::string& module) -> std::string {
        return std::filesystem::path("/" + module + "/")
            .lexically_normal()
            .lexically_relative("/")
            .parent_path()
            .string();
    }

    [[nodiscard]] auto operator==(
        BuildMaps::Base::EntityName const& other) const noexcept -> bool {
        return module == other.module && name == other.name &&
               repository == other.repository && anonymous == other.anonymous &&
               explicit_file_reference == other.explicit_file_reference;
    }

    [[nodiscard]] auto ToJson() const -> nlohmann::json {
        nlohmann::json j;
        if (IsAnonymousTarget()) {
            j.push_back(kAnonymousMarker);
            j.push_back(anonymous->rule_map.ToIdentifier());
            j.push_back(anonymous->target_node.ToIdentifier());
        }
        else {
            j.push_back(kLocationMarker);
            j.push_back(repository);
            if (explicit_file_reference) {
                j.push_back(kFileLocationMarker);
            }
            j.push_back(module);
            j.push_back(name);
        }
        return j;
    }

    [[nodiscard]] auto ToString() const -> std::string {
        return ToJson().dump();
    }

    [[nodiscard]] auto ToModule() const -> ModuleName {
        return ModuleName{repository, module};
    }

    [[nodiscard]] auto IsDefinitionName() const -> bool {
        return (not explicit_file_reference);
    }

    [[nodiscard]] auto IsAnonymousTarget() const -> bool {
        return static_cast<bool>(anonymous);
    }

    EntityName(std::string repository,
               const std::string& module,
               std::string name,
               bool explicit_file_reference)
        : repository{std::move(repository)},
          module{normal_module_name(module)},
          name{std::move(name)},
          explicit_file_reference{explicit_file_reference} {}
};
}  // namespace BuildMaps::Base

namespace std {
template <>
struct hash<BuildMaps::Base::EntityName> {
    [[nodiscard]] auto operator()(
        const BuildMaps::Base::EntityName& t) const noexcept -> std::size_t {
        size_t seed{};
        hash_combine<std::string>(&seed, t.repository);
        hash_combine<std::string>(&seed, t.module);
        hash_combine<std::string>(&seed, t.name);
        auto anonymous =
            t.anonymous.value_or(BuildMaps::Base::AnonymousTarget{});
        hash_combine<ExpressionPtr>(&seed, anonymous.rule_map);
        hash_combine<ExpressionPtr>(&seed, anonymous.target_node);
        hash_combine<bool>(&seed, t.explicit_file_reference);
        return seed;
    }
};

}  // namespace std

#endif
