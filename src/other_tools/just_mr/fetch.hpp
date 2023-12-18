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

#ifndef INCLUDED_SRC_OTHER_TOOLS_JUST_MR_FETCH_HPP
#define INCLUDED_SRC_OTHER_TOOLS_JUST_MR_FETCH_HPP

#include <memory>

#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/other_tools/just_mr/cli.hpp"

/// \brief Fetching of distfiles for a multi-repository build.
[[nodiscard]] auto MultiRepoFetch(std::shared_ptr<Configuration> const& config,
                                  MultiRepoCommonArguments const& common_args,
                                  MultiRepoSetupArguments const& setup_args,
                                  MultiRepoFetchArguments const& fetch_args,
                                  MultiRepoRemoteAuthArguments const& auth_args)
    -> int;

#endif  // INCLUDED_SRC_OTHER_TOOLS_JUST_MR_FETCH_HPP
