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

#include "src/other_tools/just_mr/setup.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <optional>
#include <thread>

#include "nlohmann/json.hpp"
#include "src/buildtool/auth/authentication.hpp"
#include "src/buildtool/common/remote/retry_config.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_common.hpp"
#include "src/buildtool/execution_api/common/api_bundle.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/execution_api/local/config.hpp"
#include "src/buildtool/execution_api/local/context.hpp"
#include "src/buildtool/execution_api/local/local_api.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_api.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/execution_api/remote/context.hpp"
#include "src/buildtool/file_system/symlinks_map/resolve_symlinks_map.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/main/retry.hpp"
#include "src/buildtool/multithreading/async_map_utils.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/buildtool/serve_api/remote/config.hpp"
#include "src/buildtool/serve_api/remote/serve_api.hpp"
#include "src/buildtool/storage/fs_utils.hpp"
#include "src/buildtool/storage/garbage_collector.hpp"
#include "src/other_tools/just_mr/exit_codes.hpp"
#include "src/other_tools/just_mr/progress_reporting/progress.hpp"
#include "src/other_tools/just_mr/progress_reporting/progress_reporter.hpp"
#include "src/other_tools/just_mr/progress_reporting/statistics.hpp"
#include "src/other_tools/just_mr/setup_utils.hpp"
#include "src/other_tools/just_mr/utils.hpp"
#include "src/other_tools/ops_maps/content_cas_map.hpp"
#include "src/other_tools/ops_maps/critical_git_op_map.hpp"
#include "src/other_tools/ops_maps/git_tree_fetch_map.hpp"
#include "src/other_tools/repo_map/repos_to_setup_map.hpp"
#include "src/other_tools/root_maps/commit_git_map.hpp"
#include "src/other_tools/root_maps/content_git_map.hpp"
#include "src/other_tools/root_maps/distdir_git_map.hpp"
#include "src/other_tools/root_maps/fpath_git_map.hpp"
#include "src/other_tools/root_maps/tree_id_git_map.hpp"
#include "src/utils/cpp/file_locking.hpp"

