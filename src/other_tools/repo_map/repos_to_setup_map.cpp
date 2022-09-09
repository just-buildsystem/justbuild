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

#include "src/other_tools/just_mr/utils.hpp"

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
        (*logger)("GitCheckout: Mandatory field \'commit\' is missing",
                  /*fatal=*/true);
        return;
    }
    if (not repo_desc_commit->get()->IsString()) {
        (*logger)(
            "GitCheckout: Unsupported value for mandatory field \'commit\'",
            /*fatal=*/true);
        return;
    }
    auto repo_desc_repository = repo_desc->At("repository");
    if (not repo_desc_repository) {
        (*logger)("GitCheckout: Mandatory field \'repository\' is missing",
                  /*fatal=*/true);
        return;
    }
    if (not repo_desc_repository->get()->IsString()) {
        (*logger)(
            "GitCheckout: Unsupported value for mandatory field "
            "\'repository\'",
            /*fatal=*/true);
        return;
    }
    auto repo_desc_branch = repo_desc->At("branch");
    if (not repo_desc_branch) {
        (*logger)("GitCheckout: Mandatory field \'branch\' is missing",
                  /*fatal=*/true);
        return;
    }
    if (not repo_desc_branch->get()->IsString()) {
        (*logger)(
            "GitCheckout: Unsupported value for mandatory field \'branch\'",
            /*fatal=*/true);
        return;
    }
    auto repo_desc_subdir = repo_desc->Get("subdir", Expression::none_t{});
    auto subdir = std::filesystem::path((repo_desc_subdir->IsString())
                                            ? repo_desc_subdir->String()
                                            : "")
                      .lexically_normal();
    // populate struct
    GitRepoInfo git_repo_info = {
        repo_desc_commit->get()->String(),     /* hash */
        repo_desc_repository->get()->String(), /* repo_url */
        repo_desc_branch->get()->String(),     /* branch */
        subdir.empty() ? "." : subdir.string() /* subdir */
    };
    // get the WS root as git tree
    commit_git_map->ConsumeAfterKeysReady(
        ts,
        {std::move(git_repo_info)},
        [repos = std::move(repos), repo_name, setter, logger](
            auto const& values) {
            auto ws_root = *values[0];
            nlohmann::json cfg({});
            cfg["workspace_root"] = ws_root;
            SetReposTakeOver(&cfg, repos, repo_name);
            (*setter)(std::move(cfg));
        },
        [logger, repo_name](auto const& msg, bool fatal) {
            (*logger)(fmt::format("While setting the workspace root for "
                                  "repository {} of type git:\n{}",
                                  repo_name,
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
    // enforce mandatory fields
    auto repo_desc_content = repo_desc->At("content");
    if (not repo_desc_content) {
        (*logger)("ArchiveCheckout: Mandatory field \'content\' is missing",
                  /*fatal=*/true);
        return;
    }
    if (not repo_desc_content->get()->IsString()) {
        (*logger)(
            "ArchiveCheckout: Unsupported value for mandatory field "
            "\'content\'",
            /*fatal=*/true);
        return;
    }
    auto repo_desc_fetch = repo_desc->At("fetch");
    if (not repo_desc_fetch) {
        (*logger)("ArchiveCheckout: Mandatory field \'fetch\' is missing",
                  /*fatal=*/true);
        return;
    }
    if (not repo_desc_fetch->get()->IsString()) {
        (*logger)(
            "ArchiveCheckout: Unsupported value for mandatory field "
            "\'fetch\'",
            /*fatal=*/true);
        return;
    }
    auto repo_desc_subdir = repo_desc->Get("subdir", Expression::none_t{});
    auto subdir = std::filesystem::path(repo_desc_subdir->IsString()
                                            ? repo_desc_subdir->String()
                                            : "")
                      .lexically_normal();
    auto repo_desc_distfile = repo_desc->Get("distfile", Expression::none_t{});
    auto repo_desc_sha256 = repo_desc->Get("sha256", Expression::none_t{});
    auto repo_desc_sha512 = repo_desc->Get("sha512", Expression::none_t{});
    // populate struct
    ArchiveRepoInfo archive_repo_info = {
        {
            repo_desc_content->get()->String(), /* content */
            repo_desc_distfile->IsString()
                ? std::make_optional(repo_desc_distfile->String())
                : std::nullopt,               /* distfile */
            repo_desc_fetch->get()->String(), /* fetch_url */
            repo_desc_sha256->IsString()
                ? std::make_optional(repo_desc_sha256->String())
                : std::nullopt, /* sha256 */
            repo_desc_sha512->IsString()
                ? std::make_optional(repo_desc_sha512->String())
                : std::nullopt                 /* sha512 */
        },                                     /* archive */
        repo_type,                             /* repo_type */
        subdir.empty() ? "." : subdir.string() /* subdir */
    };
    // get the WS root as git tree
    content_git_map->ConsumeAfterKeysReady(
        ts,
        {std::move(archive_repo_info)},
        [repos = std::move(repos), repo_name, setter, logger](
            auto const& values) {
            auto ws_root = *values[0];
            nlohmann::json cfg({});
            cfg["workspace_root"] = ws_root;
            SetReposTakeOver(&cfg, repos, repo_name);
            (*setter)(std::move(cfg));
        },
        [logger, repo_name, repo_type](auto const& msg, bool fatal) {
            (*logger)(fmt::format("While setting the workspace root for "
                                  "repository {} of type {}:\n{}",
                                  repo_name,
                                  repo_type,
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
                  gsl::not_null<TaskSystem*> const& ts,
                  ReposToSetupMap::SetterPtr const& setter,
                  ReposToSetupMap::LoggerPtr const& logger) {
    // enforce mandatory fields
    auto repo_desc_path = repo_desc->At("path");
    if (not repo_desc_path) {
        (*logger)("FileCheckout: Mandatory field \'path\' is missing",
                  /*fatal=*/true);
        return;
    }
    if (not repo_desc_path->get()->IsString()) {
        (*logger)(
            "FileCheckout: Unsupported value for mandatory field \'path\'",
            /*fatal=*/true);
        return;
    }
    // get absolute path
    auto fpath = ToNormalPath(
        std::filesystem::absolute(repo_desc_path->get()->String()));
    // check to_git pragma
    auto repo_desc_pragma = repo_desc->At("pragma");
    auto pragma_to_git =
        repo_desc_pragma ? repo_desc_pragma->get()->At("to_git") : std::nullopt;
    if (pragma_to_git and pragma_to_git->get()->IsBool() and
        pragma_to_git->get()->Bool()) {
        // get the WS root as git tree
        fpath_git_map->ConsumeAfterKeysReady(
            ts,
            {std::move(fpath)},
            [repos = std::move(repos), repo_name, setter, logger](
                auto const& values) {
                auto ws_root = *values[0];
                nlohmann::json cfg({});
                cfg["workspace_root"] = ws_root;
                SetReposTakeOver(&cfg, repos, repo_name);
                (*setter)(std::move(cfg));
            },
            [logger, repo_name](auto const& msg, bool fatal) {
                (*logger)(fmt::format("While setting the workspace root for "
                                      "repository {} of type file:\n{}",
                                      repo_name,
                                      msg),
                          fatal);
            });
    }
    else {
        // get the WS root as filesystem location
        nlohmann::json cfg({});
        cfg["workspace_root"] =
            nlohmann::json::array({"file", fpath.string()});  // explicit array
        SetReposTakeOver(&cfg, repos, repo_name);
        (*setter)(std::move(cfg));
    }
}

/// \brief Perform checkout for a distdir type repository.
/// Guarantees the logger is called exactly once with fatal if a failure occurs.
void DistdirCheckout(ExpressionPtr const& repo_desc,
                     ExpressionPtr&& repos,
                     std::string const& repo_name,
                     gsl::not_null<ContentCASMap*> const& content_cas_map,
                     gsl::not_null<DistdirGitMap*> const& distdir_git_map,
                     gsl::not_null<TaskSystem*> const& ts,
                     ReposToSetupMap::SetterPtr const& setter,
                     ReposToSetupMap::LoggerPtr const& logger) {
    // enforce mandatory fields
    auto repo_desc_repositories = repo_desc->At("repositories");
    if (not repo_desc_repositories) {
        (*logger)(
            "DistdirCheckout: Mandatory field \'repositories\' is missing",
            /*fatal=*/true);
        return;
    }
    if (not repo_desc_repositories->get()->IsList()) {
        (*logger)(
            "DistdirCheckout: Unsupported value for mandatory field "
            "\'repositories\'",
            /*fatal=*/true);
        return;
    }
    // map of distfile to content
    auto distdir_content =
        std::make_shared<std::unordered_map<std::string, std::string>>();
    // get distdir list
    auto distdir_repos = repo_desc_repositories->get()->List();
    // create list of archives to fetch
    std::vector<ArchiveContent> dist_repos_to_fetch{};
    for (auto const& dist_repo : distdir_repos) {
        // get name of dist_repo
        auto dist_repo_name = dist_repo->String();
        // check that repo name exists
        auto repos_dist_repo_name = repos->At(dist_repo_name);
        if (not repos_dist_repo_name) {
            (*logger)(fmt::format("DistdirCheckout: no repository named {}",
                                  dist_repo_name),
                      /*fatal=*/true);
            return;
        }
        auto repo_desc = repos_dist_repo_name->get()->At("repository");
        if (not repo_desc) {
            (*logger)(
                fmt::format("DistdirCheckout: mandatory key \'repository\' "
                            "missing for repository {}",
                            dist_repo_name),
                /*fatal=*/true);
            return;
        }
        auto resolved_repo_desc =
            JustMR::Utils::ResolveRepo(repo_desc->get(), repos);
        if (not resolved_repo_desc) {
            (*logger)(
                fmt::format("DistdirCheckout: found cyclic dependency for "
                            "repository {}",
                            dist_repo_name),
                /*fatal=*/true);
            return;
        }
        auto repo_type = (*resolved_repo_desc)->At("type");
        if (not repo_type) {
            (*logger)(
                fmt::format("DistdirCheckout: mandatory key \'type\' missing "
                            "for repository {}",
                            dist_repo_name),
                /*fatal=*/true);
            return;
        }
        if (not repo_type->get()->IsString()) {
            (*logger)(fmt::format("DistdirCheckout: unsupported value for key "
                                  "\'type\' for repository {}",
                                  dist_repo_name),
                      /*fatal=*/true);
            return;
        }
        // get repo_type
        auto repo_type_str = repo_type->get()->String();
        if (not kCheckoutTypeMap.contains(repo_type_str)) {
            (*logger)(fmt::format("DistdirCheckout: unknown type {} for "
                                  "repository {}",
                                  repo_type_str,
                                  dist_repo_name),
                      /*fatal=*/true);
            return;
        }
        // only do work if repo is archive type
        if (kCheckoutTypeMap.at(repo_type_str) == CheckoutType::Archive) {
            // check mandatory fields
            auto repo_desc_content = (*resolved_repo_desc)->At("content");
            if (not repo_desc_content) {
                (*logger)(
                    "DistdirCheckout: Mandatory field \'content\' is "
                    "missing",
                    /*fatal=*/true);
                return;
            }
            if (not repo_desc_content->get()->IsString()) {
                (*logger)(
                    "DistdirCheckout: Unsupported value for mandatory "
                    "field \'content\'",
                    /*fatal=*/true);
                return;
            }
            auto repo_desc_fetch = (*resolved_repo_desc)->At("fetch");
            if (not repo_desc_fetch) {
                (*logger)(
                    "DistdirCheckout: Mandatory field \'fetch\' is missing",
                    /*fatal=*/true);
                return;
            }
            if (not repo_desc_fetch->get()->IsString()) {
                (*logger)(
                    "DistdirCheckout: Unsupported value for mandatory "
                    "field \'fetch\'",
                    /*fatal=*/true);
                return;
            }
            auto repo_desc_distfile =
                (*resolved_repo_desc)->Get("distfile", Expression::none_t{});
            auto repo_desc_sha256 =
                (*resolved_repo_desc)->Get("sha256", Expression::none_t{});
            auto repo_desc_sha512 =
                (*resolved_repo_desc)->Get("sha512", Expression::none_t{});

            ArchiveContent archive = {
                repo_desc_content->get()->String(), /* content */
                repo_desc_distfile->IsString()
                    ? std::make_optional(repo_desc_distfile->String())
                    : std::nullopt,               /* distfile */
                repo_desc_fetch->get()->String(), /* fetch_url */
                repo_desc_sha256->IsString()
                    ? std::make_optional(repo_desc_sha256->String())
                    : std::nullopt, /* sha256 */
                repo_desc_sha512->IsString()
                    ? std::make_optional(repo_desc_sha512->String())
                    : std::nullopt /* sha512 */
            };                     /* archive */

            // add to distdir content map
            auto repo_distfile =
                (archive.distfile ? archive.distfile.value()
                                  : std::filesystem::path(archive.fetch_url)
                                        .filename()
                                        .string());
            distdir_content->insert_or_assign(repo_distfile, archive.content);
            // add to fetch list
            dist_repos_to_fetch.emplace_back(std::move(archive));
        }
    }
    // fetch the gathered distdir repos into CAS
    content_cas_map->ConsumeAfterKeysReady(
        ts,
        dist_repos_to_fetch,
        [distdir_content = std::move(distdir_content),
         repos = std::move(repos),
         repo_name,
         distdir_git_map,
         ts,
         setter,
         logger]([[maybe_unused]] auto const& values) mutable {
            // repos are in CAS
            // get hash of distdir content
            auto distdir_content_id =
                HashFunction::ComputeBlobHash(
                    nlohmann::json(*distdir_content).dump())
                    .HexString();
            // get the WS root as git tree
            DistdirInfo distdir_info = {distdir_content_id, distdir_content};
            distdir_git_map->ConsumeAfterKeysReady(
                ts,
                {std::move(distdir_info)},
                [repos = std::move(repos), repo_name, setter, logger](
                    auto const& values) {
                    auto ws_root = *values[0];
                    nlohmann::json cfg({});
                    cfg["workspace_root"] = ws_root;
                    SetReposTakeOver(&cfg, repos, repo_name);
                    (*setter)(std::move(cfg));
                },
                [logger, repo_name](auto const& msg, bool fatal) {
                    (*logger)(
                        fmt::format("While setting the workspace root for "
                                    "repository {} of type distdir:\n{}",
                                    repo_name,
                                    msg),
                        fatal);
                });
        },
        [logger, repo_name](auto const& msg, bool fatal) {
            (*logger)(fmt::format("While fetching archives for distdir "
                                  "repository {}:\n{}",
                                  repo_name,
                                  msg),
                      fatal);
        });
}

}  // namespace

auto CreateReposToSetupMap(std::shared_ptr<Configuration> const& config,
                           std::optional<std::string> const& main,
                           bool interactive,
                           gsl::not_null<CommitGitMap*> const& commit_git_map,
                           gsl::not_null<ContentGitMap*> const& content_git_map,
                           gsl::not_null<FilePathGitMap*> const& fpath_git_map,
                           gsl::not_null<ContentCASMap*> const& content_cas_map,
                           gsl::not_null<DistdirGitMap*> const& distdir_git_map,
                           std::size_t jobs) -> ReposToSetupMap {
    auto setup_repo = [config,
                       main,
                       interactive,
                       commit_git_map,
                       content_git_map,
                       fpath_git_map,
                       content_cas_map,
                       distdir_git_map](auto ts,
                                        auto setter,
                                        auto logger,
                                        auto /* unused */,
                                        auto const& key) {
        auto repos = (*config)["repositories"];
        if (main && (key == *main) && interactive) {
            // no repository checkout required
            nlohmann::json cfg({});
            SetReposTakeOver(&cfg, repos, key);
            (*setter)(std::move(cfg));
        }
        else {
            // repository requires checkout
            auto repo_desc_key = repos->At(key);
            if (not repo_desc_key) {
                (*logger)(fmt::format("Config: missing config entry "
                                      "for repository {}",
                                      key),
                          /*fatal=*/true);
                return;
            }
            auto repo_desc = repo_desc_key->get()->At("repository");
            if (not repo_desc) {
                (*logger)(fmt::format("Config: mandatory key \'repository\' "
                                      "missing for repository {}",
                                      key),
                          /*fatal=*/true);
                return;
            }
            auto resolved_repo_desc =
                JustMR::Utils::ResolveRepo(repo_desc->get(), repos);
            if (not resolved_repo_desc) {
                (*logger)(fmt::format("Config: found cyclic dependency for "
                                      "repository {}",
                                      key),
                          /*fatal=*/true);
                return;
            }
            auto repo_type = (*resolved_repo_desc)->At("type");
            if (not repo_type) {
                (*logger)(fmt::format("Config: mandatory key \'type\' missing "
                                      "for repository {}",
                                      key),
                          /*fatal=*/true);
                return;
            }
            if (not repo_type->get()->IsString()) {
                (*logger)(fmt::format("Config: unsupported value for key "
                                      "\'type\' for repository {}",
                                      key),
                          /*fatal=*/true);
                return;
            }
            // get repo_type
            auto repo_type_str = repo_type->get()->String();
            if (not kCheckoutTypeMap.contains(repo_type_str)) {
                (*logger)(
                    fmt::format("Config: unknown type {} for repository {}",
                                repo_type_str,
                                key),
                    /*fatal=*/true);
                return;
            }
            // setup a wrapped_logger
            auto wrapped_logger = std::make_shared<AsyncMapConsumerLogger>(
                [&logger, repo_name = key](auto const& msg, bool fatal) {
                    (*logger)(
                        fmt::format("While checking out repository {}:\n{}",
                                    repo_name,
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
                case CheckoutType::File: {
                    FileCheckout(*resolved_repo_desc,
                                 std::move(repos),
                                 key,
                                 fpath_git_map,
                                 ts,
                                 setter,
                                 wrapped_logger);
                    break;
                }
                case CheckoutType::Distdir: {
                    DistdirCheckout(*resolved_repo_desc,
                                    std::move(repos),
                                    key,
                                    content_cas_map,
                                    distdir_git_map,
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
