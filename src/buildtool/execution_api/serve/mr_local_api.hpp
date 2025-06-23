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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_SERVE_MR_LOCAL_API_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_SERVE_MR_LOCAL_API_HPP

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
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/common/execution_action.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/execution_api/local/context.hpp"
#include "src/buildtool/execution_engine/dag/dag.hpp"
#include "src/utils/cpp/tmp_dir.hpp"

/// \brief Multi-repo-specific implementation of the abstract Execution API.
/// Handles interaction between a native storage and a remote, irrespective of
/// the remote protocol used. In compatible mode, both native and compatible
/// storages are available.
class MRLocalApi final : public IExecutionApi {
  public:
    /// \brief Construct a new MRLocalApi object. In native mode only the native
    /// storage in instantiated (hence behaving like regular LocalApi), while in
    /// compatible mode both storages are instantiated.
    MRLocalApi(gsl::not_null<LocalContext const*> const& native_context,
               gsl::not_null<IExecutionApi const*> const& native_local_api,
               LocalContext const* compatible_context = nullptr,
               IExecutionApi const* compatible_local_api = nullptr) noexcept;

    /// \brief Not supported.
    [[nodiscard]] auto CreateAction(
        ArtifactDigest const& /*root_digest*/,
        std::vector<std::string> const& /*command*/,
        std::string const& /*cwd*/,
        std::vector<std::string> const& /*output_files*/,
        std::vector<std::string> const& /*output_dirs*/,
        std::map<std::string, std::string> const& /*env_vars*/,
        std::map<std::string, std::string> const& /*properties*/,
        bool /*force_legacy*/) const noexcept -> IExecutionAction::Ptr final {
        // Execution not supported
        return nullptr;
    }

    /// \brief Stages artifacts from CAS to the file system.
    /// Handles both native and compatible artifacts. Dispatches to appropriate
    /// local api instance based on digest hash type. Alternative api is never
    /// used.
    // NOLINTNEXTLINE(google-default-arguments)
    [[nodiscard]] auto RetrieveToPaths(
        std::vector<Artifact::ObjectInfo> const& artifacts_info,
        std::vector<std::filesystem::path> const& output_paths,
        IExecutionApi const* /*alternative*/ = nullptr) const noexcept
        -> bool final;

    // NOLINTNEXTLINE(google-default-arguments)
    [[nodiscard]] auto RetrieveToFds(
        std::vector<Artifact::ObjectInfo> const& /*artifacts_info*/,
        std::vector<int> const& /*fds*/,
        bool /*raw_tree*/,
        IExecutionApi const* /*alternative*/ = nullptr) const noexcept
        -> bool final {
        // Retrieval to file descriptors not supported
        return false;
    }

    /// \brief Passes artifacts from native CAS to specified api. Handles both
    /// native and compatible digests. In compatible mode, if passed native
    /// digests it must rehash them to be able to upload to a compatible remote.
    /// \note Caller is responsible for passing vectors with artifacts of the
    /// same digest type. For simplicity, this method takes the first digest of
    /// the vector as representative for figuring out hash function type.
    [[nodiscard]] auto RetrieveToCas(
        std::vector<Artifact::ObjectInfo> const& artifacts_info,
        IExecutionApi const& api) const noexcept -> bool final;

    [[nodiscard]] auto RetrieveToMemory(
        Artifact::ObjectInfo const& /*artifact_info*/) const noexcept
        -> std::optional<std::string> final {
        // Retrieval to memory not supported
        return std::nullopt;
    }

    /// \brief Uploads artifacts from local CAS into specified api. Dispatches
    /// the blobs to the appropriate local api instance based on used protocol.
    /// \note Caller is responsible for passing vectors with artifacts of the
    /// same digest type.
    // NOLINTNEXTLINE(google-default-arguments)
    [[nodiscard]] auto Upload(std::unordered_set<ArtifactBlob>&& blobs,
                              bool skip_find_missing = false) const noexcept
        -> bool final;

    [[nodiscard]] auto UploadTree(
        std::vector<DependencyGraph::NamedArtifactNodePtr> const& /*artifacts*/)
        const noexcept -> std::optional<ArtifactDigest> final {
        // Upload tree not supported -- only used in execution
        return std::nullopt;
    }

    /// \brief Check availability of an artifact in CAS. Handles both native and
    /// compatible digests. Dispatches to appropriate local api instance based
    /// on digest hash type.
    [[nodiscard]] auto IsAvailable(ArtifactDigest const& digest) const noexcept
        -> bool final;

    /// \brief Check availability of artifacts in CAS. Handles both native and
    /// compatible digests. Dispatches to appropriate local api instance based
    /// on hash type of digests.
    /// \note The caller is responsible for passing vectors with digests of the
    /// same type. For simplicity, this method takes the first digest of the
    /// vector as representative for figuring out hash function type.
    [[nodiscard]] auto GetMissingDigests(
        std::unordered_set<ArtifactDigest> const& digests) const noexcept
        -> std::unordered_set<ArtifactDigest> final;

    [[nodiscard]] auto GetHashType() const noexcept -> HashFunction::Type final;

    [[nodiscard]] auto GetTempSpace() const noexcept -> TmpDir::Ptr final;

  private:
    // retain local context references to have direct access to storages
    gsl::not_null<LocalContext const*> native_context_;
    LocalContext const* compat_context_;

    // local api accessing native storage; all artifacts must pass through it
    gsl::not_null<IExecutionApi const*> native_local_api_;
    // local api accessing compatible storage, used purely to communicate with
    // a compatible remote; only instantiated if in compatible mode
    IExecutionApi const* compat_local_api_;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_SERVE_MR_LOCAL_API_HPP
