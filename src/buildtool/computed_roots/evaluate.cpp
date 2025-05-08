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

#include "src/buildtool/computed_roots/evaluate.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "fmt/core.h"
#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name_data.hpp"
#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/build_engine/target_map/configured_target.hpp"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/cli.hpp"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/common/statistics.hpp"
#include "src/buildtool/computed_roots/analyse_and_build.hpp"
#include "src/buildtool/computed_roots/inquire_serve.hpp"
#include "src/buildtool/computed_roots/lookup_cache.hpp"
#include "src/buildtool/computed_roots/roots_progress.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/common/api_bundle.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/execution_api/utils/rehash_utils.hpp"
#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/file_system/precomputed_root.hpp"
#include "src/buildtool/graph_traverser/graph_traverser.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/log_sink.hpp"
#include "src/buildtool/logging/log_sink_file.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/main/analyse_context.hpp"
#include "src/buildtool/multithreading/async_map_consumer.hpp"
#include "src/buildtool/multithreading/async_map_utils.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/buildtool/progress_reporting/base_progress_reporter.hpp"
#include "src/buildtool/progress_reporting/progress.hpp"
#include "src/buildtool/progress_reporting/task_tracker.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/buildtool/tree_structure/tree_structure_utils.hpp"
#include "src/utils/cpp/expected.hpp"
#include "src/utils/cpp/tmp_dir.hpp"
#include "src/utils/cpp/vector.hpp"

