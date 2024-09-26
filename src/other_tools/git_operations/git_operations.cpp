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

#include "src/other_tools/git_operations/git_operations.hpp"

#include <memory>
#include <vector>

#include "fmt/core.h"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/other_tools/git_operations/git_repo_remote.hpp"

auto CriticalGitOps::GitInitialCommit(GitOpParams const& crit_op_params,
                                      AsyncMapConsumerLoggerPtr const& logger)
    -> GitOpValue {
#ifndef NDEBUG
    // Check required fields have been set
    if (not crit_op_params.message) {
        (*logger)("missing message for operation creating commit",
                  true /*fatal*/);
        return {.git_cas = nullptr, .result = std::nullopt};
    }
    if (not crit_op_params.source_path) {
        (*logger)("missing source_path for operation creating commit",
                  true /*fatal*/);
        return {.git_cas = nullptr, .result = std::nullopt};
    }
#endif
    // Create and open a GitRepoRemote at given target location
    auto git_repo = GitRepoRemote::InitAndOpen(crit_op_params.target_path,
                                               /*is_bare=*/false);
    if (git_repo == std::nullopt) {
        (*logger)(fmt::format("could not initialize git repository {}",
                              crit_op_params.target_path.string()),
                  true /*fatal*/);
        return {.git_cas = nullptr, .result = std::nullopt};
    }
    // setup wrapped logger
    auto wrapped_logger = std::make_shared<AsyncMapConsumerLogger>(
        [logger](auto const& msg, bool fatal) {
            (*logger)(
                fmt::format("While doing initial commit Git op:\n{}", msg),
                fatal);
        });
    // Commit all at the target directory
    auto commit_hash =
        git_repo->CommitDirectory(crit_op_params.source_path.value(),
                                  crit_op_params.message.value(),
                                  wrapped_logger);
    if (commit_hash == std::nullopt) {
        return {.git_cas = nullptr, .result = std::nullopt};
    }
    // success
    return {.git_cas = git_repo->GetGitCAS(), .result = std::move(commit_hash)};
}

auto CriticalGitOps::GitEnsureInit(GitOpParams const& crit_op_params,
                                   AsyncMapConsumerLoggerPtr const& logger)
    -> GitOpValue {
    // Make sure folder we want to access exists
    if (not FileSystemManager::CreateDirectory(crit_op_params.target_path)) {
        (*logger)(fmt::format("target directory {} could not be created",
                              crit_op_params.target_path.string()),
                  true /*fatal*/);
        return {.git_cas = nullptr, .result = std::nullopt};
    }
    // Create and open a GitRepo at given target location
    auto git_repo = GitRepoRemote::InitAndOpen(
        crit_op_params.target_path,
        /*is_bare=*/crit_op_params.init_bare.value());
    if (git_repo == std::nullopt) {
        (*logger)(
            fmt::format("could not initialize {} git repository {}",
                        crit_op_params.init_bare.value() ? "bare" : "non-bare",
                        crit_op_params.target_path.string()),
            true /*fatal*/);
        return {.git_cas = nullptr, .result = std::nullopt};
    }
    // success
    return {.git_cas = git_repo->GetGitCAS(), .result = ""};
}

auto CriticalGitOps::GitKeepTag(GitOpParams const& crit_op_params,
                                AsyncMapConsumerLoggerPtr const& logger)
    -> GitOpValue {
#ifndef NDEBUG
    // Check required fields have been set
    if (not crit_op_params.message) {
        (*logger)("missing message for operation tagging a commit",
                  true /*fatal*/);
        return {.git_cas = nullptr, .result = std::nullopt};
    }
#endif
    // Make sure folder we want to access exists
    if (not FileSystemManager::Exists(crit_op_params.target_path)) {
        (*logger)(fmt::format("target directory {} does not exist!",
                              crit_op_params.target_path.string()),
                  true /*fatal*/);
        return {.git_cas = nullptr, .result = std::nullopt};
    }
    // Open a GitRepo at given location
    auto git_repo = GitRepoRemote::Open(crit_op_params.target_path);
    if (git_repo == std::nullopt) {
        (*logger)(fmt::format("could not open git repository {}",
                              crit_op_params.target_path.string()),
                  true /*fatal*/);
        return {.git_cas = nullptr, .result = std::nullopt};
    }
    // setup wrapped logger
    auto wrapped_logger = std::make_shared<AsyncMapConsumerLogger>(
        [logger](auto const& msg, bool fatal) {
            (*logger)(fmt::format("While doing keep tag Git op:\n{}", msg),
                      fatal);
        });
    // Create tag of given commit
    auto tag_result = git_repo->KeepTag(crit_op_params.git_hash,
                                        crit_op_params.message.value(),
                                        wrapped_logger);
    if (not tag_result) {
        return {.git_cas = nullptr, .result = std::nullopt};
    }
    // success
    return {.git_cas = git_repo->GetGitCAS(), .result = std::move(tag_result)};
}

