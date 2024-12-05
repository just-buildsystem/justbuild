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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_EXECUTION_APIHPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_EXECUTION_APIHPP

#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/buildtool/common/artifact.hpp"  // Artifact::ObjectInfo
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/execution_api/common/artifact_blob_container.hpp"
#include "src/buildtool/execution_api/common/execution_action.hpp"
#include "src/buildtool/execution_engine/dag/dag.hpp"

/// \brief Abstract remote execution API
/// Can be used to create actions.
class IExecutionApi {
  public:
    using Ptr = std::shared_ptr<IExecutionApi const>;

    IExecutionApi() = default;
    IExecutionApi(IExecutionApi const&) = delete;
    IExecutionApi(IExecutionApi&&) = default;
    auto operator=(IExecutionApi const&) -> IExecutionApi& = delete;
    auto operator=(IExecutionApi&&) -> IExecutionApi& = default;
    virtual ~IExecutionApi() = default;

    /// \brief Create a new action.
    /// \param[in] root_digest  Digest of the build root.
    /// \param[in] command      Command as argv vector
    /// \param[in] cwd          Working direcotry, relative to execution root
    /// \param[in] output_files List of paths to output files, relative to cwd
    /// \param[in] output_dirs  List of paths to output directories.
    /// \param[in] env_vars     The environment variables to set.
    /// \param[in] properties   Platform properties to set.
    /// \returns The new action.
    [[nodiscard]] virtual auto CreateAction(
        ArtifactDigest const& root_digest,
        std::vector<std::string> const& command,
        std::string const& cwd,
        std::vector<std::string> const& output_files,
        std::vector<std::string> const& output_dirs,
        std::map<std::string, std::string> const& env_vars,
        std::map<std::string, std::string> const& properties) const noexcept
        -> IExecutionAction::Ptr = 0;

    /// \brief Retrieve artifacts from CAS and store to specified paths.
    /// Tree artifacts are resolved its containing file artifacts are
    /// recursively retrieved.
    /// If the alternative is provided, it can be assumed that this
    /// alternative CAS is more close, but it might not contain all the
    /// needed artifacts.
    /// NOLINTNEXTLINE(google-default-arguments)
    [[nodiscard]] virtual auto RetrieveToPaths(
        std::vector<Artifact::ObjectInfo> const& artifacts_info,
        std::vector<std::filesystem::path> const& output_paths,
        IExecutionApi const* alternative = nullptr) const noexcept -> bool = 0;

    /// \brief Retrieve artifacts from CAS and write to file descriptors.
    /// Tree artifacts are not resolved and instead the tree object will be
    /// pretty-printed before writing to fd. If `raw_tree` is set, pretty
    /// printing will be omitted and the raw tree object will be written
    /// instead.
    /// NOLINTNEXTLINE(google-default-arguments)
    [[nodiscard]] virtual auto RetrieveToFds(
        std::vector<Artifact::ObjectInfo> const& artifacts_info,
        std::vector<int> const& fds,
        bool raw_tree,
        IExecutionApi const* alternative = nullptr) const noexcept -> bool = 0;

    /// \brief Synchronization of artifacts between two CASes. Retrieves
    /// artifacts from one CAS and writes to another CAS. Tree artifacts are
    /// resolved and its containing file artifacts are recursively retrieved.
    [[nodiscard]] virtual auto RetrieveToCas(
        std::vector<Artifact::ObjectInfo> const& artifacts_info,
        IExecutionApi const& api) const noexcept -> bool = 0;

    /// \brief A variant of RetrieveToCas that is allowed to internally use
    /// the specified number of threads to carry out the task in parallel.
    /// Given it is supported by the server, blob splitting enables traffic
    /// reduction when fetching blobs from the remote by reusing locally
    /// available blob chunks and just fetching unknown blob chunks to assemble
    /// the remote blobs.
    [[nodiscard]] virtual auto ParallelRetrieveToCas(
        std::vector<Artifact::ObjectInfo> const& artifacts_info,
        IExecutionApi const& api,
        std::size_t /* jobs */,
        bool /* use_blob_splitting */) const noexcept -> bool {
        return RetrieveToCas(artifacts_info, api);
    }

    /// \brief Retrieve one artifact from CAS and make it available for
    /// furter in-memory processing
    [[nodiscard]] virtual auto RetrieveToMemory(
        Artifact::ObjectInfo const& artifact_info) const noexcept
        -> std::optional<std::string> = 0;

    /// \brief Upload blobs to CAS. Uploads only the blobs that are not yet
    /// available in CAS, unless `skip_find_missing` is specified.
    /// \param blobs                Container of blobs to upload.
    /// \param skip_find_missing    Skip finding missing blobs, just upload all.
    /// NOLINTNEXTLINE(google-default-arguments)
    [[nodiscard]] virtual auto Upload(
        ArtifactBlobContainer&& blobs,
        bool skip_find_missing = false) const noexcept -> bool = 0;

    [[nodiscard]] virtual auto UploadTree(
        std::vector<DependencyGraph::NamedArtifactNodePtr> const& artifacts)
        const noexcept -> std::optional<ArtifactDigest> = 0;

    [[nodiscard]] virtual auto IsAvailable(
        ArtifactDigest const& digest) const noexcept -> bool = 0;

    [[nodiscard]] virtual auto IsAvailable(
        std::vector<ArtifactDigest> const& digests) const noexcept
        -> std::vector<ArtifactDigest> = 0;

    [[nodiscard]] virtual auto SplitBlob(ArtifactDigest const& /*blob_digest*/)
        const noexcept -> std::optional<std::vector<ArtifactDigest>> {
        return std::nullopt;
    }

    [[nodiscard]] virtual auto BlobSplitSupport() const noexcept -> bool {
        return false;
    }

    [[nodiscard]] virtual auto SpliceBlob(
        ArtifactDigest const& /*blob_digest*/,
        std::vector<ArtifactDigest> const& /*chunk_digests*/) const noexcept
        -> std::optional<ArtifactDigest> {
        return std::nullopt;
    }

    [[nodiscard]] virtual auto BlobSpliceSupport() const noexcept -> bool {
        return false;
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_EXECUTION_APIHPP