namespace {
// Add the description of a computed root to a vector, if the given
// root is computed.
void AddDescriptionIfPrecomputed(
    FileRoot const& root,
    gsl::not_null<std::vector<PrecomputedRoot>*> croots) {
    if (auto desc = root.GetPrecomputedDescription()) {
        croots->emplace_back(*std::move(desc));
    }
}

// Traverse, starting from a given repository, in order to find the
// computed roots it depends on.
void TraverseRepoForComputedRoots(
    std::string const& name,
    gsl::not_null<RepositoryConfig*> const& repository_config,
    gsl::not_null<std::vector<PrecomputedRoot>*> roots,
    gsl::not_null<std::set<std::string>*> seen) {
    if (seen->contains(name)) {
        return;
    }
    seen->insert(name);
    const auto* info = repository_config->Info(name);
    if (info == nullptr) {
        Logger::Log(LogLevel::Warning,
                    "Ignoring unknown repository {} while determining the "
                    "derived roots to compute",
                    nlohmann::json(name).dump());
        return;
    }
    AddDescriptionIfPrecomputed(info->workspace_root, roots);
    AddDescriptionIfPrecomputed(info->target_root, roots);
    AddDescriptionIfPrecomputed(info->rule_root, roots);
    AddDescriptionIfPrecomputed(info->expression_root, roots);
    for (auto const& [k, v] : info->name_mapping) {
        TraverseRepoForComputedRoots(v, repository_config, roots, seen);
    }
}

// For a given repository, return the list of computed roots it directly depends
// upon
auto GetRootDeps(std::string const& name,
                 gsl::not_null<RepositoryConfig*> const& repository_config)
    -> std::vector<PrecomputedRoot> {
    std::vector<PrecomputedRoot> result{};
    std::set<std::string> seen{};
    TraverseRepoForComputedRoots(name, repository_config, &result, &seen);
    sort_and_deduplicate(&result);
    Logger::Log(LogLevel::Debug, [&]() {
        std::ostringstream msg{};
        msg << "Roots for " << nlohmann::json(name) << ", total of "
            << result.size() << ":";
        for (auto const& root : result) {
            msg << "\n - " << root.ToString();
        }
        return msg.str();
    });
    return result;
}

// For each computed root, we have to determine the git tree identifier; it has
// to be given as string, as this is the format needed to update a repository
// root.
using RootMap = AsyncMapConsumer<PrecomputedRoot, std::string>;

auto WhileHandling(PrecomputedRoot const& root,
                   AsyncMapConsumerLoggerPtr const& logger)
    -> AsyncMapConsumerLoggerPtr {
    return std::make_shared<AsyncMapConsumerLogger>(
        [root, logger](auto const& msg, auto fatal) {
            (*logger)(fmt::format(
                          "While materializing {}:\n{}", root.ToString(), msg),
                      fatal);
        });
}

void ComputeAndFill(
    ComputedRoot const& key,
    gsl::not_null<RepositoryConfig*> const& repository_config,
    gsl::not_null<const GraphTraverser::CommandLineArguments*> const&
        traverser_args,
    ServeApi const* serve,
    gsl::not_null<const ExecutionContext*> const& context,
    gsl::not_null<const StorageConfig*> const& storage_config,
    gsl::not_null<std::optional<RehashUtils::Rehasher>*> const& rehash,
    gsl::not_null<std::shared_mutex*> const& config_lock,
    gsl::not_null<std::mutex*> const& git_lock,
    gsl::not_null<Statistics*> const& root_stats,
    gsl::not_null<TaskTracker*> const& root_tasks,
    std::size_t jobs,
    RootMap::LoggerPtr const& logger,
    RootMap::SetterPtr const& setter) {

    auto tmpdir = storage_config->CreateTypedTmpDir("computed-root");
    if (not tmpdir) {
        (*logger)("Failed to create temporary directory", true);
        return;
    }
    auto root_dir = tmpdir->GetPath() / "root";
    auto log_file = tmpdir->GetPath() / "log";
    auto storage = Storage::Create(storage_config);
    auto statistics = Statistics();
    auto progress = Progress();
    auto reporter = BaseProgressReporter::Reporter([]() {});
    BuildMaps::Target::ConfiguredTarget target{
        BuildMaps::Base::EntityName{
            key.repository, key.target_module, key.target_name},
        Configuration{Expression::FromJson(key.config)}};
    AnalyseContext analyse_context{.repo_config = repository_config,
                                   .storage = &storage,
                                   .statistics = &statistics,
                                   .progress = &progress,
                                   .serve = serve};
    root_tasks->Start(target.ToString());
    root_stats->IncrementActionsQueuedCounter();
    Logger build_logger = Logger(
        target.ToString(),
        std::vector<LogSinkFactory>{LogSinkFile::CreateFactory(log_file)});
    auto root_build_args = *traverser_args;
    root_build_args.stage =
        StageArguments{.output_dir = root_dir, .remember = true};
    root_build_args.rebuild = std::nullopt;
    root_build_args.build.print_to_stdout = std::nullopt;
    root_build_args.build.print_unique = false;
    root_build_args.build.dump_artifacts = std::vector<std::filesystem::path>{};
    root_build_args.build.show_runfiles = false;
    auto root_exec_context = ExecutionContext{context->repo_config,
                                              context->apis,
                                              context->remote_context,
                                              &statistics,
                                              &progress,
                                              std::nullopt};

    auto cache_lookup =
        expected<std::optional<std::string>, std::monostate>(std::nullopt);
    {
        std::shared_lock computing{*config_lock};
        cache_lookup =
            LookupCache(target, repository_config, storage, logger, *rehash);
    }
    if (not cache_lookup) {
        // prerequisite failure; fatal logger call already handled by
        // LookupCache
        return;
    }
    if (*cache_lookup) {

        root_stats->IncrementActionsCachedCounter();
        root_tasks->Stop(target.ToString());

        std::string root = **cache_lookup;
        auto tree_present =
            GitRepo::IsTreeInRepo(storage_config->GitRoot(), root);
        if (not tree_present) {
            (*logger)(fmt::format("While checking presence of tree {} in local "
                                  "git repo:\n{}",
                                  root,
                                  std::move(tree_present).error()),
                      /*fatal=*/true);
            return;
        }
        if (*tree_present) {
            Logger::Log(LogLevel::Performance,
                        "Root {} taken from cache to be {}",
                        target.ToString(),
                        root);
            auto root_result =
                FileRoot::FromGit(storage_config->GitRoot(), root);
            if (not root_result) {
                (*logger)(fmt::format("Failed to create git root for {}", root),
                          /*fatal=*/true);
                return;
            }
            {
                std::unique_lock setting{*config_lock};
                repository_config->SetPrecomputedRoot(PrecomputedRoot{key},
                                                      *root_result);
            }
            (*setter)(std::move(root));
            return;
        }
    }

    if (key.absent) {
        if (storage_config->hash_function.GetType() !=
            HashFunction::Type::GitSHA1) {
            Logger::Log(LogLevel::Performance,
                        "Computing root {} locally as rehahing would have to "
                        "be done locally",
                        key.ToString());
        }
        else {
            auto serve_result =
                InquireServe(&analyse_context, target, &build_logger);
            if (serve_result) {
                auto root_result = FileRoot(*serve_result);
                Logger::Log(LogLevel::Performance,
                            "Absent root {} obtained from serve to be {}",
                            target.ToString(),
                            *serve_result);
                {
                    std::unique_lock setting{*config_lock};
                    repository_config->SetPrecomputedRoot(PrecomputedRoot{key},
                                                          root_result);
                }

                root_stats->IncrementExportsServedCounter();
                root_tasks->Stop(target.ToString());

                (*setter)(std::move(*serve_result));
                return;
            }
        }
    }

    GraphTraverser traverser{
        root_build_args, &root_exec_context, reporter, &build_logger};
    std::optional<AnalyseAndBuildResult> build_result{};
    {
        std::shared_lock computing{*config_lock};
        build_result = AnalyseAndBuild(&analyse_context,
                                       traverser,
                                       target,
                                       jobs,
                                       context->apis,
                                       &build_logger);
    }
    auto log_blob = storage.CAS().StoreBlob(log_file, false);
    std::string log_desc{};
    if (not log_blob) {
        (*logger)(fmt::format("Failed to store log file {} to CAS",
                              log_file.string()),
                  false);
        log_desc = "???";
    }
    else {
        log_desc = log_blob->hash();
        if (not build_result) {
            // synchronize the build log to a remote endpoint, if specified
            if (not context->apis->local->RetrieveToCas(
                    {Artifact::ObjectInfo{.digest = *log_blob,
                                          .type = ObjectType::File,
                                          .failed = false}},
                    *context->apis->remote)) {
                (*logger)(fmt::format("Failed to upload build log {} to remote",
                                      log_desc),
                          false);
            }
        }
    }
    if (not build_result) {
        (*logger)(fmt::format("Build failed, see {} for details", log_desc),
                  true);
        return;
    }
    auto result = GitRepo::ImportToGit(
        *storage_config, root_dir, "computed root", git_lock);
    if (not result) {
        (*logger)(std::move(result).error(), /*fatal=*/true);
        return;
    }

    root_stats->IncrementActionsExecutedCounter();
    root_tasks->Stop(target.ToString());

    Logger::Log(LogLevel::Performance,
                "Root {} evaluated to {}, log {}",
                target.ToString(),
                *result,
                log_desc);
    auto root_result = FileRoot::FromGit(storage_config->GitRoot(), *result);
    if (not root_result) {
        (*logger)(fmt::format("Failed to create git root for {}", *result),
                  /*fatal=*/true);
        return;
    }
    if (key.absent) {
        if (serve == nullptr) {
            (*logger)(fmt::format("Requested root {} to be absent, without "
                                  "providing serve endpoint",
                                  key.ToString()),
                      /*fatal=*/true);
            return;
        }
        auto known = serve->CheckRootTree(*result);
        if (known.has_value() and not *known) {
            auto tree_digest =
                ArtifactDigestFactory::Create(HashFunction::Type::GitSHA1,
                                              *result,
                                              /*size_unknown=*/0,
                                              /*is_tree=*/true);
            if (not tree_digest) {
                (*logger)(
                    fmt::format("Internal error getting digest for tree {}: {}",
                                *known,
                                tree_digest.error()),
                    /*fatal=*/true);
                return;
            }
            auto uploaded_to_serve =
                serve->UploadTree(*tree_digest, storage_config->GitRoot());
            if (not uploaded_to_serve) {
                (*logger)(fmt::format("Failed to sync {} to serve:{}",
                                      *result,
                                      uploaded_to_serve.error().Message()),
                          /*fatal=*/true);
                return;
            }
            Logger::Log(LogLevel::Performance, "Uploaded {} to serve", *result);
            known = true;
        }
        if (not known.has_value() or not *known) {
            (*logger)(
                fmt::format("Failed to ensure {} is known to serve", *result),
                /*fatal=*/true);
            return;
        }
    }
    {
        // For setting, we need an exclusiver lock; so get one after we
        // dropped the shared one
        std::unique_lock setting{*config_lock};
        repository_config->SetPrecomputedRoot(PrecomputedRoot{key},
                                              *root_result);
    }
    (*setter)(*std::move(result));
}

/// \brief Compute tree structure of the given root and return the resolved real
/// root.
/// There are a number of scenarios:
/// 1. (LOCAL-LOCAL) Local tree structure of a local root:
///     Finds the source tree locally and computes tree structure. After
///     this evaluation the tree structure is present in local native CAS, in
///     git repository, and in the TreeStructureCache.
/// 2. (LOCAL-ABSENT)  Local tree structure of an absent root:
///     2.1 First performs LOCAL-LOCAL using root's git identifier. This might
///     minimize the network traffic if the source tree is present locally
///     somewhere.
///     2.2 If fails, asks serve to compute the tree structure and performs
///     LOCAL-LOCAL using tree_structure's git identifier. This might minimize
///     the traffic as well since the tree structure may be already present
///     locally.
///     2.3 If fails, downloads the tree from serve via the remote end point
///     (with a possible rehashing), and runs LOCAL-LOCAL on this tree_structure
///     to make the tree structure available for roots.
/// 3. (ABSENT-ABSENT) Absent tree of an absent root:
///     Compute absent tree structure on serve.
/// 4. (ABSENT-LOCAL): Absent tree structure of a local root:
///     Perform logic of LOCAL-LOCAL (compute tree structure locally) and
///     upload the result of computation to serve.
[[nodiscard]] auto ResolveTreeStructureRoot(
    TreeStructureRoot const& key,
    gsl::not_null<RepositoryConfig*> const& repository_config,
    ServeApi const* serve,
    gsl::not_null<StorageConfig const*> const& storage_config,
    gsl::not_null<std::shared_mutex*> const& config_lock,
    gsl::not_null<std::mutex*> const& git_lock)
    -> expected<FileRoot, std::string> {
    // Obtain the file root that the key root is referring to
    FileRoot ref_root;
    {
        std::shared_lock lock{*config_lock};
        auto const* root = repository_config->WorkspaceRoot(key.repository);
        if (root == nullptr) {
            return unexpected{fmt::format(
                "Failed to get referenced repository for {}", key.ToString())};
        }
        ref_root = *root;
    }

    auto const hash = ref_root.GetTreeHash();
    if (not hash) {
        return unexpected{
            fmt::format("Failed to get the hash of the referenced git "
                        "tree for {}",
                        key.ToString())};
    }

    auto const digest =
        ArtifactDigestFactory::Create(HashFunction::Type::GitSHA1,
                                      *hash,
                                      /*size_unknown=*/0,
                                      /*is_tree=*/true);
    if (not digest) {
        return unexpected{digest.error()};
    }

    // Create a native storage config if needed
    // Since tree structure works with git trees, a native storage is required.
    std::optional<StorageConfig> substitution_storage_config;
    if (not ProtocolTraits::IsNative(storage_config->hash_function.GetType())) {
        auto config = StorageConfig::Builder::Rebuild(*storage_config)
                          .SetHashType(HashFunction::Type::GitSHA1)
                          .Build();
        if (not config) {
            return unexpected{
                fmt::format("Failed to create a native storage config for {}",
                            key.ToString())};
        }
        substitution_storage_config.emplace(*config);
    }
    StorageConfig const& native_storage_config =
        substitution_storage_config.has_value()
            ? substitution_storage_config.value()
            : *storage_config;

    std::vector known_repositories{native_storage_config.GitRoot()};
    if (not ref_root.IsAbsent()) {
        auto const path_to_git_cas = ref_root.GetGitCasRoot();
        if (not path_to_git_cas) {
            return unexpected{
                fmt::format("Failed to get the path to the git cas for {}",
                            key.ToString())};
        }
        known_repositories.push_back(*path_to_git_cas);
    }

    bool const compute_locally = not key.absent or not ref_root.IsAbsent();

    std::optional<ArtifactDigest> local_tree_structure;
    // Try to compute the tree structure locally:
    if (compute_locally) {
        auto from_local = TreeStructureUtils::ComputeStructureLocally(
            *digest, known_repositories, native_storage_config, git_lock);
        if (not from_local.has_value()) {
            // A critical error occurred:
            return unexpected{std::move(from_local).error()};
        }
        local_tree_structure = *from_local;
    }

    // For absent roots ask serve to process the tree:
    std::optional<ArtifactDigest> absent_tree_structure;
    if (not local_tree_structure.has_value() and ref_root.IsAbsent()) {
        if (serve == nullptr) {
            return unexpected{fmt::format(
                "No serve end point is given to compute {}", key.ToString())};
        }

        auto on_serve = serve->ComputeTreeStructure(*digest);
        if (not on_serve.has_value()) {
            return unexpected{
                fmt::format("Failed to compute {} on serve", key.ToString())};
        }
        absent_tree_structure = *on_serve;
    }

    // Try to process an absent tree structure locally. It might be found in
    // CAS or git cache, so there'll be no need to download it from the
    // remote end point:
    if (compute_locally and not local_tree_structure.has_value() and
        absent_tree_structure.has_value()) {
        if (auto from_local = TreeStructureUtils::ComputeStructureLocally(
                *absent_tree_structure,
                known_repositories,
                native_storage_config,
                git_lock)) {
            local_tree_structure = *from_local;
        }
        else {
            // A critical error occurred:
            return unexpected{std::move(from_local).error()};
        }

        // Failed to process absent tree structure locally, download artifacts
        // from remote:
        if (not local_tree_structure.has_value()) {
            auto downloaded = serve->DownloadTree(*absent_tree_structure);
            if (not downloaded.has_value()) {
                return unexpected{std::move(downloaded).error()};
            }
            Logger::Log(LogLevel::Performance,
                        "Root {} has been downloaded from the remote end point",
                        key.ToString());

            // The tree structure has been downloaded successfully, try to
            // resolve the root one more time:
            if (auto from_local = TreeStructureUtils::ComputeStructureLocally(
                    *absent_tree_structure,
                    known_repositories,
                    native_storage_config,
                    git_lock)) {
                local_tree_structure = *from_local;
            }
            else {
                // A critical error occurred:
                return unexpected{std::move(from_local).error()};
            }
        }
        else {
            Logger::Log(LogLevel::Performance,
                        "Root {} has been taken from local cache",
                        key.ToString());
        }
    }

    if (key.absent and absent_tree_structure.has_value()) {
        Logger::Log(LogLevel::Performance,
                    "Root {} was computed on serve",
                    key.ToString());
        return FileRoot(absent_tree_structure->hash());
    }

    if (key.absent and local_tree_structure.has_value()) {
        if (serve == nullptr) {
            return unexpected{fmt::format(
                "No serve end point is given to compute {}", key.ToString())};
        }

        // Make sure the tree structure is available on serve:
        auto known = serve->CheckRootTree(local_tree_structure->hash());
        if (known.has_value() and not *known) {
            auto uploaded = serve->UploadTree(*local_tree_structure,
                                              native_storage_config.GitRoot());
            if (not uploaded.has_value()) {
                return unexpected{std::move(uploaded).error().Message()};
            }
            known = true;
        }

        if (not known.has_value() or not *known) {
            return unexpected{
                fmt::format("Failed to ensure that tree {} is "
                            "available on serve",
                            local_tree_structure->hash())};
        }
        Logger::Log(LogLevel::Performance,
                    "Root {} was computed locally and uploaded to serve",
                    key.ToString());
        return FileRoot(local_tree_structure->hash());
    }

    if (local_tree_structure.has_value()) {
        auto resolved_root = FileRoot::FromGit(native_storage_config.GitRoot(),
                                               local_tree_structure->hash());
        if (not resolved_root) {
            return unexpected{
                fmt::format("Failed to create root for {}", key.ToString())};
        }
        return *resolved_root;
    }

    return unexpected{fmt::format("Failed to calculate tree structure for {}",
                                  key.ToString())};
}

void ComputeTreeStructureAndFill(
    TreeStructureRoot const& key,
    gsl::not_null<RepositoryConfig*> const& repository_config,
    ServeApi const* serve,
    gsl::not_null<StorageConfig const*> const& storage_config,
    gsl::not_null<std::shared_mutex*> const& config_lock,
    gsl::not_null<std::mutex*> const& git_lock,
    RootMap::LoggerPtr const& logger,
    RootMap::SetterPtr const& setter) {
    auto resolved_root = ResolveTreeStructureRoot(
        key, repository_config, serve, storage_config, config_lock, git_lock);
    if (not resolved_root) {
        std::invoke(*logger, std::move(resolved_root).error(), /*fatal=*/true);
        return;
    }

    {
        // For setting, we need an exclusive lock; so get one after we
        // dropped the shared one
        std::unique_lock setting{*config_lock};
        repository_config->SetPrecomputedRoot(PrecomputedRoot{key},
                                              *resolved_root);
    }
    (*setter)(*std::move(resolved_root)->GetTreeHash());
}

auto FillRoots(
    std::size_t jobs,
    gsl::not_null<RepositoryConfig*> const& repository_config,
    gsl::not_null<const GraphTraverser::CommandLineArguments*> const&
        traverser_args,
    gsl::not_null<const ExecutionContext*> const& context,
    ServeApi const* serve,
    gsl::not_null<const StorageConfig*> const& storage_config,
    gsl::not_null<std::optional<RehashUtils::Rehasher>*> const& rehash,
    gsl::not_null<std::shared_mutex*> const& config_lock,
    gsl::not_null<std::mutex*> const& git_lock,
    gsl::not_null<Statistics*> const& stats,
    gsl::not_null<TaskTracker*> const& tasks) -> RootMap {
    RootMap::ValueCreator fill_roots = [storage_config,
                                        rehash,
                                        repository_config,
                                        traverser_args,
                                        serve,
                                        context,
                                        config_lock,
                                        git_lock,
                                        stats,
                                        tasks,
                                        jobs](auto /*ts*/,
                                              auto setter,
                                              auto logger,
                                              auto subcaller,
                                              PrecomputedRoot const& key) {
        auto annotated_logger = WhileHandling(key, logger);
        std::vector<PrecomputedRoot> roots{};
        {
            std::shared_lock reading{*config_lock};
            roots =
                GetRootDeps(key.GetReferencedRepository(), repository_config);
        }
        (*subcaller)(
            roots,
            [key,
             repository_config,
             traverser_args,
             context,
             serve,
             storage_config,
             config_lock,
             rehash,
             git_lock,
             jobs,
             stats,
             tasks,
             logger = annotated_logger,
             setter](auto /*values*/) {
                if (auto const computed = key.AsComputed()) {
                    ComputeAndFill(*computed,
                                   repository_config,
                                   traverser_args,
                                   serve,
                                   context,
                                   storage_config,
                                   rehash,
                                   config_lock,
                                   git_lock,
                                   stats,
                                   tasks,
                                   jobs,
                                   logger,
                                   setter);
                }
                if (auto const tree_structure = key.AsTreeStructure()) {
                    ComputeTreeStructureAndFill(*tree_structure,
                                                repository_config,
                                                serve,
                                                storage_config,
                                                config_lock,
                                                git_lock,
                                                logger,
                                                setter);
                }
            },
            annotated_logger);
    };
    return RootMap{fill_roots, jobs};
}

}  // namespace

