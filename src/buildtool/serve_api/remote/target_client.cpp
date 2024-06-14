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

#ifndef BOOTSTRAP_BUILD_TOOL

#include "src/buildtool/serve_api/remote/target_client.hpp"

#include <exception>
#include <utility>

#include "fmt/core.h"
#include "nlohmann/json.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/common/remote/client_common.hpp"
#include "src/buildtool/logging/log_level.hpp"

TargetClient::TargetClient(std::string const& server, Port port) noexcept {
    stub_ = justbuild::just_serve::Target::NewStub(
        CreateChannelWithCredentials(server, port));
}

auto TargetClient::ServeTarget(const TargetCacheKey& key,
                               const std::string& repo_key) const noexcept
    -> std::optional<serve_target_result_t> {
    // make sure the blob containing the key is in the remote cas
    if (!local_api_->RetrieveToCas({key.Id()}, &*remote_api_)) {
        return serve_target_result_t{
            std::in_place_index<1>,
            fmt::format("Failed to retrieve to remote cas ObjectInfo {}",
                        key.Id().ToString())};
    }
    // make sure the repository configuration blob is in the remote cas
    if (!local_api_->RetrieveToCas(
            {Artifact::ObjectInfo{.digest = ArtifactDigest{repo_key, 0, false},
                                  .type = ObjectType::File}},
            &*remote_api_)) {
        return serve_target_result_t{
            std::in_place_index<1>,
            fmt::format("Failed to retrieve to remote cas blob {}", repo_key)};
    }

    // add target cache key to request
    bazel_re::Digest key_dgst{key.Id().digest};
    justbuild::just_serve::ServeTargetRequest request{};
    request.mutable_target_cache_key_id()->CopyFrom(key_dgst);

    // add execution properties to request
    for (auto const& [k, v] : RemoteExecutionConfig::PlatformProperties()) {
        auto* prop = request.add_execution_properties();
        prop->set_name(k);
        prop->set_value(v);
    }

    // add dispatch information to request, while ensuring blob is uploaded
    // to remote cas
    auto dispatch_list = nlohmann::json::array();
    try {
        for (auto const& [props, endpoint] :
             RemoteExecutionConfig::DispatchList()) {
            auto entry = nlohmann::json::array();
            entry.push_back(nlohmann::json(props));
            entry.push_back(endpoint.ToJson());
            dispatch_list.push_back(entry);
        }
    } catch (std::exception const& ex) {
        return serve_target_result_t{
            std::in_place_index<1>,
            fmt::format("Populating dispatch JSON array failed with:\n{}",
                        ex.what())};
    }

    auto dispatch_dgst =
        Storage::Instance().CAS().StoreBlob(dispatch_list.dump(2));
    if (not dispatch_dgst) {
        return serve_target_result_t{
            std::in_place_index<1>,
            fmt::format("Failed to store blob {} to local cas",
                        dispatch_list.dump(2))};
    }
    auto const& dispatch_info = Artifact::ObjectInfo{
        .digest = ArtifactDigest{*dispatch_dgst}, .type = ObjectType::File};
    if (!local_api_->RetrieveToCas({dispatch_info}, &*remote_api_)) {
        return serve_target_result_t{
            std::in_place_index<1>,
            fmt::format("Failed to upload blob {} to remote cas",
                        dispatch_info.ToString())};
    }
    request.mutable_dispatch_info()->CopyFrom(*dispatch_dgst);

    // call rpc
    grpc::ClientContext context;
    justbuild::just_serve::ServeTargetResponse response;
    auto const& status = stub_->ServeTarget(&context, request, &response);

    // differentiate status codes
    switch (status.error_code()) {
        case grpc::StatusCode::OK: {
            // if log has been set, pass it along as index 0
            if (response.has_log()) {
                return serve_target_result_t{
                    std::in_place_index<0>,
                    ArtifactDigest(response.log()).hash()};
            }
            // if no log has been set, it must have the target cache value
            if (not response.has_target_value()) {
                return serve_target_result_t{
                    std::in_place_index<1>,
                    "Serve endpoint failed to set expected response field"};
            }
            auto const& target_value_dgst =
                ArtifactDigest{response.target_value()};
            auto const& obj_info = Artifact::ObjectInfo{
                .digest = target_value_dgst, .type = ObjectType::File};
            if (!local_api_->IsAvailable(target_value_dgst)) {
                if (!remote_api_->RetrieveToCas({obj_info}, &*local_api_)) {
                    return serve_target_result_t{
                        std::in_place_index<1>,
                        fmt::format(
                            "Failed to retrieve blob {} from remote cas",
                            obj_info.ToString())};
                }
            }
            auto const& target_value_str =
                local_api_->RetrieveToMemory(obj_info);
            if (!target_value_str) {
                return serve_target_result_t{
                    std::in_place_index<1>,
                    fmt::format("Failed to retrieve blob {} from local cas",
                                obj_info.ToString())};
            }
            try {
                auto const& result = TargetCacheEntry::FromJson(
                    nlohmann::json::parse(*target_value_str));
                // return the target cache value information
                return serve_target_result_t{std::in_place_index<3>,
                                             std::make_pair(result, obj_info)};
            } catch (std::exception const& ex) {
                return serve_target_result_t{
                    std::in_place_index<1>,
                    fmt::format("Parsing target cache value failed with:\n{}",
                                ex.what())};
            }
        }
        case grpc::StatusCode::INTERNAL: {
            return serve_target_result_t{
                std::in_place_index<1>,
                fmt::format(
                    "Serve endpoint reported the fatal internal error:\n{}",
                    status.error_message())};
        }
        case grpc::StatusCode::NOT_FOUND: {
            return std::nullopt;  // this might allow a local build to continue
        }
        default:;  // fallthrough
    }
    return serve_target_result_t{
        std::in_place_index<2>,
        fmt::format("Serve endpoint failed with:\n{}", status.error_message())};
}

auto TargetClient::ServeTargetVariables(std::string const& target_root_id,
                                        std::string const& target_file,
                                        std::string const& target)
    const noexcept -> std::optional<std::vector<std::string>> {
    justbuild::just_serve::ServeTargetVariablesRequest request{};
    request.set_root_tree(target_root_id);
    request.set_target_file(target_file);
    request.set_target(target);

    grpc::ClientContext context;
    justbuild::just_serve::ServeTargetVariablesResponse response;
    grpc::Status status =
        stub_->ServeTargetVariables(&context, request, &response);

    if (not status.ok()) {
        LogStatus(&logger_, LogLevel::Error, status);
        return std::nullopt;
    }
    auto size = response.flexible_config_size();
    if (size == 0) {
        return std::vector<std::string>();
    }
    std::vector<std::string> res{};
    res.reserve(size);
    for (auto const& var : response.flexible_config()) {
        res.emplace_back(var);
    }
    return res;
}

auto TargetClient::ServeTargetDescription(
    std::string const& target_root_id,
    std::string const& target_file,
    std::string const& target) const noexcept -> std::optional<ArtifactDigest> {
    justbuild::just_serve::ServeTargetDescriptionRequest request{};
    request.set_root_tree(target_root_id);
    request.set_target_file(target_file);
    request.set_target(target);

    grpc::ClientContext context;
    justbuild::just_serve::ServeTargetDescriptionResponse response;
    grpc::Status status =
        stub_->ServeTargetDescription(&context, request, &response);

    if (not status.ok()) {
        LogStatus(&logger_, LogLevel::Error, status);
        return std::nullopt;
    }
    return ArtifactDigest{response.description_id()};
}

#endif  // BOOTSTRAP_BUILD_TOOL
