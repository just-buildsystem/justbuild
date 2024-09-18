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
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/execution_api/local/context.hpp"
#include "src/buildtool/execution_api/remote/context.hpp"

/// \brief Utility structure for instantiation of local and remote apis at the
/// same time. If the remote api cannot be instantiated, it falls back to
/// exactly the same instance that local api is (&*remote == & *local).
struct ApiBundle final {
    /// \brief Create an ApiBundle instance.
    /// \note A creator method is used instead of a constructor to allow for
    /// tests to instantiate ApiBundles with own implementations of the APIs.
    [[nodiscard]] static auto Create(
        gsl::not_null<LocalContext const*> const& local_context,
        gsl::not_null<RemoteContext const*> const& remote_context,
        RepositoryConfig const* repo_config) -> ApiBundle;

    /// \brief Create a Remote object based on the given arguments.
    /// \param address          The endpoint address.
    /// \param authentication   The remote authentication configuration.
    /// \param retry_config     The retry strategy configuration.
    /// \returns A configured api: BazelApi if a remote address is given,
    /// otherwise fall back to the already configured LocalApi instance.
    [[nodiscard]] auto MakeRemote(
        std::optional<ServerAddress> const& address,
        gsl::not_null<Auth const*> const& authentication,
        gsl::not_null<RetryConfig const*> const& retry_config) const
        -> gsl::not_null<IExecutionApi::Ptr>;

    HashFunction const& hash_function;
    gsl::not_null<IExecutionApi::Ptr> const local;
    gsl::not_null<IExecutionApi::Ptr> const remote;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_API_BUNDLE_HPP
