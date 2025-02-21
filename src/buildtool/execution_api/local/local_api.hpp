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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_API_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_API_HPP

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "gsl/gsl"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_blob.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/common/execution_action.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/execution_api/git/git_api.hpp"
#include "src/buildtool/execution_api/local/context.hpp"
#include "src/buildtool/execution_engine/dag/dag.hpp"
#include "src/utils/cpp/tmp_dir.hpp"

class LocalApi final : public IExecutionApi {
  public:
    explicit LocalApi(gsl::not_null<LocalContext const*> const& local_context,
                      RepositoryConfig const* repo_config = nullptr) noexcept;

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
        IExecutionApi const* /*alternative*/ = nullptr) const noexcept
        -> bool final;

    // NOLINTNEXTLINE(google-default-arguments)
    [[nodiscard]] auto RetrieveToFds(
        std::vector<Artifact::ObjectInfo> const& artifacts_info,
        std::vector<int> const& fds,
        bool raw_tree,
        IExecutionApi const* /*alternative*/ = nullptr) const noexcept
        -> bool final;

    [[nodiscard]] auto RetrieveToCas(
        std::vector<Artifact::ObjectInfo> const& artifacts_info,
        IExecutionApi const& api) const noexcept -> bool final;

    [[nodiscard]] auto RetrieveToMemory(
        Artifact::ObjectInfo const& artifact_info) const noexcept
        -> std::optional<std::string> override;

    [[nodiscard]] auto Upload(std::unordered_set<ArtifactBlob>&& blobs,
                              bool /*skip_find_missing*/) const noexcept
        -> bool final;

    [[nodiscard]] auto UploadTree(
        std::vector<DependencyGraph::NamedArtifactNodePtr> const& artifacts)
        const noexcept -> std::optional<ArtifactDigest> final;

    [[nodiscard]] auto IsAvailable(ArtifactDigest const& digest) const noexcept
        -> bool final;

    [[nodiscard]] auto GetMissingDigests(
        std::unordered_set<ArtifactDigest> const& digests) const noexcept
        -> std::unordered_set<ArtifactDigest> final;

    [[nodiscard]] auto SplitBlob(ArtifactDigest const& blob_digest)
        const noexcept -> std::optional<std::vector<ArtifactDigest>> final;

    [[nodiscard]] auto BlobSplitSupport() const noexcept -> bool final {
        return true;
    }

    [[nodiscard]] auto SpliceBlob(
        ArtifactDigest const& blob_digest,
        std::vector<ArtifactDigest> const& chunk_digests) const noexcept
        -> std::optional<ArtifactDigest> final;

    [[nodiscard]] auto BlobSpliceSupport() const noexcept -> bool final {
        return true;
    }

    [[nodiscard]] auto GetHashType() const noexcept -> HashFunction::Type final;

    [[nodiscard]] auto GetTempSpace() const noexcept -> TmpDir::Ptr final;

  private:
    LocalContext const& local_context_;
    std::optional<GitApi> const git_api_;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_API_HPP
