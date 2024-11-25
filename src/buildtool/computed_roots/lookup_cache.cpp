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

#include "src/buildtool/computed_roots/lookup_cache.hpp"

#include <functional>
#include <memory>
#include <vector>

#include "fmt/core.h"
#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name_data.hpp"
#include "src/buildtool/build_engine/base_maps/field_reader.hpp"
#include "src/buildtool/build_engine/base_maps/module_name.hpp"
#include "src/buildtool/build_engine/base_maps/targets_file_map.hpp"
#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/buildtool/build_engine/expression/target_result.hpp"
#include "src/buildtool/computed_roots/artifacts_root.hpp"
#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/buildtool/storage/target_cache_entry.hpp"
#include "src/buildtool/storage/target_cache_key.hpp"

auto LookupCache(BuildMaps::Target::ConfiguredTarget const& ctarget,
                 gsl::not_null<RepositoryConfig*> const& repository_config,
                 Storage const& storage,
                 AsyncMapConsumerLoggerPtr const& logger)
    -> expected<std::optional<std::string>, std::monostate> {
    auto const* target_root =
        repository_config->TargetRoot(ctarget.target.ToModule().repository);
    if ((target_root == nullptr) or target_root->IsAbsent()) {
        // TODO(aehlig): avoid local installing in case of absent target root of
        // the base repository
        return expected<std::optional<std::string>, std::monostate>(
            std::nullopt);
    }
    auto repo_key = repository_config->RepositoryKey(
        storage, ctarget.target.GetNamedTarget().repository);
    if (not repo_key) {
        (*logger)(fmt::format("Repository {} is not content-fixed",
                              nlohmann::json(
                                  ctarget.target.GetNamedTarget().repository)),
                  /*fatal=*/true);
        return unexpected(std::monostate{});
    }
    auto targets_file_map =
        BuildMaps::Base::CreateTargetsFileMap(repository_config, 1);
    nlohmann::json targets_file{};
    bool failed{false};
    {
        TaskSystem ts{1};
        targets_file_map.ConsumeAfterKeysReady(
            &ts,
            {ctarget.target.ToModule()},
            [&targets_file](auto values) { targets_file = *values[0]; },
            [&logger, &failed](auto const& msg, bool fatal) {
                (*logger)(
                    fmt::format("While searching for target description:\n{}",
                                msg),
                    fatal);
                failed = failed or fatal;
            });
    }
    if (failed) {
        return unexpected(std::monostate{});
    }
    auto desc_it = targets_file.find(ctarget.target.GetNamedTarget().name);
    if (desc_it == targets_file.end()) {
        (*logger)("Not referring to a defined target", /*fatal=*/true);
        return unexpected(std::monostate{});
    }
    nlohmann::json const& desc = *desc_it;
    auto rule_it = desc.find("type");
    if (rule_it == desc.end()) {
        (*logger)(fmt::format("No type specified in target-description {}",
                              desc.dump()),
                  true);
        return unexpected(std::monostate{});
    }
    auto const& rule = *rule_it;
    if (not(rule.is_string() and rule.get<std::string>() == "export")) {
        (*logger)(fmt::format("Target not an export target, but of type {}",
                              rule.dump()),
                  true);
        return unexpected(std::monostate{});
    }
    auto reader = BuildMaps::Base::FieldReader::CreatePtr(
        desc, ctarget.target, "export target", logger);
    auto flexible_vars = reader->ReadStringList("flexible_config");
    if (not flexible_vars) {
        return unexpected(std::monostate{});
    }
    auto effective_config = ctarget.config.Prune(*flexible_vars);
    auto cache_key = storage.TargetCache().ComputeKey(
        *repo_key, ctarget.target.GetNamedTarget(), effective_config);
    if (not cache_key) {
        (*logger)("Target-cache key generation failed", true);
        return unexpected(std::monostate{});
    }
    auto target_cache_value = storage.TargetCache().Read(*cache_key);
    if (not target_cache_value) {
        return expected<std::optional<std::string>, std::monostate>(
            std::nullopt);
    }
    auto const& [entry, info] = *target_cache_value;
    auto result = entry.ToResult();
    if (not result) {
        (*logger)(fmt::format("Failed to deserialize cache entry {} for key {}",
                              info.ToString(),
                              cache_key->Id().ToString()),
                  true);
        return unexpected(std::monostate{});
    }

    auto wrapped_logger = std::make_shared<AsyncMapConsumerLogger>(
        [&logger](auto const& msg, bool fatal) {
            (*logger)(
                fmt::format("While computing git tree for artifacts stage:\n{}",
                            msg),
                fatal);
        });
    auto root = ArtifactsRoot(result->artifact_stage, wrapped_logger);
    if (not root) {
        return unexpected(std::monostate{});
    }
    return root;
}