auto MultiRepoSetup(std::shared_ptr<Configuration> const& config,
                    MultiRepoCommonArguments const& common_args,
                    MultiRepoSetupArguments const& setup_args,
                    MultiRepoJustSubCmdsArguments const& just_cmd_args,
                    MultiRepoRemoteAuthArguments const& auth_args,
                    RetryArguments const& retry_args,
                    StorageConfig const& native_storage_config,
                    Storage const& native_storage,
                    bool interactive,
                    std::string const& multi_repo_tool_name)
    -> std::optional<std::filesystem::path> {
    // provide report
    Logger::Log(LogLevel::Info, "Performing repositories setup");
    // set anchor dir to setup_root; current dir will be reverted when anchor
    // goes out of scope
    auto cwd_anchor = FileSystemManager::ChangeDirectory(
        common_args.just_mr_paths->setup_root);

    auto repos = (*config)["repositories"];
    if (not repos.IsNotNull()) {
        Logger::Log(LogLevel::Error,
                    "Config: Mandatory key \"repositories\" missing");
        return std::nullopt;
    }
    if (not repos->IsMap()) {
        Logger::Log(LogLevel::Error,
                    "Config: Value for key \"repositories\" is not a map");
        return std::nullopt;
    }

    auto setup_repos =
        std::make_shared<JustMR::SetupRepos>();  // repos to setup and include
    nlohmann::json mr_config{};                  // output config to populate

    auto main = common_args.main;  // get local copy of updated clarg 'main', as
                                   // it might be updated again from config

    // check if config provides main repo name
    if (not main) {
        auto main_from_config = (*config)["main"];
        if (main_from_config.IsNotNull()) {
            if (main_from_config->IsString()) {
                main = main_from_config->String();
            }
            else {
                Logger::Log(
                    LogLevel::Error,
                    "Unsupported value {} for field \"main\" in configuration.",
                    main_from_config->ToString());
                return std::nullopt;
            }
        }
    }
    // pass on main that was explicitly set via command line or config
    if (main) {
        mr_config["main"] = *main;
    }
    // get default repos to setup and to include
    JustMR::Utils::DefaultReachableRepositories(repos, setup_repos);
    // check if main is to be taken as first repo name lexicographically
    if (not main and not setup_repos->to_setup.empty()) {
        main = *std::min_element(setup_repos->to_setup.begin(),
                                 setup_repos->to_setup.end());
    }
    // final check on which repos are to be set up
    if (main and not setup_args.sub_all) {
        JustMR::Utils::ReachableRepositories(repos, *main, setup_repos);
    }
    Logger::Log(LogLevel::Info,
                "Found {} repositories to set up",
                setup_repos->to_setup.size());

    // setup local execution config
    auto const local_exec_config =
        JustMR::Utils::CreateLocalExecutionConfig(common_args);
    if (not local_exec_config) {
        return std::nullopt;
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
    if (common_args.compatible) {
        auto config = StorageConfig::Builder{}
                          .SetBuildRoot(native_storage_config.build_root)
                          .SetHashType(HashFunction::Type::PlainSHA256)
                          .Build();
        if (not config) {
            Logger::Log(LogLevel::Error, config.error());
            return std::nullopt;
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
            return std::nullopt;
        }
    }

    // setup authentication config
    auto const auth_config = JustMR::Utils::CreateAuthConfig(auth_args);
    if (not auth_config) {
        return std::nullopt;
    }

    // setup the retry config
    auto const retry_config = CreateRetryConfig(retry_args);
    if (not retry_config) {
        return std::nullopt;
    }

    // setup remote execution config
    auto const remote_exec_config = JustMR::Utils::CreateRemoteExecutionConfig(
        common_args.remote_execution_address, common_args.remote_serve_address);
    if (not remote_exec_config) {
        return std::nullopt;
    }

    // create the remote api
    auto const hash_fct =
        compat_local_context != nullptr
            ? compat_local_context->storage_config->hash_function
            : native_local_context.storage_config->hash_function;
    IExecutionApi::Ptr remote_api = nullptr;
    if (auto const address = remote_exec_config->remote_address) {
        ExecutionConfiguration config;
        config.skip_cache_lookup = false;
        remote_api = std::make_shared<BazelApi>("remote-execution",
                                                address->host,
                                                address->port,
                                                &*auth_config,
                                                &*retry_config,
                                                config,
                                                &hash_fct);
    }
    bool const has_remote_api =
        remote_api != nullptr and not common_args.compatible;

    // pack the remote context
    RemoteContext const remote_context{.auth = &*auth_config,
                                       .retry_config = &*retry_config,
                                       .exec_config = &*remote_exec_config};

    // setup the api for serving roots
    auto serve_config =
        JustMR::Utils::CreateServeConfig(common_args.remote_serve_address);
    if (not serve_config) {
        return std::nullopt;
    }
    auto const apis =
        ApiBundle{.hash_function = hash_fct,
                  .local = native_local_api,
                  .remote = has_remote_api ? remote_api : native_local_api};
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
            return std::nullopt;  // this check logs error on failure
        }

        // check the compatibility mode of the serve endpoint
        auto compatible = serve->IsCompatible();
        if (not compatible) {
            Logger::Log(LogLevel::Warning,
                        "Checking compatibility configuration of the provided "
                        "serve endpoint failed.");
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
    JustMRProgress progress{setup_repos->to_setup.size()};

    // setup the required async maps
    auto crit_git_op_ptr = std::make_shared<CriticalGitOpGuard>();
    auto critical_git_op_map = CreateCriticalGitOpMap(crit_git_op_ptr);

    auto content_cas_map =
        CreateContentCASMap(common_args.just_mr_paths,
                            common_args.alternative_mirrors,
                            common_args.ca_info,
                            &critical_git_op_map,
                            serve ? &*serve : nullptr,
                            &native_storage_config,
                            &native_storage,
                            &(*apis.local),
                            has_remote_api ? &*apis.remote : nullptr,
                            &progress,
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
        serve ? &*serve : nullptr,
        &native_storage_config,
        compat_storage_config != nullptr ? &*compat_storage_config : nullptr,
        compat_storage != nullptr ? &*compat_storage : nullptr,
        &(*apis.local),
        has_remote_api ? &*apis.remote : nullptr,
        false, /* backup_to_remote */
        &progress,
        common_args.jobs);

    auto resolve_symlinks_map = CreateResolveSymlinksMap();

    auto commit_git_map = CreateCommitGitMap(
        &critical_git_op_map,
        &import_to_git_map,
        common_args.just_mr_paths,
        common_args.alternative_mirrors,
        common_args.git_path->string(),
        *common_args.local_launcher,
        serve ? &*serve : nullptr,
        &native_storage_config,
        compat_storage_config != nullptr ? &*compat_storage_config : nullptr,
        compat_storage != nullptr ? &*compat_storage : nullptr,
        &(*apis.local),
        has_remote_api ? &*apis.remote : nullptr,
        common_args.fetch_absent,
        &progress,
        common_args.jobs);

    auto content_git_map = CreateContentGitMap(
        &content_cas_map,
        &import_to_git_map,
        common_args.just_mr_paths,
        common_args.alternative_mirrors,
        common_args.ca_info,
        &resolve_symlinks_map,
        &critical_git_op_map,
        serve ? &*serve : nullptr,
        &native_storage_config,
        compat_storage_config != nullptr ? &*compat_storage_config : nullptr,
        &native_storage,
        compat_storage != nullptr ? &*compat_storage : nullptr,
        has_remote_api ? &*apis.local : nullptr,  // only needed if remote given
        has_remote_api ? &*apis.remote : nullptr,
        common_args.fetch_absent,
        &progress,
        common_args.jobs);

    auto foreign_file_git_map =
        CreateForeignFileGitMap(&content_cas_map,
                                &import_to_git_map,
                                serve ? &*serve : nullptr,
                                &native_storage_config,
                                &native_storage,
                                common_args.fetch_absent,
                                common_args.jobs);

    auto fpath_git_map = CreateFilePathGitMap(
        just_cmd_args.subcmd_name,
        &critical_git_op_map,
        &import_to_git_map,
        &resolve_symlinks_map,
        serve ? &*serve : nullptr,
        &native_storage_config,
        compat_storage_config != nullptr ? &*compat_storage_config : nullptr,
        compat_storage != nullptr ? &*compat_storage : nullptr,
        has_remote_api ? &*apis.local : nullptr,  // only needed if remote given
        has_remote_api ? &*apis.remote : nullptr,
        common_args.jobs,
        multi_repo_tool_name,
        common_args.just_path ? common_args.just_path->string()
                              : kDefaultJustPath);

    auto distdir_git_map = CreateDistdirGitMap(
        &content_cas_map,
        &import_to_git_map,
        &critical_git_op_map,
        serve ? &*serve : nullptr,
        &native_storage_config,
        compat_storage_config != nullptr ? &*compat_storage_config : nullptr,
        &native_storage,
        compat_storage != nullptr ? &*compat_storage : nullptr,
        &(*apis.local),
        has_remote_api ? &*apis.remote : nullptr,
        common_args.jobs);

    auto tree_id_git_map = CreateTreeIdGitMap(
        &git_tree_fetch_map,
        &critical_git_op_map,
        &import_to_git_map,
        common_args.fetch_absent,
        serve ? &*serve : nullptr,
        &native_storage_config,
        compat_storage_config != nullptr ? &*compat_storage_config : nullptr,
        compat_storage != nullptr ? &*compat_storage : nullptr,
        &(*apis.local),
        has_remote_api ? &*apis.remote : nullptr,
        common_args.jobs);

    auto repos_to_setup_map = CreateReposToSetupMap(config,
                                                    main,
                                                    interactive,
                                                    &commit_git_map,
                                                    &content_git_map,
                                                    &foreign_file_git_map,
                                                    &fpath_git_map,
                                                    &distdir_git_map,
                                                    &tree_id_git_map,
                                                    common_args.fetch_absent,
                                                    &stats,
                                                    common_args.jobs);

    // set up progress observer
    std::atomic<bool> done{false};
    std::condition_variable cv{};
    auto reporter = JustMRProgressReporter::Reporter(&stats, &progress);
    auto observer =
        std::thread([reporter, &done, &cv]() { reporter(&done, &cv); });

    // Populate workspace_root and TAKE_OVER fields
    bool failed{false};
    bool has_value{false};

    {
        TaskSystem ts{common_args.jobs};
        repos_to_setup_map.ConsumeAfterKeysReady(
            &ts,
            setup_repos->to_setup,
            [&failed,
             &has_value,
             &mr_config,
             repos,
             setup_repos,
             main,
             interactive,
             multi_repo_tool_name](auto const& values) noexcept {
                try {
                    has_value = true;
                    // set the initial setup repositories
                    nlohmann::json mr_repos{};
                    for (auto const& repo : setup_repos->to_setup) {
                        auto i = static_cast<std::size_t>(
                            &repo - setup_repos->to_setup.data());  // get index
                        mr_repos[repo] = *values[i];
                    }
                    // populate ALT_DIRS
                    constexpr auto kErrMsgFormat =
                        "While performing {} {}:\nWhile populating fields for "
                        "repository {}:\nExpected value for key \"{}\" to be a "
                        "string, but found {}";
                    for (auto const& repo : setup_repos->to_include) {
                        auto desc = repos->Get(repo, Expression::none_t{});
                        if (desc.IsNotNull()) {
                            if (not((main and (repo == *main)) and
                                    interactive)) {
                                for (auto const& key : kAltDirs) {
                                    auto val =
                                        desc->Get(key, Expression::none_t{});
                                    if (val.IsNotNull()) {
                                        // we expect a string
                                        if (not val->IsString()) {
                                            Logger::Log(LogLevel::Error,
                                                        kErrMsgFormat,
                                                        multi_repo_tool_name,
                                                        interactive
                                                            ? "setup-env"
                                                            : "setup",
                                                        repo,
                                                        key,
                                                        val->ToString());
                                            failed = true;
                                            return;
                                        }
                                        if (not((main and
                                                 (val->String() == *main)) and
                                                interactive)) {
                                            mr_repos[repo][key] =
                                                mr_repos[val->String()]
                                                        ["workspace_root"];
                                        }
                                        // otherwise, continue
                                    }
                                }
                            }
                        }
                    }
                    // retain only the repos we need
                    for (auto const& repo : setup_repos->to_include) {
                        mr_config["repositories"][repo] = mr_repos[repo];
                    }
                } catch (std::exception const& ex) {
                    Logger::Log(LogLevel::Error,
                                "While performing {} {}:\nPopulating the "
                                "configuration failed with:\n{}",
                                multi_repo_tool_name,
                                interactive ? "setup-env" : "setup",
                                ex.what());
                    failed = true;
                }
            },
            [&failed, interactive, multi_repo_tool_name](auto const& msg,
                                                         bool fatal) {
                Logger::Log(fatal ? LogLevel::Error : LogLevel::Warning,
                            "While performing {} {}:\n{}",
                            multi_repo_tool_name,
                            interactive ? "setup-env" : "setup",
                            msg);
                failed = failed or fatal;
            });
    }

    // close progress observer
    done = true;
    cv.notify_all();
    observer.join();

    if (failed) {
        return std::nullopt;
    }
    if (not has_value) {
        // check for cycles in maps where cycles can occur and have meaning
        if (auto error = DetectAndReportCycle("resolving symlinks",
                                              resolve_symlinks_map,
                                              kGitObjectToResolvePrinter)) {
            Logger::Log(LogLevel::Error, *error);
            return std::nullopt;
        }
        DetectAndReportPending(
            "setup", repos_to_setup_map, kReposToSetupPrinter);
        return std::nullopt;
    }
    // if successful, return the output config
    return StorageUtils::AddToCAS(native_storage, mr_config.dump(2));
}
