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

#include "src/buildtool/execution_api/bazel_msg/bazel_common.hpp"
#include "src/buildtool/execution_api/local/local_api.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_api.hpp"

ApiBundle::ApiBundle(gsl::not_null<LocalContext const*> const& local_context,
                     gsl::not_null<RemoteContext const*> const& remote_context,
                     RepositoryConfig const* repo_config)
    : hash_function{local_context->storage_config->hash_function},
      local{std::make_shared<LocalApi>(local_context, repo_config)},
      remote{CreateRemote(remote_context->exec_config->remote_address,
                          remote_context->auth,
                          remote_context->retry_config)} {}

auto ApiBundle::CreateRemote(
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
