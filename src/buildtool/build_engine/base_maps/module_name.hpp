#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_MODULE_NAME_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_MODULE_NAME_HPP

#include "src/utils/cpp/hash_combine.hpp"

namespace BuildMaps::Base {

struct ModuleName {
    std::string repository{};
    std::string module{};

    ModuleName(std::string repository, std::string module)
        : repository{std::move(repository)}, module{std::move(module)} {}

    [[nodiscard]] auto operator==(ModuleName const& other) const noexcept
        -> bool {
        return module == other.module && repository == other.repository;
    }
};
}  // namespace BuildMaps::Base

namespace std {
template <>
struct hash<BuildMaps::Base::ModuleName> {
    [[nodiscard]] auto operator()(
        const BuildMaps::Base::ModuleName& t) const noexcept -> std::size_t {
        size_t seed{};
        hash_combine<std::string>(&seed, t.repository);
        hash_combine<std::string>(&seed, t.module);
        return seed;
    }
};

}  // namespace std

#endif
