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
#include "src/buildtool/common/cli.hpp"
#include "src/buildtool/common/statistics.hpp"
#include "src/buildtool/computed_roots/analyse_and_build.hpp"
#include "src/buildtool/computed_roots/lookup_cache.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/common/api_bundle.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/execution_api/utils/rehash_utils.hpp"
#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/git_cas.hpp"
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
#include "src/buildtool/storage/storage.hpp"
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

auto ImportToGitCas(std::filesystem::path root_dir,
                    StorageConfig const& storage_config,
                    gsl::not_null<std::mutex*> const& git_lock,
                    RootMap::LoggerPtr const& logger)
    -> std::optional<std::string> {
    auto tmp_dir = storage_config.CreateTypedTmpDir("import-repo");
    if (not tmp_dir) {
        (*logger)("Failed to create temporary directory", true);
        return std::nullopt;
    }
    auto const& repo_path = tmp_dir->GetPath();
    auto git_repo = GitRepo::InitAndOpen(repo_path, /*is_bare=*/false);
    if (not git_repo) {
        (*logger)(fmt::format("Could not initializie git repository {}",
                              repo_path.string()),
                  true);
        return std::nullopt;
    }
    auto wrapped_logger = std::make_shared<GitRepo::anon_logger_t>(
        [logger, &root_dir, &repo_path](auto const& msg, bool fatal) {
            (*logger)(fmt::format(
                          "While commiting directory {} in repository {}:\n{}",
                          root_dir.string(),
                          repo_path.string(),
                          msg),
                      fatal);
        });
    auto commit_hash =
        git_repo->CommitDirectory(root_dir, "computed root", wrapped_logger);
    if (not commit_hash) {
        return std::nullopt;
    }
    auto just_git_cas = GitCAS::Open(storage_config.GitRoot());
    if (not just_git_cas) {
        (*logger)(fmt::format("Failed to open Git ODB at {}",
                              storage_config.GitRoot().string()),
                  true);
        return std::nullopt;
    }
    auto just_git_repo = GitRepo::Open(just_git_cas);
    if (not just_git_repo) {
        (*logger)(fmt::format("Failed to open Git repository at {}",
                              storage_config.GitRoot().string()),
                  true);
        return std::nullopt;
    }
    wrapped_logger = std::make_shared<GitRepo::anon_logger_t>(
        [logger, &storage_config](auto const& msg, bool fatal) {
            (*logger)(fmt::format("While fetchting in repository {}:\n{}",
                                  storage_config.GitRoot().string(),
                                  msg),
                      fatal);
        });
    if (not just_git_repo->LocalFetchViaTmpRepo(storage_config,
                                                repo_path.string(),
                                                /*branch=*/std::nullopt,
                                                wrapped_logger)) {
        return std::nullopt;
    }
    wrapped_logger = std::make_shared<GitRepo::anon_logger_t>(
        [logger, &commit_hash, &storage_config](auto const& msg, bool fatal) {
            (*logger)(
                fmt::format("While tagging commit {} in repository {}:\n{}",
                            *commit_hash,
                            storage_config.GitRoot().string(),
                            msg),
                fatal);
        });
    {
        std::unique_lock tagging{*git_lock};
        auto git_repo = GitRepo::Open(storage_config.GitRoot());
        if (not git_repo) {
            (*logger)(fmt::format("Failed to open git CAS repository {}",
                                  storage_config.GitRoot().string()),
                      true);
            return std::nullopt;
        }
        // Important: message must be consistent with just-mr!
        if (not git_repo->KeepTag(
                *commit_hash, "Keep referenced tree alive", wrapped_logger)) {
            return std::nullopt;
        }
    }
    wrapped_logger = std::make_shared<GitRepo::anon_logger_t>(
        [logger, &commit_hash, &storage_config](auto const& msg, bool fatal) {
            (*logger)(fmt::format("While retrieving tree id of commit {} in "
                                  "repository {}:\n{}",
                                  *commit_hash,
                                  storage_config.GitRoot().string(),
                                  msg),
                      fatal);
        });
    auto res =
        just_git_repo->GetSubtreeFromCommit(*commit_hash, ".", wrapped_logger);
    if (not res) {
        return std::nullopt;
    }

    return *std::move(res);
}

