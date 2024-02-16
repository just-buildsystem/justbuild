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

#include "src/buildtool/serve_api/remote/configuration_client.hpp"

#include <optional>

#include "nlohmann/json.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/serve_api/remote/config.hpp"

auto ConfigurationClient::CheckServeRemoteExecution() -> bool {
    auto client_remote_address = RemoteExecutionConfig::RemoteAddress();
    if (!client_remote_address) {
        logger_.Emit(LogLevel::Error,
                     "Internal error: the remote execution endpoint should "
                     "have been set.");
        return false;
    }
    auto client_serve_address = RemoteServeConfig::RemoteAddress();
    if (!client_serve_address) {
        logger_.Emit(
            LogLevel::Error,
            "Internal error: the serve endpoint should have been set.");
        return false;
    }

    grpc::ClientContext context;
    justbuild::just_serve::RemoteExecutionEndpointRequest request{};
    justbuild::just_serve::RemoteExecutionEndpointResponse response{};
    grpc::Status status =
        stub_->RemoteExecutionEndpoint(&context, request, &response);

    if (not status.ok()) {
        LogStatus(&logger_, LogLevel::Error, status);
        return false;
    }
    auto client_msg = client_remote_address->ToJson().dump();
    std::string serve_msg{};

    if (response.address().empty()) {
        // just serve acts as just execute, so from the server's perspective
        // there is nothing to be checked and it's the client's job to ensure
        // that its remote execution and serve endpoints match
        //
        // NOTE: This check might make sense to be removed altogether in the
        // future, or updated to (at most) a warning.
        if (client_remote_address->ToJson() == client_serve_address->ToJson()) {
            return true;
        }
        serve_msg = client_serve_address->ToJson().dump();
    }
    else {
        nlohmann::json serve_remote_endpoint{};
        try {
            serve_remote_endpoint = nlohmann::json::parse(response.address());
        } catch (std::exception const& ex) {
            logger_.Emit(
                LogLevel::Error,
                "Parsing configured address from response failed with:\n{}",
                ex.what());
        }
        if (serve_remote_endpoint == client_remote_address->ToJson()) {
            return true;
        }
        serve_msg = serve_remote_endpoint.dump();
    }
    // log any mismatch found
    logger_.Emit(
        LogLevel::Error,
        "Different execution endpoint detected!\nIn order to correctly use "
        "the serve service, its remote execution endpoint must be the same "
        "used by the client.\nserve remote endpoint:  {}\nclient remote "
        "endpoint: {}",
        serve_msg,
        client_msg);
    return false;
}

auto ConfigurationClient::IsCompatible() -> std::optional<bool> {
    grpc::ClientContext context;
    justbuild::just_serve::CompatibilityRequest request{};
    justbuild::just_serve::CompatibilityResponse response{};
    grpc::Status status = stub_->Compatibility(&context, request, &response);

    if (not status.ok()) {
        LogStatus(&logger_, LogLevel::Error, status);
        return std::nullopt;
    }
    return response.compatible();
}
