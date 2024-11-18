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

#include "src/buildtool/execution_api/serve/mr_local_api.hpp"

#include <utility>
#include <variant>

#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
#include "src/buildtool/execution_api/utils/rehash_utils.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/utils/cpp/expected.hpp"

MRLocalApi::MRLocalApi(
    gsl::not_null<LocalContext const*> const& native_context,
    gsl::not_null<IExecutionApi const*> const& native_local_api,
    LocalContext const* compatible_context,
    IExecutionApi const* compatible_local_api) noexcept
    : native_context_{native_context},
      compat_context_{compatible_context},
      native_local_api_{native_local_api},
      compat_local_api_{compatible_local_api} {}

// NOLINTNEXTLINE(google-default-arguments)
auto MRLocalApi::RetrieveToPaths(
    std::vector<Artifact::ObjectInfo> const& artifacts_info,
    std::vector<std::filesystem::path> const& output_paths,
    IExecutionApi const* /*alternative*/) const noexcept -> bool {
    // This method can legitimately be called with both native and
    // compatible digests when in compatible mode, therefore we need to
    // interrogate the hash type of the input.

    // we need at least one digest to interrogate the hash type
    if (artifacts_info.empty()) {
        return true;  // nothing to do
    }
    // native artifacts get dispatched to native local api
    if (ProtocolTraits::IsNative(artifacts_info[0].digest.GetHashType())) {
        return native_local_api_->RetrieveToPaths(artifacts_info, output_paths);
    }
    // compatible digests get dispatched to compatible local api
    if (compat_local_api_ == nullptr) {
        Logger::Log(LogLevel::Error,
                    "MRLocalApi: Unexpected digest type provided");
        return false;
    }
    return compat_local_api_->RetrieveToPaths(artifacts_info, output_paths);
}

