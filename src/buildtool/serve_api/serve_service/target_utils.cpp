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

#include "src/buildtool/serve_api/serve_service/target_utils.hpp"

#include <exception>
#include <fstream>
#include <string>

#include "fmt/core.h"
#include "nlohmann/json.hpp"
#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/file_system/git_cas.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/storage.hpp"

auto IsTreeInRepo(std::string const& tree_id,
                  std::filesystem::path const& repo_path,
                  std::shared_ptr<Logger> const& logger) -> bool {
    if (auto git_cas = GitCAS::Open(repo_path)) {
        if (auto repo = GitRepo::Open(git_cas)) {
            // wrap logger for GitRepo call
            auto wrapped_logger = std::make_shared<GitRepo::anon_logger_t>(
                [logger, repo_path, tree_id](auto const& msg, bool fatal) {
                    if (fatal) {
                        logger->Emit(LogLevel::Info,
                                     "ServeTarget: While checking existence of "
                                     "tree {} in repository {}:\n{}",
                                     tree_id,
                                     repo_path.string(),
                                     msg);
                    }
                });
            if (auto res = repo->CheckTreeExists(tree_id, wrapped_logger)) {
                return *res;
            }
        }
    }
    return false;  // tree not found
}

auto GetServingRepository(RemoteServeConfig const& serve_config,
                          std::string const& tree_id,
                          std::shared_ptr<Logger> const& logger)
    -> std::optional<std::filesystem::path> {
    // try the Git cache repository
    if (IsTreeInRepo(tree_id, StorageConfig::Instance().GitRoot(), logger)) {
        return StorageConfig::Instance().GitRoot();
    }
    // check the known repositories
    for (auto const& path : serve_config.known_repositories) {
        if (IsTreeInRepo(tree_id, path, logger)) {
            return path;
        }
    }
    return std::nullopt;  // tree cannot be served
}

auto DetermineRoots(RemoteServeConfig const& serve_config,
                    std::string const& main_repo,
                    std::filesystem::path const& repo_config_path,
                    gsl::not_null<RepositoryConfig*> const& repository_config,
                    std::shared_ptr<Logger> const& logger)
    -> std::optional<std::string> {
    // parse repository configuration file
    auto repos = nlohmann::json::object();
    try {
        std::ifstream fs(repo_config_path);
        repos = nlohmann::json::parse(fs);
        if (not repos.is_object()) {
            return fmt::format(
                "Repository configuration file {} does not contain a map.",
                repo_config_path.string());
        }
    } catch (std::exception const& ex) {
        return fmt::format("Parsing repository config file {} failed with:\n{}",
                           repo_config_path.string(),
                           ex.what());
    }
    if (not repos.contains(main_repo)) {
        return fmt::format(
            "Repository configuration does not contain expected main "
            "repository {}",
            main_repo);
    }
    // populate RepositoryConfig instance
    std::string error_msg;
    for (auto const& [repo, desc] : repos.items()) {
        // root parser
        auto parse_keyword_root =
            [&serve_config,
             &desc = desc,
             &repo = repo,
             &error_msg = error_msg,
             logger](std::string const& keyword) -> std::optional<FileRoot> {
            auto it = desc.find(keyword);
            if (it != desc.end()) {
                if (auto parsed_root =
                        FileRoot::ParseRoot(repo, keyword, *it, &error_msg)) {
                    // check that root has absent-like format
                    if (not parsed_root->first.IsAbsent()) {
                        error_msg = fmt::format(
                            "Expected {} to have absent Git tree format, but "
                            "found {}",
                            keyword,
                            it->dump());
                        return std::nullopt;
                    }
                    // find the serving repository for the root tree
                    auto tree_id = *parsed_root->first.GetAbsentTreeId();
                    auto repo_path =
                        GetServingRepository(serve_config, tree_id, logger);
                    if (not repo_path) {
                        error_msg = fmt::format(
                            "{} tree {} is not known", keyword, tree_id);
                        return std::nullopt;
                    }
                    // set the root as present
                    if (auto root = FileRoot::FromGit(
                            *repo_path,
                            tree_id,
                            parsed_root->first.IgnoreSpecial())) {
                        return root;
                    }
                }
                error_msg =
                    fmt::format("Failed to parse {} {}", keyword, it->dump());
                return std::nullopt;
            }
            error_msg =
                fmt::format("Missing {} for repository {}", keyword, repo);
            return std::nullopt;
        };

        std::optional<FileRoot> ws_root = parse_keyword_root("workspace_root");
        if (not ws_root) {
            return error_msg;
        }
        auto info = RepositoryConfig::RepositoryInfo{std::move(*ws_root)};

        if (auto target_root = parse_keyword_root("target_root")) {
            info.target_root = std::move(*target_root);
        }
        else {
            return error_msg;
        }

        if (auto rule_root = parse_keyword_root("rule_root")) {
            info.rule_root = std::move(*rule_root);
        }
        else {
            return error_msg;
        }

        if (auto expression_root = parse_keyword_root("expression_root")) {
            info.expression_root = std::move(*expression_root);
        }
        else {
            return error_msg;
        }

        auto it_bindings = desc.find("bindings");
        if (it_bindings != desc.end()) {
            if (not it_bindings->is_object()) {
                return fmt::format(
                    "bindings has to be a string-string map, but found {}",
                    it_bindings->dump());
            }
            for (auto const& [local_name, global_name] : it_bindings->items()) {
                if (not repos.contains(global_name)) {
                    return fmt::format(
                        "Binding {} for {} in {} does not refer to a "
                        "defined repository.",
                        global_name,
                        local_name,
                        repo);
                }
                info.name_mapping[local_name] = global_name;
            }
        }
        else {
            return fmt::format("Missing bindings for repository {}", repo);
        }

        auto parse_keyword_file_name =
            [&desc = desc, &repo = repo, &error_msg = error_msg](
                std::string* keyword_file_name,
                std::string const& keyword) -> bool {
            auto it = desc.find(keyword);
            if (it != desc.end()) {
                *keyword_file_name = *it;
                return true;
            }
            error_msg =
                fmt::format("Missing {} for repository {}", keyword, repo);
            return false;
        };

        if (not parse_keyword_file_name(&info.target_file_name,
                                        "target_file_name")) {
            return error_msg;
        }

        if (not parse_keyword_file_name(&info.rule_file_name,
                                        "rule_file_name")) {
            return error_msg;
        }

        if (not parse_keyword_file_name(&info.expression_file_name,
                                        "expression_file_name")) {
            return error_msg;
        }

        repository_config->SetInfo(repo, std::move(info));
    }
    // success
    return std::nullopt;
}

