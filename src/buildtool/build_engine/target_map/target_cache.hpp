// Copyright 2022 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_TARGET_MAP_TARGET_CACHE_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_TARGET_MAP_TARGET_CACHE_HPP

#include <filesystem>
#include <functional>
#include <optional>
#include <utility>

#include <gsl-lite/gsl-lite.hpp>
#include <nlohmann/json.hpp>

#include "src/buildtool/build_engine/target_map/target_cache_entry.hpp"
#include "src/buildtool/build_engine/target_map/target_cache_key.hpp"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/execution_api/local/local_cas.hpp"
#include "src/buildtool/file_system/file_storage.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/logger.hpp"

class TargetCache {
  public:
    using ArtifactDownloader =
        std::function<bool(std::vector<Artifact::ObjectInfo> const&)>;

    TargetCache() = default;
    TargetCache(TargetCache const&) = delete;
    TargetCache(TargetCache&&) = delete;
    auto operator=(TargetCache const&) -> TargetCache& = delete;
    auto operator=(TargetCache&&) -> TargetCache& = delete;
    ~TargetCache() noexcept = default;

    [[nodiscard]] static auto Instance() -> TargetCache& {
        static TargetCache instance;
        return instance;
    }

    // Store new key entry pair in the target cache.
    [[nodiscard]] auto Store(
        TargetCacheKey const& key,
        TargetCacheEntry const& value,
        ArtifactDownloader const& downloader) const noexcept -> bool;

    // Read existing entry and object info for given key from the target cache.
    [[nodiscard]] auto Read(TargetCacheKey const& key) const noexcept
        -> std::optional<std::pair<TargetCacheEntry, Artifact::ObjectInfo>>;

  private:
    Logger logger_{"TargetCache"};
    FileStorage<ObjectType::File,
                StoreMode::LastWins,
                /*kSetEpochTime=*/false>
        file_store_{ComputeCacheDir(0)};

    [[nodiscard]] auto DownloadKnownArtifacts(
        TargetCacheEntry const& value,
        ArtifactDownloader const& downloader) const noexcept -> bool;
    [[nodiscard]] static auto CAS() noexcept -> LocalCAS<ObjectType::File>& {
        return LocalCAS<ObjectType::File>::Instance();
    }
    [[nodiscard]] static auto ComputeCacheDir(int index)
        -> std::filesystem::path;
};

namespace std {
template <>
struct hash<TargetCacheKey> {
    [[nodiscard]] auto operator()(TargetCacheKey const& k) const {
        return std::hash<Artifact::ObjectInfo>{}(k.Id());
    }
};
}  // namespace std

#endif  // INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_TARGET_MAP_TARGET_CACHE_HPP
