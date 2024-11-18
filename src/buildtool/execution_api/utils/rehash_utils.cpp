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

#include "src/buildtool/execution_api/utils/rehash_utils.hpp"

#include <concepts>
#include <cstddef>  // std::size_t
#include <filesystem>
#include <functional>
#include <system_error>
#include <utility>  // std::move
#include <variant>

#include "fmt/core.h"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/storage/fs_utils.hpp"
#include "src/buildtool/storage/storage.hpp"

namespace RehashUtils {

auto ReadRehashedDigest(ArtifactDigest const& digest,
                        StorageConfig const& source_config,
                        StorageConfig const& target_config,
                        bool from_git) noexcept
    -> expected<std::optional<Artifact::ObjectInfo>, std::string> {
    // check for mapping file in all generations
    std::size_t generation = 0;
    std::optional<std::filesystem::path> rehash_id_file = std::nullopt;
    auto const compat_hash_type = target_config.hash_function.GetType();
    for (; generation < source_config.num_generations; ++generation) {
        auto path = StorageUtils::GetRehashIDFile(source_config,
                                                  compat_hash_type,
                                                  digest.hash(),
                                                  from_git,
                                                  generation);
        if (FileSystemManager::Exists(path)) {
            rehash_id_file = std::move(path);
            break;  // found the generation
        }
    }
    if (rehash_id_file) {
        // read id file
        auto compat_obj_str = FileSystemManager::ReadFile(*rehash_id_file);
        if (not compat_obj_str) {
            return unexpected{fmt::format("failed to read rehash id file {}",
                                          rehash_id_file->string())};
        }
        // get artifact object from content
        auto compat_obj =
            Artifact::ObjectInfo::FromString(compat_hash_type, *compat_obj_str);
        if (not compat_obj) {
            // handle nullopt value explicitly
            return unexpected{
                fmt::format("failed to read rehashed artifact from id file {}",
                            rehash_id_file->string())};
        }
        // ensure the id file is in generation 0 for future calls
        if (generation != 0) {
            auto dest_id_file = StorageUtils::GetRehashIDFile(
                source_config, compat_hash_type, digest.hash(), from_git);
            auto ok = FileSystemManager::CreateFileHardlink(*rehash_id_file,
                                                            dest_id_file);
            if (not ok) {
                auto const& err = ok.error();
                if (err != std::errc::too_many_links) {
                    return unexpected{
                        fmt::format("failed to link rehash id file {}:\n{} {}",
                                    dest_id_file.string(),
                                    err.value(),
                                    err.message())};
                }
                // if too many links reported, write id file ourselves
                if (not StorageUtils::WriteTreeIDFile(dest_id_file,
                                                      *compat_obj_str)) {
                    return unexpected{
                        fmt::format("failed to write rehash id file {}",
                                    dest_id_file.string())};
                }
            }
        }
        return std::move(compat_obj);  // not dereferenced to assist type
                                       // deduction in variant
    }
    // no mapping file found
    return std::optional<Artifact::ObjectInfo>{std::nullopt};
}

auto StoreRehashedDigest(ArtifactDigest const& source_digest,
                         ArtifactDigest const& target_digest,
                         ObjectType obj_type,
                         StorageConfig const& source_config,
                         StorageConfig const& target_config,
                         bool from_git) noexcept -> std::optional<std::string> {
    // write mapping
    auto const rehash_id_file =
        StorageUtils::GetRehashIDFile(source_config,
                                      target_config.hash_function.GetType(),
                                      source_digest.hash(),
                                      from_git);
    if (not StorageUtils::WriteTreeIDFile(
            rehash_id_file,
            Artifact::ObjectInfo{.digest = target_digest, .type = obj_type}
                .ToString())) {
        return fmt::format("failed to write rehash id to file {}",
                           rehash_id_file.string());
    }
    return std::nullopt;  // a-ok
}

namespace {

template <std::invocable<ArtifactDigest const&, ObjectType> TReadCallback>
[[nodiscard]] auto RehashDigestImpl(
    std::vector<Artifact::ObjectInfo> const& infos,
    StorageConfig const& source_config,
    StorageConfig const& target_config,
    TReadCallback read_callback,
    bool from_git) -> expected<std::vector<Artifact::ObjectInfo>, std::string> {

    auto const target_storage = Storage::Create(&target_config);

    BazelMsgFactory::BlobStoreFunc store_file =
        [&target_storage](
            std::variant<std::filesystem::path, std::string> const& data,
            bool is_exec) -> std::optional<ArtifactDigest> {
        if (not std::holds_alternative<std::filesystem::path>(data)) {
            return std::nullopt;
        }
        return target_storage.CAS().StoreBlob(
            std::get<std::filesystem::path>(data), is_exec);
    };
    BazelMsgFactory::TreeStoreFunc store_dir =
        [&cas = target_storage.CAS()](std::string const& content) {
            return cas.StoreTree(content);
        };
    BazelMsgFactory::SymlinkStoreFunc store_symlink =
        [&cas = target_storage.CAS()](std::string const& content) {
            return cas.StoreBlob(content);
        };
    BazelMsgFactory::RehashedDigestReadFunc read_rehashed =
        [&source_config, &target_config, from_git](ArtifactDigest const& digest)
        -> expected<std::optional<Artifact::ObjectInfo>, std::string> {
        return RehashUtils::ReadRehashedDigest(
            digest, source_config, target_config, from_git);
    };
    BazelMsgFactory::RehashedDigestStoreFunc store_rehashed =
        [&source_config, &target_config, from_git](
            ArtifactDigest const& source_digest,
            ArtifactDigest const& target_digest,
            ObjectType obj_type) -> std::optional<std::string> {
        return RehashUtils::StoreRehashedDigest(source_digest,
                                                target_digest,
                                                obj_type,
                                                source_config,
                                                target_config,
                                                from_git);
    };

    // collect the native blobs and rehash them as compatible to be able to
    // check what is missing in the other api
    std::vector<Artifact::ObjectInfo> compat_artifacts;
    compat_artifacts.reserve(infos.size());
    for (auto const& source_object : infos) {
        // check if we know already the compatible digest
        auto cached_obj = read_rehashed(source_object.digest);
        if (not cached_obj) {
            return unexpected(std::move(cached_obj).error());
        }
        if (*cached_obj) {
            // add object to the vector of compatible artifacts
            compat_artifacts.emplace_back(std::move(cached_obj)->value());
            continue;
        }

        // process object; trees need to be handled appropriately
        if (IsTreeObject(source_object.type)) {
            // get the directory digest
            auto target_tree =
                BazelMsgFactory::CreateDirectoryDigestFromGitTree(
                    source_object.digest,
                    read_callback,
                    store_file,
                    store_dir,
                    store_symlink,
                    read_rehashed,
                    store_rehashed);
            if (not target_tree) {
                return unexpected(std::move(target_tree).error());
            }
            // add object to the vector of compatible artifacts
            compat_artifacts.emplace_back(Artifact::ObjectInfo{
                .digest = *std::move(target_tree), .type = ObjectType::Tree});
        }
        else {
            // blobs can be directly rehashed
            auto path = read_callback(source_object.digest, source_object.type);
            if (not path) {
                return unexpected(
                    fmt::format("failed to get path of entry {}",
                                source_object.digest.hash()));
            }
            auto target_blob =
                store_file(*path, IsExecutableObject(source_object.type));
            if (not target_blob) {
                return unexpected(fmt::format("failed to rehash entry {}",
                                              source_object.digest.hash()));
            }
            // cache the digest association
            if (auto error_msg = store_rehashed(
                    source_object.digest, *target_blob, source_object.type)) {
                return unexpected(*std::move(error_msg));
            }
            // add object to the vector of compatible artifacts
            compat_artifacts.emplace_back(Artifact::ObjectInfo{
                .digest = *std::move(target_blob), .type = source_object.type});
        }
    }

    return compat_artifacts;
}
}  // namespace

auto RehashDigest(std::vector<Artifact::ObjectInfo> const& digests,
                  StorageConfig const& source_config,
                  StorageConfig const& target_config)
    -> expected<std::vector<Artifact::ObjectInfo>, std::string> {
    auto const source_storage = Storage::Create(&source_config);
    auto read = [&source_storage](
                    ArtifactDigest const& digest,
                    ObjectType type) -> std::optional<std::filesystem::path> {
        return IsTreeObject(type) ? source_storage.CAS().TreePath(digest)
                                  : source_storage.CAS().BlobPath(
                                        digest, IsExecutableObject(type));
    };
    return RehashDigestImpl(digests,
                            source_config,
                            target_config,
                            std::move(read),
                            /*from_git=*/false);
}

}  // namespace RehashUtils
