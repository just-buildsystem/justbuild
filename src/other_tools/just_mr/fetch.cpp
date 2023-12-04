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

#include <filesystem>

#include "nlohmann/json.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/execution_api/local/local_api.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/other_tools/just_mr/exit_codes.hpp"
#include "src/other_tools/just_mr/progress_reporting/progress.hpp"
#include "src/other_tools/just_mr/progress_reporting/progress_reporter.hpp"
#include "src/other_tools/just_mr/setup_utils.hpp"
#include "src/other_tools/ops_maps/archive_fetch_map.hpp"
#include "src/other_tools/ops_maps/content_cas_map.hpp"

auto MultiRepoFetch(std::shared_ptr<Configuration> const& config,
                    MultiRepoCommonArguments const& common_args,
                    MultiRepoSetupArguments const& setup_args,
                    MultiRepoFetchArguments const& fetch_args,
                    MultiRepoRemoteAuthArguments const& auth_args) -> int {
    // provide report
    Logger::Log(LogLevel::Info, "Performing repositories fetch");

    // find fetch dir
    auto fetch_dir = fetch_args.fetch_dir;
    if (not fetch_dir) {
        for (auto const& d : common_args.just_mr_paths->distdirs) {
            if (FileSystemManager::IsDirectory(d)) {
                fetch_dir = std::filesystem::weakly_canonical(
                    std::filesystem::absolute(d));
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
                        std::filesystem::absolute(
                            common_args.just_mr_paths->setup_root /
                            repo_path_as_path));
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
    std::vector<ArchiveRepoInfo> archives_to_fetch{};
    archives_to_fetch.reserve(
        fetch_repos->to_include.size());  // pre-reserve a maximum size
    for (auto const& repo_name : fetch_repos->to_include) {
        auto repo_desc = repos->At(repo_name);
        if (not repo_desc) {
            Logger::Log(LogLevel::Error,
                        "Config: Missing config entry for repository {}",
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
            if (not kCheckoutTypeMap.contains(repo_type_str)) {
                Logger::Log(LogLevel::Error,
                            "Unknown repository type {} for {}",
                            nlohmann::json(repo_type_str).dump(),
                            nlohmann::json(repo_name).dump());
                return kExitFetchError;
            }
            // only do work if repo is archive type
            if (kCheckoutTypeMap.at(repo_type_str) == CheckoutType::Archive) {
                // check "absent" pragma
                auto repo_desc_pragma = (*resolved_repo_desc)->At("pragma");
                auto pragma_absent = repo_desc_pragma
                                         ? repo_desc_pragma->get()->At("absent")
                                         : std::nullopt;
                auto pragma_absent_value = pragma_absent and
                                           pragma_absent->get()->IsBool() and
                                           pragma_absent->get()->Bool();
                // only fetch if either archive is not marked absent, or if
                // explicitly told to fetch absent archives
                if (not pragma_absent_value or common_args.fetch_absent) {
                    // check mandatory fields
                    auto repo_desc_content =
                        (*resolved_repo_desc)->At("content");
                    if (not repo_desc_content) {
                        Logger::Log(LogLevel::Error,
                                    "Mandatory field \"content\" is missing");
                        return kExitFetchError;
                    }
                    if (not repo_desc_content->get()->IsString()) {
                        Logger::Log(LogLevel::Error,
                                    "Unsupported value {} for mandatory field "
                                    "\"content\"",
                                    repo_desc_content->get()->ToString());
                        return kExitFetchError;
                    }
                    auto repo_desc_fetch = (*resolved_repo_desc)->At("fetch");
                    if (not repo_desc_fetch) {
                        Logger::Log(LogLevel::Error,
                                    "Mandatory field \"fetch\" is missing");
                        return kExitFetchError;
                    }
                    if (not repo_desc_fetch->get()->IsString()) {
                        Logger::Log(LogLevel::Error,
                                    "ArchiveCheckout: Unsupported value {} for "
                                    "mandatory field \"fetch\"",
                                    repo_desc_fetch->get()->ToString());
                        return kExitFetchError;
                    }
                    auto repo_desc_subdir =
                        (*resolved_repo_desc)
                            ->Get("subdir", Expression::none_t{});
                    auto subdir =
                        std::filesystem::path(repo_desc_subdir->IsString()
                                                  ? repo_desc_subdir->String()
                                                  : "")
                            .lexically_normal();
                    auto repo_desc_distfile =
                        (*resolved_repo_desc)
                            ->Get("distfile", Expression::none_t{});
                    auto repo_desc_sha256 =
                        (*resolved_repo_desc)
                            ->Get("sha256", Expression::none_t{});
                    auto repo_desc_sha512 =
                        (*resolved_repo_desc)
                            ->Get("sha512", Expression::none_t{});

                    ArchiveRepoInfo archive_info = {
                        .archive =
                            {.content = repo_desc_content->get()->String(),
                             .distfile = repo_desc_distfile->IsString()
                                             ? std::make_optional(
                                                   repo_desc_distfile->String())
                                             : std::nullopt,
                             .fetch_url = repo_desc_fetch->get()->String(),
                             .sha256 = repo_desc_sha256->IsString()
                                           ? std::make_optional(
                                                 repo_desc_sha256->String())
                                           : std::nullopt,
                             .sha512 = repo_desc_sha512->IsString()
                                           ? std::make_optional(
                                                 repo_desc_sha512->String())
                                           : std::nullopt,
                             .origin = repo_name,
                             .fetch_only = true},
                        .repo_type = repo_type_str,
                        .subdir = subdir.empty() ? "." : subdir.string(),
                        .pragma_special = std::nullopt,  // not used
                        .absent = false                  // not used
                    };
                    // add to list
                    archives_to_fetch.emplace_back(std::move(archive_info));
                }
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
    auto nr = archives_to_fetch.size();
    Logger::Log(LogLevel::Info,
                "Found {} {} to fetch",
                nr,
                nr == 1 ? "archive" : "archives");

    // setup the APIs for archive fetches
    auto remote_api = JustMR::Utils::GetRemoteApi(
        common_args.remote_execution_address, auth_args);
    IExecutionApi::Ptr local_api{remote_api ? std::make_unique<LocalApi>()
                                            : nullptr};

    // setup the API for serving trees of Git repos or archives
    auto serve_api_exists = JustMR::Utils::SetupServeApi(
        common_args.remote_serve_address, auth_args);

    // create async maps
    auto content_cas_map =
        CreateContentCASMap(common_args.just_mr_paths,
                            common_args.alternative_mirrors,
                            common_args.ca_info,
                            serve_api_exists,
                            local_api ? &(*local_api) : nullptr,
                            remote_api ? &(*remote_api) : nullptr,
                            common_args.jobs);
    auto archive_fetch_map = CreateArchiveFetchMap(
        &content_cas_map,
        *fetch_dir,
        (fetch_args.backup_to_remote and local_api) ? &(*local_api) : nullptr,
        (fetch_args.backup_to_remote and remote_api) ? &(*remote_api) : nullptr,
        common_args.jobs);

    // set up progress observer
    JustMRProgress::Instance().SetTotal(archives_to_fetch.size());
    std::atomic<bool> done{false};
    std::condition_variable cv{};
    auto reporter = JustMRProgressReporter::Reporter();
    auto observer =
        std::thread([reporter, &done, &cv]() { reporter(&done, &cv); });

    // do the fetch
    bool failed{false};
    {
        TaskSystem ts{common_args.jobs};
        archive_fetch_map.ConsumeAfterKeysReady(
            &ts,
            archives_to_fetch,
            []([[maybe_unused]] auto const& values) {},
            [&failed](auto const& msg, bool fatal) {
                Logger::Log(fatal ? LogLevel::Error : LogLevel::Warning,
                            "While performing just-mr fetch:\n{}",
                            msg);
                failed = failed or fatal;
            });
    }

    // close progress observer
    done = true;
    cv.notify_all();
    observer.join();

    if (failed) {
        return kExitFetchError;
    }
    // report success
    Logger::Log(LogLevel::Info, "Fetch completed");
    return kExitSuccess;
}
