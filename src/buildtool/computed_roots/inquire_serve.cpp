// Copyright 2025 Huawei Cloud Computing Technology Co., Ltd.
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

#include "src/buildtool/computed_roots/inquire_serve.hpp"

#include <filesystem>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name_data.hpp"
#include "src/buildtool/build_engine/base_maps/module_name.hpp"
#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/build_engine/expression/expression_ptr.hpp"
#include "src/buildtool/build_engine/expression/target_result.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/computed_roots/artifacts_root.hpp"
#include "src/buildtool/execution_api/utils/rehash_utils.hpp"
#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/multithreading/async_map_consumer.hpp"
#include "src/buildtool/serve_api/remote/serve_api.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/buildtool/storage/target_cache_entry.hpp"
#include "src/buildtool/storage/target_cache_key.hpp"

[[nodiscard]] auto InquireServe(
    gsl::not_null<AnalyseContext*> const& analyse_context,
    BuildMaps::Target::ConfiguredTarget const& id,
    gsl::not_null<ApiBundle const*> const& /* apis */,
    gsl::not_null<Logger const*> const& logger) -> std::optional<std::string> {

    auto const& repo_name = id.target.ToModule().repository;
    if (not analyse_context->repo_config->TargetRoot(repo_name)->IsAbsent()) {
        logger->Emit(LogLevel::Info,
                     "Base root is concrete, will manage build locally.");
        return std::nullopt;
    }

    if (analyse_context->serve == nullptr) {
        logger->Emit(LogLevel::Warning,
                     "Cannot treat a root absent without serve");
        return std::nullopt;
    }

    auto target = id.target.GetNamedTarget();

    auto target_root_id =
        analyse_context->repo_config->TargetRoot(repo_name)->GetAbsentTreeId();
    if (not target_root_id) {
        logger->Emit(LogLevel::Warning,
                     "Failed to get the target root id for repository {}",
                     nlohmann::json(repo_name).dump());
        return std::nullopt;
    }
    std::filesystem::path module{id.target.ToModule().module};
    auto vars = analyse_context->serve->ServeTargetVariables(
        *target_root_id,
        (module / *(analyse_context->repo_config->TargetFileName(repo_name)))
            .string(),
        target.name);

    if (not vars) {
        logger->Emit(LogLevel::Warning,
                     "Failed to obtain variables for {}",
                     id.target.ToString());
        return std::nullopt;
    }

    auto effective_config = id.config.Prune(*vars);
    logger->Emit(LogLevel::Info,
                 "Effective configuration {}",
                 effective_config.ToString());

    auto repo_key = analyse_context->repo_config->RepositoryKey(
        *analyse_context->storage, target.repository);
    if (not repo_key) {
        logger->Emit(LogLevel::Warning, "Cannot obtain repository key");
        return std::nullopt;
    }
    auto target_cache_key = analyse_context->storage->TargetCache().ComputeKey(
        *repo_key, target, effective_config);

    if (not target_cache_key) {
        logger->Emit(LogLevel::Warning, "Failed to obtain target-cache key");
        return std::nullopt;
    }

    logger->Emit(LogLevel::Info,
                 "Target cache key {}",
                 target_cache_key->Id().ToString());

    auto res = analyse_context->serve->ServeTarget(
        *target_cache_key, *repo_key, /*keep_artifacts_root=*/true);
    if (not res) {
        logger->Emit(LogLevel::Warning, "Could not obtain target from serve");
        return std::nullopt;
    }
    if (res->index() != 3) {
        logger->Emit(LogLevel::Warning, "Failed to obtain root from serve");
        return std::nullopt;
    }
    auto target_cache_value = std::get<3>(*res);
    auto const& [entry, info] = target_cache_value;
    auto result = entry.ToResult();
    if (not result) {
        logger->Emit(LogLevel::Warning, "Reading entry cache entry failed.");
        return std::nullopt;
    }

    auto wrapped_logger = std::make_shared<AsyncMapConsumerLogger>(
        [&logger](auto const& msg, bool fatal) {
            logger->Emit(fatal ? LogLevel::Warning : LogLevel::Info,
                         "While computing root from stage:{}",
                         msg);
        });
    auto git_tree = ArtifactsRoot(
        result->artifact_stage, wrapped_logger, /*rehash=*/std::nullopt);
    if (not git_tree) {
        logger->Emit(
            LogLevel::Warning,
            "Failed to compute git tree from obtained artifact stage {}",
            result->artifact_stage->ToString());
        return std::nullopt;
    }
    logger->Emit(LogLevel::Info, "Tree identifier for root is {}.", *git_tree);
    return git_tree;
}
