// Copyright 2025 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_CAPABILITIES_CLIENT_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_CAPABILITIES_CLIENT_HPP

#include <cstddef>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>

#include "build/bazel/remote/execution/v2/remote_execution.grpc.pb.h"
#include "gsl/gsl"
#include "src/buildtool/auth/authentication.hpp"
#include "src/buildtool/common/remote/port.hpp"
#include "src/buildtool/common/remote/retry_config.hpp"
#include "src/buildtool/execution_api/common/message_limits.hpp"
#include "src/buildtool/logging/logger.hpp"

namespace bazel_re = build::bazel::remote::execution::v2;

struct Capabilities final {
    using Ptr = gsl::not_null<std::shared_ptr<Capabilities>>;

    std::size_t const MaxBatchTransferSize = MessageLimits::kMaxGrpcLength;
};

class BazelCapabilitiesClient final {
  public:
    explicit BazelCapabilitiesClient(
        std::string const& server,
        Port port,
        gsl::not_null<Auth const*> const& auth,
        gsl::not_null<RetryConfig const*> const& retry_config) noexcept;

    /// \brief Obtain server capabilities for instance_name.
    /// \return Capabilities corresponding to the given instance_name. Requested
    /// capabilities are cached if a valid response is received from the server.
    /// Otherwise, the default capabilities are returned and the caching step is
    /// skipped to try again next time.
    [[nodiscard]] auto GetCapabilities(
        std::string const& instance_name) const noexcept -> Capabilities::Ptr;

  private:
    RetryConfig const& retry_config_;
    std::unique_ptr<bazel_re::Capabilities::Stub> stub_;
    Logger logger_{"RemoteCapabilitiesClient"};

    mutable std::shared_mutex lock_;
    mutable std::unordered_map<std::string, Capabilities::Ptr> capabilities_;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_CAPABILITIES_CLIENT_HPP
