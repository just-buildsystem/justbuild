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

#include "src/buildtool/execution_api/serve/mr_git_api.hpp"

#include <utility>

#include "src/buildtool/execution_api/git/git_api.hpp"
#include "src/buildtool/execution_api/utils/rehash_utils.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/expected.hpp"

MRGitApi::MRGitApi(
    gsl::not_null<RepositoryConfig const*> const& repo_config,
    gsl::not_null<StorageConfig const*> const& native_storage_config,
    StorageConfig const* compatible_storage_config,
    Storage const* compatible_storage,
    IExecutionApi const* compatible_local_api) noexcept
    : repo_config_{repo_config},
      native_storage_config_{native_storage_config},
      compat_storage_config_{compatible_storage_config},
      compat_storage_{compatible_storage},
      compat_local_api_{compatible_local_api} {}

auto MRGitApi::RetrieveToCas(
    std::vector<Artifact::ObjectInfo> const& artifacts_info,
    IExecutionApi const& api) const noexcept -> bool {
    // Return immediately if target CAS is this CAS
    if (this == &api) {
        return true;
    }

    // in native mode: dispatch to regular GitApi
    if (compat_storage_config_ == nullptr) {
        GitApi const git_api{repo_config_};
        return git_api.RetrieveToCas(artifacts_info, api);
    }

    auto compat_artifacts =
        RehashUtils::RehashGitDigest(artifacts_info,
                                     *native_storage_config_,
                                     *compat_storage_config_,
                                     *repo_config_);
    if (not compat_artifacts) {
        Logger::Log(LogLevel::Error,
                    "MRGitApi: {}",
                    std::move(compat_artifacts).error());
        return false;
    }
    return compat_local_api_->RetrieveToCas(*compat_artifacts, api);
}
