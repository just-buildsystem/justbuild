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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_API_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_API_HPP

#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "gsl/gsl"
#include "src/buildtool/auth/authentication.hpp"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/remote/port.hpp"
#include "src/buildtool/common/remote/retry_config.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_common.hpp"
#include "src/buildtool/execution_api/common/artifact_blob.hpp"
#include "src/buildtool/execution_api/common/execution_action.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/execution_engine/dag/dag.hpp"

// forward declaration for actual implementations
class BazelNetwork;

/// \brief Bazel implementation of the abstract Execution API.
class BazelApi final : public IExecutionApi {
  public:
    BazelApi(std::string const& instance_name,
             std::string const& host,
             Port port,
             gsl::not_null<Auth const*> const& auth,
             gsl::not_null<RetryConfig const*> const& retry_config,
             ExecutionConfiguration const& exec_config,
             gsl::not_null<HashFunction const*> const& hash_function) noexcept;
    BazelApi(BazelApi const&) = delete;
    BazelApi(BazelApi&& other) noexcept;
    auto operator=(BazelApi const&) -> BazelApi& = delete;
    auto operator=(BazelApi&&) -> BazelApi& = delete;
    ~BazelApi() final;

    [[nodiscard]] auto CreateAction(
        ArtifactDigest const& root_digest,
        std::vector<std::string> const& command,
        std::string const& cwd,
        std::vector<std::string> const& output_files,
        std::vector<std::string> const& output_dirs,
        std::map<std::string, std::string> const& env_vars,
        std::map<std::string, std::string> const& properties) const noexcept
        -> IExecutionAction::Ptr final;

    // NOLINTNEXTLINE(google-default-arguments)
    [[nodiscard]] auto RetrieveToPaths(
        std::vector<Artifact::ObjectInfo> const& artifacts_info,
        std::vector<std::filesystem::path> const& output_paths,
        IExecutionApi const* alternative = nullptr) const noexcept
        -> bool final;

    // NOLINTNEXTLINE(google-default-arguments)
    [[nodiscard]] auto RetrieveToFds(
        std::vector<Artifact::ObjectInfo> const& artifacts_info,
        std::vector<int> const& fds,
        bool raw_tree,
        IExecutionApi const* alternative = nullptr) const noexcept
        -> bool final;

    [[nodiscard]] auto ParallelRetrieveToCas(
        std::vector<Artifact::ObjectInfo> const& artifacts_info,
        IExecutionApi const& api,
        std::size_t jobs,
        bool use_blob_splitting) const noexcept -> bool final;

    [[nodiscard]] auto RetrieveToCas(
        std::vector<Artifact::ObjectInfo> const& artifacts_info,
        IExecutionApi const& api) const noexcept -> bool final;

    [[nodiscard]] auto Upload(std::unordered_set<ArtifactBlob>&& blobs,
                              bool skip_find_missing) const noexcept
        -> bool final;

    [[nodiscard]] auto UploadTree(
        std::vector<DependencyGraph::NamedArtifactNodePtr> const& artifacts)
        const noexcept -> std::optional<ArtifactDigest> final;

    [[nodiscard]] auto IsAvailable(ArtifactDigest const& digest) const noexcept
        -> bool final;

    [[nodiscard]] auto GetMissingDigests(
        std::unordered_set<ArtifactDigest> const& digests) const noexcept
        -> std::unordered_set<ArtifactDigest> final;

    [[nodiscard]] auto RetrieveToMemory(
        Artifact::ObjectInfo const& artifact_info) const noexcept
        -> std::optional<std::string> final;

    [[nodiscard]] auto SplitBlob(ArtifactDigest const& blob_digest)
        const noexcept -> std::optional<std::vector<ArtifactDigest>> final;

    [[nodiscard]] auto BlobSplitSupport() const noexcept -> bool final;

    [[nodiscard]] auto SpliceBlob(
        ArtifactDigest const& blob_digest,
        std::vector<ArtifactDigest> const& chunk_digests) const noexcept
        -> std::optional<ArtifactDigest> final;

    [[nodiscard]] auto BlobSpliceSupport() const noexcept -> bool final;

  private:
    std::shared_ptr<BazelNetwork> network_;

    [[nodiscard]] auto ParallelRetrieveToCasWithCache(
        std::vector<Artifact::ObjectInfo> const& all_artifacts_info,
        IExecutionApi const& api,
        std::size_t jobs,
        bool use_blob_splitting,
        gsl::not_null<std::unordered_set<Artifact::ObjectInfo>*> done)
        const noexcept -> bool;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_API_HPP
