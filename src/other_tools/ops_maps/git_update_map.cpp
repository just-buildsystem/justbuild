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

#include "fmt/core.h"
#include "src/buildtool/execution_api/local/config.hpp"
#include "src/other_tools/just_mr/progress_reporting/progress.hpp"
#include "src/other_tools/just_mr/progress_reporting/statistics.hpp"
#include "src/other_tools/just_mr/utils.hpp"
#include "src/utils/cpp/tmp_dir.hpp"

auto CreateGitUpdateMap(GitCASPtr const& git_cas,
                        std::string const& git_bin,
                        std::vector<std::string> const& launcher,
                        std::size_t jobs) -> GitUpdateMap {
    auto update_commits = [git_cas, git_bin, launcher](auto /* unused */,
                                                       auto setter,
                                                       auto logger,
                                                       auto /* unused */,
                                                       auto const& key) {
        // perform git update commit
        auto git_repo = GitRepoRemote::Open(git_cas);  // wrap the tmp odb
        if (not git_repo) {
            (*logger)(
                fmt::format("Failed to open tmp Git repository for remote {}",
                            key.first),
                /*fatal=*/true);
            return;
        }
        auto tmp_dir = JustMR::Utils::CreateTypedTmpDir("update");
        if (not tmp_dir) {
            (*logger)(fmt::format("Failed to create commit update tmp dir for "
                                  "remote {}",
                                  key.first),
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
        auto id = fmt::format("{}:{}", key.first, key.second);
        JustMRProgress::Instance().TaskTracker().Start(id);
        auto new_commit = git_repo->UpdateCommitViaTmpRepo(tmp_dir->GetPath(),
                                                           key.first,
                                                           key.second,
                                                           git_bin,
                                                           launcher,
                                                           wrapped_logger);
        JustMRProgress::Instance().TaskTracker().Stop(id);
        if (not new_commit) {
            return;
        }
        JustMRStatistics::Instance().IncrementExecutedCounter();
        (*setter)(new_commit->c_str());
    };
    return AsyncMapConsumer<StringPair, std::string>(update_commits, jobs);
}
