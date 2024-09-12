// Copyright 2023 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_UTILS_SUBOBJECT_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_UTILS_SUBOBJECT_HPP

#ifndef BOOTSTRAP_BUILD_TOOL

#include <filesystem>
#include <optional>

#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/execution_api/common/api_bundle.hpp"

auto RetrieveSubPathId(Artifact::ObjectInfo object_info,
                       ApiBundle const& apis,
                       const std::filesystem::path& sub_path)
    -> std::optional<Artifact::ObjectInfo>;

#endif  // BOOTSTRAP_BUILD_TOOL

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_UTILS_SUBOBJECT_HPP
