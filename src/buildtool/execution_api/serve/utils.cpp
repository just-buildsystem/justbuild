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

#include "src/buildtool/execution_api/serve/utils.hpp"

#include <cstddef>  // std::size_t
#include <filesystem>
#include <system_error>
#include <utility>  // std::move

#include "fmt/core.h"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/storage/fs_utils.hpp"

namespace MRApiUtils {

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

}  // namespace MRApiUtils