auto GetBlobContent(std::filesystem::path const& repo_path,
                    std::string const& tree_id,
                    std::string const& rel_path,
                    std::shared_ptr<Logger> const& logger)
    -> std::optional<std::pair<bool, std::optional<std::string>>> {
    if (auto git_cas = GitCAS::Open(repo_path)) {
        if (auto repo = GitRepo::Open(git_cas)) {
            // check if tree exists
            auto wrapped_logger = std::make_shared<GitRepo::anon_logger_t>(
                [logger, repo_path, tree_id](auto const& msg, bool fatal) {
                    if (fatal) {
                        logger->Emit(LogLevel::Debug,
                                     "ServeTargetVariables: While checking if "
                                     "tree {} exists in repository {}:\n{}",
                                     tree_id,
                                     repo_path.string(),
                                     msg);
                    }
                });
            if (repo->CheckTreeExists(tree_id, wrapped_logger) == true) {
                // get tree entry by path
                if (auto entry_info =
                        repo->GetObjectByPathFromTree(tree_id, rel_path)) {
                    wrapped_logger = std::make_shared<GitRepo::anon_logger_t>(
                        [logger, repo_path, blob_id = entry_info->id](
                            auto const& msg, bool fatal) {
                            if (fatal) {
                                logger->Emit(
                                    LogLevel::Debug,
                                    "ServeTargetVariables: While retrieving "
                                    "blob {} from repository {}:\n{}",
                                    blob_id,
                                    repo_path.string(),
                                    msg);
                            }
                        });
                    // get blob content
                    return repo->TryReadBlob(entry_info->id, wrapped_logger);
                }
                // trace failure to get entry
                logger->Emit(LogLevel::Debug,
                             "ServeTargetVariables: Failed to retrieve entry "
                             "{} in tree {} from repository {}",
                             rel_path,
                             tree_id,
                             repo_path.string());
                return std::pair(false, std::nullopt);  // could not read blob
            }
        }
    }
    return std::nullopt;  // tree not found or errors while retrieving tree
}
