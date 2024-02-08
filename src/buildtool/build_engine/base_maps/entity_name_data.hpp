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

#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_ENTITY_NAME_DATA_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_ENTITY_NAME_DATA_HPP

#include <filesystem>
#include <optional>
#include <utility>
#include <variant>

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

enum class ReferenceType : std::int8_t {
    kTarget,
    kFile,
    kTree,
    kGlob,
    kSymlink
};

struct NamedTarget {
    std::string repository{};
    std::string module{};
    std::string name{};
    ReferenceType reference_t{ReferenceType::kTarget};
    NamedTarget() = default;
    NamedTarget(std::string repository,
                std::string const& module,
                std::string name,
                ReferenceType reference_type = ReferenceType::kTarget)
        : repository{std::move(repository)},
          module{normal_module_name(module)},
          name{std::move(name)},
          reference_t{reference_type} {}

    static auto normal_module_name(const std::string& module) -> std::string {
        return std::filesystem::path("/" + module + "/")
            .lexically_normal()
            .lexically_relative("/")
            .parent_path()
            .string();
    }
    [[nodiscard]] auto ToString() const -> std::string;
    [[nodiscard]] friend auto operator==(NamedTarget const& x,
                                         NamedTarget const& y) -> bool {
        return x.repository == y.repository && x.module == y.module &&
               x.name == y.name && x.reference_t == y.reference_t;
    }
    [[nodiscard]] friend auto operator!=(NamedTarget const& x,
                                         NamedTarget const& y) -> bool {
        return not(x == y);
    }
};

class EntityName {
  public:
    using variant_t = std::variant<NamedTarget, AnonymousTarget>;
    static constexpr auto kLocationMarker = "@";
    static constexpr auto kFileLocationMarker = "FILE";
    static constexpr auto kTreeLocationMarker = "TREE";
    static constexpr auto kGlobMarker = "GLOB";
    static constexpr auto kSymlinkLocationMarker = "SYMLINK";
    static constexpr auto kRelativeLocationMarker = "./";
    static constexpr auto kAnonymousMarker = "#";

    EntityName() : EntityName{NamedTarget{}} {}
    explicit EntityName(variant_t x) : entity_name_{std::move(x)} {}
    EntityName(std::string repository,
               const std::string& module,
               std::string name,
               ReferenceType reference_type = ReferenceType::kTarget)
        : EntityName{NamedTarget{std::move(repository),
                                 module,
                                 std::move(name),
                                 reference_type}} {}

    friend auto operator==(EntityName const& a, EntityName const& b) -> bool {
        return a.entity_name_ == b.entity_name_;
    }
    [[nodiscard]] auto IsAnonymousTarget() const -> bool {
        return std::holds_alternative<AnonymousTarget>(entity_name_);
    }
    [[nodiscard]] auto IsNamedTarget() const -> bool {
        return std::holds_alternative<NamedTarget>(entity_name_);
    }
    [[nodiscard]] auto GetAnonymousTarget() -> AnonymousTarget& {
        return std::get<AnonymousTarget>(entity_name_);
    }
    [[nodiscard]] auto GetNamedTarget() -> NamedTarget& {
        return std::get<NamedTarget>(entity_name_);
    }
    [[nodiscard]] auto GetAnonymousTarget() const -> AnonymousTarget const& {
        return std::get<AnonymousTarget>(entity_name_);
    }
    [[nodiscard]] auto GetNamedTarget() const -> NamedTarget const& {
        return std::get<NamedTarget>(entity_name_);
    }

    [[nodiscard]] auto ToJson() const -> nlohmann::json {
        nlohmann::json j;
        if (IsAnonymousTarget()) {
            j.push_back(kAnonymousMarker);
            const auto& x = GetAnonymousTarget();
            j.push_back(x.rule_map.ToIdentifier());
            j.push_back(x.target_node.ToIdentifier());
        }
        else {
            j.push_back(kLocationMarker);
            const auto& x = GetNamedTarget();
            j.push_back(x.repository);
            if (x.reference_t == ReferenceType::kFile) {
                j.push_back(kFileLocationMarker);
            }
            else if (x.reference_t == ReferenceType::kTree) {
                j.push_back(kTreeLocationMarker);
            }
            else if (x.reference_t == ReferenceType::kGlob) {
                j.push_back(kGlobMarker);
            }
            else if (x.reference_t == ReferenceType::kSymlink) {
                j.push_back(kSymlinkLocationMarker);
            }
            j.push_back(x.module);
            j.push_back(x.name);
        }
        return j;
    }

    [[nodiscard]] auto ToString() const -> std::string {
        return ToJson().dump();
    }

    [[nodiscard]] auto ToModule() const -> ModuleName {
        const auto& x = GetNamedTarget();
        return ModuleName{x.repository, x.module};
    }

    [[nodiscard]] auto IsDefinitionName() const -> bool {
        return GetNamedTarget().reference_t == ReferenceType::kTarget;
    }

  private:
    variant_t entity_name_;
};
}  // namespace BuildMaps::Base

namespace std {
template <>
struct hash<BuildMaps::Base::NamedTarget> {
    [[nodiscard]] auto operator()(
        const BuildMaps::Base::NamedTarget& t) const noexcept -> std::size_t {
        size_t seed{};
        hash_combine<std::string>(&seed, t.repository);
        hash_combine<std::string>(&seed, t.module);
        hash_combine<std::string>(&seed, t.name);
        hash_combine<std::int8_t>(&seed,
                                  static_cast<std::int8_t>(t.reference_t));
        return seed;
    }
};
template <>
struct hash<BuildMaps::Base::AnonymousTarget> {
    [[nodiscard]] auto operator()(const BuildMaps::Base::AnonymousTarget& t)
        const noexcept -> std::size_t {
        size_t seed{};
        hash_combine<ExpressionPtr>(&seed, t.rule_map);
        hash_combine<ExpressionPtr>(&seed, t.target_node);
        return seed;
    }
};
template <>
struct hash<BuildMaps::Base::EntityName> {
    [[nodiscard]] auto operator()(
        const BuildMaps::Base::EntityName& t) const noexcept -> std::size_t {
        if (t.IsAnonymousTarget()) {
            return hash<BuildMaps::Base::AnonymousTarget>{}(
                t.GetAnonymousTarget());
        }
        return hash<BuildMaps::Base::NamedTarget>{}(t.GetNamedTarget());
    }
};

}  // namespace std

#endif
