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

#include "src/other_tools/just_mr/update.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <thread>

#include "fmt/core.h"
#include "nlohmann/json.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/other_tools/git_operations/git_repo_remote.hpp"
#include "src/other_tools/just_mr/exit_codes.hpp"
#include "src/other_tools/just_mr/progress_reporting/progress.hpp"
#include "src/other_tools/just_mr/progress_reporting/progress_reporter.hpp"
#include "src/other_tools/just_mr/utils.hpp"
#include "src/other_tools/ops_maps/git_update_map.hpp"

auto MultiRepoUpdate(std::shared_ptr<Configuration> const& config,
                     MultiRepoCommonArguments const& common_args,
                     MultiRepoUpdateArguments const& update_args,
                     std::string multi_repo_tool_name) -> int {
    // provide report
    Logger::Log(LogLevel::Info, "Performing repositories update");

    // Check trivial case
    if (update_args.repos_to_update.empty()) {
        // report success
        Logger::Log(LogLevel::Info, "No update needed");
        // print config file
        std::cout << config->ToJson().dump(2) << std::endl;
        return kExitSuccess;
    }
    auto repos = (*config)["repositories"];
    if (not repos.IsNotNull()) {
        Logger::Log(LogLevel::Error,
                    "Config: Mandatory key \"repositories\" missing");
        return kExitUpdateError;
    }
    if (not repos->IsMap()) {
        Logger::Log(LogLevel::Error,
                    "Config: Value for key \"repositories\" is not a map");
        return kExitUpdateError;
    }
    // gather repos to update
    std::vector<RepoDescriptionForUpdating> repos_to_update{};
    repos_to_update.reserve(update_args.repos_to_update.size());
    for (auto const& repo_name : update_args.repos_to_update) {
        auto repo_desc_parent = repos->At(repo_name);
        if (not repo_desc_parent) {
            Logger::Log(LogLevel::Error,
                        "Config: Missing config entry for repository {}",
                        nlohmann::json(repo_name).dump());
            return kExitUpdateError;
        }
        auto repo_desc = repo_desc_parent->get()->At("repository");
        if (repo_desc) {
            auto resolved_repo_desc =
                JustMR::Utils::ResolveRepo(repo_desc->get(), repos);
            if (not resolved_repo_desc) {
                Logger::Log(LogLevel::Error,
                            fmt::format("Config: Found cyclic dependency for "
                                        "repository {}",
                                        nlohmann::json(repo_name).dump()));
                return kExitUpdateError;
            }
            if (not resolved_repo_desc.value()->IsMap()) {
                Logger::Log(
                    LogLevel::Error,
                    "Config: Repository {} resolves to a non-map description",
                    nlohmann::json(repo_name).dump());
                return kExitUpdateError;
            }
            // get repo_type
            auto repo_type = (*resolved_repo_desc)->At("type");
            if (not repo_type) {
                Logger::Log(
                    LogLevel::Error,
                    "Config: Mandatory key \"type\" missing for repository {}",
                    nlohmann::json(repo_name).dump());
                return kExitUpdateError;
            }
            if (not repo_type->get()->IsString()) {
                Logger::Log(LogLevel::Error,
                            "Config: Unsupported value {} for key \"type\" for "
                            "repository {}",
                            repo_type->get()->ToString(),
                            nlohmann::json(repo_name).dump());
                return kExitUpdateError;
            }
            auto repo_type_str = repo_type->get()->String();
            if (not kCheckoutTypeMap.contains(repo_type_str)) {
                Logger::Log(LogLevel::Error,
                            "Unknown repository type {} for {}",
                            nlohmann::json(repo_type_str).dump(),
                            nlohmann::json(repo_name).dump());
                return kExitUpdateError;
            }
            // only do work if repo is git type
            if (kCheckoutTypeMap.at(repo_type_str) == CheckoutType::Git) {
                auto repo_desc_repository =
                    (*resolved_repo_desc)->At("repository");
                if (not repo_desc_repository) {
                    Logger::Log(
                        LogLevel::Error,
                        "Config: Mandatory field \"repository\" is missing");
                    return kExitUpdateError;
                }
                if (not repo_desc_repository->get()->IsString()) {
                    Logger::Log(LogLevel::Error,
                                "Config: Unsupported value {} for key "
                                "\"repository\" for repository {}",
                                repo_desc_repository->get()->ToString(),
                                nlohmann::json(repo_name).dump());
                    return kExitUpdateError;
                }
                auto repo_desc_branch = (*resolved_repo_desc)->At("branch");
                if (not repo_desc_branch) {
                    Logger::Log(
                        LogLevel::Error,
                        "Config: Mandatory field \"branch\" is missing");
                    return kExitUpdateError;
                }
                if (not repo_desc_branch->get()->IsString()) {
                    Logger::Log(LogLevel::Error,
                                "Config: Unsupported value {} for key "
                                "\"branch\" for repository {}",
                                repo_desc_branch->get()->ToString(),
                                nlohmann::json(repo_name).dump());
                    return kExitUpdateError;
                }
                std::vector<std::string> inherit_env{};
                auto repo_desc_inherit_env =
                    (*resolved_repo_desc)
                        ->Get("inherit env", Expression::kEmptyList);
                if (not repo_desc_inherit_env->IsList()) {
                    Logger::Log(LogLevel::Error,
                                "GitCheckout: optional field \"inherit env\" "
                                "should be a list of strings, but found {}",
                                repo_desc_inherit_env->ToString());
                    return kExitUpdateError;
                }
                for (auto const& var : repo_desc_inherit_env->List()) {
                    if (not var->IsString()) {
                        Logger::Log(
                            LogLevel::Error,
                            "GitCheckout: optional field \"inherit env\" "
                            "should be a list of strings, but found entry {}",
                            var->ToString());
                        return kExitUpdateError;
                    }
                    inherit_env.emplace_back(var->String());
                }

                repos_to_update.emplace_back(RepoDescriptionForUpdating{
                    .repo = repo_desc_repository->get()->String(),
                    .branch = repo_desc_branch->get()->String(),
                    .inherit_env = inherit_env});
            }
            else {
                Logger::Log(LogLevel::Error,
                            "Config: Argument {} is not the name of a \"git\" "
                            "type repository",
                            nlohmann::json(repo_name).dump());
                return kExitUpdateError;
            }
        }
        else {
            Logger::Log(LogLevel::Error,
                        "Config: Missing repository description for {}",
                        nlohmann::json(repo_name).dump());
            return kExitUpdateError;
        }
    }
    // Create fake repo for the anonymous remotes
    auto tmp_dir = StorageConfig::CreateTypedTmpDir("update");
    if (not tmp_dir) {
        Logger::Log(LogLevel::Error, "Failed to create commit update tmp dir");
        return kExitUpdateError;
    }
    // Init and open git repo
    auto git_repo =
        GitRepoRemote::InitAndOpen(tmp_dir->GetPath(), /*is_bare=*/true);
    if (not git_repo) {
        Logger::Log(LogLevel::Error,
                    "Failed to initialize repository in tmp dir {} for git "
                    "commit update",
                    tmp_dir->GetPath().string());
        return kExitUpdateError;
    }

    // report progress
    auto nr = repos_to_update.size();
    Logger::Log(LogLevel::Info,
                "Discovered {} Git {} to update",
                nr,
                nr == 1 ? "repository" : "repositories");

    // Initialize resulting config to be updated
    auto mr_config = config->ToJson();
    // Create async map
    auto git_update_map = CreateGitUpdateMap(git_repo->GetGitCAS(),
                                             common_args.git_path->string(),
                                             *common_args.local_launcher,
                                             common_args.jobs);

    // set up progress observer
    JustMRProgress::Instance().SetTotal(repos_to_update.size());
    std::atomic<bool> done{false};
    std::condition_variable cv{};
    auto reporter = JustMRProgressReporter::Reporter();
    auto observer =
        std::thread([reporter, &done, &cv]() { reporter(&done, &cv); });

    // do the update
    bool failed{false};
    {
        TaskSystem ts{common_args.jobs};
        git_update_map.ConsumeAfterKeysReady(
            &ts,
            repos_to_update,
            [&failed,
             &mr_config,
             repos_to_update_names = update_args.repos_to_update,
             multi_repo_tool_name](auto const& values) noexcept {
                try {
                    for (auto const& repo_name : repos_to_update_names) {
                        auto i = static_cast<std::size_t>(
                            &repo_name -
                            &repos_to_update_names[0]);  // get index
                        // we know "repository" is a map for repo_name, so
                        // field "commit" is here either overwritten or set if
                        // missing; either way, this should always work
                        mr_config["repositories"][repo_name]["repository"]
                                 ["commit"] = *values[i];
                    }
                } catch (std::exception const& ex) {
                    Logger::Log(
                        LogLevel::Error,
                        "While performing {} update:\nUpdating configuration "
                        "fields failed with:\n{}",
                        multi_repo_tool_name,
                        ex.what());
                    failed = true;
                }
            },
            [&failed, &multi_repo_tool_name](auto const& msg, bool fatal) {
                Logger::Log(fatal ? LogLevel::Error : LogLevel::Warning,
                            "While performing {} update:\n{}",
                            multi_repo_tool_name,
                            msg);
                failed = failed or fatal;
            });
    }

    // close progress observer
    done = true;
    cv.notify_all();
    observer.join();

    if (failed) {
        return kExitUpdateError;
    }
    // report success
    Logger::Log(LogLevel::Info, "Update completed");
    // print mr_config to stdout
    std::cout << mr_config.dump(2) << std::endl;
    return kExitSuccess;
}
