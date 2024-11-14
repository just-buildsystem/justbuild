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

#ifndef INCLUDED_SRC_BUILDTOOL_COMPUTED_ROOT_EVALUTE_HPP
#define INCLUDED_SRC_BUILDTOOL_COMPUTED_ROOT_EVALUTE_HPP

#include <cstddef>
#include <string>

#include "gsl/gsl"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/execution_engine/executor/context.hpp"
#include "src/buildtool/graph_traverser/graph_traverser.hpp"
#include "src/buildtool/storage/config.hpp"

auto EvaluateComputedRoots(
    gsl::not_null<RepositoryConfig*> const& repository_config,
    std::string const& main_repo,
    StorageConfig const& storage_config,
    GraphTraverser::CommandLineArguments const& traverser_args,
    gsl::not_null<const ExecutionContext*> const& context,
    std::size_t jobs) -> bool;
#endif
