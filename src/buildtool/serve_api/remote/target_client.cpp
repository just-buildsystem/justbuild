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

#include "src/buildtool/serve_api/remote/target_client.hpp"

#include <exception>
#include <utility>

#include "nlohmann/json.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/common/remote/client_common.hpp"

TargetClient::TargetClient(std::string const& server, Port port) noexcept {
    stub_ = justbuild::just_serve::Target::NewStub(
        CreateChannelWithCredentials(server, port));
}

auto TargetClient::ServeTarget(const TargetCacheKey& key,
                               const std::string& repo_key) noexcept
    -> std::optional<std::pair<TargetCacheEntry, Artifact::ObjectInfo>> {
    // make sure the blob containing the key is in the remote cas
    if (!local_api_->RetrieveToCas({key.Id()}, &*remote_api_)) {
        logger_.Emit(LogLevel::Performance,
                     "failed to retrieve to remote cas ObjectInfo {}",
                     key.Id().ToString());
        return std::nullopt;
    }
    // make sure the repository configuration blob is in the remote cas
    if (!local_api_->RetrieveToCas(
            {Artifact::ObjectInfo{.digest = ArtifactDigest{repo_key, 0, false},
                                  .type = ObjectType::File}},
            &*remote_api_)) {
        logger_.Emit(LogLevel::Performance,
                     "failed to retrieve to remote cas blob {}",
                     repo_key);
        return std::nullopt;
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
        logger_.Emit(LogLevel::Performance,
                     "populating dispatch JSON array failed with:\n{}",
                     ex.what());
        return std::nullopt;
    }

    auto dispatch_dgst =
        Storage::Instance().CAS().StoreBlob(dispatch_list.dump(2));
    if (not dispatch_dgst) {
        logger_.Emit(LogLevel::Performance,
                     "failed to store blob {} to local cas",
                     dispatch_list.dump(2));
        return std::nullopt;
    }
    auto const& dispatch_info = Artifact::ObjectInfo{
        .digest = ArtifactDigest{*dispatch_dgst}, .type = ObjectType::File};
    if (!local_api_->RetrieveToCas({dispatch_info}, &*remote_api_)) {
        logger_.Emit(LogLevel::Performance,
                     "failed to upload blob {} to remote cas",
                     dispatch_info.ToString());
        return std::nullopt;
    }
    request.mutable_dispatch_info()->CopyFrom(*dispatch_dgst);

    // call rpc
    grpc::ClientContext context;
    justbuild::just_serve::ServeTargetResponse response;
    auto const& status = stub_->ServeTarget(&context, request, &response);
    if (!status.ok()) {
        LogStatus(&logger_, LogLevel::Performance, status);
        return std::nullopt;
    }

    auto const& target_value_dgst = ArtifactDigest{response.target_value()};
    auto const& obj_info = Artifact::ObjectInfo{.digest = target_value_dgst,
                                                .type = ObjectType::File};
    if (!local_api_->IsAvailable(target_value_dgst)) {
        if (!remote_api_->RetrieveToCas({obj_info}, &*local_api_)) {
            logger_.Emit(LogLevel::Performance,
                         "failed to retrieve blob {} from remote cas",
                         obj_info.ToString());
            return std::nullopt;
        }
    }

    auto const& target_value_str = local_api_->RetrieveToMemory(obj_info);
    if (!target_value_str) {
        logger_.Emit(LogLevel::Performance,
                     "failed to retrieve blob {} from local cas",
                     obj_info.ToString());
        return std::nullopt;
    }
    try {
        auto const& result = TargetCacheEntry::FromJson(
            nlohmann::json::parse(*target_value_str));
        return std::make_pair(result, obj_info);
    } catch (std::exception const& ex) {
        logger_.Emit(LogLevel::Performance,
                     "parsing target cache value failed with:\n{}",
                     ex.what());
        return std::nullopt;
    }
}

auto TargetClient::ServeTargetVariables(std::string const& target_root_id,
                                        std::string const& target_file,
                                        std::string const& target) noexcept
    -> std::optional<std::vector<std::string>> {
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

auto TargetClient::ServeTargetDescription(std::string const& target_root_id,
                                          std::string const& target_file,
                                          std::string const& target) noexcept
    -> std::optional<ArtifactDigest> {
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
