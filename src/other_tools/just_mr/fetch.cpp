// Copyright 2023 Huawei Cloud Computing Technology Co., Ltd.
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

#include "src/other_tools/just_mr/fetch.hpp"

#include <algorithm>
#include <atomic>
#include <compare>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <optional>
#include <thread>
#include <unordered_map>
#include <utility>  // std::move
#include <vector>

#include "fmt/core.h"
#include "gsl/gsl"
#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/build_engine/expression/expression_ptr.hpp"
#include "src/buildtool/common/remote/remote_common.hpp"
#include "src/buildtool/common/user_structs.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/bazel_msg/execution_config.hpp"
#include "src/buildtool/execution_api/common/api_bundle.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/execution_api/local/context.hpp"
#include "src/buildtool/execution_api/local/local_api.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_api.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/execution_api/remote/context.hpp"
#include "src/buildtool/execution_api/serve/mr_local_api.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/main/retry.hpp"
#include "src/buildtool/multithreading/async_map_consumer.hpp"
#include "src/buildtool/multithreading/async_map_utils.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/buildtool/progress_reporting/base_progress_reporter.hpp"
#include "src/buildtool/serve_api/remote/serve_api.hpp"
#include "src/buildtool/storage/garbage_collector.hpp"
#include "src/other_tools/just_mr/exit_codes.hpp"
#include "src/other_tools/just_mr/progress_reporting/progress.hpp"
#include "src/other_tools/just_mr/progress_reporting/progress_reporter.hpp"
#include "src/other_tools/just_mr/progress_reporting/statistics.hpp"
#include "src/other_tools/just_mr/setup_utils.hpp"
#include "src/other_tools/just_mr/utils.hpp"
#include "src/other_tools/ops_maps/archive_fetch_map.hpp"
#include "src/other_tools/ops_maps/content_cas_map.hpp"
#include "src/other_tools/ops_maps/critical_git_op_map.hpp"
#include "src/other_tools/ops_maps/git_tree_fetch_map.hpp"
#include "src/other_tools/ops_maps/import_to_git_map.hpp"
#include "src/other_tools/utils/parse_archive.hpp"
#include "src/other_tools/utils/parse_git_tree.hpp"
#include "src/utils/cpp/expected.hpp"
#include "src/utils/cpp/file_locking.hpp"