auto EvaluatePrecomputedRoots(
    gsl::not_null<RepositoryConfig*> const& repository_config,
    std::string const& main_repo,
    ServeApi const* serve,
    StorageConfig const& storage_config,
    GraphTraverser::CommandLineArguments const& traverser_args,
    gsl::not_null<const ExecutionContext*> const& context,
    std::size_t jobs) -> bool {
    auto roots = GetRootDeps(main_repo, repository_config);
    if (not roots.empty()) {
        Logger::Log(LogLevel::Info,
                    "Repository {} depends on {} top-level computed roots",
                    nlohmann::json(main_repo).dump(),
                    roots.size());
        // First, ensure the local git repository is present
        if (not FileSystemManager::CreateDirectory(storage_config.GitRoot())) {
            Logger::Log(LogLevel::Error,
                        "Failed to create directory {}",
                        storage_config.GitRoot().string());
            return false;
        }
        if (not GitRepo::InitAndOpen(storage_config.GitRoot(),
                                     /*is_bare=*/true)) {
            Logger::Log(LogLevel::Error,
                        "Failed to init and open git repository {}",
                        storage_config.GitRoot().string());
            return false;
        }

        // Prepare rehash-function, if rehashing is required
        std::optional<RehashUtils::Rehasher> rehash;
        if (not ProtocolTraits::IsNative(
                storage_config.hash_function.GetType())) {
            auto native_storage_config =
                StorageConfig::Builder::Rebuild(storage_config)
                    .SetHashType(HashFunction::Type::GitSHA1)
                    .Build();
            if (not native_storage_config) {
                Logger::Log(
                    LogLevel::Error,
                    "Failed to create native storage config for rehashing:\n{}",
                    std::move(native_storage_config).error());
                return false;
            }
            rehash.emplace(storage_config,
                           *std::move(native_storage_config),
                           context->apis);
        }

        // Our RepositoryConfig is a bit problematic: it cannot be copied, hence
        // we have to change in place. Moreover, it is tread-safe for read
        // access, but not for writing, so we have to synchronize access out of
        // bound.
        std::shared_mutex repo_config_access{};
        std::mutex git_access{};
        Statistics stats{};
        TaskTracker root_tasks{};
        auto root_map = FillRoots(jobs,
                                  repository_config,
                                  &traverser_args,
                                  context,
                                  serve,
                                  &storage_config,
                                  &rehash,
                                  &repo_config_access,
                                  &git_access,
                                  &stats,
                                  &root_tasks);
        std::atomic<bool> done = false;
        std::atomic<bool> failed = false;

        std::atomic<bool> build_done = false;
        std::condition_variable cv{};
        auto reporter = RootsProgress::Reporter(&stats, &root_tasks);
        auto observer = std::thread(
            [&reporter, &build_done, &cv] { reporter(&build_done, &cv); });
        {
            TaskSystem ts{jobs};
            root_map.ConsumeAfterKeysReady(
                &ts,
                roots,
                [&roots, &done](auto values) {
                    Logger::Log(LogLevel::Debug, [&]() {
                        std::ostringstream msg{};
                        msg << "Root building completed; top-level computed "
                               "roots";
                        for (int i = 0; i < roots.size(); i++) {
                            auto const& root = roots[i];
                            msg << "\n - " << root.ToString()
                                << " evaluates to " << *values[i];
                        }
                        return msg.str();
                    });
                    done = true;
                },
                [&failed](auto const& msg, bool fatal) {
                    Logger::Log(
                        fatal ? LogLevel::Error : LogLevel::Warning,
                        "While materializing top-level computed roots:\n{}",
                        msg);
                    failed = failed or fatal;
                }

            );
        }
        build_done = true;
        cv.notify_all();
        observer.join();

        if (failed) {
            return false;
        }
        if (not done) {
            const std::function k_root_printer =
                [](PrecomputedRoot const& x) -> std::string {
                return x.ToString();
            };

            auto cycle_msg = DetectAndReportCycle(
                "computed roots", root_map, k_root_printer);
            if (cycle_msg) {
                Logger::Log(LogLevel::Error, "{}", *cycle_msg);
            }
            else {
                DetectAndReportPending(
                    "computed roots", root_map, k_root_printer);
            }
            return false;
        }
        return true;
    }
    return true;
}
