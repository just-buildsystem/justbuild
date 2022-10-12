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

#include "gsl-lite/gsl-lite.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_client_common.hpp"

BazelAcClient::BazelAcClient(std::string const& server,
                             Port port,
                             std::string const& user,
                             std::string const& pwd) noexcept {
    stub_ = bazel_re::ActionCache::NewStub(
        CreateChannelWithCredentials(server, port, user, pwd));
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

    grpc::ClientContext context;
    bazel_re::ActionResult response;
    grpc::Status status = stub_->GetActionResult(&context, request, &response);

    if (not status.ok()) {
        if (status.error_code() == grpc::StatusCode::NOT_FOUND) {
            logger_.Emit(
                LogLevel::Debug, "cache miss '{}'", status.error_message());
        }
        else {
            LogStatus(&logger_, LogLevel::Debug, status);
        }
        return std::nullopt;
    }
    return response;
}

auto BazelAcClient::UpdateActionResult(std::string const& instance_name,
                                       bazel_re::Digest const& action_digest,
                                       bazel_re::ActionResult const& result,
                                       int priority) noexcept
    -> std::optional<bazel_re::ActionResult> {
    auto policy = std::make_unique<bazel_re::ResultsCachePolicy>();
    policy->set_priority(priority);

    bazel_re::UpdateActionResultRequest request{};
    request.set_instance_name(instance_name);
    request.set_allocated_action_digest(
        gsl::owner<bazel_re::Digest*>{new bazel_re::Digest{action_digest}});
    request.set_allocated_action_result(gsl::owner<bazel_re::ActionResult*>{
        new bazel_re::ActionResult{result}});
    request.set_allocated_results_cache_policy(policy.release());

    grpc::ClientContext context;
    bazel_re::ActionResult response;
    grpc::Status status =
        stub_->UpdateActionResult(&context, request, &response);

    if (not status.ok()) {
        LogStatus(&logger_, LogLevel::Debug, status);
        return std::nullopt;
    }
    return response;
}
