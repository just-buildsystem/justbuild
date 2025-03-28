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

#ifndef INCLUDED_SRC_OTHER_TOOLS_JUST_MR_SETUP_HPP
#define INCLUDED_SRC_OTHER_TOOLS_JUST_MR_SETUP_HPP

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/buildtool/common/retry_cli.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/other_tools/just_mr/cli.hpp"

/// \brief Setup for a multi-repository build. Return the pair
/// of path and hash (as hex-string) of the multi-repository configuration.
[[nodiscard]] auto MultiRepoSetup(
    std::shared_ptr<Configuration> const& config,
    MultiRepoCommonArguments const& common_args,
    MultiRepoSetupArguments const& setup_args,
    MultiRepoJustSubCmdsArguments const& just_cmd_args,
    MultiRepoRemoteAuthArguments const& auth_args,
    RetryArguments const& retry_args,
    StorageConfig const& native_storage_config,
    Storage const& native_storage,
    bool interactive,
    std::string const& multi_repo_tool_name)
    -> std::optional<std::pair<std::filesystem::path, std::string>>;

#endif  // INCLUDED_SRC_OTHER_TOOLS_JUST_MR_SETUP_HPP