auto CriticalGitOps::GitGetHeadId(GitOpParams const& crit_op_params,
                                  AsyncMapConsumerLoggerPtr const& logger)
    -> GitOpValue {
    // Make sure folder we want to access exists
    if (not FileSystemManager::Exists(crit_op_params.target_path)) {
        (*logger)(fmt::format("target directory {} does not exist!",
                              crit_op_params.target_path.string()),
                  true /*fatal*/);
        return {.git_cas = nullptr, .result = std::nullopt};
    }
    // Open a GitRepo at given location
    auto git_repo = GitRepoRemote::Open(crit_op_params.target_path);
    if (git_repo == std::nullopt) {
        (*logger)(fmt::format("could not open git repository {}",
                              crit_op_params.target_path.string()),
                  true /*fatal*/);
        return {.git_cas = nullptr, .result = std::nullopt};
    }
    // setup wrapped logger
    auto wrapped_logger = std::make_shared<AsyncMapConsumerLogger>(
        [logger](auto const& msg, bool fatal) {
            (*logger)(fmt::format("While doing get HEAD id Git op:\n{}", msg),
                      fatal);
        });
    // Get head commit
    auto head_commit = git_repo->GetHeadCommit(wrapped_logger);
    if (head_commit == std::nullopt) {
        return {.git_cas = nullptr, .result = std::nullopt};
    }
    // success
    return {.git_cas = git_repo->GetGitCAS(), .result = std::move(head_commit)};
}

auto CriticalGitOps::GitKeepTree(GitOpParams const& crit_op_params,
                                 AsyncMapConsumerLoggerPtr const& logger)
    -> GitOpValue {
#ifndef NDEBUG
    // Check required fields have been set
    if (not crit_op_params.message) {
        (*logger)("missing message for operation keeping a tree comitted",
                  true /*fatal*/);
        return {.git_cas = nullptr, .result = std::nullopt};
    }
#endif
    // Make sure folder we want to access exists
    if (not FileSystemManager::Exists(crit_op_params.target_path)) {
        (*logger)(fmt::format("target directory {} does not exist!",
                              crit_op_params.target_path.string()),
                  true /*fatal*/);
        return {.git_cas = nullptr, .result = std::nullopt};
    }
    // Open a GitRepo at given location
    auto git_repo = GitRepoRemote::Open(crit_op_params.target_path);
    if (git_repo == std::nullopt) {
        (*logger)(fmt::format("could not open git repository {}",
                              crit_op_params.target_path.string()),
                  true /*fatal*/);
        return {.git_cas = nullptr, .result = std::nullopt};
    }
    // setup wrapped logger
    auto wrapped_logger = std::make_shared<AsyncMapConsumerLogger>(
        [logger](auto const& msg, bool fatal) {
            (*logger)(fmt::format("While doing keep tree Git op:\n{}", msg),
                      fatal);
        });
    // Create tag for given tree
    auto tag_result = git_repo->KeepTree(crit_op_params.git_hash,
                                         crit_op_params.message.value(),
                                         wrapped_logger);
    if (not tag_result) {
        return {.git_cas = nullptr, .result = std::nullopt};
    }
    // success
    return {.git_cas = git_repo->GetGitCAS(), .result = std::move(tag_result)};
}
