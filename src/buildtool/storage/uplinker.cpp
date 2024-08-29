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

#ifndef BOOTSTRAP_BUILD_TOOL

#include "src/buildtool/storage/uplinker.hpp"

#include <algorithm>
#include <cstddef>

#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/storage/local_ac.hpp"
#include "src/buildtool/storage/local_cas.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/buildtool/storage/target_cache.hpp"
#include "src/buildtool/storage/target_cache_entry.hpp"

namespace {
[[nodiscard]] auto CreateGenerations(
    gsl::not_null<StorageConfig const*> const& storage_config) noexcept
    -> std::vector<Generation> {
    std::vector<Generation> generations;
    generations.reserve(storage_config->num_generations);
    for (std::size_t i = 0; i < storage_config->num_generations; ++i) {
        generations.emplace_back(Generation::Create(storage_config,
                                                    /*generation=*/i));
    }
    return generations;
}
}  // namespace

GlobalUplinker::GlobalUplinker(
    gsl::not_null<StorageConfig const*> const& storage_config) noexcept
    : storage_config_{*storage_config},
      generations_{CreateGenerations(&storage_config_)} {}

auto GlobalUplinker::UplinkBlob(ArtifactDigest const& digest,
                                bool is_executable) const noexcept -> bool {
    // Try to find blob in all generations.
    auto const& latest = generations_[Generation::kYoungest].CAS();
    return std::any_of(
        generations_.begin(),
        generations_.end(),
        [&latest, &digest, is_executable](Generation const& generation) {
            return generation.CAS().LocalUplinkBlob(latest,
                                                    digest,
                                                    is_executable,
                                                    /*skip_sync=*/true,
                                                    /*splice_result=*/true);
        });
}

auto GlobalUplinker::UplinkTree(ArtifactDigest const& digest) const noexcept
    -> bool {
    // Try to find blob in all generations.
    auto const& latest = generations_[Generation::kYoungest].CAS();
    return std::any_of(generations_.begin(),
                       generations_.end(),
                       [&latest, &digest](Generation const& generation) {
                           return generation.CAS().LocalUplinkTree(
                               latest, digest, /*splice_result=*/true);
                       });
}

auto GlobalUplinker::UplinkLargeBlob(
    ArtifactDigest const& digest) const noexcept -> bool {
    // Try to find large entry in all generations.
    auto const& latest = generations_[Generation::kYoungest].CAS();
    return std::any_of(
        generations_.begin(),
        generations_.end(),
        [&latest, &digest](Generation const& generation) {
            return generation.CAS().LocalUplinkLargeObject<ObjectType::File>(
                latest, digest);
        });
}

auto GlobalUplinker::UplinkActionCacheEntry(
    ArtifactDigest const& action_id) const noexcept -> bool {
    // Try to find action-cache entry in all generations.
    auto const& latest = generations_[Generation::kYoungest].ActionCache();
    return std::any_of(generations_.begin(),
                       generations_.end(),
                       [&latest, &action_id](Generation const& generation) {
                           return generation.ActionCache().LocalUplinkEntry(
                               latest, action_id);
                       });
}

auto GlobalUplinker::UplinkTargetCacheEntry(
    TargetCacheKey const& key,
    std::optional<std::string> const& shard) const noexcept -> bool {
    // Try to find target-cache entry in all generations.
    auto const& latest =
        generations_[Generation::kYoungest].TargetCache().WithShard(shard);
    return std::any_of(
        generations_.begin(),
        generations_.end(),
        [&latest, &key, &shard](Generation const& generation) {
            return generation.TargetCache().WithShard(shard).LocalUplinkEntry(
                latest, key);
        });
}

#endif  // BOOTSTRAP_BUILD_TOOL
