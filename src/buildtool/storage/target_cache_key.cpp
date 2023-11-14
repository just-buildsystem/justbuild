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

#include "src/buildtool/storage/target_cache_key.hpp"

#include <exception>

#include <nlohmann/json.hpp>

#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/storage/storage.hpp"

auto TargetCacheKey::Create(std::string const& repo_key,
                            BuildMaps::Base::NamedTarget const& target_name,
                            Configuration const& effective_config) noexcept
    -> std::optional<TargetCacheKey> {
    try {
        // target's repository is content-fixed, we can compute a cache key
        auto target_desc = nlohmann::json{
            {"repo_key", repo_key},
            {"target_name",
             nlohmann::json{target_name.module, target_name.name}.dump()},
            {"effective_config", effective_config.ToString()}};
        if (auto target_key = Storage::Instance().CAS().StoreBlob(
                target_desc.dump(2), /*is_executable=*/false)) {
            return TargetCacheKey{
                {ArtifactDigest{*target_key}, ObjectType::File}};
        }
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "Creating target cache key failed with:\n{}",
                    ex.what());
    }
    return std::nullopt;
}
