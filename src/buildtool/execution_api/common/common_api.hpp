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
#include "src/buildtool/execution_api/bazel_msg/blob_tree.hpp"
#include "src/buildtool/execution_api/bazel_msg/directory_tree.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"

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
    gsl::not_null<IExecutionApi*> const& api,
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
    res.digests = api->IsAvailable(digests);
    return res;
}

/// \brief Upload missing blobs from a given BlobTree.
[[nodiscard]] auto CommonUploadBlobTree(
    BlobTreePtr const& blob_tree,
    gsl::not_null<IExecutionApi*> const& api) noexcept -> bool;

/// \brief Runs the compatible branch of local/bazel UploadTree API.
[[nodiscard]] auto CommonUploadTreeCompatible(
    gsl::not_null<IExecutionApi*> const& api,
    DirectoryTreePtr const& build_root,
    BazelMsgFactory::LinkDigestResolveFunc const& resolve_links) noexcept
    -> std::optional<ArtifactDigest>;

/// \brief Runs the native branch of local/bazel UploadTree API.
[[nodiscard]] auto CommonUploadTreeNative(
    gsl::not_null<IExecutionApi*> const& api,
    DirectoryTreePtr const& build_root) noexcept
    -> std::optional<ArtifactDigest>;

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_COMMON_API_HPP