auto IsTreePresent(std::string const& tree_id,
                   gsl::not_null<const StorageConfig*> const& storage_config,
                   GitRepo::anon_logger_ptr const& logger)
    -> std::optional<bool> {
    auto just_git_cas = GitCAS::Open(storage_config->GitRoot());
    if (not just_git_cas) {
        (*logger)(fmt::format("Failed to open Git ODB at {}",
                              storage_config->GitRoot().string()),
                  true);
        return std::nullopt;
    }
    auto just_git_repo = GitRepo::Open(just_git_cas);
    if (not just_git_repo) {
        (*logger)(fmt::format("Failed to open Git repository at {}",
                              storage_config->GitRoot().string()),
                  true);
        return std::nullopt;
    }
    return just_git_repo->CheckTreeExists(tree_id, logger);
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
    Logger build_logger = Logger(
        target.ToString(),
        std::vector<LogSinkFactory>{LogSinkFile::CreateFactory(log_file)});
    auto root_build_args = *traverser_args;
    root_build_args.stage =
        StageArguments{.output_dir = root_dir, .remember = true};
    root_build_args.rebuild = std::nullopt;
    root_build_args.build.print_to_stdout = std::nullopt;
    root_build_args.build.dump_artifacts = std::nullopt;
    root_build_args.build.show_runfiles = false;
    auto root_exec_context = ExecutionContext{context->repo_config,
                                              context->apis,
                                              context->remote_context,
                                              &statistics,
                                              &progress};

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
        std::string root = **cache_lookup;
        auto wrapped_logger = std::make_shared<GitRepo::anon_logger_t>(
            [&logger, &root](auto const& msg, bool fatal) {
                (*logger)(fmt::format("While checking presence of tree {} in "
                                      "local git repo:\n{}",
                                      root,
                                      msg),
                          fatal);
            });
        auto tree_present = IsTreePresent(root, storage_config, wrapped_logger);
        if (not tree_present) {
            // fatal error; logger already called by IsTreePresent
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
    auto result = ImportToGitCas(root_dir, *storage_config, git_lock, logger);
    if (not result) {
        return;
    }
    Logger::Log(LogLevel::Performance,
                "Root {} evaluted to {}, log {}",
                target.ToString(),
                *result,
                log_desc);
    auto root_result = FileRoot::FromGit(storage_config->GitRoot(), *result);
    if (not root_result) {
        (*logger)(fmt::format("Failed to create git root for {}", *result),
                  /*fatal=*/true);
        return;
    }
    {
        // For setting, we need an exclusiver lock; so get one after we
        // dropped the shared one
        std::unique_lock setting{*config_lock};
        repository_config->SetPrecomputedRoot(PrecomputedRoot{key},
                                              *root_result);
    }
    (*setter)(std::move(*result));
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
    gsl::not_null<std::mutex*> const& git_lock) -> RootMap {
    RootMap::ValueCreator fill_roots = [storage_config,
                                        rehash,
                                        repository_config,
                                        traverser_args,
                                        serve,
                                        context,
                                        config_lock,
                                        git_lock,
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
                                   jobs,
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
    std::optional<StorageConfig> const& git_storage_config,
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
        const bool needs_rehash =
            git_storage_config and
            (git_storage_config->hash_function.GetType() !=
             storage_config.hash_function.GetType());
        auto rehash =
            needs_rehash
                ? std::make_optional<RehashUtils::Rehasher>(
                      storage_config, *git_storage_config, context->apis)
                : std::nullopt;

        // Our RepositoryConfig is a bit problematic: it cannot be copied, hence
        // we have to change in place. Moreover, it is tread-safe for read
        // access, but not for writing, so we have to synchronize access out of
        // bound.
        std::shared_mutex repo_config_access{};
        std::mutex git_access{};
        auto root_map = FillRoots(jobs,
                                  repository_config,
                                  &traverser_args,
                                  context,
                                  serve,
                                  &storage_config,
                                  &rehash,
                                  &repo_config_access,
                                  &git_access);
        bool failed = false;
        bool done = false;
        {
            TaskSystem ts{jobs};
            root_map.ConsumeAfterKeysReady(
                &ts,
                roots,
                [&roots, &done](auto values) {
                    Logger::Log(LogLevel::Progress,
                                "Computed roots evaluated, {} top level",
                                roots.size());
                    Logger::Log(LogLevel::Debug, [&]() {
                        std::ostringstream msg{};
                        msg << "Top-level computed roots";
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
