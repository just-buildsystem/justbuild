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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_ENGINE_EXECUTOR_CONTEXT_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_ENGINE_EXECUTOR_CONTEXT_HPP

#include <optional>

#include "gsl/gsl"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/common/statistics.hpp"
#include "src/buildtool/execution_api/common/api_bundle.hpp"
#include "src/buildtool/execution_api/remote/context.hpp"
#include "src/buildtool/profile/profile.hpp"
#include "src/buildtool/progress_reporting/progress.hpp"

/// \brief Aggregate to be passed to graph traverser.
/// \note No field is stored as const ref to avoid binding to temporaries.
struct ExecutionContext final {
    gsl::not_null<RepositoryConfig const*> const repo_config;
    gsl::not_null<ApiBundle const*> const& apis;
    gsl::not_null<RemoteContext const*> const remote_context;
    gsl::not_null<Statistics*> const statistics;
    gsl::not_null<Progress*> const progress;
    std::optional<gsl::not_null<Profile*>> const profile;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_ENGINE_EXECUTOR_CONTEXT_HPP
