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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_SERVE_MR_GIT_API_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_SERVE_MR_GIT_API_HPP

#include <vector>

#include "gsl/gsl"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/storage/config.hpp"

/// \brief Handles interaction between the Git CAS and another api, irrespective
/// of the remote-execution protocol used.
class MRGitApi final {
  public:
    MRGitApi(gsl::not_null<RepositoryConfig const*> const& repo_config,
             gsl::not_null<StorageConfig const*> const& native_storage_config,
             StorageConfig const* compatible_storage_config = nullptr,
             IExecutionApi const* compatible_local_api = nullptr) noexcept;

    /// \brief Passes artifacts from Git CAS to specified (remote) api. In
    /// compatible mode, it must rehash the native digests to be able to upload
    /// to a compatible remote. Expects native digests.
    /// \note Caller is responsible for passing vectors with artifacts of the
    /// same digest type.
    [[nodiscard]] auto RetrieveToCas(
        std::vector<Artifact::ObjectInfo> const& artifacts_info,
        IExecutionApi const& api) const noexcept -> bool;

  private:
    gsl::not_null<const RepositoryConfig*> repo_config_;

    // retain references to needed storages and configs
    gsl::not_null<StorageConfig const*> native_storage_config_;
    StorageConfig const* compat_storage_config_;

    // an api accessing compatible storage, used purely to communicate with a
    // compatible remote; only instantiated if in compatible mode
    IExecutionApi const* compat_local_api_;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_SERVE_MR_GIT_API_HPP
