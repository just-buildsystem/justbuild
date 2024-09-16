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

#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_MODULE_NAME_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_MODULE_NAME_HPP

#include <cstddef>
#include <string>
#include <utility>

#include "src/utils/cpp/hash_combine.hpp"

namespace BuildMaps::Base {

struct ModuleName {
    std::string repository{};
    std::string module{};

    ModuleName(std::string repository, std::string module)
        : repository{std::move(repository)}, module{std::move(module)} {}

    [[nodiscard]] auto operator==(ModuleName const& other) const noexcept
        -> bool {
        return module == other.module and repository == other.repository;
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

#endif  // INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_MODULE_NAME_HPP