auto MRLocalApi::RetrieveToCas(
    std::vector<Artifact::ObjectInfo> const& artifacts_info,
    IExecutionApi const& api) const noexcept -> bool {
    // return immediately if being passed the same api
    if (this == &api) {
        return true;
    }

    // in native mode: dispatch directly to native local api
    if (compat_local_api_ == nullptr) {
        return native_local_api_->RetrieveToCas(artifacts_info, api);
    }

    // in compatible mode: if compatible hashes passed, dispatch them to
    // compatible local api
    if (not artifacts_info.empty() and
        not ProtocolTraits::IsNative(artifacts_info[0].digest.GetHashType())) {
        return compat_local_api_->RetrieveToCas(artifacts_info, api);
    }

    // in compatible mode: if passed native digests, one must rehash them;
    // first, set up needed callbacks for caching digest mappings
    auto read_rehashed = [native_storage = native_context_->storage_config,
                          compat_storage = compat_context_->storage_config](
                             ArtifactDigest const& digest)
        -> expected<std::optional<Artifact::ObjectInfo>, std::string> {
        return RehashUtils::ReadRehashedDigest(
            digest, *native_storage, *compat_storage);
    };
    auto store_rehashed =
        [native_storage = native_context_->storage_config,
         compat_storage = compat_context_->storage_config](
            ArtifactDigest const& source_digest,
            ArtifactDigest const& target_digest,
            ObjectType obj_type) -> std::optional<std::string> {
        return RehashUtils::StoreRehashedDigest(source_digest,
                                                target_digest,
                                                obj_type,
                                                *native_storage,
                                                *compat_storage);
    };

    // collect the native blobs and rehash them as compatible to be able to
    // check what is missing in the other api
    std::vector<Artifact::ObjectInfo> compat_artifacts;
    compat_artifacts.reserve(artifacts_info.size());
    for (auto const& native_obj : artifacts_info) {
        // check if we know already the compatible digest
        auto cached_obj = read_rehashed(native_obj.digest);
        if (not cached_obj) {
            Logger::Log(LogLevel::Error,
                        "MRLocalApi: {}",
                        std::move(cached_obj).error());
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
                auto read_git = [cas = &native_context_->storage->CAS()](
                                    ArtifactDigest const& digest,
                                    ObjectType type)
                    -> std::optional<
                        std::variant<std::filesystem::path, std::string>> {
                    return IsTreeObject(type)
                               ? cas->TreePath(digest)
                               : cas->BlobPath(digest,
                                               IsExecutableObject(type));
                };
                auto store_file =
                    [cas = &compat_context_->storage->CAS()](
                        std::variant<std::filesystem::path, std::string> const&
                            data,
                        bool is_exec) -> std::optional<ArtifactDigest> {
                    if (not std::holds_alternative<std::filesystem::path>(
                            data)) {
                        return std::nullopt;
                    }
                    return cas->StoreBlob(std::get<std::filesystem::path>(data),
                                          is_exec);
                };
                BazelMsgFactory::TreeStoreFunc store_dir =
                    [cas = &compat_context_->storage->CAS()](
                        std::string const& content)
                    -> std::optional<ArtifactDigest> {
                    return cas->StoreTree(content);
                };
                BazelMsgFactory::SymlinkStoreFunc store_symlink =
                    [cas = &compat_context_->storage->CAS()](
                        std::string const& content)
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
                                "MRLocalApi: {}",
                                std::move(tree_digest).error());
                    return false;
                }
                // add object to the vector of compatible artifacts
                compat_artifacts.emplace_back(
                    Artifact::ObjectInfo{.digest = *std::move(tree_digest),
                                         .type = ObjectType::Tree});
            }
            else {
                // blobs can be directly rehashed
                auto const is_exec = IsExecutableObject(native_obj.type);
                auto path = native_context_->storage->CAS().BlobPath(
                    native_obj.digest, is_exec);
                if (not path) {
                    Logger::Log(
                        LogLevel::Error,
                        "MRLocalApi: failed to get path of CAS entry {}",
                        native_obj.digest.hash());
                    return false;
                }
                auto blob_digest =
                    compat_context_->storage->CAS().StoreBlob(*path, is_exec);
                if (not blob_digest) {
                    Logger::Log(LogLevel::Error,
                                "MRLocalApi: failed to rehash CAS entry {}",
                                native_obj.digest.hash());
                    return false;
                }
                // cache the digest association
                if (auto error_msg = store_rehashed(
                        native_obj.digest, *blob_digest, native_obj.type)) {
                    Logger::Log(LogLevel::Error,
                                "MRLocalApi: {}",
                                *std::move(error_msg));
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
    // them to the compatible local api
    return compat_local_api_->RetrieveToCas(compat_artifacts, api);
}

// NOLINTNEXTLINE(google-default-arguments)
auto MRLocalApi::Upload(ArtifactBlobContainer&& blobs,
                        bool skip_find_missing) const noexcept -> bool {
    // in native mode, dispatch to native local api
    if (compat_local_api_ == nullptr) {
        return native_local_api_->Upload(std::move(blobs), skip_find_missing);
    }
    // in compatible mode, dispatch to compatible local api
    return compat_local_api_->Upload(std::move(blobs), skip_find_missing);
}

auto MRLocalApi::IsAvailable(ArtifactDigest const& digest) const noexcept
    -> bool {
    // This method can legitimately be called with both native and
    // compatible digests when in compatible mode, therefore we need to
    // interrogate the hash type of the input.

    // a native digest gets dispatched to native local api
    if (ProtocolTraits::IsNative(digest.GetHashType())) {
        return native_local_api_->IsAvailable(digest);
    }
    // compatible digests get dispatched to compatible local api
    if (compat_local_api_ == nullptr) {
        Logger::Log(LogLevel::Warning,
                    "MRLocalApi: unexpected digest type provided");
        return false;
    }
    return compat_local_api_->IsAvailable(digest);
}

auto MRLocalApi::IsAvailable(std::vector<ArtifactDigest> const& digests)
    const noexcept -> std::vector<ArtifactDigest> {
    // This method can legitimately be called with both native and
    // compatible digests when in compatible mode, therefore we need to
    // interrogate the hash type of the input.

    // we need at least one digest to interrogate the hash type
    if (digests.empty()) {
        return {};  // nothing to do
    }
    // native digests get dispatched to native local api
    if (ProtocolTraits::IsNative(digests[0].GetHashType())) {
        return native_local_api_->IsAvailable(digests);
    }
    // compatible digests get dispatched to compatible local api
    if (compat_local_api_ == nullptr) {
        Logger::Log(LogLevel::Warning,
                    "MRLocalApi: Unexpected digest type provided");
        return {};
    }
    return compat_local_api_->IsAvailable(digests);
}
