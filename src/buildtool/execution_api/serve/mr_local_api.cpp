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

#include "src/buildtool/execution_api/serve/mr_local_api.hpp"

#include <functional>
#include <utility>

#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/execution_api/utils/rehash_utils.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/utils/cpp/expected.hpp"

MRLocalApi::MRLocalApi(
    gsl::not_null<LocalContext const*> const& native_context,
    gsl::not_null<IExecutionApi const*> const& native_local_api,
    LocalContext const* compatible_context,
    IExecutionApi const* compatible_local_api) noexcept
    : native_context_{native_context},
      compat_context_{compatible_context},
      native_local_api_{native_local_api},
      compat_local_api_{compatible_local_api} {}

// NOLINTNEXTLINE(google-default-arguments)
auto MRLocalApi::RetrieveToPaths(
    std::vector<Artifact::ObjectInfo> const& artifacts_info,
    std::vector<std::filesystem::path> const& output_paths,
    IExecutionApi const* /*alternative*/) const noexcept -> bool {
    // This method can legitimately be called with both native and
    // compatible digests when in compatible mode, therefore we need to
    // interrogate the hash type of the input.

    // we need at least one digest to interrogate the hash type
    if (artifacts_info.empty()) {
        return true;  // nothing to do
    }
    // native artifacts get dispatched to native local api
    if (ProtocolTraits::IsNative(artifacts_info[0].digest.GetHashType())) {
        return native_local_api_->RetrieveToPaths(artifacts_info, output_paths);
    }
    // compatible digests get dispatched to compatible local api
    if (compat_local_api_ == nullptr) {
        Logger::Log(LogLevel::Error,
                    "MRLocalApi: Unexpected digest type provided");
        return false;
    }
    return compat_local_api_->RetrieveToPaths(artifacts_info, output_paths);
}

auto MRLocalApi::RetrieveToCas(
    std::vector<Artifact::ObjectInfo> const& artifacts_info,
    IExecutionApi const& api) const noexcept -> bool {
    // return immediately if being passed the same api
    if (this == &api) {
        return true;
    }

    // in native mode: dispatch directly to native local api
    if (compat_local_api_ == nullptr) {
        return native_local_api_->RetrieveToCas(artifacts_info, api);
    }

    // in compatible mode: if compatible hashes passed, dispatch them to
    // compatible local api
    if (not artifacts_info.empty() and
        not ProtocolTraits::IsNative(artifacts_info[0].digest.GetHashType())) {
        return compat_local_api_->RetrieveToCas(artifacts_info, api);
    }

    auto compat_artifacts = RehashUtils::RehashDigest(
        artifacts_info,
        *native_context_->storage_config,
        *compat_context_->storage_config,
        /*apis=*/std::nullopt);  // all parts of git trees are present locally
    if (not compat_artifacts) {
        Logger::Log(LogLevel::Error,
                    "MRLocalApi: {}",
                    std::move(compat_artifacts).error());
        return false;
    }
    return compat_local_api_->RetrieveToCas(*compat_artifacts, api);
}

// NOLINTNEXTLINE(google-default-arguments)
auto MRLocalApi::Upload(ArtifactBlobContainer&& blobs,
                        bool skip_find_missing) const noexcept -> bool {
    // in native mode, dispatch to native local api
    if (compat_local_api_ == nullptr) {
        return native_local_api_->Upload(std::move(blobs), skip_find_missing);
    }
    // in compatible mode, dispatch to compatible local api
    return compat_local_api_->Upload(std::move(blobs), skip_find_missing);
}

auto MRLocalApi::IsAvailable(ArtifactDigest const& digest) const noexcept
    -> bool {
    // This method can legitimately be called with both native and
    // compatible digests when in compatible mode, therefore we need to
    // interrogate the hash type of the input.

    // a native digest gets dispatched to native local api
    if (ProtocolTraits::IsNative(digest.GetHashType())) {
        return native_local_api_->IsAvailable(digest);
    }
    // compatible digests get dispatched to compatible local api
    if (compat_local_api_ == nullptr) {
        Logger::Log(LogLevel::Warning,
                    "MRLocalApi: unexpected digest type provided");
        return false;
    }
    return compat_local_api_->IsAvailable(digest);
}

auto MRLocalApi::IsAvailable(std::unordered_set<ArtifactDigest> const& digests)
    const noexcept -> std::unordered_set<ArtifactDigest> {
    // This method can legitimately be called with both native and
    // compatible digests when in compatible mode, therefore we need to
    // interrogate the hash type of the input.

    // we need at least one digest to interrogate the hash type
    if (digests.empty()) {
        return {};  // nothing to do
    }
    // native digests get dispatched to native local api
    if (ProtocolTraits::IsNative(digests.begin()->GetHashType())) {
        return native_local_api_->IsAvailable(digests);
    }
    // compatible digests get dispatched to compatible local api
    if (compat_local_api_ == nullptr) {
        Logger::Log(LogLevel::Warning,
                    "MRLocalApi: Unexpected digest type provided");
        return {};
    }
    return compat_local_api_->IsAvailable(digests);
}
