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

#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_TARGET_MAP_CONFIGURED_TARGET_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_TARGET_MAP_CONFIGURED_TARGET_HPP

#include <cstddef>
#include <memory>

#include "fmt/core.h"
#include "src/buildtool/build_engine/base_maps/entity_name_data.hpp"
#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/utils/cpp/hash_combine.hpp"
#include "src/utils/cpp/json.hpp"

namespace BuildMaps::Target {

struct ConfiguredTarget {
    BuildMaps::Base::EntityName target;
    Configuration config;

    static constexpr std::size_t kConfigLength = 320;

    [[nodiscard]] auto operator==(BuildMaps::Target::ConfiguredTarget const&
                                      other) const noexcept -> bool {
        return target == other.target and config == other.config;
    }

    [[nodiscard]] auto ToString() const noexcept -> std::string {
        return fmt::format("[{},{}]", target.ToString(), config.ToString());
    }

    [[nodiscard]] auto ToShortString() const noexcept -> std::string {
        return fmt::format(
            "[{},{}]",
            target.ToString(),
            AbbreviateJson(PruneJson(config.ToJson()), kConfigLength));
    }
};

using ConfiguredTargetPtr = std::shared_ptr<ConfiguredTarget>;

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

#endif  // INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_TARGET_MAP_CONFIGURED_TARGET_HPP
