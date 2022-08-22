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

#include "src/other_tools/ops_maps/critical_git_op_map.hpp"

// define the mapping to actual operations being called
GitOpKeyMap const GitOpKey::map_ = {
    {GitOpType::INITIAL_COMMIT, CriticalGitOps::GitInitialCommit},
    {GitOpType::ENSURE_INIT, CriticalGitOps::GitEnsureInit},
    {GitOpType::KEEP_TAG, CriticalGitOps::GitKeepTag},
    {GitOpType::GET_HEAD_ID, CriticalGitOps::GitGetHeadId},
    {GitOpType::GET_BRANCH_REFNAME, CriticalGitOps::GitGetBranchRefname}};

/// \brief Create a CriticalOpMap object
auto CreateCriticalGitOpMap(CriticalGitOpGuardPtr const& crit_git_op_ptr)
    -> CriticalGitOpMap {
    auto crit_op_runner = [crit_git_op_ptr](auto /*unused*/,
                                            auto setter,
                                            auto logger,
                                            auto subcaller,
                                            auto const& key) {
        auto curr_key = crit_git_op_ptr->FetchAndSetCriticalKey(key);
        if (curr_key == std::nullopt) {
            // do critical operation now
            (*setter)(key.operation(key.params, logger));
        }
        else {
            // do critical operation after curr_key was processed
            (*subcaller)(
                {*curr_key},
                [key, setter, logger](auto const& /*unused*/) {
                    (*setter)(key.operation(key.params, logger));
                },
                logger);
        }
    };
    return AsyncMapConsumer<GitOpKey, GitOpValue>(crit_op_runner);
}
