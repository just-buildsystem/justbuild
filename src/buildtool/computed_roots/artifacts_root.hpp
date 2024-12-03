// Copyright 2024 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDTOOL_COMPUTED_ROOT_ARTIFACTS_ROOT_HPP
#define INCLUDED_SRC_BUILDTOOL_COMPUTED_ROOT_ARTIFACTS_ROOT_HPP

#include <optional>
#include <string>

#include "src/buildtool/build_engine/expression/expression_ptr.hpp"
#include "src/buildtool/execution_api/utils/rehash_utils.hpp"
#include "src/buildtool/multithreading/async_map_consumer.hpp"

/// \brief Compute to the git tree identifier, as hex string, corresponding to
/// an artifact stage; return std::nullopt in case of errors
/// \param stage Expression pointer supposed to represent a map from logical
/// paths to known artifacts
/// \param logger Logger to report problems; will be called with the fatal
/// property in case of error
auto ArtifactsRoot(ExpressionPtr const& stage,
                   AsyncMapConsumerLoggerPtr const& logger,
                   std::optional<RehashUtils::Rehasher> const& rehash =
                       std::nullopt) -> std::optional<std::string>;

#endif
