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

#include "src/buildtool/serve_api/serve_service/target.hpp"

#include <filesystem>
#include <string>
#include <utility>

#include "fmt/core.h"
#include "fmt/format.h"
#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name_data.hpp"
#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/build_engine/expression/expression_ptr.hpp"
#include "src/buildtool/build_engine/target_map/configured_target.hpp"
#include "src/buildtool/build_engine/target_map/result_map.hpp"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/file_system/git_cas.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/graph_traverser/graph_traverser.hpp"
#include "src/buildtool/main/analyse.hpp"
#include "src/buildtool/main/build_utils.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/buildtool/progress_reporting/progress_reporter.hpp"
#include "src/buildtool/serve_api/remote/config.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/buildtool/storage/target_cache_key.hpp"
#include "src/utils/cpp/verify_hash.hpp"

auto TargetService::IsTreeInRepo(std::string const& tree_id,
                                 std::filesystem::path const& repo_path,
                                 std::shared_ptr<Logger> const& logger)
    -> bool {
    if (auto git_cas = GitCAS::Open(repo_path)) {
        if (auto repo = GitRepo::Open(git_cas)) {
            // wrap logger for GitRepo call
            auto wrapped_logger = std::make_shared<GitRepo::anon_logger_t>(
                [logger, repo_path, tree_id](auto const& msg, bool fatal) {
                    if (fatal) {
                        auto err = fmt::format(
                            "ServeTarget: While checking existence of tree {} "
                            "in repository {}:\n{}",
                            tree_id,
                            repo_path.string(),
                            msg);
                        logger->Emit(LogLevel::Info, err);
                    }
                });
            if (auto res = repo->CheckTreeExists(tree_id, wrapped_logger)) {
                return *res;
            }
        }
    }
    return false;  // tree not found
}

auto TargetService::GetServingRepository(std::string const& tree_id,
                                         std::shared_ptr<Logger> const& logger)
    -> std::optional<std::filesystem::path> {
    // try the Git cache repository
    if (IsTreeInRepo(tree_id, StorageConfig::GitRoot(), logger)) {
        return StorageConfig::GitRoot();
    }
    // check the known repositories
    for (auto const& path : RemoteServeConfig::KnownRepositories()) {
        if (IsTreeInRepo(tree_id, path, logger)) {
            return path;
        }
    }
    return std::nullopt;  // tree cannot be served
}

