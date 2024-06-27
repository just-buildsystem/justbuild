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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_API_BUNDLE_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_API_BUNDLE_HPP

#include <memory>
#include <optional>

#include "gsl/gsl"
#include "src/buildtool/auth/authentication.hpp"
#include "src/buildtool/common/remote/remote_common.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/storage.hpp"

/// \brief Utility structure for instantiation of local and remote apis at the
/// same time. If the remote api cannot be instantiated, it falls back to
/// exactly the same instance that local api is (&*remote == & *local).
struct ApiBundle final {
    explicit ApiBundle(
        gsl::not_null<StorageConfig const*> const& storage_config,
        gsl::not_null<Storage const*> const& storage,
        RepositoryConfig const* repo_config,
        gsl::not_null<Auth const*> const& authentication,
        std::optional<ServerAddress> const& remote_address);

    [[nodiscard]] auto CreateRemote(std::optional<ServerAddress> const& address)
        const -> gsl::not_null<IExecutionApi::Ptr>;

    gsl::not_null<IExecutionApi::Ptr> const local;  // needed by remote
    Auth const& auth;                               // needed by remote
    gsl::not_null<IExecutionApi::Ptr> const remote;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_API_BUNDLE_HPP
