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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_SERVE_MR_GIT_API_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_SERVE_MR_GIT_API_HPP

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "gsl/gsl"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/execution_api/common/artifact_blob_container.hpp"
#include "src/buildtool/execution_api/common/execution_action.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/execution_engine/dag/dag.hpp"
#include "src/buildtool/storage/config.hpp"

/// \brief Multi-repo-specific implementation of the abstract Execution API.
/// Handles interaction between the Git CAS and another api, irrespective of the
/// remote-execution protocol used. This instance cannot create actions or store
/// anything to the Git CAS, but has access to local storages.
class MRGitApi final : public IExecutionApi {
  public:
    MRGitApi(gsl::not_null<RepositoryConfig const*> const& repo_config,
             gsl::not_null<StorageConfig const*> const& native_storage_config,
             StorageConfig const* compatible_storage_config = nullptr,
             IExecutionApi const* compatible_local_api = nullptr) noexcept;

    /// \brief Not supported.
    [[nodiscard]] auto CreateAction(
        ArtifactDigest const& /*root_digest*/,
        std::vector<std::string> const& /*command*/,
        std::string const& /*cwd*/,
        std::vector<std::string> const& /*output_files*/,
        std::vector<std::string> const& /*output_dirs*/,
        std::map<std::string, std::string> const& /*env_vars*/,
        std::map<std::string, std::string> const& /*properties*/) const noexcept
        -> IExecutionAction::Ptr final {
        // Execution not supported.
        return nullptr;
    }

    /// \brief Not supported.
    // NOLINTNEXTLINE(google-default-arguments)
    [[nodiscard]] auto RetrieveToPaths(
        std::vector<Artifact::ObjectInfo> const& /*artifacts_info*/,
        std::vector<std::filesystem::path> const& /*output_paths*/,
        IExecutionApi const* /*alternative*/ = nullptr) const noexcept
        -> bool final {
        // Retrieval to paths not suported.
        return false;
    }

    /// \brief Not supported.
    // NOLINTNEXTLINE(google-default-arguments)
    [[nodiscard]] auto RetrieveToFds(
        std::vector<Artifact::ObjectInfo> const& /*artifacts_info*/,
        std::vector<int> const& /*fds*/,
        bool /*raw_tree*/,
        IExecutionApi const* /*alternative*/ = nullptr) const noexcept
        -> bool final {
        // Retrieval to file descriptors not supported.
        return false;
    }

    /// \brief Passes artifacts from Git CAS to specified (remote) api. In
    /// compatible mode, it must rehash the native digests to be able to upload
    /// to a compatible remote. Expects native digests.
    /// \note Caller is responsible for passing vectors with artifacts of the
    /// same digest type.
    [[nodiscard]] auto RetrieveToCas(
        std::vector<Artifact::ObjectInfo> const& artifacts_info,
        IExecutionApi const& api) const noexcept -> bool final;

    /// \brief Not supported.
    [[nodiscard]] auto RetrieveToMemory(
        Artifact::ObjectInfo const& /*artifact_info*/) const noexcept
        -> std::optional<std::string> final {
        // Retrieval to memory not supported.
        return std::nullopt;
    }

    /// \brief Not supported.
    // NOLINTNEXTLINE(google-default-arguments)
    [[nodiscard]] auto Upload(ArtifactBlobContainer&& /*blobs*/,
                              bool /*skip_find_missing*/ = false) const noexcept
        -> bool final {
        // Upload not suppoorted.
        return false;
    }

    /// \brief Not supported.
    [[nodiscard]] auto UploadTree(
        std::vector<DependencyGraph::NamedArtifactNodePtr> const& /*artifacts*/)
        const noexcept -> std::optional<ArtifactDigest> final {
        // Upload tree not supported.
        return std::nullopt;
    }

    /// \brief Not supported.
    [[nodiscard]] auto IsAvailable(
        ArtifactDigest const& /*digest*/) const noexcept -> bool final {
        // Not supported.
        return false;
    }

    /// \brief Not implemented.
    [[nodiscard]] auto GetMissingDigests(
        std::unordered_set<ArtifactDigest> const& /*digests*/) const noexcept
        -> std::unordered_set<ArtifactDigest> final {
        // Not supported.
        return {};
    }

  private:
    gsl::not_null<const RepositoryConfig*> repo_config_;

    // retain references to needed storages and configs
    gsl::not_null<StorageConfig const*> native_storage_config_;
    StorageConfig const* compat_storage_config_;

    // an api accessing compatible storage, used purely to communicate with a
    // compatible remote; only instantiated if in compatible mode
    IExecutionApi const* compat_local_api_;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_SERVE_MR_GIT_API_HPP
