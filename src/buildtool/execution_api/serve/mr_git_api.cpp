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

#include "src/buildtool/execution_api/serve/mr_git_api.hpp"

#include <utility>
#include <variant>

#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
#include "src/buildtool/execution_api/git/git_api.hpp"
#include "src/buildtool/execution_api/serve/utils.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/expected.hpp"

MRGitApi::MRGitApi(
    gsl::not_null<RepositoryConfig const*> const& repo_config,
    gsl::not_null<StorageConfig const*> const& native_storage_config,
    StorageConfig const* compatible_storage_config,
    Storage const* compatible_storage,
    IExecutionApi const* compatible_local_api) noexcept
    : repo_config_{repo_config},
      native_storage_config_{native_storage_config},
      compat_storage_config_{compatible_storage_config},
      compat_storage_{compatible_storage},
      compat_local_api_{compatible_local_api} {}

auto MRGitApi::RetrieveToCas(
    std::vector<Artifact::ObjectInfo> const& artifacts_info,
    IExecutionApi const& api) const noexcept -> bool {
    // Return immediately if target CAS is this CAS
    if (this == &api) {
        return true;
    }

    // in native mode: dispatch to regular GitApi
    if (compat_storage_config_ == nullptr) {
        GitApi const git_api{repo_config_};
        return git_api.RetrieveToCas(artifacts_info, api);
    }

    // in compatible mode: set up needed callbacks for caching digest mappings
    auto read_rehashed =
        [native_sc = native_storage_config_,
         compat_sc = compat_storage_config_](ArtifactDigest const& digest)
        -> expected<std::optional<Artifact::ObjectInfo>, std::string> {
        return MRApiUtils::ReadRehashedDigest(
            digest, *native_sc, *compat_sc, /*from_git=*/true);
    };
    auto store_rehashed =
        [native_sc = native_storage_config_,
         compat_sc = compat_storage_config_](
            ArtifactDigest const& source_digest,
            ArtifactDigest const& target_digest,
            ObjectType obj_type) -> std::optional<std::string> {
        return MRApiUtils::StoreRehashedDigest(source_digest,
                                               target_digest,
                                               obj_type,
                                               *native_sc,
                                               *compat_sc,
                                               /*from_git=*/true);
    };

    // collect the native blobs and rehash them as compatible to be able to
    // check what is missing in the other api
    std::vector<Artifact::ObjectInfo> compat_artifacts;
    compat_artifacts.reserve(artifacts_info.size());
    for (auto const& native_obj : artifacts_info) {
        // check if we know already the compatible digest
        auto cached_obj = read_rehashed(native_obj.digest);
        if (not cached_obj) {
            Logger::Log(
                LogLevel::Error, "MRGitApi: {}", std::move(cached_obj).error());
            return false;
        }
        if (*cached_obj) {
            // add object to the vector of compatible artifacts
            compat_artifacts.emplace_back(std::move(cached_obj)->value());
        }
        else {
            // process object; trees need to be handled appropriately
            if (IsTreeObject(native_obj.type)) {
                // set up all the callbacks needed
                auto read_git = [repo_config = repo_config_](
                                    ArtifactDigest const& digest,
                                    ObjectType /*type*/)
                    -> std::optional<
                        std::variant<std::filesystem::path, std::string>> {
                    return repo_config->ReadBlobFromGitCAS(digest.hash());
                };
                auto store_file =
                    [cas = &compat_storage_->CAS()](
                        std::variant<std::filesystem::path, std::string> const&
                            data,
                        bool is_exec) -> std::optional<ArtifactDigest> {
                    if (not std::holds_alternative<std::string>(data)) {
                        return std::nullopt;
                    }
                    return cas->StoreBlob(std::get<std::string>(data), is_exec);
                };
                BazelMsgFactory::TreeStoreFunc store_dir =
                    [cas = &compat_storage_->CAS()](std::string const& content)
                    -> std::optional<ArtifactDigest> {
                    return cas->StoreTree(content);
                };
                BazelMsgFactory::SymlinkStoreFunc store_symlink =
                    [cas = &compat_storage_->CAS()](std::string const& content)
                    -> std::optional<ArtifactDigest> {
                    return cas->StoreBlob(content);
                };
                // get the directory digest
                auto tree_digest =
                    BazelMsgFactory::CreateDirectoryDigestFromGitTree(
                        native_obj.digest,
                        read_git,
                        store_file,
                        store_dir,
                        store_symlink,
                        read_rehashed,
                        store_rehashed);
                if (not tree_digest) {
                    Logger::Log(LogLevel::Error,
                                "MRGitApi: {}",
                                std::move(tree_digest).error());
                    return false;
                }
                // add object to the vector of compatible artifacts
                compat_artifacts.emplace_back(
                    Artifact::ObjectInfo{.digest = *std::move(tree_digest),
                                         .type = ObjectType::Tree});
            }
            else {
                // blobs are read from repo and added to compatible CAS
                auto const blob_content =
                    repo_config_->ReadBlobFromGitCAS(native_obj.digest.hash());
                if (not blob_content) {
                    Logger::Log(LogLevel::Error,
                                "MRGitApi: failed reading Git entry {}",
                                native_obj.digest.hash());
                    return false;
                }
                auto blob_digest = compat_storage_->CAS().StoreBlob(
                    *blob_content, IsExecutableObject(native_obj.type));
                if (not blob_digest) {
                    Logger::Log(LogLevel::Error,
                                "MRGitApi: failed to rehash Git entry {}",
                                native_obj.digest.hash());
                    return false;
                }
                // cache the digest association
                if (auto error_msg = store_rehashed(
                        native_obj.digest, *blob_digest, native_obj.type)) {
                    Logger::Log(
                        LogLevel::Error, "MRGitApi: {}", *std::move(error_msg));
                    return false;
                }
                // add object to the vector of compatible artifacts
                compat_artifacts.emplace_back(
                    Artifact::ObjectInfo{.digest = *std::move(blob_digest),
                                         .type = native_obj.type});
            }
        }
    }
    // now that we have gathered all the compatible object infos, simply pass
    // them to a local api that can interact with the remote
    return compat_local_api_->RetrieveToCas(compat_artifacts, api);
}
