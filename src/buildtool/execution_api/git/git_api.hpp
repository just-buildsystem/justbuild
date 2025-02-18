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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_GIT_GIT_API_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_GIT_GIT_API_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "gsl/gsl"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"

class GitApi final {
  public:
    explicit GitApi(gsl::not_null<RepositoryConfig const*> const& repo_config);

    /// \brief Retrieve artifacts from git and store to specified paths.
    /// Tree artifacts are resolved and its containing file artifacts are
    /// recursively retrieved.
    [[nodiscard]] auto RetrieveToPaths(
        std::vector<Artifact::ObjectInfo> const& artifacts_info,
        std::vector<std::filesystem::path> const& output_paths) const noexcept
        -> bool;

    /// \brief Retrieve artifacts from git and write to file descriptors.
    /// Tree artifacts are not resolved and instead the tree object will be
    /// pretty-printed before writing to fd. If `raw_tree` is set, pretty
    /// printing will be omitted and the raw tree object will be written
    /// instead.
    [[nodiscard]] auto RetrieveToFds(
        std::vector<Artifact::ObjectInfo> const& artifacts_info,
        std::vector<int> const& fds,
        bool raw_tree) const noexcept -> bool;

    /// \brief Synchronization of artifacts between api and git. Retrieves
    /// artifacts from git and uploads to api. Tree artifacts are resolved and
    /// its containing file artifacts are recursively retrieved.
    [[nodiscard]] auto RetrieveToCas(
        std::vector<Artifact::ObjectInfo> const& artifacts_info,
        IExecutionApi const& api) const noexcept -> bool;

    /// \brief Retrieve one artifact from git and make it available for further
    /// in-memory processing
    [[nodiscard]] auto RetrieveToMemory(
        Artifact::ObjectInfo const& artifact_info) const noexcept
        -> std::optional<std::string>;

    /// \brief Check if the given digest is available in git.
    [[nodiscard]] auto IsAvailable(ArtifactDigest const& digest) const noexcept
        -> bool;

    [[nodiscard]] auto GetMissingDigests(
        std::unordered_set<ArtifactDigest> const& digests) const noexcept
        -> std::unordered_set<ArtifactDigest>;

  private:
    gsl::not_null<RepositoryConfig const*> repo_config_;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_GIT_GIT_API_HPP
