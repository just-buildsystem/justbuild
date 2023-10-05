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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_CREATE_EXECUTION_API_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_CREATE_EXECUTION_API_HPP

#include <memory>
#include <optional>

#include "gsl/gsl"
#include "src/buildtool/common/remote/remote_common.hpp"
#include "src/buildtool/execution_api/local/local_api.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_api.hpp"

/// \brief Utility function to instantiate either a Local or Bazel Execution
/// API.
/// \param address if provided, a BazelApi is instantiated
/// \param instance_name only used in the construction of the BazelApi object
[[nodiscard]] static inline auto CreateExecutionApi(
    std::optional<ServerAddress> const& address,
    std::string const& instance_name = "remote-execution")
    -> gsl::not_null<IExecutionApi::Ptr> {
    if (address) {
        ExecutionConfiguration config;
        config.skip_cache_lookup = false;

        return std::make_unique<BazelApi>(
            instance_name, address->host, address->port, config);
    }
    return std::make_unique<LocalApi>();
}

#endif