auto TargetService::DetermineRoots(
    std::string const& main_repo,
    std::filesystem::path const& repo_config_path,
    gsl::not_null<RepositoryConfig*> const& repository_config,
    std::shared_ptr<Logger> const& logger) -> std::optional<std::string> {
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
            [&desc = desc, &repo = repo, &error_msg = error_msg, logger](
                std::string const& keyword) -> std::optional<FileRoot> {
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
                    auto repo_path = GetServingRepository(tree_id, logger);
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

auto TargetService::ServeTarget(
    ::grpc::ServerContext* /*context*/,
    const ::justbuild::just_serve::ServeTargetRequest* request,
    ::justbuild::just_serve::ServeTargetResponse* response) -> ::grpc::Status {
    if (auto error_msg = IsAHash(request->target_cache_key_id().hash());
        error_msg) {
        logger_->Emit(LogLevel::Debug, *error_msg);
        return ::grpc::Status{::grpc::StatusCode::INVALID_ARGUMENT, *error_msg};
    }
    if (auto error_msg =
            IsAHash(request->execution_backend_description_id().hash());
        error_msg) {
        logger_->Emit(LogLevel::Debug, *error_msg);
        return ::grpc::Status{::grpc::StatusCode::INVALID_ARGUMENT, *error_msg};
    }
    auto const& target_cache_key_digest =
        ArtifactDigest{request->target_cache_key_id()};

    // acquire lock for CAS
    auto lock = GarbageCollector::SharedLock();
    if (!lock) {
        auto error_msg = fmt::format("Could not acquire gc SharedLock");
        logger_->Emit(LogLevel::Error, error_msg);
        return ::grpc::Status{::grpc::StatusCode::INTERNAL, error_msg};
    }

    auto const& tc = Storage::Instance().TargetCache().WithShard(
        ArtifactDigest{request->execution_backend_description_id()}.hash());
    auto const& tc_key =
        TargetCacheKey{{target_cache_key_digest, ObjectType::File}};

    // check if target-level cache entry has already been computed
    if (auto target_entry = tc->Read(tc_key); target_entry) {

        // make sure all artifacts referenced in the target cache value are in
        // the remote cas
        std::vector<Artifact::ObjectInfo> artifacts;
        if (!target_entry->first.ToArtifacts(&artifacts)) {
            auto msg = fmt::format(
                "Failed to extract artifacts from target cache entry {}",
                target_entry->second.ToString());
            logger_->Emit(LogLevel::Error, msg);
            return grpc::Status{grpc::StatusCode::FAILED_PRECONDITION, msg};
        }
        if (!local_api_->RetrieveToCas(artifacts, &*remote_api_)) {
            auto msg = fmt::format(
                "Failed to upload to remote cas the artifacts referenced in "
                "the target cache entry {}",
                target_entry->second.ToString());
            logger_->Emit(LogLevel::Error, msg);
            return grpc::Status{grpc::StatusCode::FAILED_PRECONDITION, msg};
        }

        // make sure the target cache value is in the remote cas
        if (!local_api_->RetrieveToCas({target_entry->second}, &*remote_api_)) {
            auto msg = fmt::format(
                "Failed to upload to remote cas the target cache entry {}",
                target_entry->second.ToString());
            logger_->Emit(LogLevel::Error, msg);
            return grpc::Status{grpc::StatusCode::UNAVAILABLE, msg};
        }

        *(response->mutable_target_value()) =
            std::move(target_entry->second.digest);
        return ::grpc::Status::OK;
    }

    // get target description from remote cas
    auto const& target_cache_key_info = Artifact::ObjectInfo{
        .digest = target_cache_key_digest, .type = ObjectType::File};

    if (!local_api_->IsAvailable(target_cache_key_digest) and
        !remote_api_->RetrieveToCas({target_cache_key_info}, &*local_api_)) {
        auto msg = fmt::format(
            "could not retrieve from remote-execution end point blob {}",
            target_cache_key_info.ToString());
        logger_->Emit(LogLevel::Error, msg);
        return grpc::Status{grpc::StatusCode::FAILED_PRECONDITION, msg};
    }

    auto const& target_description_str =
        local_api_->RetrieveToMemory(target_cache_key_info);

    ExpressionPtr target_description_dict{};
    try {
        target_description_dict = Expression::FromJson(
            nlohmann::json::parse(*target_description_str));
    } catch (std::exception const& ex) {
        auto msg = fmt::format("Parsing TargetCacheKey {} failed with:\n{}",
                               target_cache_key_digest.hash(),
                               ex.what());
        logger_->Emit(LogLevel::Error, msg);
        return grpc::Status{grpc::StatusCode::FAILED_PRECONDITION, msg};
    }
    if (not target_description_dict.IsNotNull() or
        not target_description_dict->IsMap()) {
        auto msg =
            fmt::format("TargetCacheKey {} should contain a map, but found {}",
                        target_cache_key_digest.hash(),
                        target_description_dict.ToJson().dump());
        logger_->Emit(LogLevel::Error, msg);
        return grpc::Status{grpc::StatusCode::FAILED_PRECONDITION, msg};
    }

    std::string error_msg{};  // buffer to store various error messages

    // utility function to check the correctness of the TargetCacheKey
    auto check_key = [&target_description_dict,
                      this,
                      &target_cache_key_digest,
                      &error_msg](std::string const& key) -> bool {
        if (!target_description_dict->At(key)) {
            error_msg =
                fmt::format("TargetCacheKey {} does not contain key \"{}\"",
                            target_cache_key_digest.hash(),
                            key);
            logger_->Emit(LogLevel::Error, error_msg);
            return false;
        }
        return true;
    };

    if (!check_key("repo_key") or !check_key("target_name") or
        !check_key("effective_config")) {
        return grpc::Status{grpc::StatusCode::FAILED_PRECONDITION, error_msg};
    }

    // get repository config blob path
    auto const& repo_key =
        target_description_dict->Get("repo_key", Expression::none_t{});
    if (not repo_key.IsNotNull() or not repo_key->IsString()) {
        auto msg = fmt::format(
            "TargetCacheKey {}: \"repo_key\" value should be a string, but "
            "found {}",
            target_cache_key_digest.hash(),
            repo_key.ToJson().dump());
        logger_->Emit(LogLevel::Error, msg);
        return grpc::Status{grpc::StatusCode::FAILED_PRECONDITION, msg};
    }
    ArtifactDigest repo_key_dgst{repo_key->String(), 0, /*is_tree=*/false};
    if (!local_api_->IsAvailable(repo_key_dgst) and
        !remote_api_->RetrieveToCas(
            {Artifact::ObjectInfo{.digest = repo_key_dgst,
                                  .type = ObjectType::File}},
            &*local_api_)) {
        auto msg = fmt::format(
            "Could not retrieve from remote-execution end point blob {}",
            repo_key->String());
        logger_->Emit(LogLevel::Error, msg);
        return grpc::Status{grpc::StatusCode::UNAVAILABLE, msg};
    }
    auto repo_config_path = Storage::Instance().CAS().BlobPath(
        repo_key_dgst, /*is_executable=*/false);
    if (not repo_config_path) {
        auto msg = fmt::format("Repository configuration blob {} not in CAS",
                               repo_key->String());
        logger_->Emit(LogLevel::Error, msg);
        return grpc::Status{grpc::StatusCode::FAILED_PRECONDITION, msg};
    }

    // populate the RepositoryConfig instance
    RepositoryConfig repository_config{};
    std::string const main_repo{"0"};  // known predefined main repository name
    if (auto err_msg = DetermineRoots(
            main_repo, *repo_config_path, &repository_config, logger_)) {
        logger_->Emit(LogLevel::Error, *err_msg);
        return grpc::Status{grpc::StatusCode::FAILED_PRECONDITION, *err_msg};
    }

    // get the target name
    auto const& target_expr =
        target_description_dict->Get("target_name", Expression::none_t{});
    if (not target_expr.IsNotNull() or not target_expr->IsString()) {
        auto msg = fmt::format(
            "TargetCacheKey {}: \"target_name\" value should be a string, but"
            " found {}",
            target_cache_key_digest.hash(),
            target_expr.ToJson().dump());
        logger_->Emit(LogLevel::Error, msg);
        return grpc::Status{grpc::StatusCode::FAILED_PRECONDITION, msg};
    }
    auto target_name = nlohmann::json::object();
    try {
        target_name = nlohmann::json::parse(target_expr->String());
    } catch (std::exception const& ex) {
        auto msg = fmt::format(
            "TargetCacheKey {}: parsing \"target_name\" failed with:\n{}",
            target_cache_key_digest.hash(),
            ex.what());
        logger_->Emit(LogLevel::Error, msg);
        return grpc::Status{grpc::StatusCode::FAILED_PRECONDITION, msg};
    }

    // get the effective config of the export target
    auto const& config_expr =
        target_description_dict->Get("effective_config", Expression::none_t{});
    if (not config_expr.IsNotNull() or not config_expr->IsString()) {
        auto msg = fmt::format(
            "TargetCacheKey {}: \"effective_config\" value should be a string,"
            " but found {}",
            target_cache_key_digest.hash(),
            config_expr.ToJson().dump());
        logger_->Emit(LogLevel::Error, msg);
        return grpc::Status{grpc::StatusCode::FAILED_PRECONDITION, msg};
    }
    Configuration config{};
    try {
        config = Configuration{
            Expression::FromJson(nlohmann::json::parse(config_expr->String()))};
    } catch (std::exception const& ex) {
        auto msg = fmt::format(
            "TargetCacheKey {}: parsing \"effective_config\" failed with:\n{}",
            target_cache_key_digest.hash(),
            ex.what());
        logger_->Emit(LogLevel::Error, msg);
        return grpc::Status{grpc::StatusCode::FAILED_PRECONDITION, msg};
    }

    // get the ConfiguredTarget
    auto entity = BuildMaps::Base::ParseEntityNameFromJson(
        target_name,
        BuildMaps::Base::EntityName{
            BuildMaps::Base::NamedTarget{main_repo, ".", ""}},
        &repository_config,
        [&error_msg, &target_name](std::string const& parse_err) {
            error_msg = fmt::format("Parsing target name {} failed with:\n {} ",
                                    target_name.dump(),
                                    parse_err);
        });
    if (not entity) {
        logger_->Emit(LogLevel::Error, error_msg);
        return grpc::Status{grpc::StatusCode::FAILED_PRECONDITION, error_msg};
    }

    BuildMaps::Target::ResultTargetMap result_map{RemoteServeConfig::Jobs()};
    auto configured_target = BuildMaps::Target::ConfiguredTarget{
        .target = std::move(*entity), .config = std::move(config)};

    // analyse the configured target
    auto result = AnalyseTarget(configured_target,
                                &result_map,
                                &repository_config,
                                RemoteServeConfig::Jobs(),
                                std::nullopt /*request_action_input*/);

    if (not result) {
        auto msg = fmt::format("Failed to analyse target {}",
                               configured_target.target.ToString());
        logger_->Emit(LogLevel::Error, msg);
        return grpc::Status{grpc::StatusCode::FAILED_PRECONDITION, msg};
    }

    // get the output artifacts
    auto const [artifacts, runfiles] = ReadOutputArtifacts(result->target);

    // get the result map outputs
    auto const& [actions, blobs, trees] = result_map.ToResult();

    // collect cache targets and artifacts for target-level caching
    auto const cache_targets = result_map.CacheTargets();
    auto cache_artifacts = CollectNonKnownArtifacts(cache_targets);

    // Clean up result map, now that it is no longer needed
    {
        TaskSystem ts;
        result_map.Clear(&ts);
    }

    auto jobs = RemoteServeConfig::BuildJobs();
    if (jobs == 0) {
        jobs = RemoteServeConfig::Jobs();
    }

    // setup graph traverser
    GraphTraverser::CommandLineArguments traverser_args{};
    traverser_args.jobs = jobs;
    traverser_args.build.timeout = RemoteServeConfig::ActionTimeout();
    traverser_args.stage = std::nullopt;
    traverser_args.rebuild = std::nullopt;
    GraphTraverser const traverser{std::move(traverser_args),
                                   &repository_config,
                                   ProgressReporter::Reporter()};

    // perform build
    auto build_result = traverser.BuildAndStage(
        artifacts, runfiles, actions, blobs, trees, std::move(cache_artifacts));

    if (not build_result) {
        auto msg = fmt::format("Build for target {} failed",
                               configured_target.target.ToString());
        logger_->Emit(LogLevel::Error, msg);
        return grpc::Status{grpc::StatusCode::FAILED_PRECONDITION, msg};
    }

    WriteTargetCacheEntries(cache_targets,
                            build_result->extra_infos,
                            jobs,
                            traverser.GetLocalApi(),
                            traverser.GetRemoteApi());

    if (build_result->failed_artifacts) {
        auto msg =
            fmt::format("Build result for target {} contains failed artifacts ",
                        configured_target.target.ToString());
        logger_->Emit(LogLevel::Warning, msg);
        return grpc::Status{grpc::StatusCode::FAILED_PRECONDITION, msg};
    }

    // make sure remote CAS has all artifacts
    if (auto target_entry = tc->Read(tc_key); target_entry) {
        // make sure the target cache value is in the remote cas
        if (!local_api_->RetrieveToCas({target_entry->second}, &*remote_api_)) {
            auto msg = fmt::format(
                "Failed to upload to remote cas the target cache entry {}",
                target_entry->second.ToString());
            logger_->Emit(LogLevel::Error, msg);
            return grpc::Status{grpc::StatusCode::UNAVAILABLE, msg};
        }
        *(response->mutable_target_value()) =
            std::move(target_entry->second.digest);
        return ::grpc::Status::OK;
    }

    // target cache value missing -- internally something is very wrong
    auto msg = fmt::format("Failed to read TargetCacheKey {} after store",
                           target_cache_key_digest.hash());
    logger_->Emit(LogLevel::Error, msg);
    return grpc::Status{grpc::StatusCode::INTERNAL, msg};
}

auto TargetService::GetBlobContent(std::filesystem::path const& repo_path,
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
                        auto err = fmt::format(
                            "ServeTargetVariables: While checking if tree {} "
                            "exists in repository {}:\n{}",
                            tree_id,
                            repo_path.string(),
                            msg);
                        logger->Emit(LogLevel::Debug, err);
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
                                auto err = fmt::format(
                                    "ServeTargetVariables: While retrieving "
                                    "blob {} from repository {}:\n{}",
                                    blob_id,
                                    repo_path.string(),
                                    msg);
                                logger->Emit(LogLevel::Debug, err);
                            }
                        });
                    // get blob content
                    return repo->TryReadBlob(entry_info->id, wrapped_logger);
                }
                // trace failure to get entry
                auto err = fmt::format(
                    "ServeTargetVariables: Failed to retrieve entry {} in "
                    "tree {} from repository {}",
                    rel_path,
                    tree_id,
                    repo_path.string());
                logger->Emit(LogLevel::Debug, err);
                return std::pair(false, std::nullopt);  // could not read blob
            }
        }
    }
    return std::nullopt;  // tree not found or errors while retrieving tree
}

