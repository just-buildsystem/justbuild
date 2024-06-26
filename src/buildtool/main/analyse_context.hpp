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

#ifndef INCLUDED_SRC_BUILDOOL_MAIN_ANALYSE_CONTEXT_HPP
#define INCLUDED_SRC_BUILDOOL_MAIN_ANALYSE_CONTEXT_HPP

#include "gsl/gsl"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/common/statistics.hpp"
#include "src/buildtool/progress_reporting/progress.hpp"
#include "src/buildtool/serve_api/remote/serve_api.hpp"
#include "src/buildtool/storage/target_cache.hpp"

struct AnalyseContext final {
    gsl::not_null<RepositoryConfig const*> const repo_config;
    gsl::not_null<ActiveTargetCache const*> const target_cache;
    gsl::not_null<Statistics*> const statistics;
    gsl::not_null<Progress*> const progress;
    ServeApi const* const serve = nullptr;
};

#endif  // INCLUDED_SRC_BUILDOOL_MAIN_ANALYSE_CONTEXT_HPP
