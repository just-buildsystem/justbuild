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

#ifndef INCLUDED_SRC_OTHER_TOOLS_JUST_MR_LAUNCH_HPP
#define INCLUDED_SRC_OTHER_TOOLS_JUST_MR_LAUNCH_HPP

#include <filesystem>

#include "src/other_tools/just_mr/cli.hpp"

/// \brief Runs execvp for configured command. Only returns if execvp fails.
[[nodiscard]] auto CallJust(
    std::optional<std::filesystem::path> const& config_file,
    MultiRepoCommonArguments const& common_args,
    MultiRepoSetupArguments const& setup_args,
    MultiRepoJustSubCmdsArguments const& just_cmd_args,
    MultiRepoLogArguments const& log_args,
    MultiRepoRemoteAuthArguments const& auth_args,
    bool forward_build_root) -> int;

#endif  // INCLUDED_SRC_OTHER_TOOLS_JUST_MR_LAUNCH_HPP
