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

#include "src/other_tools/ops_maps/git_update_map.hpp"

#include <memory>
#include <optional>

#include "fmt/core.h"
#include "src/buildtool/progress_reporting/task_tracker.hpp"
#include "src/other_tools/git_operations/git_repo_remote.hpp"

auto CreateGitUpdateMap(
    GitCASPtr const& git_cas,
    std::string const& git_bin,
    std::vector<std::string> const& launcher,
    gsl::not_null<StorageConfig const*> const& storage_config,
    gsl::not_null<JustMRStatistics*> const& stats,
    gsl::not_null<JustMRProgress*> const& progress,
    std::size_t jobs) -> GitUpdateMap {
    auto update_commits = [git_cas,
                           git_bin,
                           launcher,
                           storage_config,
                           stats,
                           progress](auto /* unused */,
                                     auto setter,
                                     auto logger,
                                     auto /* unused */,
                                     auto const& key) {
        // perform git update commit
        auto git_repo = GitRepoRemote::Open(git_cas);  // wrap the tmp odb
        if (not git_repo) {
            (*logger)(
                fmt::format("Failed to open tmp Git repository for remote {}",
                            key.repo),
                /*fatal=*/true);
            return;
        }
        // setup wrapped logger
        auto wrapped_logger = std::make_shared<AsyncMapConsumerLogger>(
            [logger](auto const& msg, bool fatal) {
                (*logger)(
                    fmt::format("While updating commit from remote:\n{}", msg),
                    fatal);
            });
        // update commit
        auto id = fmt::format("{}:{}", key.repo, key.branch);
        progress->TaskTracker().Start(id);
        auto new_commit = git_repo->UpdateCommitViaTmpRepo(*storage_config,
                                                           key.repo,
                                                           key.branch,
                                                           key.inherit_env,
                                                           git_bin,
                                                           launcher,
                                                           wrapped_logger);
        progress->TaskTracker().Stop(id);
        if (not new_commit) {
            return;
        }
        stats->IncrementExecutedCounter();
        (*setter)(new_commit->c_str());
    };
    return AsyncMapConsumer<RepoDescriptionForUpdating, std::string>(
        update_commits, jobs);
}
