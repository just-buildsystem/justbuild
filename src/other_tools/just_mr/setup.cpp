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

#include <filesystem>

#include "nlohmann/json.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/execution_api/local/local_api.hpp"
#include "src/buildtool/file_system/symlinks_map/resolve_symlinks_map.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/buildtool/storage/fs_utils.hpp"
#include "src/other_tools/just_mr/exit_codes.hpp"
#include "src/other_tools/just_mr/progress_reporting/progress.hpp"
#include "src/other_tools/just_mr/progress_reporting/progress_reporter.hpp"
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

auto MultiRepoSetup(std::shared_ptr<Configuration> const& config,
                    MultiRepoCommonArguments const& common_args,
                    MultiRepoSetupArguments const& setup_args,
                    MultiRepoJustSubCmdsArguments const& just_cmd_args,
                    MultiRepoRemoteAuthArguments const& auth_args,
                    bool interactive) -> std::optional<std::filesystem::path> {
    // provide report
    Logger::Log(LogLevel::Info, "Performing repositories setup");
    // set anchor dir to setup_root; current dir will be reverted when anchor
    // goes out of scope
    auto cwd_anchor = FileSystemManager::ChangeDirectory(
        common_args.just_mr_paths->setup_root);

    auto repos = (*config)["repositories"];
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

    // setup the APIs for archive fetches
    auto remote_api = JustMR::Utils::GetRemoteApi(
        common_args.remote_execution_address, auth_args);
    IExecutionApi::Ptr local_api{remote_api ? std::make_unique<LocalApi>()
                                            : nullptr};

    // setup the API for serving trees of Git repos or archives
    auto serve_api_exists = JustMR::Utils::SetupServeApi(
        common_args.remote_serve_address, auth_args);

    // setup the required async maps
    auto crit_git_op_ptr = std::make_shared<CriticalGitOpGuard>();
    auto critical_git_op_map = CreateCriticalGitOpMap(crit_git_op_ptr);
    auto content_cas_map =
        CreateContentCASMap(common_args.just_mr_paths,
                            common_args.alternative_mirrors,
                            common_args.ca_info,
                            serve_api_exists,
                            local_api ? &(*local_api) : nullptr,
                            remote_api ? &(*remote_api) : nullptr,
                            common_args.jobs);
    auto import_to_git_map =
        CreateImportToGitMap(&critical_git_op_map,
                             common_args.git_path->string(),
                             *common_args.local_launcher,
                             common_args.jobs);
    auto git_tree_fetch_map =
        CreateGitTreeFetchMap(&critical_git_op_map,
                              &import_to_git_map,
                              common_args.git_path->string(),
                              *common_args.local_launcher,
                              local_api ? &(*local_api) : nullptr,
                              remote_api ? &(*remote_api) : nullptr,
                              common_args.jobs);
    auto resolve_symlinks_map = CreateResolveSymlinksMap();

    auto commit_git_map =
        CreateCommitGitMap(&critical_git_op_map,
                           &import_to_git_map,
                           common_args.just_mr_paths,
                           common_args.alternative_mirrors,
                           common_args.git_path->string(),
                           *common_args.local_launcher,
                           serve_api_exists,
                           local_api ? &(*local_api) : nullptr,
                           remote_api ? &(*remote_api) : nullptr,
                           common_args.fetch_absent,
                           common_args.jobs);
    auto content_git_map =
        CreateContentGitMap(&content_cas_map,
                            &import_to_git_map,
                            common_args.just_mr_paths,
                            common_args.alternative_mirrors,
                            common_args.ca_info,
                            &resolve_symlinks_map,
                            &critical_git_op_map,
                            serve_api_exists,
                            local_api ? &(*local_api) : nullptr,
                            remote_api ? &(*remote_api) : nullptr,
                            common_args.fetch_absent,
                            common_args.jobs);
    auto fpath_git_map = CreateFilePathGitMap(just_cmd_args.subcmd_name,
                                              &critical_git_op_map,
                                              &import_to_git_map,
                                              &resolve_symlinks_map,
                                              common_args.jobs);
    auto distdir_git_map = CreateDistdirGitMap(&content_cas_map,
                                               &import_to_git_map,
                                               &critical_git_op_map,
                                               common_args.jobs);
    auto tree_id_git_map =
        CreateTreeIdGitMap(&git_tree_fetch_map, common_args.jobs);
    auto repos_to_setup_map = CreateReposToSetupMap(config,
                                                    main,
                                                    interactive,
                                                    &commit_git_map,
                                                    &content_git_map,
                                                    &fpath_git_map,
                                                    &distdir_git_map,
                                                    &tree_id_git_map,
                                                    common_args.fetch_absent,
                                                    common_args.jobs);

    // set up progress observer
    Logger::Log(LogLevel::Info,
                "Found {} repositories to set up",
                setup_repos->to_setup.size());
    JustMRProgress::Instance().SetTotal(setup_repos->to_setup.size());
    std::atomic<bool> done{false};
    std::condition_variable cv{};
    auto reporter = JustMRProgressReporter::Reporter();
    auto observer =
        std::thread([reporter, &done, &cv]() { reporter(&done, &cv); });

    // Populate workspace_root and TAKE_OVER fields
    bool failed{false};
    {
        TaskSystem ts{common_args.jobs};
        repos_to_setup_map.ConsumeAfterKeysReady(
            &ts,
            setup_repos->to_setup,
            [&mr_config, config, setup_repos, main, interactive](
                auto const& values) {
                nlohmann::json mr_repos{};
                for (auto const& repo : setup_repos->to_setup) {
                    auto i = static_cast<size_t>(
                        &repo - &setup_repos->to_setup[0]);  // get index
                    mr_repos[repo] = *values[i];
                }
                // populate ALT_DIRS
                auto repos = (*config)["repositories"];
                if (repos.IsNotNull()) {
                    for (auto const& repo : setup_repos->to_include) {
                        auto desc = repos->Get(repo, Expression::none_t{});
                        if (desc.IsNotNull()) {
                            if (not((main and (repo == *main)) and
                                    interactive)) {
                                for (auto const& key : kAltDirs) {
                                    auto val =
                                        desc->Get(key, Expression::none_t{});
                                    if (val.IsNotNull() and
                                        not((main and val->IsString() and
                                             (val->String() == *main)) and
                                            interactive)) {
                                        mr_repos[repo][key] =
                                            mr_repos[val->String()]
                                                    ["workspace_root"];
                                    }
                                }
                            }
                        }
                    }
                }
                // retain only the repos we need
                for (auto const& repo : setup_repos->to_include) {
                    mr_config["repositories"][repo] = mr_repos[repo];
                }
            },
            [&failed, interactive](auto const& msg, bool fatal) {
                Logger::Log(fatal ? LogLevel::Error : LogLevel::Warning,
                            "While performing just-mr {}:\n{}",
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
    // if successful, return the output config
    return StorageUtils::AddToCAS(mr_config.dump(2));
}
