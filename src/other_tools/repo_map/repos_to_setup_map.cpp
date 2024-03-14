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

#include "src/other_tools/repo_map/repos_to_setup_map.hpp"

#include "fmt/core.h"
#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/file_system/symlinks_map/pragma_special.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/other_tools/just_mr/progress_reporting/progress.hpp"
#include "src/other_tools/just_mr/progress_reporting/statistics.hpp"
#include "src/other_tools/ops_maps/content_cas_map.hpp"
#include "src/other_tools/ops_maps/git_tree_fetch_map.hpp"
#include "src/other_tools/utils/parse_archive.hpp"

namespace {

/// \brief Updates output config with specific keys from input config.
void SetReposTakeOver(gsl::not_null<nlohmann::json*> const& cfg,
                      ExpressionPtr const& repos,
                      std::string const& repo_name) {
    if (repos.IsNotNull()) {
        for (auto const& key : kTakeOver) {
            auto repos_repo_name = repos->Get(repo_name, Expression::none_t{});
            if (repos_repo_name.IsNotNull()) {
                auto value = repos_repo_name->Get(key, Expression::none_t{});
                if (value.IsNotNull()) {
                    (*cfg)[key] = value.ToJson();
                }
            }
        }
    }
}

/// \brief Perform checkout for a Git type repository.
/// Guarantees the logger is called exactly once with fatal if a failure occurs.
void GitCheckout(ExpressionPtr const& repo_desc,
                 ExpressionPtr&& repos,
                 std::string const& repo_name,
                 gsl::not_null<CommitGitMap*> const& commit_git_map,
                 gsl::not_null<TaskSystem*> const& ts,
                 ReposToSetupMap::SetterPtr const& setter,
                 ReposToSetupMap::LoggerPtr const& logger) {
    // enforce mandatory fields
    auto repo_desc_commit = repo_desc->At("commit");
    if (not repo_desc_commit) {
        (*logger)("GitCheckout: Mandatory field \"commit\" is missing",
                  /*fatal=*/true);
        return;
    }
    if (not repo_desc_commit->get()->IsString()) {
        (*logger)(fmt::format("GitCheckout: Unsupported value {} for mandatory "
                              "field \"commit\"",
                              repo_desc_commit->get()->ToString()),
                  /*fatal=*/true);
        return;
    }
    auto repo_desc_repository = repo_desc->At("repository");
    if (not repo_desc_repository) {
        (*logger)("GitCheckout: Mandatory field \"repository\" is missing",
                  /*fatal=*/true);
        return;
    }
    if (not repo_desc_repository->get()->IsString()) {
        (*logger)(fmt::format("GitCheckout: Unsupported value {} for mandatory "
                              "field \"repository\"",
                              repo_desc_repository->get()->ToString()),
                  /*fatal=*/true);
        return;
    }
    auto repo_desc_branch = repo_desc->At("branch");
    if (not repo_desc_branch) {
        (*logger)("GitCheckout: Mandatory field \"branch\" is missing",
                  /*fatal=*/true);
        return;
    }
    if (not repo_desc_branch->get()->IsString()) {
        (*logger)(fmt::format("GitCheckout: Unsupported value {} for mandatory "
                              "field \"branch\"",
                              repo_desc_branch->get()->ToString()),
                  /*fatal=*/true);
        return;
    }
    auto repo_desc_subdir = repo_desc->Get("subdir", Expression::none_t{});
    auto subdir = std::filesystem::path((repo_desc_subdir->IsString())
                                            ? repo_desc_subdir->String()
                                            : "")
                      .lexically_normal();
    // check optional mirrors
    auto repo_desc_mirrors = repo_desc->Get("mirrors", Expression::list_t{});
    std::vector<std::string> mirrors{};
    if (repo_desc_mirrors->IsList()) {
        mirrors.reserve(repo_desc_mirrors->List().size());
        for (auto const& elem : repo_desc_mirrors->List()) {
            if (not elem->IsString()) {
                (*logger)(fmt::format("GitCheckout: Unsupported list entry {} "
                                      "in optional field \"mirrors\"",
                                      elem->ToString()),
                          /*fatal=*/true);
                return;
            }
            mirrors.emplace_back(elem->String());
        }
    }
    else {
        (*logger)(fmt::format("GitCheckout: Optional field \"mirrors\" should "
                              "be a list of strings, but found: {}",
                              repo_desc_mirrors->ToString()),
                  /*fatal=*/true);
        return;
    }
    // check "special" pragma
    auto repo_desc_pragma = repo_desc->At("pragma");
    bool const& pragma_is_map =
        repo_desc_pragma and repo_desc_pragma->get()->IsMap();
    auto pragma_special =
        pragma_is_map ? repo_desc_pragma->get()->At("special") : std::nullopt;
    auto pragma_special_value =
        pragma_special and pragma_special->get()->IsString() and
                kPragmaSpecialMap.contains(pragma_special->get()->String())
            ? std::make_optional(
                  kPragmaSpecialMap.at(pragma_special->get()->String()))
            : std::nullopt;
    // check "absent" pragma
    auto pragma_absent =
        pragma_is_map ? repo_desc_pragma->get()->At("absent") : std::nullopt;
    auto pragma_absent_value = pragma_absent and
                               pragma_absent->get()->IsBool() and
                               pragma_absent->get()->Bool();
    std::vector<std::string> inherit_env{};
    auto repo_desc_inherit_env =
        repo_desc->Get("inherit env", Expression::kEmptyList);
    if (not repo_desc_inherit_env->IsList()) {
        (*logger)(fmt::format("GitCheckout: optional field \"inherit env\" "
                              "should be a list of strings, but found {}",
                              repo_desc_inherit_env->ToString()),
                  true);
        return;
    }
    for (auto const& var : repo_desc_inherit_env->List()) {
        if (not var->IsString()) {
            (*logger)(
                fmt::format("GitCheckout: optional field \"inherit env\" "
                            "should be a list of strings, but found entry {}",
                            var->ToString()),
                true);
            return;
        }
        inherit_env.emplace_back(var->String());
    }

    // populate struct
    GitRepoInfo git_repo_info = {
        .hash = repo_desc_commit->get()->String(),
        .repo_url = repo_desc_repository->get()->String(),
        .branch = repo_desc_branch->get()->String(),
        .subdir = subdir.empty() ? "." : subdir.string(),
        .inherit_env = inherit_env,
        .mirrors = std::move(mirrors),
        .origin = repo_name,
        .ignore_special = pragma_special_value == PragmaSpecial::Ignore,
        .absent = pragma_absent_value};
    // get the WS root as git tree
    commit_git_map->ConsumeAfterKeysReady(
        ts,
        {std::move(git_repo_info)},
        [repos = std::move(repos), repo_name, setter](auto const& values) {
            auto ws_root = values[0]->first;
            nlohmann::json cfg({});
            cfg["workspace_root"] = ws_root;
            SetReposTakeOver(&cfg, repos, repo_name);
            if (values[0]->second) {
                JustMRStatistics::Instance().IncrementCacheHitsCounter();
            }
            else {
                JustMRStatistics::Instance().IncrementExecutedCounter();
            }
            (*setter)(std::move(cfg));
        },
        [logger, repo_name](auto const& msg, bool fatal) {
            (*logger)(fmt::format("While setting the workspace root for "
                                  "repository {} of type \"git\":\n{}",
                                  nlohmann::json(repo_name).dump(),
                                  msg),
                      fatal);
        });
}

/// \brief Perform checkout for an archive type repository.
/// Guarantees the logger is called exactly once with fatal if a failure occurs.
void ArchiveCheckout(ExpressionPtr const& repo_desc,
                     ExpressionPtr&& repos,
                     std::string const& repo_name,
                     std::string const& repo_type,
                     gsl::not_null<ContentGitMap*> const& content_git_map,
                     gsl::not_null<TaskSystem*> const& ts,
                     ReposToSetupMap::SetterPtr const& setter,
                     ReposToSetupMap::LoggerPtr const& logger) {
    auto archive_repo_info =
        ParseArchiveDescription(repo_desc, repo_type, repo_name, logger);
    if (not archive_repo_info) {
        return;
    }
    // get the WS root as git tree
    content_git_map->ConsumeAfterKeysReady(
        ts,
        {std::move(*archive_repo_info)},
        [repos = std::move(repos), repo_name, setter](auto const& values) {
            auto ws_root = values[0]->first;
            nlohmann::json cfg({});
            cfg["workspace_root"] = ws_root;
            SetReposTakeOver(&cfg, repos, repo_name);
            if (values[0]->second) {
                JustMRStatistics::Instance().IncrementCacheHitsCounter();
            }
            else {
                JustMRStatistics::Instance().IncrementExecutedCounter();
            }
            (*setter)(std::move(cfg));
        },
        [logger, repo_name, repo_type](auto const& msg, bool fatal) {
            (*logger)(fmt::format("While setting the workspace root for "
                                  "repository {} of type {}:\n{}",
                                  nlohmann::json(repo_name).dump(),
                                  nlohmann::json(repo_type).dump(),
                                  msg),
                      fatal);
        });
}

/// \brief Perform checkout for an archive type repository.
/// Guarantees the logger is called exactly once with fatal if a failure occurs.
void ForeignFileCheckout(
    ExpressionPtr const& repo_desc,
    ExpressionPtr&& repos,
    std::string const& repo_name,
    gsl::not_null<ForeignFileGitMap*> const& foreign_file_git_map,
    gsl::not_null<TaskSystem*> const& ts,
    ReposToSetupMap::SetterPtr const& setter,
    ReposToSetupMap::LoggerPtr const& logger) {
    auto foreign_file_repo_info =
        ParseForeignFileDescription(repo_desc, repo_name, logger);
    if (not foreign_file_repo_info) {
        return;
    }
    // get the WS root as git tree
    foreign_file_git_map->ConsumeAfterKeysReady(
        ts,
        {std::move(*foreign_file_repo_info)},
        [repos = std::move(repos), repo_name, setter](auto const& values) {
            auto ws_root = values[0]->first;
            nlohmann::json cfg({});
            cfg["workspace_root"] = ws_root;
            SetReposTakeOver(&cfg, repos, repo_name);
            if (values[0]->second) {
                JustMRStatistics::Instance().IncrementCacheHitsCounter();
            }
            else {
                JustMRStatistics::Instance().IncrementExecutedCounter();
            }
            (*setter)(std::move(cfg));
        },
        [logger, repo_name](auto const& msg, bool fatal) {
            (*logger)(fmt::format("While setting the workspace root for "
                                  "foreign-file repository {}:\n{}",
                                  nlohmann::json(repo_name).dump(),
                                  msg),
                      fatal);
        });
}

/// \brief Perform checkout for a file type repository.
/// Guarantees the logger is called exactly once with fatal if a failure occurs.
void FileCheckout(ExpressionPtr const& repo_desc,
                  ExpressionPtr&& repos,
                  std::string const& repo_name,
                  gsl::not_null<FilePathGitMap*> const& fpath_git_map,
                  bool fetch_absent,
                  gsl::not_null<TaskSystem*> const& ts,
                  ReposToSetupMap::SetterPtr const& setter,
                  ReposToSetupMap::LoggerPtr const& logger) {
    // enforce mandatory fields
    auto repo_desc_path = repo_desc->At("path");
    if (not repo_desc_path) {
        (*logger)("FileCheckout: Mandatory field \"path\" is missing",
                  /*fatal=*/true);
        return;
    }
    if (not repo_desc_path->get()->IsString()) {
        (*logger)(fmt::format("FileCheckout: Unsupported value {} for "
                              "mandatory field \"path\"",
                              repo_desc_path->get()->ToString()),
                  /*fatal=*/true);
        return;
    }
    // get absolute path
    auto fpath = ToNormalPath(
        std::filesystem::absolute(repo_desc_path->get()->String()));
    // check "special" pragma
    auto repo_desc_pragma = repo_desc->At("pragma");
    bool const& pragma_is_map =
        repo_desc_pragma and repo_desc_pragma->get()->IsMap();
    auto pragma_special =
        pragma_is_map ? repo_desc_pragma->get()->At("special") : std::nullopt;
    auto pragma_special_value =
        pragma_special and pragma_special->get()->IsString() and
                kPragmaSpecialMap.contains(pragma_special->get()->String())
            ? std::make_optional(
                  kPragmaSpecialMap.at(pragma_special->get()->String()))
            : std::nullopt;
    // check "to_git" pragma
    auto pragma_to_git =
        pragma_is_map ? repo_desc_pragma->get()->At("to_git") : std::nullopt;
    // resolving symlinks implies also to_git
    if (pragma_special_value == PragmaSpecial::ResolvePartially or
        pragma_special_value == PragmaSpecial::ResolveCompletely or
        (pragma_to_git and pragma_to_git->get()->IsBool() and
         pragma_to_git->get()->Bool())) {
        // check "absent" pragma
        auto pragma_absent = pragma_is_map
                                 ? repo_desc_pragma->get()->At("absent")
                                 : std::nullopt;
        auto pragma_absent_value = pragma_absent and
                                   pragma_absent->get()->IsBool() and
                                   pragma_absent->get()->Bool();
        // get the WS root as git tree
        FpathInfo fpath_info = {
            .fpath = fpath,
            .pragma_special = pragma_special_value,
            .absent = not fetch_absent and pragma_absent_value};
        fpath_git_map->ConsumeAfterKeysReady(
            ts,
            {std::move(fpath_info)},
            [repos = std::move(repos), repo_name, setter](auto const& values) {
                auto ws_root = *values[0];
                nlohmann::json cfg({});
                cfg["workspace_root"] = ws_root;
                SetReposTakeOver(&cfg, repos, repo_name);
                (*setter)(std::move(cfg));
                // report work done
                JustMRStatistics::Instance().IncrementLocalPathsCounter();
            },
            [logger, repo_name](auto const& msg, bool fatal) {
                (*logger)(fmt::format("While setting the workspace root for "
                                      "repository {} of type \"file\":\n{}",
                                      nlohmann::json(repo_name).dump(),
                                      msg),
                          fatal);
            });
    }
    else {
        // get the WS root as filesystem location
        nlohmann::json cfg({});
        cfg["workspace_root"] =
            nlohmann::json::array({pragma_special_value == PragmaSpecial::Ignore
                                       ? FileRoot::kFileIgnoreSpecialMarker
                                       : "file",
                                   fpath.string()});  // explicit array
        SetReposTakeOver(&cfg, repos, repo_name);
        (*setter)(std::move(cfg));
        // report local path
        JustMRStatistics::Instance().IncrementLocalPathsCounter();
    }
}

/// \brief Perform checkout for a distdir type repository.
/// Guarantees the logger is called exactly once with fatal if a failure occurs.
void DistdirCheckout(ExpressionPtr const& repo_desc,
                     ExpressionPtr&& repos,
                     std::string const& repo_name,
                     gsl::not_null<DistdirGitMap*> const& distdir_git_map,
                     bool fetch_absent,
                     gsl::not_null<TaskSystem*> const& ts,
                     ReposToSetupMap::SetterPtr const& setter,
                     ReposToSetupMap::LoggerPtr const& logger) {
    // enforce mandatory fields
    auto repo_desc_repositories = repo_desc->At("repositories");
    if (not repo_desc_repositories) {
        (*logger)(
            "DistdirCheckout: Mandatory field \"repositories\" is missing",
            /*fatal=*/true);
        return;
    }
    if (not repo_desc_repositories->get()->IsList()) {
        (*logger)(fmt::format("DistdirCheckout: Unsupported value {} for "
                              "mandatory field \"repositories\"",
                              repo_desc_repositories->get()->ToString()),
                  /*fatal=*/true);
        return;
    }
    // check "absent" pragma
    auto repo_desc_pragma = repo_desc->At("pragma");
    auto pragma_absent = (repo_desc_pragma and repo_desc_pragma->get()->IsMap())
                             ? repo_desc_pragma->get()->At("absent")
                             : std::nullopt;
    auto pragma_absent_value = pragma_absent and
                               pragma_absent->get()->IsBool() and
                               pragma_absent->get()->Bool();
    // map of distfile to content
    auto distdir_content_for_id = std::make_shared<
        std::unordered_map<std::string, std::pair<std::string, bool>>>();
    auto distdir_content =
        std::make_shared<std::unordered_map<std::string, std::string>>();
    // get distdir list
    auto distdir_repos = repo_desc_repositories->get()->List();
    // create list of archives to fetch
    auto dist_repos_to_fetch = std::make_shared<std::vector<ArchiveContent>>();
    for (auto const& dist_repo : distdir_repos) {
        if (not dist_repo->IsString()) {
            (*logger)(fmt::format("DistdirCheckout: Unsupported value {} for "
                                  "\"repositories\" list entry",
                                  dist_repo->ToString()),
                      /*fatal=*/true);
            return;
        }
        // get name of dist_repo
        auto dist_repo_name = dist_repo->String();
        // check that repo name exists
        auto repos_dist_repo_name = repos->At(dist_repo_name);
        if (not repos_dist_repo_name) {
            (*logger)(fmt::format("DistdirCheckout: No repository named {}",
                                  nlohmann::json(dist_repo_name).dump()),
                      /*fatal=*/true);
            return;
        }
        auto repo_desc = repos_dist_repo_name->get()->At("repository");
        if (not repo_desc) {
            (*logger)(
                fmt::format("DistdirCheckout: Mandatory key \"repository\" "
                            "missing for repository {}",
                            nlohmann::json(dist_repo_name).dump()),
                /*fatal=*/true);
            return;
        }
        auto resolved_repo_desc =
            JustMR::Utils::ResolveRepo(repo_desc->get(), repos);
        if (not resolved_repo_desc) {
            (*logger)(
                fmt::format("DistdirCheckout: Found cyclic dependency for "
                            "repository {}",
                            nlohmann::json(dist_repo_name).dump()),
                /*fatal=*/true);
            return;
        }
        auto repo_type = (*resolved_repo_desc)->At("type");
        if (not repo_type) {
            (*logger)(
                fmt::format("DistdirCheckout: Mandatory key \"type\" missing "
                            "for repository {}",
                            nlohmann::json(dist_repo_name).dump()),
                /*fatal=*/true);
            return;
        }
        if (not repo_type->get()->IsString()) {
            (*logger)(fmt::format("DistdirCheckout: Unsupported value {} for "
                                  "key \"type\" for repository {}",
                                  repo_type->get()->ToString(),
                                  nlohmann::json(dist_repo_name).dump()),
                      /*fatal=*/true);
            return;
        }
        // get repo_type
        auto repo_type_str = repo_type->get()->String();
        if (not kCheckoutTypeMap.contains(repo_type_str)) {
            (*logger)(fmt::format("DistdirCheckout: Unknown type {} for "
                                  "repository {}",
                                  nlohmann::json(repo_type_str).dump(),
                                  nlohmann::json(dist_repo_name).dump()),
                      /*fatal=*/true);
            return;
        }
        // only do work if repo is archive type
        if (kCheckoutTypeMap.at(repo_type_str) == CheckoutType::Archive) {
            // check mandatory fields
            auto repo_desc_content = (*resolved_repo_desc)->At("content");
            if (not repo_desc_content) {
                (*logger)(fmt::format(
                              "DistdirCheckout: Mandatory field \"content\" is "
                              "missing for repository {}",
                              nlohmann::json(dist_repo_name).dump()),
                          /*fatal=*/true);
                return;
            }
            if (not repo_desc_content->get()->IsString()) {
                (*logger)(fmt::format("DistdirCheckout: Unsupported value {} "
                                      "for mandatory field \"content\" for "
                                      "repository {}",
                                      repo_desc_content->get()->ToString(),
                                      nlohmann::json(dist_repo_name).dump()),
                          /*fatal=*/true);
                return;
            }
            auto repo_desc_fetch = (*resolved_repo_desc)->At("fetch");
            if (not repo_desc_fetch) {
                (*logger)(fmt::format("DistdirCheckout: Mandatory field "
                                      "\"fetch\" is missing for repository {}",
                                      nlohmann::json(dist_repo_name).dump()),
                          /*fatal=*/true);
                return;
            }
            if (not repo_desc_fetch->get()->IsString()) {
                (*logger)(fmt::format(
                              "DistdirCheckout: Unsupported value {} "
                              "for mandatory field \"fetch\" for repository {}",
                              repo_desc_fetch->get()->ToString(),
                              nlohmann::json(dist_repo_name).dump()),
                          /*fatal=*/true);
                return;
            }
            auto repo_desc_distfile =
                (*resolved_repo_desc)->Get("distfile", Expression::none_t{});
            auto repo_desc_sha256 =
                (*resolved_repo_desc)->Get("sha256", Expression::none_t{});
            auto repo_desc_sha512 =
                (*resolved_repo_desc)->Get("sha512", Expression::none_t{});

            // check optional mirrors
            auto repo_desc_mirrors =
                (*resolved_repo_desc)->Get("mirrors", Expression::list_t{});
            std::vector<std::string> mirrors{};
            if (repo_desc_mirrors->IsList()) {
                mirrors.reserve(repo_desc_mirrors->List().size());
                for (auto const& elem : repo_desc_mirrors->List()) {
                    if (not elem->IsString()) {
                        (*logger)(fmt::format(
                                      "DistdirCheckout: Unsupported list entry "
                                      "{} in optional field \"mirrors\" for "
                                      "repository {}",
                                      elem->ToString(),
                                      nlohmann::json(dist_repo_name).dump()),
                                  /*fatal=*/true);
                        return;
                    }
                    mirrors.emplace_back(elem->String());
                }
            }
            else {
                (*logger)(fmt::format("DistdirCheckout: Optional field "
                                      "\"mirrors\" for repository {} should be "
                                      "a list of strings, but found: {}",
                                      nlohmann::json(dist_repo_name).dump(),
                                      repo_desc_mirrors->ToString()),
                          /*fatal=*/true);
                return;
            }

            ArchiveContent archive = {
                .content = repo_desc_content->get()->String(),
                .distfile =
                    repo_desc_distfile->IsString()
                        ? std::make_optional(repo_desc_distfile->String())
                        : std::nullopt,
                .fetch_url = repo_desc_fetch->get()->String(),
                .mirrors = std::move(mirrors),
                .sha256 = repo_desc_sha256->IsString()
                              ? std::make_optional(repo_desc_sha256->String())
                              : std::nullopt,
                .sha512 = repo_desc_sha512->IsString()
                              ? std::make_optional(repo_desc_sha512->String())
                              : std::nullopt,
                .origin = dist_repo_name};

            // add to distdir content map
            auto repo_distfile =
                (archive.distfile ? archive.distfile.value()
                                  : std::filesystem::path(archive.fetch_url)
                                        .filename()
                                        .string());
            distdir_content_for_id->insert_or_assign(
                repo_distfile, std::make_pair(archive.content, false));
            distdir_content->insert_or_assign(repo_distfile, archive.content);
            // add to fetch list
            dist_repos_to_fetch->emplace_back(std::move(archive));
        }
    }
    // get hash of distdir content
    auto distdir_content_id =
        HashFunction::ComputeBlobHash(
            nlohmann::json(*distdir_content_for_id).dump())
            .HexString();
    // get the WS root as git tree
    DistdirInfo distdir_info = {
        .content_id = distdir_content_id,
        .content_list = distdir_content,
        .repos_to_fetch = dist_repos_to_fetch,
        .origin = repo_name,
        .absent = not fetch_absent and pragma_absent_value};
    distdir_git_map->ConsumeAfterKeysReady(
        ts,
        {std::move(distdir_info)},
        [repos = std::move(repos), repo_name, setter](auto const& values) {
            auto ws_root = values[0]->first;
            nlohmann::json cfg({});
            cfg["workspace_root"] = ws_root;
            SetReposTakeOver(&cfg, repos, repo_name);
            if (values[0]->second) {
                JustMRStatistics::Instance().IncrementCacheHitsCounter();
            }
            else {
                JustMRStatistics::Instance().IncrementExecutedCounter();
            }
            (*setter)(std::move(cfg));
        },
        [logger, repo_name](auto const& msg, bool fatal) {
            (*logger)(fmt::format("While setting the workspace root for "
                                  "repository {} of type \"distdir\":\n{}",
                                  nlohmann::json(repo_name).dump(),
                                  msg),
                      fatal);
        });
}

/// \brief Perform checkout for a git tree type repository.
/// Guarantees the logger is called exactly once with fatal if a failure occurs.
void GitTreeCheckout(ExpressionPtr const& repo_desc,
                     ExpressionPtr&& repos,
                     std::string const& repo_name,
                     gsl::not_null<TreeIdGitMap*> const& tree_id_git_map,
                     bool fetch_absent,
                     gsl::not_null<TaskSystem*> const& ts,
                     ReposToSetupMap::SetterPtr const& setter,
                     ReposToSetupMap::LoggerPtr const& logger) {
    // enforce mandatory fields
    auto repo_desc_hash = repo_desc->At("id");
    if (not repo_desc_hash) {
        (*logger)("GitTreeCheckout: Mandatory field \"id\" is missing",
                  /*fatal=*/true);
        return;
    }
    if (not repo_desc_hash->get()->IsString()) {
        (*logger)(fmt::format("GitTreeCheckout: Unsupported value {} for "
                              "mandatory field \"id\"",
                              repo_desc_hash->get()->ToString()),
                  /*fatal=*/true);
        return;
    }
    auto repo_desc_cmd = repo_desc->At("cmd");
    if (not repo_desc_cmd) {
        (*logger)("GitTreeCheckout: Mandatory field \"cmd\" is missing",
                  /*fatal=*/true);
        return;
    }
    if (not repo_desc_cmd->get()->IsList()) {
        (*logger)(fmt::format("GitTreeCheckout: Unsupported value {} for "
                              "mandatory field \"cmd\"",
                              repo_desc_cmd->get()->ToString()),
                  /*fatal=*/true);
        return;
    }
    std::vector<std::string> cmd{};
    for (auto const& token : repo_desc_cmd->get()->List()) {
        if (token.IsNotNull() and token->IsString()) {
            cmd.emplace_back(token->String());
        }
        else {
            (*logger)(fmt::format("GitTreeCheckout: Unsupported entry {} in "
                                  "mandatory field \"cmd\"",
                                  token->ToString()),
                      /*fatal=*/true);
            return;
        }
    }
    std::map<std::string, std::string> env{};
    auto repo_desc_env = repo_desc->Get("env", Expression::none_t{});
    if (repo_desc_env.IsNotNull() and repo_desc_env->IsMap()) {
        for (auto const& envar : repo_desc_env->Map().Items()) {
            if (envar.second.IsNotNull() and envar.second->IsString()) {
                env.insert({envar.first, envar.second->String()});
            }
            else {
                (*logger)(fmt::format("GitTreeCheckout: Unsupported value {} "
                                      "for key {} in optional field \"envs\"",
                                      envar.second->ToString(),
                                      nlohmann::json(envar.first).dump()),
                          /*fatal=*/true);
                return;
            }
        }
    }
    std::vector<std::string> inherit_env{};
    auto repo_desc_inherit_env =
        repo_desc->Get("inherit env", Expression::none_t{});
    if (repo_desc_inherit_env.IsNotNull() and repo_desc_inherit_env->IsList()) {
        for (auto const& envvar : repo_desc_inherit_env->List()) {
            if (envvar->IsString()) {
                inherit_env.emplace_back(envvar->String());
            }
            else {
                (*logger)(
                    fmt::format("GitTreeCheckout: Not a variable name in the "
                                "specification of \"inherit env\": {}",
                                envvar->ToString()),
                    /*fatal=*/true);
                return;
            }
        }
    }
    // check "special" pragma
    auto repo_desc_pragma = repo_desc->At("pragma");
    bool const& pragma_is_map =
        repo_desc_pragma and repo_desc_pragma->get()->IsMap();
    auto pragma_special =
        pragma_is_map ? repo_desc_pragma->get()->At("special") : std::nullopt;
    auto pragma_special_value =
        pragma_special and pragma_special->get()->IsString() and
                kPragmaSpecialMap.contains(pragma_special->get()->String())
            ? std::make_optional(
                  kPragmaSpecialMap.at(pragma_special->get()->String()))
            : std::nullopt;
    // check "absent" pragma
    auto pragma_absent =
        pragma_is_map ? repo_desc_pragma->get()->At("absent") : std::nullopt;
    auto pragma_absent_value = pragma_absent and
                               pragma_absent->get()->IsBool() and
                               pragma_absent->get()->Bool();
    // populate struct
    TreeIdInfo tree_id_info = {
        .tree_info = GitTreeInfo{.hash = repo_desc_hash->get()->String(),
                                 .env_vars = std::move(env),
                                 .inherit_env = std::move(inherit_env),
                                 .command = std::move(cmd)},
        .ignore_special = pragma_special_value == PragmaSpecial::Ignore,
        .absent = not fetch_absent and pragma_absent_value};
    // get the WS root as git tree
    tree_id_git_map->ConsumeAfterKeysReady(
        ts,
        {std::move(tree_id_info)},
        [repos = std::move(repos), repo_name, setter](auto const& values) {
            auto ws_root = values[0]->first;
            nlohmann::json cfg({});
            cfg["workspace_root"] = ws_root;
            SetReposTakeOver(&cfg, repos, repo_name);
            if (values[0]->second) {
                JustMRStatistics::Instance().IncrementCacheHitsCounter();
            }
            else {
                JustMRStatistics::Instance().IncrementExecutedCounter();
            }
            (*setter)(std::move(cfg));
        },
        [logger, repo_name](auto const& msg, bool fatal) {
            (*logger)(fmt::format("While setting the workspace root for "
                                  "repository {} of type \"git tree\":\n{}",
                                  nlohmann::json(repo_name).dump(),
                                  msg),
                      fatal);
        });
}

}  // namespace

