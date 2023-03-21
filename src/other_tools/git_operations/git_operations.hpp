// Copyright 2022 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_OTHER_TOOLS_GIT_OPERATIONS_GIT_OPERATIONS_HPP
#define INCLUDED_SRC_OTHER_TOOLS_GIT_OPERATIONS_GIT_OPERATIONS_HPP

#include "src/buildtool/multithreading/async_map_consumer.hpp"
#include "src/other_tools/git_operations/git_ops_types.hpp"

/// \brief Class defining the critical Git operations, i.e., those which write
/// to the underlying Git ODB.
/// The target_path is a mandatory argument, as it is used in a file locking
/// mechanism, ensuring only one process at a time works on a particular
/// repository on the file system.
class CriticalGitOps {
  public:
    // This operations needs the params: target_path, message
    // Will perform: "git init && git add . && git commit -m <message>".
    // Called to setup first commit in new repository. Assumes folder exists.
    // It guarantees the logger is called exactly once with fatal if failure.
    [[nodiscard]] static auto GitInitialCommit(
        GitOpParams const& crit_op_params,
        AsyncMapConsumerLoggerPtr const& logger) -> GitOpValue;

    // This operation needs the params: target_path
    // Called to initialize a repository. Creates folder if not there.
    // It guarantees the logger is called exactly once with fatal if failure.
    [[nodiscard]] static auto GitEnsureInit(
        GitOpParams const& crit_op_params,
        AsyncMapConsumerLoggerPtr const& logger) -> GitOpValue;

    // This operation needs the params: target_path, git_hash (commit), message
    // Called after a git fetch to retain the commit. Assumes folder exists.
    // It guarantees the logger is called exactly once with fatal if failure.
    [[nodiscard]] static auto GitKeepTag(
        GitOpParams const& crit_op_params,
        AsyncMapConsumerLoggerPtr const& logger) -> GitOpValue;

    // This operations needs the params: target_path
    // Called to retrieve the HEAD commit hash. Assumes folder exists.
    // It guarantees the logger is called exactly once with fatal if failure.
    [[nodiscard]] static auto GitGetHeadId(
        GitOpParams const& crit_op_params,
        AsyncMapConsumerLoggerPtr const& logger) -> GitOpValue;
};

#endif  // INCLUDED_SRC_OTHER_TOOLS_GIT_OPERATIONS_GIT_OPERATIONS_HPP