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

#include <vector>

#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/logging/logger.hpp"

auto CriticalGitOps::GitInitialCommit(GitOpParams const& crit_op_params,
                                      AsyncMapConsumerLoggerPtr const& logger)
    -> GitOpValue {
    // Create and open a GitRepo at given target location
    auto git_repo =
        GitRepo::InitAndOpen(crit_op_params.target_path, /*is_bare=*/false);
    if (git_repo == std::nullopt) {
        (*logger)(fmt::format("could not initialize git repository {}",
                              crit_op_params.target_path.string()),
                  true /*fatal*/);
        return GitOpValue({nullptr, std::nullopt});
    }
    // setup wrapped logger
    auto wrapped_logger = std::make_shared<AsyncMapConsumerLogger>(
        [logger](auto const& msg, bool fatal) {
            (*logger)(
                fmt::format("While doing initial commit Git op:\n{}", msg),
                fatal);
        });
    // Stage and commit all at the target location
    auto commit_hash = git_repo->StageAndCommitAllAnonymous(
        crit_op_params.message.value(), wrapped_logger);
    if (commit_hash == std::nullopt) {
        return GitOpValue({nullptr, std::nullopt});
    }
    // success
    return GitOpValue({git_repo->GetGitCAS(), commit_hash.value()});
}

auto CriticalGitOps::GitEnsureInit(GitOpParams const& crit_op_params,
                                   AsyncMapConsumerLoggerPtr const& logger)
    -> GitOpValue {
    // Make sure folder we want to access exists
    if (not FileSystemManager::CreateDirectory(crit_op_params.target_path)) {
        (*logger)(fmt::format("target directory {} could not be created",
                              crit_op_params.target_path.string()),
                  true /*fatal*/);
        return GitOpValue({nullptr, std::nullopt});
    }
    // Create and open a GitRepo at given target location
    auto git_repo =
        GitRepo::InitAndOpen(crit_op_params.target_path,
                             /*is_bare=*/crit_op_params.init_bare.value());
    if (git_repo == std::nullopt) {
        (*logger)(
            fmt::format("could not initialize {} git repository {}",
                        crit_op_params.init_bare.value() ? "bare" : "non-bare",
                        crit_op_params.target_path.string()),
            true /*fatal*/);
        return GitOpValue({nullptr, std::nullopt});
    }
    // success
    return GitOpValue({git_repo->GetGitCAS(), ""});
}

auto CriticalGitOps::GitKeepTag(GitOpParams const& crit_op_params,
                                AsyncMapConsumerLoggerPtr const& logger)
    -> GitOpValue {
    // Make sure folder we want to access exists
    if (not FileSystemManager::Exists(crit_op_params.target_path)) {
        (*logger)(fmt::format("target directory {} does not exist!",
                              crit_op_params.target_path.string()),
                  true /*fatal*/);
        return GitOpValue({nullptr, std::nullopt});
    }
    // Open a GitRepo at given location
    auto git_repo = GitRepo::Open(crit_op_params.target_path);
    if (git_repo == std::nullopt) {
        (*logger)(fmt::format("could not open git repository {}",
                              crit_op_params.target_path.string()),
                  true /*fatal*/);
        return GitOpValue({nullptr, std::nullopt});
    }
    // setup wrapped logger
    auto wrapped_logger = std::make_shared<AsyncMapConsumerLogger>(
        [logger](auto const& msg, bool fatal) {
            (*logger)(fmt::format("While doing keep tag Git op:\n{}", msg),
                      fatal);
        });
    // Create tag of given commit
    if (not git_repo->KeepTag(crit_op_params.git_hash,
                              crit_op_params.message.value(),
                              wrapped_logger)) {
        return GitOpValue({nullptr, std::nullopt});
    }
    // success
    return GitOpValue({git_repo->GetGitCAS(), ""});
}

auto CriticalGitOps::GitGetHeadId(GitOpParams const& crit_op_params,
                                  AsyncMapConsumerLoggerPtr const& logger)
    -> GitOpValue {
    // Make sure folder we want to access exists
    if (not FileSystemManager::Exists(crit_op_params.target_path)) {
        (*logger)(fmt::format("target directory {} does not exist!",
                              crit_op_params.target_path.string()),
                  true /*fatal*/);
        return GitOpValue({nullptr, std::nullopt});
    }
    // Open a GitRepo at given location
    auto git_repo = GitRepo::Open(crit_op_params.target_path);
    if (git_repo == std::nullopt) {
        (*logger)(fmt::format("could not open git repository {}",
                              crit_op_params.target_path.string()),
                  true /*fatal*/);
        return GitOpValue({nullptr, std::nullopt});
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
        return GitOpValue({nullptr, std::nullopt});
    }
    // success
    return GitOpValue({git_repo->GetGitCAS(), *head_commit});
}
