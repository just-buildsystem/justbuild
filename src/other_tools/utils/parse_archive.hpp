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

#ifndef INCLUDED_SRC_OTHER_TOOLS_UTILS_PARSE_ARCHIVE_HPP
#define INCLUDED_SRC_OTHER_TOOLS_UTILS_PARSE_ARCHIVE_HPP

#include <optional>
#include <string>

#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/other_tools/ops_maps/content_cas_map.hpp"

// Parse the description of an archive repository; if an error
// occurs, call the logger with fatal set to true and return std::nullopt
// instead.
auto ParseArchiveDescription(ExpressionPtr const& repo_desc,
                             std::string const& repo_type,
                             std::string const& origin,
                             const AsyncMapConsumerLoggerPtr& logger)
    -> std::optional<ArchiveRepoInfo>;

// Parse the description of a foreign-file repository; if an error
// occurs, call the logger with fatal set to true and return std::nullopt
// instead.
auto ParseForeignFileDescription(ExpressionPtr const& repo_desc,
                                 std::string const& origin,
                                 const AsyncMapConsumerLoggerPtr& logger)
    -> std::optional<ForeignFileInfo>;

#endif
