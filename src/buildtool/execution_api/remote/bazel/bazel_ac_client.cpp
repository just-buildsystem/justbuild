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

#include "src/buildtool/execution_api/remote/bazel/bazel_ac_client.hpp"

#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/common/remote/client_common.hpp"
#include "src/buildtool/common/remote/retry.hpp"
#include "src/buildtool/common/remote/retry_config.hpp"
#include "src/buildtool/logging/log_level.hpp"

BazelAcClient::BazelAcClient(
    std::string const& server,
    Port port,
    gsl::not_null<Auth const*> const& auth,
    gsl::not_null<RetryConfig const*> const& retry_config) noexcept
    : retry_config_{*retry_config} {
    stub_ = bazel_re::ActionCache::NewStub(
        CreateChannelWithCredentials(server, port, auth));
}

auto BazelAcClient::GetActionResult(
    std::string const& instance_name,
    bazel_re::Digest const& action_digest,
    bool inline_stdout,
    bool inline_stderr,
    std::vector<std::string> const& inline_output_files) noexcept
    -> std::optional<bazel_re::ActionResult> {
    bazel_re::GetActionResultRequest request{};
    request.set_instance_name(instance_name);
    request.set_allocated_action_digest(
        gsl::owner<bazel_re::Digest*>{new bazel_re::Digest{action_digest}});
    request.set_inline_stdout(inline_stdout);
    request.set_inline_stderr(inline_stderr);
    std::copy(inline_output_files.begin(),
              inline_output_files.end(),
              pb::back_inserter(request.mutable_inline_output_files()));

    bazel_re::ActionResult response;
    auto [ok, status] = WithRetry(
        [this, &response, &request]() {
            grpc::ClientContext context;
            return stub_->GetActionResult(&context, request, &response);
        },
        retry_config_,
        logger_);
    if (not ok) {
        if (status.error_code() == grpc::StatusCode::NOT_FOUND) {
            logger_.Emit(
                LogLevel::Debug, "cache miss '{}'", status.error_message());
        }
        else {
            LogStatus(&logger_, LogLevel::Error, status);
        }
        return std::nullopt;
    }
    return response;
}
