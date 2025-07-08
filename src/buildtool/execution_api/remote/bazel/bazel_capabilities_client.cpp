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

#include "src/buildtool/execution_api/remote/bazel/bazel_capabilities_client.hpp"

#include <algorithm>
#include <mutex>
#include <optional>
#include <utility>

#include <grpcpp/grpcpp.h>

#include "build/bazel/semver/semver.pb.h"
#include "fmt/core.h"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/common/remote/client_common.hpp"
#include "src/buildtool/common/remote/retry.hpp"
#include "src/buildtool/logging/log_level.hpp"

namespace {

[[nodiscard]] auto ParseSemVer(build::bazel::semver::SemVer const&
                                   version) noexcept -> Capabilities::Version {
    return Capabilities::Version{.major = version.major(),
                                 .minor = version.minor(),
                                 .patch = version.patch()};
}

[[nodiscard]] auto Parse(std::optional<bazel_re::ServerCapabilities>
                             response) noexcept -> Capabilities {
    if (not response.has_value()) {
        return Capabilities{};
    }

    // To not duplicate default values here, create default capabilities and
    // copy data from there.
    Capabilities const default_capabilities;

    // If capabilities don't contain cache capabilities, max_batch_total_size is
    // unlimited (equals 0), or greater than the internal limit, fall back to
    // the default max_batch_total_size.
    std::size_t max_batch = default_capabilities.MaxBatchTransferSize;
    if (response->has_cache_capabilities() and
        response->cache_capabilities().max_batch_total_size_bytes() != 0) {
        max_batch = std::min<std::size_t>(
            static_cast<std::size_t>(
                response->cache_capabilities().max_batch_total_size_bytes()),
            default_capabilities.MaxBatchTransferSize);
    }
    return Capabilities{
        .MaxBatchTransferSize = max_batch,
        .low_api_version = response->has_deprecated_api_version()
                               ? ParseSemVer(response->deprecated_api_version())
                               : (response->has_low_api_version()  // NOLINT
                                      ? ParseSemVer(response->low_api_version())
                                      : Capabilities::kMinVersion),
        .high_api_version = response->has_high_api_version()
                                ? ParseSemVer(response->high_api_version())
                                : Capabilities::kMaxVersion};
}

}  // namespace

BazelCapabilitiesClient::BazelCapabilitiesClient(
    std::string const& server,
    Port port,
    gsl::not_null<Auth const*> const& auth,
    gsl::not_null<RetryConfig const*> const& retry_config) noexcept
    : retry_config_{*retry_config} {
    stub_ = bazel_re::Capabilities::NewStub(
        CreateChannelWithCredentials(server, port, auth));
}

auto BazelCapabilitiesClient::GetCapabilities(
    std::string const& instance_name) const noexcept -> Capabilities::Ptr {
    {
        // Check the cache already contains capabilities for this instance:
        std::shared_lock guard{lock_};
        auto it = capabilities_.find(instance_name);
        if (it != capabilities_.end()) {
            return it->second;
        }
    }

    std::optional<bazel_re::ServerCapabilities> response;
    bool is_reasonable_to_retry = true;
    auto get_capabilities = [&instance_name,
                             &stub = *stub_,
                             &response,
                             &is_reasonable_to_retry]() -> RetryResponse {
        grpc::ClientContext context;

        bazel_re::GetCapabilitiesRequest request;
        *request.mutable_instance_name() = instance_name;

        bazel_re::ServerCapabilities capabilities;
        auto const status =
            stub.GetCapabilities(&context, request, &capabilities);
        if (status.ok()) {
            response.emplace(std::move(capabilities));
            return RetryResponse{.ok = true};
        }

        is_reasonable_to_retry = IsReasonableToRetry(status);
        return RetryResponse{
            .ok = false,
            .exit_retry_loop = not is_reasonable_to_retry,
            .error_msg = fmt::format("While obtaining capabilities: {}",
                                     status.error_message())};
    };

    if (not WithRetry(get_capabilities,
                      retry_config_,
                      logger_,
                      /*fatal_log_level=*/LogLevel::Debug) or
        not response.has_value()) {
        logger_.Emit(
            LogLevel::Warning,
            "Failed to obtain Capabilities. Falling back to default values.");
    }

    bool const cache_result = response.has_value();
    auto result = std::make_shared<Capabilities>(Parse(std::move(response)));

    // Cache results only if they contain meaningful non-default capabilities or
    // there's no point in retrying:
    if (cache_result or not is_reasonable_to_retry) {
        std::unique_lock lock{lock_};
        capabilities_.insert_or_assign(instance_name, result);
    }
    return result;
}
