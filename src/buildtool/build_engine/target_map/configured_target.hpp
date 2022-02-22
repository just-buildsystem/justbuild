#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_TARGET_MAP_CONFIGURED_TARGET_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_TARGET_MAP_CONFIGURED_TARGET_HPP

#include "fmt/core.h"
#include "src/buildtool/build_engine/base_maps/entity_name.hpp"
#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/utils/cpp/hash_combine.hpp"

namespace BuildMaps::Target {

struct ConfiguredTarget {
    BuildMaps::Base::EntityName target;
    Configuration config;

    [[nodiscard]] auto operator==(
        BuildMaps::Target::ConfiguredTarget const& other) const noexcept
        -> bool {
        return target == other.target && config == other.config;
    }

    [[nodiscard]] auto ToString() const noexcept -> std::string {
        return fmt::format("[{},{}]", target.ToString(), config.ToString());
    }
};

}  // namespace BuildMaps::Target

namespace std {
template <>
struct hash<BuildMaps::Target::ConfiguredTarget> {
    [[nodiscard]] auto operator()(BuildMaps::Target::ConfiguredTarget const& ct)
        const noexcept -> std::size_t {
        size_t seed{};
        hash_combine<>(&seed, ct.target);
        hash_combine<>(&seed, ct.config);
        return seed;
    }
};
}  // namespace std

#endif
