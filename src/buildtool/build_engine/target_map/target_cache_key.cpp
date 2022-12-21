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

#include "src/buildtool/build_engine/target_map/target_cache_key.hpp"

#include <exception>

#include <nlohmann/json.hpp>

#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/execution_api/local/local_cas.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

auto TargetCacheKey::Create(BuildMaps::Base::EntityName const& target,
                            Configuration const& effective_config) noexcept
    -> std::optional<TargetCacheKey> {
    static auto const& repos = RepositoryConfig::Instance();
    try {
        if (auto repo_key =
                repos.RepositoryKey(target.GetNamedTarget().repository)) {
            // target's repository is content-fixed, we can compute a cache key
            auto const& name = target.GetNamedTarget();
            auto target_desc = nlohmann::json{
                {{"repo_key", *repo_key},
                 {"target_name", nlohmann::json{name.module, name.name}.dump()},
                 {"effective_config", effective_config.ToString()}}};
            static auto const& cas = LocalCAS<ObjectType::File>::Instance();
            if (auto target_key = cas.StoreBlobFromBytes(target_desc.dump(2))) {
                return TargetCacheKey{
                    {ArtifactDigest{*target_key}, ObjectType::File}};
            }
        }
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "Creating target cache key failed with:\n{}",
                    ex.what());
    }
    return std::nullopt;
}