auto CreateReposToSetupMap(
    std::shared_ptr<Configuration> const& config,
    std::optional<std::string> const& main,
    bool interactive,
    gsl::not_null<CommitGitMap*> const& commit_git_map,
    gsl::not_null<ContentGitMap*> const& content_git_map,
    gsl::not_null<ForeignFileGitMap*> const& foreign_file_git_map,
    gsl::not_null<FilePathGitMap*> const& fpath_git_map,
    gsl::not_null<DistdirGitMap*> const& distdir_git_map,
    gsl::not_null<TreeIdGitMap*> const& tree_id_git_map,
    bool fetch_absent,
    std::size_t jobs) -> ReposToSetupMap {
    auto setup_repo = [config,
                       main,
                       interactive,
                       commit_git_map,
                       content_git_map,
                       foreign_file_git_map,
                       fpath_git_map,
                       distdir_git_map,
                       tree_id_git_map,
                       fetch_absent](auto ts,
                                     auto setter,
                                     auto logger,
                                     auto /* unused */,
                                     auto const& key) {
        auto repos = (*config)["repositories"];
        if (main && (key == *main) && interactive) {
            // no repository checkout required
            nlohmann::json cfg({});
            SetReposTakeOver(&cfg, repos, key);
            JustMRStatistics::Instance().IncrementLocalPathsCounter();
            (*setter)(std::move(cfg));
        }
        else {
            // repository requires checkout
            auto repo_desc_key = repos->At(key);
            if (not repo_desc_key) {
                (*logger)(fmt::format(
                              "Config: Missing config entry for repository {}",
                              nlohmann::json(key).dump()),
                          /*fatal=*/true);
                return;
            }
            if (not repo_desc_key->get()->IsMap()) {
                (*logger)(fmt::format("Config: Config entry for repository {} "
                                      "is not a map",
                                      nlohmann::json(key).dump()),
                          /*fatal=*/true);
                return;
            }
            auto repo_desc = repo_desc_key->get()->At("repository");
            if (not repo_desc) {
                (*logger)(fmt::format("Config: Mandatory key \"repository\" "
                                      "missing for repository {}",
                                      nlohmann::json(key).dump()),
                          /*fatal=*/true);
                return;
            }
            auto resolved_repo_desc =
                JustMR::Utils::ResolveRepo(repo_desc->get(), repos);
            if (not resolved_repo_desc) {
                (*logger)(fmt::format("Config: Found cyclic dependency for "
                                      "repository {}",
                                      nlohmann::json(key).dump()),
                          /*fatal=*/true);
                return;
            }
            if (not resolved_repo_desc.value()->IsMap()) {
                Logger::Log(
                    LogLevel::Error,
                    "Config: Repository {} resolves to a non-map description",
                    nlohmann::json(key).dump());
                return;
            }
            auto repo_type = (*resolved_repo_desc)->At("type");
            if (not repo_type) {
                (*logger)(fmt::format("Config: Mandatory key \"type\" missing "
                                      "for repository {}",
                                      nlohmann::json(key).dump()),
                          /*fatal=*/true);
                return;
            }
            if (not repo_type->get()->IsString()) {
                (*logger)(fmt::format("Config: Unsupported value {} for key "
                                      "\"type\" for repository {}",
                                      repo_type->get()->ToString(),
                                      nlohmann::json(key).dump()),
                          /*fatal=*/true);
                return;
            }
            // get repo_type
            auto repo_type_str = repo_type->get()->String();
            if (not kCheckoutTypeMap.contains(repo_type_str)) {
                (*logger)(
                    fmt::format("Config: Unknown type {} for repository {}",
                                nlohmann::json(repo_type_str).dump(),
                                nlohmann::json(key).dump()),
                    /*fatal=*/true);
                return;
            }
            // setup a wrapped_logger
            auto wrapped_logger = std::make_shared<AsyncMapConsumerLogger>(
                [logger, repo_name = key](auto const& msg, bool fatal) {
                    (*logger)(fmt::format("While setting up repository {}:\n{}",
                                          nlohmann::json(repo_name).dump(),
                                          msg),
                              fatal);
                });
            // do checkout
            switch (kCheckoutTypeMap.at(repo_type_str)) {
                case CheckoutType::Git: {
                    GitCheckout(*resolved_repo_desc,
                                std::move(repos),
                                key,
                                commit_git_map,
                                ts,
                                setter,
                                wrapped_logger);
                    break;
                }
                case CheckoutType::Archive: {
                    ArchiveCheckout(*resolved_repo_desc,
                                    std::move(repos),
                                    key,
                                    repo_type_str,
                                    content_git_map,
                                    ts,
                                    setter,
                                    wrapped_logger);
                    break;
                }
                case CheckoutType::ForeignFile: {
                    ForeignFileCheckout(*resolved_repo_desc,
                                        std::move(repos),
                                        key,
                                        foreign_file_git_map,
                                        ts,
                                        setter,
                                        wrapped_logger);
                    break;
                }
                case CheckoutType::File: {
                    FileCheckout(*resolved_repo_desc,
                                 std::move(repos),
                                 key,
                                 fpath_git_map,
                                 fetch_absent,
                                 ts,
                                 setter,
                                 wrapped_logger);
                    break;
                }
                case CheckoutType::Distdir: {
                    DistdirCheckout(*resolved_repo_desc,
                                    std::move(repos),
                                    key,
                                    distdir_git_map,
                                    fetch_absent,
                                    ts,
                                    setter,
                                    wrapped_logger);
                    break;
                }
                case CheckoutType::GitTree: {
                    GitTreeCheckout(*resolved_repo_desc,
                                    std::move(repos),
                                    key,
                                    tree_id_git_map,
                                    fetch_absent,
                                    ts,
                                    setter,
                                    wrapped_logger);
                    break;
                }
            }
        }
    };
    return AsyncMapConsumer<std::string, nlohmann::json>(setup_repo, jobs);
}
