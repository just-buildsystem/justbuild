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

#include "src/buildtool/tree_structure/tree_structure_cache.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

#include "nlohmann/json.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/utils/cpp/expected.hpp"

namespace {
inline constexpr std::size_t kHash = 0;
inline constexpr std::size_t kSize = 1;
inline constexpr std::size_t kTree = 2;

[[nodiscard]] auto IsInCas(Storage const& storage,
                           ArtifactDigest const& digest) -> bool {
    if (digest.IsTree()) {
        return storage.CAS().TreePath(digest).has_value();
    }

    static constexpr bool kIsExecutable = true;
    return storage.CAS().BlobPathNoSync(digest, kIsExecutable).has_value() or
           storage.CAS().BlobPathNoSync(digest, not kIsExecutable).has_value();
}

[[nodiscard]] auto ToJson(ArtifactDigest const& digest)
    -> std::optional<nlohmann::json> {
    try {
        return nlohmann::json{digest.hash(), digest.size(), digest.IsTree()};
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] auto Parse(HashFunction::Type hash_type,
                         std::filesystem::path const& path) noexcept
    -> std::optional<ArtifactDigest> {
    try {
        std::ifstream stream(path);
        nlohmann::json j = nlohmann::json::parse(stream);
        auto result = ArtifactDigestFactory::Create(
            hash_type,
            j.at(kHash).template get<std::string>(),
            j.at(kSize).template get<std::size_t>(),
            j.at(kTree).template get<bool>());
        if (result) {
            return *std::move(result);
        }
    } catch (...) {
        return std::nullopt;
    }
    return std::nullopt;
}
}  // namespace

TreeStructureCache::TreeStructureCache(
    gsl::not_null<StorageConfig const*> const& storage_config) noexcept
    : TreeStructureCache(storage_config, 0, /*uplink=*/true) {}

TreeStructureCache::TreeStructureCache(
    gsl::not_null<StorageConfig const*> const& storage_config,
    std::size_t generation,
    bool uplink) noexcept
    : storage_config_{*storage_config},
      file_storage_{storage_config_.RepositoryGenerationRoot(generation) /
                    "tree_structure"},
      uplink_{uplink} {}

auto TreeStructureCache::Get(ArtifactDigest const& key) const noexcept
    -> std::optional<ArtifactDigest> {
    // Check key object is in the storage AND trigger uplinking if needed
    auto const path = file_storage_.GetPath(key.hash());
    if (uplink_ and not LocalUplinkObject(key, *this)) {
        return std::nullopt;
    }

    if (not FileSystemManager::IsFile(path)) {
        return std::nullopt;
    }
    return Parse(storage_config_.hash_function.GetType(), path);
}

auto TreeStructureCache::Set(ArtifactDigest const& key,
                             ArtifactDigest const& value) const noexcept
    -> bool {
    auto result = Get(key);
    if (result) {
        return *result == value;
    }

    auto const storage = Storage::Create(&storage_config_);
    // Check both key and value are in the storage and in the latest generation.
    if (not IsInCas(storage, key) or not IsInCas(storage, value)) {
        return false;
    }

    try {
        auto j = ToJson(value);
        if (not j) {
            return false;
        }
        return file_storage_.AddFromBytes(key.hash(), j->dump());
    } catch (...) {
        return false;
    }
}

auto TreeStructureCache::LocalUplinkObject(
    ArtifactDigest const& key,
    TreeStructureCache const& latest) const noexcept -> bool {
    auto const storage = Storage::Create(&storage_config_);
    // ensure key is present in the storage AND is in the latest generation
    if (not IsInCas(storage, key)) {
        return false;
    }
    for (std::size_t i = 0; i < storage_config_.num_generations; ++i) {
        TreeStructureCache const generation_cache(&storage_config_, i, false);
        auto const path = generation_cache.file_storage_.GetPath(key.hash());
        if (not FileSystemManager::IsFile(path)) {
            continue;
        }
        auto digest = Parse(storage_config_.hash_function.GetType(), path);
        // ensure value is present in the storage AND is in the latest
        // generation
        if (not digest or not IsInCas(storage, *digest)) {
            return false;
        }
        static constexpr bool kIsOwner = true;
        return latest.file_storage_.AddFromFile(key.hash(), path, kIsOwner);
    }
    return false;
}
