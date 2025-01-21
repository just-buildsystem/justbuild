// Copyright 2025 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef BOOTSTRAP_BUILD_TOOL

#include "src/buildtool/serve_api/remote/serve_api.hpp"

#include "fmt/core.h"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/execution_api/serve/mr_git_api.hpp"
#include "src/buildtool/execution_api/utils/rehash_utils.hpp"
#include "src/buildtool/file_system/object_type.hpp"

auto ServeApi::UploadTree(ArtifactDigest const& tree,
                          std::filesystem::path const& git_repo) const noexcept
    -> expected<std::monostate, UploadError> {
    static constexpr bool kIsSyncError = true;
    if (not tree.IsTree() or not ProtocolTraits::IsNative(tree.GetHashType())) {
        return unexpected{UploadError{
            fmt::format("Not a git tree: {}", tree.hash()), not kIsSyncError}};
    }

    RepositoryConfig repo;
    if (not repo.SetGitCAS(git_repo)) {
        return unexpected{UploadError{
            fmt::format("Failed to SetGitCAS at {}", git_repo.string()),
            not kIsSyncError}};
    }

    bool const with_rehashing =
        not ProtocolTraits::IsNative(storage_config_.hash_function.GetType());

    // Create a native storage config if rehashing is needed:
    std::optional<StorageConfig> native_storage_config;
    if (with_rehashing) {
        auto config = StorageConfig::Builder::Rebuild(storage_config_)
                          .SetHashType(HashFunction::Type::GitSHA1)
                          .Build();
        if (not config.has_value()) {
            return unexpected{
                UploadError{fmt::format("Failed to create native storage: {}",
                                        std::move(config).error()),
                            not kIsSyncError}};
        }
        native_storage_config.emplace(*config);
    }

    std::shared_ptr<IExecutionApi> git_api;
    if (with_rehashing) {
        git_api = std::make_shared<MRGitApi>(
            &repo, &*native_storage_config, &storage_config_, &*apis_.local);
    }
    else {
        git_api = std::make_shared<MRGitApi>(&repo, &storage_config_);
    }

    // Upload tree to remote CAS:
    if (not git_api->RetrieveToCas(
            {Artifact::ObjectInfo{tree, ObjectType::Tree}}, *apis_.remote)) {
        return unexpected{
            UploadError{fmt::format("Failed to sync tree {} from repository {}",
                                    tree.hash(),
                                    git_repo.string()),
                        not kIsSyncError}};
    }

    ArtifactDigest on_remote = tree;
    // Read rehashed digest if needed:
    if (with_rehashing) {
        auto rehashed = RehashUtils::ReadRehashedDigest(
            tree, *native_storage_config, storage_config_, /*from_git=*/true);
        if (not rehashed.has_value()) {
            return unexpected{
                UploadError{std::move(rehashed).error(), not kIsSyncError}};
        }

        if (not rehashed.value().has_value()) {
            return unexpected{UploadError{
                fmt::format("No digest provided to sync root tree {}",
                            tree.hash()),
                kIsSyncError}};
        }
        on_remote = rehashed.value()->digest;
    }

    // Ask serve to get tree from remote:
    if (not this->GetTreeFromRemote(on_remote)) {
        return unexpected{UploadError{
            fmt::format("Serve endpoint failed to sync root tree {}.",
                        tree.hash()),
            kIsSyncError}};
    }
    return std::monostate{};
}

#endif
