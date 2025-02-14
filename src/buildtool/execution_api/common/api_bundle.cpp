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

#include "src/buildtool/execution_api/common/api_bundle.hpp"

#include <memory>
#include <utility>

#include "src/buildtool/execution_api/bazel_msg/execution_config.hpp"
#include "src/buildtool/execution_api/local/local_api.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_api.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/storage/config.hpp"

/// \note Some logic from MakeRemote is duplicated here as that method cannot
/// be used without the hash_function field being set prior to the call.
auto ApiBundle::Create(
    gsl::not_null<LocalContext const*> const& local_context,
    gsl::not_null<RemoteContext const*> const& remote_context,
    RepositoryConfig const* repo_config) -> ApiBundle {
    HashFunction const hash_fct = local_context->storage_config->hash_function;
    IExecutionApi::Ptr local_api =
        std::make_shared<LocalApi>(local_context, repo_config);
    IExecutionApi::Ptr remote_api = local_api;
    if (auto const address = remote_context->exec_config->remote_address) {
        ExecutionConfiguration config;
        config.skip_cache_lookup = false;
        remote_api = std::make_shared<BazelApi>("remote-execution",
                                                address->host,
                                                address->port,
                                                remote_context->auth,
                                                remote_context->retry_config,
                                                config,
                                                hash_fct);
    }
    return ApiBundle{.hash_function = hash_fct,
                     .local = std::move(local_api),
                     .remote = std::move(remote_api)};
}

auto ApiBundle::MakeRemote(
    std::optional<ServerAddress> const& address,
    gsl::not_null<Auth const*> const& authentication,
    gsl::not_null<RetryConfig const*> const& retry_config) const
    -> gsl::not_null<IExecutionApi::Ptr> {
    if (address) {
        ExecutionConfiguration config;
        config.skip_cache_lookup = false;
        return std::make_shared<BazelApi>("remote-execution",
                                          address->host,
                                          address->port,
                                          authentication,
                                          retry_config,
                                          config,
                                          hash_function);
    }
    return local;
}
