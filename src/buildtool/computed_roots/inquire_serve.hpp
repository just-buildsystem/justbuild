// Copyright 2025 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDOOL_COMPUTED_ROOTS_INQUIRE_SERVE_HPP
#define INCLUDED_SRC_BUILDOOL_COMPUTED_ROOTS_INQUIRE_SERVE_HPP

#include <optional>
#include <string>

#include "gsl/gsl"
#include "src/buildtool/build_engine/target_map/configured_target.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/main/analyse_context.hpp"

// Inquire serve for a given target and report the artifact stage as git tree
// identifier.
[[nodiscard]] auto InquireServe(
    gsl::not_null<AnalyseContext*> const& analyse_context,
    BuildMaps::Target::ConfiguredTarget const& id,
    gsl::not_null<Logger const*> const& logger) -> std::optional<std::string>;

#endif