auto MultiRepoFetch(std::shared_ptr<Configuration> const& config,
                    MultiRepoCommonArguments const& common_args,
                    MultiRepoSetupArguments const& setup_args,
                    MultiRepoFetchArguments const& fetch_args,
                    MultiRepoRemoteAuthArguments const& auth_args,
                    RetryArguments const& retry_args,
                    StorageConfig const& native_storage_config,
                    Storage const& native_storage,
                    std::string multi_repository_tool_name) -> int {
    // provide report
    Logger::Log(LogLevel::Info, "Performing repositories fetch");

    // find fetch dir
    auto fetch_dir = fetch_args.fetch_dir;
    if (not fetch_dir) {
        for (auto const& d : common_args.just_mr_paths->distdirs) {
            if (FileSystemManager::IsDirectory(d)) {
                fetch_dir = std::filesystem::weakly_canonical(d);
                break;
            }
        }
    }
    if (not fetch_dir) {
        auto considered = nlohmann::json(common_args.just_mr_paths->distdirs);
        Logger::Log(LogLevel::Error,
                    "No directory found to fetch to, considered {}",
                    considered.dump());
        return kExitFetchError;
    }

    auto repos = (*config)["repositories"];
    if (not repos.IsNotNull()) {
        Logger::Log(LogLevel::Error,
                    "Config: Mandatory key \"repositories\" missing");
        return kExitFetchError;
    }
    if (not repos->IsMap()) {
        Logger::Log(LogLevel::Error,
                    "Config: Value for key \"repositories\" is not a map");
        return kExitFetchError;
    }

    auto fetch_repos =
        std::make_shared<JustMR::SetupRepos>();  // repos to setup and include
    JustMR::Utils::DefaultReachableRepositories(repos, fetch_repos);

    if (not setup_args.sub_all) {
        auto main = common_args.main;
        if (not main and not fetch_repos->to_include.empty()) {
            main = *std::min_element(fetch_repos->to_include.begin(),
                                     fetch_repos->to_include.end());
        }
        if (main) {
            JustMR::Utils::ReachableRepositories(repos, *main, fetch_repos);
        }

        std::function<bool(std::filesystem::path const&,
                           std::filesystem::path const&)>
            is_subpath = [](std::filesystem::path const& path,
                            std::filesystem::path const& base) {
                return (std::filesystem::proximate(path, base) == base);
            };

        // warn if fetch_dir is in invocation workspace
        if (common_args.just_mr_paths->workspace_root and
            is_subpath(*fetch_dir,
                       *common_args.just_mr_paths->workspace_root)) {
            auto repo_desc = repos->Get(*main, Expression::none_t{});
            auto repo = repo_desc->Get("repository", Expression::none_t{});
            auto repo_path = repo->Get("path", Expression::none_t{});
            auto repo_type = repo->Get("type", Expression::none_t{});
            if (repo_path->IsString() and repo_type->IsString() and
                (repo_type->String() == "file")) {
                auto repo_path_as_path =
                    std::filesystem::path(repo_path->String());
                if (not repo_path_as_path.is_absolute()) {
                    repo_path_as_path = std::filesystem::weakly_canonical(
                        common_args.just_mr_paths->setup_root /
                        repo_path_as_path);
                }
                // only warn if repo workspace differs to invocation workspace
                if (not is_subpath(
                        repo_path_as_path,
                        *common_args.just_mr_paths->workspace_root)) {
                    Logger::Log(
                        LogLevel::Warning,
                        "Writing distribution files to workspace location {}, "
                        "which is different to the workspace of the requested "
                        "main repository {}.",
                        nlohmann::json(fetch_dir->string()).dump(),
                        nlohmann::json(repo_path_as_path.string()).dump());
                }
            }
        }
    }

    Logger::Log(LogLevel::Info, "Fetching to {}", fetch_dir->string());

    // gather all repos to be fetched
    std::vector<ArchiveContent> archives_to_fetch{};
    std::vector<GitTreeInfo> git_trees_to_fetch{};
    archives_to_fetch.reserve(
        fetch_repos->to_include.size());  // pre-reserve a maximum size
    git_trees_to_fetch.reserve(
        fetch_repos->to_include.size());  // pre-reserve a maximum size
    for (auto const& repo_name : fetch_repos->to_include) {
        auto repo_desc = repos->At(repo_name);
        if (not repo_desc) {
            Logger::Log(LogLevel::Error,
                        "Config: Missing config entry for repository {}",
                        nlohmann::json(repo_name).dump());
            return kExitFetchError;
        }
        if (not repo_desc->get()->IsMap()) {
            Logger::Log(LogLevel::Error,
                        "Config: Config entry for repository {} is not a map",
                        nlohmann::json(repo_name).dump());
            return kExitFetchError;
        }
        auto repo = repo_desc->get()->At("repository");
        if (repo) {
            auto resolved_repo_desc =
                JustMR::Utils::ResolveRepo(repo->get(), repos);
            if (not resolved_repo_desc) {
                Logger::Log(LogLevel::Error,
                            "Config: Found cyclic dependency for repository {}",
                            nlohmann::json(repo_name).dump());
                return kExitFetchError;
            }
            if (not resolved_repo_desc.value()->IsMap()) {
                Logger::Log(
                    LogLevel::Error,
                    "Config: Repository {} resolves to a non-map description",
                    nlohmann::json(repo_name).dump());
                return kExitFetchError;
            }
            // get repo_type
            auto repo_type = (*resolved_repo_desc)->At("type");
            if (not repo_type) {
                Logger::Log(
                    LogLevel::Error,
                    "Config: Mandatory key \"type\" missing for repository {}",
                    nlohmann::json(repo_name).dump());
                return kExitFetchError;
            }
            if (not repo_type->get()->IsString()) {
                Logger::Log(LogLevel::Error,
                            "Config: Unsupported value {} for key \"type\" for "
                            "repository {}",
                            repo_type->get()->ToString(),
                            nlohmann::json(repo_name).dump());
                return kExitFetchError;
            }
            auto repo_type_str = repo_type->get()->String();
            auto const checkout_type_it = kCheckoutTypeMap.find(repo_type_str);
            if (checkout_type_it == kCheckoutTypeMap.end()) {
                Logger::Log(LogLevel::Error,
                            "Config: Unknown repository type {} for {}",
                            nlohmann::json(repo_type_str).dump(),
                            nlohmann::json(repo_name).dump());
                return kExitFetchError;
            }
            // only do work if repo is archive or git tree type
            switch (checkout_type_it->second) {
                case CheckoutType::Archive: {
                    auto logger = std::make_shared<AsyncMapConsumerLogger>(
                        [&repo_name](std::string const& msg, bool fatal) {
                            Logger::Log(
                                fatal ? LogLevel::Error : LogLevel::Warning,
                                "While parsing description of repository "
                                "{}:\n{}",
                                nlohmann::json(repo_name).dump(),
                                msg);
                        });

                    auto archive_repo_info = ParseArchiveDescription(
                        *resolved_repo_desc, repo_type_str, repo_name, logger);
                    if (not archive_repo_info) {
                        return kExitFetchError;
                    }
                    // only fetch if either archive is not marked absent, or if
                    // explicitly told to fetch absent archives
                    if (not archive_repo_info->absent or
                        common_args.fetch_absent) {
                        archives_to_fetch.emplace_back(
                            archive_repo_info->archive);
                    }
                } break;
                case CheckoutType::ForeignFile: {
                    auto logger = std::make_shared<AsyncMapConsumerLogger>(
                        [&repo_name](std::string const& msg, bool fatal) {
                            Logger::Log(
                                fatal ? LogLevel::Error : LogLevel::Warning,
                                "While parsing description of repository "
                                "{}:\n{}",
                                nlohmann::json(repo_name).dump(),
                                msg);
                        });

                    auto repo_info = ParseForeignFileDescription(
                        *resolved_repo_desc, repo_name, logger);
                    if (not repo_info) {
                        return kExitFetchError;
                    }
                    // only fetch if either archive is not marked absent, or if
                    // explicitly told to fetch absent archives
                    if (not repo_info->absent or common_args.fetch_absent) {
                        archives_to_fetch.emplace_back(repo_info->archive);
                    }
                } break;
                case CheckoutType::GitTree: {
                    // check "absent" pragma
                    auto repo_desc_pragma = (*resolved_repo_desc)->At("pragma");
                    auto pragma_absent =
                        (repo_desc_pragma and repo_desc_pragma->get()->IsMap())
                            ? repo_desc_pragma->get()->At("absent")
                            : std::nullopt;
                    auto pragma_absent_value =
                        pragma_absent and pragma_absent->get()->IsBool() and
                        pragma_absent->get()->Bool();
                    // only fetch if either archive is not marked absent, or if
                    // explicitly told to fetch absent archives
                    if (not pragma_absent_value or common_args.fetch_absent) {
                        // enforce mandatory fields
                        auto tree_info =
                            ParseGitTree(*resolved_repo_desc, repo_name);
                        if (not tree_info) {
                            Logger::Log(
                                LogLevel::Error,
                                fmt::format("Config: {}",
                                            std::move(tree_info).error()));
                            return kExitFetchError;
                        }
                        // add to list
                        git_trees_to_fetch.emplace_back(*std::move(tree_info));
                    }
                } break;
                default:
                    continue;  // ignore all other repository types
            }
        }
        else {
            Logger::Log(LogLevel::Error,
                        "Config: Missing repository description for {}",
                        nlohmann::json(repo_name).dump());
            return kExitFetchError;
        }
    }

    // report progress
    auto nr_a = archives_to_fetch.size();
    auto nr_gt = git_trees_to_fetch.size();
    auto str_a = fmt::format("{} {}", nr_a, nr_a == 1 ? "archive" : "archives");
    auto str_gt =
        fmt::format("{} git {}", nr_gt, nr_gt == 1 ? "tree" : "trees");
    auto fetchables = fmt::format("{}{}{}",
                                  nr_a != 0 ? str_a : std::string(),
                                  nr_a != 0 and nr_gt != 0 ? " and " : "",
                                  nr_gt != 0 ? str_gt : std::string());
    if (fetchables.empty()) {
        Logger::Log(LogLevel::Info, "No fetch required");
    }
    else {
        Logger::Log(LogLevel::Info, "Found {} to fetch", fetchables);
    }

    // setup local execution config
    auto local_exec_config =
        JustMR::Utils::CreateLocalExecutionConfig(common_args);
    if (not local_exec_config) {
        return kExitConfigError;
    }

    // pack the native local context and create api
    LocalContext const native_local_context{
        .exec_config = &*local_exec_config,
        .storage_config = &native_storage_config,
        .storage = &native_storage};
    IExecutionApi::Ptr const native_local_api =
        std::make_shared<LocalApi>(&native_local_context);

    // pack the compatible local context, if needed
    std::unique_ptr<StorageConfig> compat_storage_config = nullptr;
    std::unique_ptr<Storage> compat_storage = nullptr;
    std::unique_ptr<LocalContext> compat_local_context = nullptr;
    std::optional<LockFile> compat_lock = std::nullopt;
    IExecutionApi::Ptr compat_local_api = nullptr;
    if (common_args.compatible) {
        auto config = StorageConfig::Builder::Rebuild(native_storage_config)
                          .SetHashType(HashFunction::Type::PlainSHA256)
                          .Build();
        if (not config) {
            Logger::Log(LogLevel::Error, config.error());
            return kExitConfigError;
        }
        compat_storage_config =
            std::make_unique<StorageConfig>(*std::move(config));
        compat_storage = std::make_unique<Storage>(
            Storage::Create(compat_storage_config.get()));
        compat_local_context = std::make_unique<LocalContext>(
            LocalContext{.exec_config = &*local_exec_config,
                         .storage_config = compat_storage_config.get(),
                         .storage = compat_storage.get()});
        // if a compatible storage is created, one must get a lock for it the
        // same way as done for the native one
        compat_lock = GarbageCollector::SharedLock(*compat_storage_config);
        if (not compat_lock) {
            Logger::Log(LogLevel::Error,
                        "Failed to acquire compatible storage gc lock");
            return kExitConfigError;
        }
        compat_local_api = std::make_shared<LocalApi>(&*compat_local_context);
    }

    // setup the overall local api, aware of compatibility
    IExecutionApi::Ptr mr_local_api = std::make_shared<MRLocalApi>(
        &native_local_context,
        &*native_local_api,
        common_args.compatible ? &*compat_local_context : nullptr,
        common_args.compatible ? &*compat_local_api : nullptr);

    // setup authentication config
    auto const auth_config = JustMR::Utils::CreateAuthConfig(auth_args);
    if (not auth_config) {
        return kExitConfigError;
    }

    // setup the retry config
    auto const retry_config = CreateRetryConfig(retry_args);
    if (not retry_config) {
        return kExitConfigError;
    }

    // setup remote execution config
    auto const remote_exec_config = JustMR::Utils::CreateRemoteExecutionConfig(
        common_args.remote_execution_address, common_args.remote_serve_address);
    if (not remote_exec_config) {
        return kExitConfigError;
    }

    // create the remote api
    IExecutionApi::Ptr remote_api = nullptr;
    if (auto const address = remote_exec_config->remote_address) {
        auto const hash_fct =
            compat_local_context != nullptr
                ? compat_local_context->storage_config->hash_function
                : native_local_context.storage_config->hash_function;

        ExecutionConfiguration config;
        config.skip_cache_lookup = false;
        remote_api = std::make_shared<BazelApi>("remote-execution",
                                                address->host,
                                                address->port,
                                                &*auth_config,
                                                &*retry_config,
                                                config,
                                                hash_fct,
                                                mr_local_api->GetTempSpace());
    }
    bool const has_remote_api = remote_api != nullptr;

    // pack the remote context
    RemoteContext const remote_context{.auth = &*auth_config,
                                       .retry_config = &*retry_config,
                                       .exec_config = &*remote_exec_config};

    // setup the api for serving roots
    auto serve_config =
        JustMR::Utils::CreateServeConfig(common_args.remote_serve_address);
    if (not serve_config) {
        return kExitConfigError;
    }
    auto const apis =
        ApiBundle{.local = mr_local_api,
                  .remote = has_remote_api ? remote_api : mr_local_api};
    auto serve = ServeApi::Create(
        *serve_config,
        compat_local_context != nullptr
            ? &*compat_local_context
            : &native_local_context,  // defines the client's hash_function
        &remote_context,
        &apis /*unused*/);

    // check configuration of the serve endpoint provided
    if (serve) {
        // if we have a remote endpoint explicitly given by the user, it must
        // match what the serve endpoint expects
        if (common_args.remote_execution_address and
            not serve->CheckServeRemoteExecution()) {
            return kExitFetchError;  // this check logs error on failure
        }

        // check the compatibility mode of the serve endpoint
        auto compatible = serve->IsCompatible();
        if (not compatible) {
            Logger::Log(LogLevel::Warning,
                        "Checking compatibility configuration of the provided "
                        "serve endpoint failed. Serve endpoint ignored.");
            serve = std::nullopt;
        }
        if (*compatible != common_args.compatible) {
            Logger::Log(
                LogLevel::Warning,
                "Provided serve endpoint operates in a different compatibility "
                "mode than stated. Serve endpoint ignored.");
            serve = std::nullopt;
        }
    }

    // setup progress and statistics instances
    JustMRStatistics stats{};
    JustMRProgress progress{nr_a + nr_gt};

    // create async maps
    auto crit_git_op_ptr = std::make_shared<CriticalGitOpGuard>();
    auto critical_git_op_map = CreateCriticalGitOpMap(crit_git_op_ptr);

    auto content_cas_map = CreateContentCASMap(
        common_args.just_mr_paths,
        common_args.alternative_mirrors,
        common_args.ca_info,
        &critical_git_op_map,
        serve ? &*serve : nullptr,
        &native_storage_config,
        compat_storage_config != nullptr ? &*compat_storage_config : nullptr,
        &native_storage,
        compat_storage != nullptr ? &*compat_storage : nullptr,
        &(*apis.local),
        has_remote_api ? &*apis.remote : nullptr,
        &progress,
        common_args.jobs);

    auto archive_fetch_map = CreateArchiveFetchMap(
        &content_cas_map,
        *fetch_dir,
        &native_storage,
        &(*apis.local),
        (fetch_args.backup_to_remote and has_remote_api) ? &*apis.remote
                                                         : nullptr,
        &stats,
        common_args.jobs);

    auto import_to_git_map =
        CreateImportToGitMap(&critical_git_op_map,
                             common_args.git_path->string(),
                             *common_args.local_launcher,
                             &native_storage_config,
                             common_args.jobs);

    auto git_tree_fetch_map = CreateGitTreeFetchMap(
        &critical_git_op_map,
        &import_to_git_map,
        common_args.git_path->string(),
        *common_args.local_launcher,
        common_args.alternative_mirrors,
        serve ? &*serve : nullptr,
        &native_storage_config,
        compat_storage_config != nullptr ? &*compat_storage_config : nullptr,
        &(*apis.local),
        has_remote_api ? &*apis.remote : nullptr,
        fetch_args.backup_to_remote,
        &progress,
        common_args.jobs);

    // set up progress observer
    std::atomic<bool> done{false};
    std::condition_variable cv{};
    auto reporter = JustMRProgressReporter::Reporter(&stats, &progress);
    auto observer =
        std::thread([reporter, &done, &cv]() { reporter(&done, &cv); });

    // do the fetch
    bool failed_archives{false};
    bool has_value_archives{false};
    {
        TaskSystem ts{common_args.jobs};
        archive_fetch_map.ConsumeAfterKeysReady(
            &ts,
            archives_to_fetch,
            [&has_value_archives]([[maybe_unused]] auto const& values) {
                has_value_archives = true;
            },
            [&failed_archives, &multi_repository_tool_name](auto const& msg,
                                                            bool fatal) {
                Logger::Log(fatal ? LogLevel::Error : LogLevel::Warning,
                            "While performing {} fetch:\n{}",
                            multi_repository_tool_name,
                            msg);
                failed_archives = failed_archives or fatal;
            });
    }
    bool failed_git_trees{false};
    bool has_value_trees{false};
    {
        TaskSystem ts{common_args.jobs};
        git_tree_fetch_map.ConsumeAfterKeysReady(
            &ts,
            git_trees_to_fetch,
            [&has_value_trees]([[maybe_unused]] auto const& values) {
                has_value_trees = true;
            },
            [&failed_git_trees, &multi_repository_tool_name](auto const& msg,
                                                             bool fatal) {
                Logger::Log(fatal ? LogLevel::Error : LogLevel::Warning,
                            "While performing {} fetch:\n{}",
                            multi_repository_tool_name,
                            msg);
                failed_git_trees = failed_git_trees or fatal;
            });
    }

    // close progress observer
    done = true;
    cv.notify_all();
    observer.join();

    if (failed_archives or failed_git_trees) {
        return kExitFetchError;
    }
    if (not has_value_archives or not has_value_trees) {
        DetectAndReportPending(
            "fetch archives", archive_fetch_map, kArchiveContentPrinter);
        DetectAndReportPending(
            "fetch trees", git_tree_fetch_map, kGitTreeInfoPrinter);
        return kExitFetchError;
    }
    // report success
    Logger::Log(LogLevel::Info, "Fetch completed");
    return kExitSuccess;
}
