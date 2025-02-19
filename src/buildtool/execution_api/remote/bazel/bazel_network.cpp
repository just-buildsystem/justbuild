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

#include "src/buildtool/execution_api/remote/bazel/bazel_network.hpp"

#include <functional>
#include <utility>

#include "src/buildtool/execution_api/common/message_limits.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/back_map.hpp"

BazelNetwork::BazelNetwork(
    std::string instance_name,
    std::string const& host,
    Port port,
    gsl::not_null<Auth const*> const& auth,
    gsl::not_null<RetryConfig const*> const& retry_config,
    ExecutionConfiguration const& exec_config,
    HashFunction hash_function) noexcept
    : instance_name_{std::move(instance_name)},
      capabilities_{std::make_unique<BazelCapabilitiesClient>(host,
                                                              port,
                                                              auth,
                                                              retry_config)},
      cas_{std::make_unique<BazelCasClient>(host,
                                            port,
                                            auth,
                                            retry_config,
                                            capabilities_.get())},
      ac_{std::make_unique<BazelAcClient>(host, port, auth, retry_config)},
      exec_{std::make_unique<BazelExecutionClient>(host,
                                                   port,
                                                   auth,
                                                   retry_config)},
      exec_config_{exec_config},
      hash_function_{hash_function} {}

auto BazelNetwork::IsAvailable(ArtifactDigest const& digest) const noexcept
    -> bool {
    return cas_->FindMissingBlobs(instance_name_, {digest}).empty();
}

auto BazelNetwork::FindMissingBlobs(
    std::unordered_set<ArtifactDigest> const& digests) const noexcept
    -> std::unordered_set<ArtifactDigest> {
    return cas_->FindMissingBlobs(instance_name_, digests);
}

auto BazelNetwork::SplitBlob(bazel_re::Digest const& blob_digest) const noexcept
    -> std::optional<std::vector<bazel_re::Digest>> {
    return cas_->SplitBlob(hash_function_, instance_name_, blob_digest);
}

auto BazelNetwork::SpliceBlob(
    bazel_re::Digest const& blob_digest,
    std::vector<bazel_re::Digest> const& chunk_digests) const noexcept
    -> std::optional<bazel_re::Digest> {
    return cas_->SpliceBlob(
        hash_function_, instance_name_, blob_digest, chunk_digests);
}

auto BazelNetwork::BlobSplitSupport() const noexcept -> bool {
    return cas_->BlobSplitSupport(hash_function_, instance_name_);
}

auto BazelNetwork::BlobSpliceSupport() const noexcept -> bool {
    return cas_->BlobSpliceSupport(hash_function_, instance_name_);
}

auto BazelNetwork::DoUploadBlobs(
    std::unordered_set<ArtifactBlob> blobs) noexcept -> bool {
    if (blobs.empty()) {
        return true;
    }

    try {
        // First upload all blobs that must use bytestream api because of their
        // size:
        for (auto it = blobs.begin(); it != blobs.end();) {
            if (it->data->size() <= MessageLimits::kMaxGrpcLength) {
                ++it;
                continue;
            }
            if (not cas_->UpdateSingleBlob(instance_name_, *it)) {
                return false;
            }
            it = blobs.erase(it);
        }

        // After uploading via stream api, only small blobs that may be uploaded
        // using batch are in the container:
        return cas_->BatchUpdateBlobs(instance_name_, blobs) == blobs.size();

    } catch (...) {
        Logger::Log(LogLevel::Warning, "Unknown exception");
        return false;
    }
}

auto BazelNetwork::UploadBlobs(std::unordered_set<ArtifactBlob>&& blobs,
                               bool skip_find_missing) noexcept -> bool {
    if (not skip_find_missing) {
        auto const back_map = BackMap<ArtifactDigest, ArtifactBlob>::Make(
            &blobs, [](ArtifactBlob const& blob) { return blob.digest; });
        if (back_map == nullptr) {
            return false;
        }

        // back_map becomes invalid after this call:
        blobs = back_map->GetValues(FindMissingBlobs(back_map->GetKeys()));
    }
    return DoUploadBlobs(std::move(blobs));
}

auto BazelNetwork::ExecuteBazelActionSync(
    bazel_re::Digest const& action) noexcept
    -> std::optional<BazelExecutionClient::ExecutionOutput> {
    auto response =
        exec_->Execute(instance_name_, action, exec_config_, true /*wait*/);

    if (response.state ==
        BazelExecutionClient::ExecutionResponse::State::Ongoing) {
        Logger::Log(
            LogLevel::Trace, "Waiting for {}", response.execution_handle);
        response = exec_->WaitExecution(response.execution_handle);
    }
    if (response.state !=
            BazelExecutionClient::ExecutionResponse::State::Finished or
        not response.output) {
        Logger::Log(LogLevel::Warning,
                    "Failed to execute action with execution id {}.",
                    action.hash());
        return std::nullopt;
    }

    return response.output;
}

auto BazelNetwork::CreateReader() const noexcept -> BazelNetworkReader {
    return BazelNetworkReader{instance_name_, cas_.get(), hash_function_};
}

auto BazelNetwork::GetCachedActionResult(
    bazel_re::Digest const& action,
    std::vector<std::string> const& output_files) const noexcept
    -> std::optional<bazel_re::ActionResult> {
    return ac_->GetActionResult(
        instance_name_, action, false, false, output_files);
}