auto TargetService::ServeTargetVariables(
    ::grpc::ServerContext* /*context*/,
    const ::justbuild::just_serve::ServeTargetVariablesRequest* request,
    ::justbuild::just_serve::ServeTargetVariablesResponse* response)
    -> ::grpc::Status {
    auto const& root_tree{request->root_tree()};
    auto const& target_file{request->target_file()};
    auto const& target{request->target()};
    // retrieve content of target file
    std::optional<std::string> target_file_content{std::nullopt};
    bool tree_found{false};
    // try in local build root Git cache
    if (auto res = GetBlobContent(
            StorageConfig::GitRoot(), root_tree, target_file, logger_)) {
        tree_found = true;
        if (res->first) {
            if (not res->second) {
                // tree exists, but does not contain target file
                auto err = fmt::format(
                    "Target-root {} found, but does not contain target file {}",
                    root_tree,
                    target_file);
                return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION,
                                      err};
            }
            target_file_content = *res->second;
        }
    }
    if (not target_file_content) {
        // try given extra repositories, in order
        for (auto const& path : RemoteServeConfig::KnownRepositories()) {
            if (auto res =
                    GetBlobContent(path, root_tree, target_file, logger_)) {
                tree_found = true;
                if (res->first) {
                    if (not res->second) {
                        // tree exists, but does not contain target file
                        auto err = fmt::format(
                            "Target-root {} found, but does not contain target "
                            "file {}",
                            root_tree,
                            target_file);
                        return ::grpc::Status{
                            ::grpc::StatusCode::FAILED_PRECONDITION, err};
                    }
                    target_file_content = *res->second;
                    break;
                }
            }
        }
    }
    // report if failed to find root tree
    if (not target_file_content) {
        if (tree_found) {
            // something went wrong trying to read the target file blob
            auto err =
                fmt::format("Could not read target file {}", target_file);
            return ::grpc::Status{::grpc::StatusCode::INTERNAL, err};
        }
        // tree not found
        auto err = fmt::format("Missing target-root tree {}", root_tree);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    // parse target file as json
    ExpressionPtr map{nullptr};
    try {
        map = Expression::FromJson(nlohmann::json::parse(*target_file_content));
    } catch (std::exception const& e) {
        auto err = fmt::format(
            "Failed to parse target file {} as json with error:\n{}",
            target_file,
            e.what());
        logger_->Emit(LogLevel::Error, err);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    if (not map->IsMap()) {
        auto err =
            fmt::format("Target file {} should contain a map, but found:\n{}",
                        target_file,
                        map->ToString());
        logger_->Emit(LogLevel::Error, err);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    // do validity checks (target present, is export, flexible_config valid)
    auto target_desc = map->At(target);
    if (not target_desc) {
        // target is not present
        auto err = fmt::format(
            "Missing target {} in target file {}", target, target_file);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    auto export_desc = target_desc->get()->At("type");
    if (not export_desc) {
        auto err = fmt::format(
            "Missing \"type\" field for target {} in target file {}.",
            target,
            target_file);
        logger_->Emit(LogLevel::Error, err);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    if (not export_desc->get()->IsString()) {
        auto err = fmt::format(
            "Field \"type\" for target {} in target file {} should be a "
            "string, but found:\n{}",
            target,
            target_file,
            export_desc->get()->ToString());
        logger_->Emit(LogLevel::Error, err);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    if (export_desc->get()->String() != "export") {
        // target is not of "type" : "export"
        auto err =
            fmt::format(R"(target {} is not of "type" : "export")", target);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    auto flexible_config = target_desc->get()->At("flexible_config");
    if (not flexible_config) {
        // respond with success and an empty flexible_config list
        return ::grpc::Status::OK;
    }
    if (not flexible_config->get()->IsList()) {
        auto err = fmt::format(
            "Field \"flexible_config\" for target {} in target file {} should "
            "be a list, but found {}",
            target,
            target_file,
            flexible_config->get()->ToString());
        logger_->Emit(LogLevel::Error, err);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    // populate response with flexible_config list
    auto flexible_config_list = flexible_config->get()->List();
    std::vector<std::string> fclist{};
    for (auto const& elem : flexible_config_list) {
        if (not elem->IsString()) {
            auto err = fmt::format(
                "Field \"flexible_config\" for target {} in target file {} has "
                "non-string entry {}",
                target,
                target_file,
                elem->ToString());
            logger_->Emit(LogLevel::Error, err);
            response->clear_flexible_config();  // must be unset
            return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
        }
        response->add_flexible_config(elem->String());
    }
    // respond with success
    return ::grpc::Status::OK;
}
