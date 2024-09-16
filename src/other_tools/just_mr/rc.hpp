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

#ifndef INCLUDED_SRC_OTHER_TOOLS_JUST_MR_RC_HPP
#define INCLUDED_SRC_OTHER_TOOLS_JUST_MR_RC_HPP

#include <filesystem>
#include <optional>

#include "gsl/gsl"
#include "src/other_tools/just_mr/cli.hpp"

/// \brief Read just-mrrc file and set up various configs. Return the path to
/// the repository config file, if any is provided.
[[nodiscard]] auto ReadJustMRRC(
    gsl::not_null<CommandLineArguments*> const& clargs)
    -> std::optional<std::filesystem::path>;
#endif  // INCLUDED_SRC_OTHER_TOOLS_JUST_MR_RC_HPP
