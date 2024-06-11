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

#ifndef INCLUDED_SRC_BUILDTOOL_MAIN_DESCRIBE_HPP
#define INCLUDED_SRC_BUILDTOOL_MAIN_DESCRIBE_HPP

#include <cstddef>

#include "gsl/gsl"
#include "src/buildtool/build_engine/base_maps/entity_name.hpp"
#include "src/buildtool/build_engine/target_map/configured_target.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/serve_api/remote/serve_api.hpp"

[[nodiscard]] auto DescribeTarget(
    BuildMaps::Target::ConfiguredTarget const& id,
    gsl::not_null<const RepositoryConfig*> const& repo_config,
    std::optional<gsl::not_null<const ServeApi*>> const& serve,
    std::size_t jobs,
    bool print_json) -> int;

[[nodiscard]] auto DescribeUserDefinedRule(
    BuildMaps::Base::EntityName const& rule_name,
    gsl::not_null<const RepositoryConfig*> const& repo_config,
    std::size_t jobs,
    bool print_json) -> int;

#endif  // INCLUDED_SRC_BUILDTOOL_MAIN_DESCRIBE_HPP
