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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_COMMON_API_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_COMMON_API_HPP

#include <cstdio>
#include <exception>
#include <functional>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gsl/gsl"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
#include "src/buildtool/execution_api/bazel_msg/directory_tree.hpp"
#include "src/buildtool/execution_api/common/blob_tree.hpp"
#include "src/buildtool/execution_api/common/content_blob_container.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/execution_api/common/message_limits.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

/// \brief Stores a list of missing artifact digests, as well as a back-mapping
/// to some given original type.
template <typename T>
struct MissingArtifactsInfo {
    std::vector<ArtifactDigest> digests;
    std::unordered_map<ArtifactDigest, T> back_map;
};

/// \brief Common logic for RetrieveToFds.
/// \param dump_to_stream Dumps the artifact to the respective open stream.
/// \param fallback Processes the respective file descriptor further in case the
/// regular dump fails.
[[nodiscard]] auto CommonRetrieveToFds(
    std::vector<Artifact::ObjectInfo> const& artifacts_info,
    std::vector<int> const& fds,
    std::function<bool(Artifact::ObjectInfo const&,
                       gsl::not_null<FILE*> const&)> const& dump_to_stream,
    std::optional<std::function<bool(Artifact::ObjectInfo const&, int)>> const&
        fallback) noexcept -> bool;

/// \brief Get the missing artifacts from a given input list, needed, e.g., to
/// be uploaded.
/// \returns A struct storing the missing artifacts and a back-mapping to the
/// original given type, or nullopt in case of exceptions.
template <typename T>
[[nodiscard]] auto GetMissingArtifactsInfo(
    IExecutionApi const& api,
    typename std::vector<T>::const_iterator const& begin,
    typename std::vector<T>::const_iterator const& end,
    typename std::function<ArtifactDigest(T const&)> const& converter) noexcept
    -> std::optional<MissingArtifactsInfo<T>> {
    std::vector<ArtifactDigest> digests;
    digests.reserve(end - begin);
    MissingArtifactsInfo<T> res{};
    for (auto it = begin; it != end; ++it) {
        try {
            auto dgst = converter(*it);  // can't enforce it to be noexcept
            digests.emplace_back(dgst);
            res.back_map.emplace(std::move(dgst), *it);
        } catch (...) {
            return std::nullopt;
        }
    }
    res.digests = api.IsAvailable(digests);
    return res;
}

/// \brief Upload missing blobs from a given BlobTree.
[[nodiscard]] auto CommonUploadBlobTree(BlobTreePtr const& blob_tree,
                                        IExecutionApi const& api) noexcept
    -> bool;

/// \brief Runs the compatible branch of local/bazel UploadTree API.
[[nodiscard]] auto CommonUploadTreeCompatible(
    IExecutionApi const& api,
    DirectoryTreePtr const& build_root,
    BazelMsgFactory::LinkDigestResolveFunc const& resolve_links) noexcept
    -> std::optional<ArtifactDigest>;

/// \brief Runs the native branch of local/bazel UploadTree API.
[[nodiscard]] auto CommonUploadTreeNative(
    IExecutionApi const& api,
    DirectoryTreePtr const& build_root) noexcept
    -> std::optional<ArtifactDigest>;

/// \brief Updates the given container based on the given blob, ensuring the
/// container is kept under the maximum transfer limit. If the given blob is
/// larger than the transfer limit, it is immediately uploaded. Otherwise,
/// it is added to the container if it fits inside the transfer limit, or it
/// is added to a new container moving forward, with the old one being uploaded.
/// This way we ensure we only store as much data as we can actually transfer in
/// one go.
/// \param container Stores blobs smaller than the transfer limit.
/// \param blob New blob to be handled (uploaded or added to container).
/// \param exception_is_fatal If true, caught exceptions are logged to Error.
/// \param uploader Lambda handling the actual upload call.
/// \param logger Use this instance for any logging. If nullptr, use the default
/// logger. This value is used only if exception_is_fatal==true.
/// \returns Returns true on success, false otherwise (failures or exceptions).
template <typename TDigest>
auto UpdateContainerAndUpload(
    gsl::not_null<ContentBlobContainer<TDigest>*> const& container,
    ContentBlob<TDigest>&& blob,
    bool exception_is_fatal,
    std::function<bool(ContentBlobContainer<TDigest>&&)> const& uploader,
    Logger const* logger = nullptr) noexcept -> bool {
    // Optimize upload of blobs with respect to the maximum transfer limit, such
    // that we never store unnecessarily more data in the container than we need
    // per remote transfer.
    try {
        if (blob.data->size() > kMaxBatchTransferSize) {
            // large blobs use individual stream upload
            if (not uploader(ContentBlobContainer<TDigest>{{blob}})) {
                return false;
            }
        }
        else {
            if (container->ContentSize() + blob.data->size() >
                kMaxBatchTransferSize) {
                // swap away from original container to allow move during upload
                ContentBlobContainer<TDigest> tmp_container{};
                std::swap(*container, tmp_container);
                // if we would surpass the transfer limit, upload the current
                // container and clear it before adding more blobs
                if (not uploader(std::move(tmp_container))) {
                    return false;
                }
            }
            // add current blob to container
            container->Emplace(std::move(blob));
        }
    } catch (std::exception const& ex) {
        if (exception_is_fatal) {
            Logger::Log(logger,
                        LogLevel::Error,
                        "failed to emplace blob with\n:{}",
                        ex.what());
        }
        return false;
    }
    return true;  // success!
}

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_COMMON_API_HPP
