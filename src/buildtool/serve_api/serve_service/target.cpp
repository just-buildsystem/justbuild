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

#ifndef BOOTSTRAP_BUILD_TOOL

#include "src/buildtool/serve_api/serve_service/target.hpp"

#include "fmt/core.h"
#include "src/buildtool/build_engine/base_maps/entity_name.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name_data.hpp"
#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/build_engine/expression/expression_ptr.hpp"
#include "src/buildtool/build_engine/target_map/configured_target.hpp"
#include "src/buildtool/build_engine/target_map/result_map.hpp"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/common/statistics.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/graph_traverser/graph_traverser.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/log_sink_file.hpp"
#include "src/buildtool/main/analyse.hpp"
#include "src/buildtool/main/analyse_context.hpp"
#include "src/buildtool/main/build_utils.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/buildtool/progress_reporting/progress.hpp"
#include "src/buildtool/progress_reporting/progress_reporter.hpp"
#include "src/buildtool/serve_api/serve_service/target_utils.hpp"
#include "src/buildtool/storage/garbage_collector.hpp"
#include "src/buildtool/storage/target_cache_key.hpp"
#include "src/utils/cpp/verify_hash.hpp"

auto TargetService::GetDispatchList(
    ArtifactDigest const& dispatch_digest) noexcept
    -> expected<dispatch_t, ::grpc::Status> {
    // get blob from remote cas
    auto const& dispatch_info = Artifact::ObjectInfo{.digest = dispatch_digest,
                                                     .type = ObjectType::File};
    if (not apis_.local->IsAvailable(dispatch_digest) and
        not apis_.remote->RetrieveToCas({dispatch_info}, *apis_.local)) {
        return unexpected{
            ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION,
                           fmt::format("Could not retrieve blob {} from "
                                       "remote-execution endpoint",
                                       dispatch_info.ToString())}};
    }
    // get blob content
    auto const& dispatch_str = apis_.local->RetrieveToMemory(dispatch_info);
    if (not dispatch_str) {
        // this should not fail unless something really broke...
        return unexpected{::grpc::Status{
            ::grpc::StatusCode::INTERNAL,
            fmt::format("Unexpected failure in reading blob {} from CAS",
                        dispatch_info.ToString())}};
    }
    // parse content
    auto parsed = ParseDispatch(*dispatch_str);
    if (not parsed) {
        // pass the parsing error forward
        return unexpected{
            ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION,
                           std::move(parsed).error()}};
    }
    return *std::move(parsed);
}

auto TargetService::HandleFailureLog(
    std::filesystem::path const& logfile,
    std::string const& failure_scope,
    ::justbuild::just_serve::ServeTargetResponse* response) noexcept
    -> ::grpc::Status {
    logger_->Emit(LogLevel::Trace, [&logfile] {
        if (auto s = FileSystemManager::ReadFile(logfile)) {
            return *s;
        }
        return fmt::format("Failed to read failure log file {}",
                           logfile.string());
    });
    // ...but try to give the client the proper log
    auto const& cas = storage_.CAS();
    auto digest = cas.StoreBlob(logfile, /*is_executable=*/false);
    if (not digest) {
        auto msg = fmt::format("Failed to store log of failed {} to local CAS",
                               failure_scope);
        logger_->Emit(LogLevel::Error, "{}", msg);
        return ::grpc::Status{::grpc::StatusCode::INTERNAL, msg};
    }
    // upload log blob to remote
    if (not apis_.local->RetrieveToCas(
            {Artifact::ObjectInfo{.digest = ArtifactDigest{*digest},
                                  .type = ObjectType::File}},
            *apis_.remote)) {
        auto msg =
            fmt::format("Failed to upload to remote CAS the failed {} log {}",
                        failure_scope,
                        digest->hash());
        logger_->Emit(LogLevel::Error, "{}", msg);
        return ::grpc::Status{::grpc::StatusCode::UNAVAILABLE, msg};
    }
    // set response with log digest
    response->mutable_log()->CopyFrom(*digest);
    return ::grpc::Status::OK;
}

auto TargetService::ServeTarget(
    ::grpc::ServerContext* /*context*/,
    const ::justbuild::just_serve::ServeTargetRequest* request,
    ::justbuild::just_serve::ServeTargetResponse* response) -> ::grpc::Status {
    // check target cache key hash for validity
    if (auto msg = IsAHash(request->target_cache_key_id().hash()); msg) {
        logger_->Emit(LogLevel::Error, "{}", *msg);
        return ::grpc::Status{::grpc::StatusCode::INVALID_ARGUMENT, *msg};
    }
    auto const& target_cache_key_digest =
        ArtifactDigest{request->target_cache_key_id()};

    // acquire lock for CAS
    auto lock = GarbageCollector::SharedLock(storage_config_);
    if (!lock) {
        auto msg = std::string("Could not acquire gc SharedLock");
        logger_->Emit(LogLevel::Error, msg);
        return ::grpc::Status{::grpc::StatusCode::INTERNAL, msg};
    }

    // start filling in the backend description
    auto const address = RemoteExecutionConfig::RemoteAddress();
    // read in the execution properties and add it to the description;
    // Important: we will need to pass these platform properties also to the
    // executor (via the graph_traverser) in order for the build to be properly
    // dispatched to the correct remote-execution endpoint.
    auto platform_properties = std::map<std::string, std::string>{};
    for (auto const& p : request->execution_properties()) {
        platform_properties[p.name()] = p.value();
    }
    auto description = nlohmann::json{
        {"remote_address", address ? address->ToJson() : nlohmann::json{}},
        {"platform_properties", platform_properties}};

    // read in the dispatch list and add it to the description, if not empty
    if (auto msg = IsAHash(request->dispatch_info().hash()); msg) {
        logger_->Emit(LogLevel::Error, "{}", *msg);
        return ::grpc::Status{::grpc::StatusCode::INVALID_ARGUMENT, *msg};
    }
    auto const& dispatch_info_digest = ArtifactDigest{request->dispatch_info()};
    auto res = GetDispatchList(dispatch_info_digest);
    if (not res) {
        auto err = move(res).error();
        logger_->Emit(LogLevel::Error, "{}", err.error_message());
        return err;
    }
    // keep dispatch list, as it needs to be passed to the executor (via the
    // graph_traverser)
    auto dispatch_list = *res;
    // parse dispatch list to json and add to description
    auto dispatch_json = nlohmann::json::array();
    try {
        for (auto const& [props, endpoint] : dispatch_list) {
            auto entry = nlohmann::json::array();
            entry.push_back(nlohmann::json(props));
            entry.push_back(endpoint.ToJson());
            dispatch_json.push_back(entry);
        }
    } catch (std::exception const& ex) {
        logger_->Emit(LogLevel::Info,
                      "Parsing dispatch list to JSON failed with:\n{}",
                      ex.what());
    }
    if (not dispatch_json.empty()) {
        description["endpoint dispatch list"] = std::move(dispatch_json);
    }

    // add backend description to CAS;
    // we match the sharding strategy from regular just builds, i.e., allowing
    // fields with invalid UTF-8 characters to be considered for the serialized
    // JSON description, but using the UTF-8 replacement character to solve any
    // decoding errors.
    std::string description_str;
    try {
        description_str = description.dump(
            2, ' ', false, nlohmann::json::error_handler_t::replace);
    } catch (std::exception const& ex) {
        // normally shouldn't happen
        std::string err{"Failed to dump backend JSON description to string"};
        logger_->Emit(LogLevel::Error, err);
        return ::grpc::Status{::grpc::StatusCode::INTERNAL, err};
    }
    auto execution_backend_dgst = storage_.CAS().StoreBlob(description_str);
    if (not execution_backend_dgst) {
        std::string err{
            "Failed to store execution backend description in local CAS"};
        logger_->Emit(LogLevel::Error, err);
        return ::grpc::Status{::grpc::StatusCode::INTERNAL, err};
    }

    // get a target cache instance with the correct computed shard
    auto shard =
        address
            ? std::make_optional(ArtifactDigest(*execution_backend_dgst).hash())
            : std::nullopt;
    auto const& tc = storage_.TargetCache().WithShard(shard);
    auto const& tc_key =
        TargetCacheKey{{target_cache_key_digest, ObjectType::File}};

    // check if target-level cache entry has already been computed
    if (auto target_entry = tc.Read(tc_key); target_entry) {

        // make sure all artifacts referenced in the target cache value are in
        // the remote cas
        std::vector<Artifact::ObjectInfo> artifacts;
        if (!target_entry->first.ToArtifacts(&artifacts)) {
            auto msg = fmt::format(
                "Failed to extract artifacts from target cache entry {}",
                target_entry->second.ToString());
            logger_->Emit(LogLevel::Error, "{}", msg);
            return ::grpc::Status{::grpc::StatusCode::INTERNAL, msg};
        }
        artifacts.emplace_back(target_entry->second);  // add the tc value
        if (not apis_.local->RetrieveToCas(artifacts, *apis_.remote)) {
            auto msg = fmt::format(
                "Failed to upload to remote cas the artifacts referenced in "
                "the target cache entry {}",
                target_entry->second.ToString());
            logger_->Emit(LogLevel::Error, "{}", msg);
            return ::grpc::Status{::grpc::StatusCode::UNAVAILABLE, msg};
        }
        // populate response with the target cache value
        response->mutable_target_value()->CopyFrom(target_entry->second.digest);
        return ::grpc::Status::OK;
    }

    // get target description from remote cas
    auto const& target_cache_key_info = Artifact::ObjectInfo{
        .digest = target_cache_key_digest, .type = ObjectType::File};

    if (not apis_.local->IsAvailable(target_cache_key_digest) and
        not apis_.remote->RetrieveToCas({target_cache_key_info},
                                        *apis_.local)) {
        auto msg = fmt::format(
            "Could not retrieve blob {} from remote-execution endpoint",
            target_cache_key_info.ToString());
        logger_->Emit(LogLevel::Error, "{}", msg);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, msg};
    }

    auto const& target_description_str =
        apis_.local->RetrieveToMemory(target_cache_key_info);
    if (not target_description_str) {
        // this should not fail unless something really broke...
        auto msg = fmt::format(
            "Unexpected failure in retrieving blob {} from local CAS",
            target_cache_key_info.ToString());
        logger_->Emit(LogLevel::Error, "{}", msg);
        return ::grpc::Status{::grpc::StatusCode::INTERNAL, msg};
    }

    ExpressionPtr target_description_dict{};
    try {
        target_description_dict = Expression::FromJson(
            nlohmann::json::parse(*target_description_str));
    } catch (std::exception const& ex) {
        auto msg = fmt::format("Parsing TargetCacheKey {} failed with:\n{}",
                               target_cache_key_digest.hash(),
                               ex.what());
        logger_->Emit(LogLevel::Error, "{}", msg);
        return ::grpc::Status{::grpc::StatusCode::INTERNAL, msg};
    }
    if (not target_description_dict.IsNotNull() or
        not target_description_dict->IsMap()) {
        auto msg =
            fmt::format("TargetCacheKey {} should contain a map, but found {}",
                        target_cache_key_digest.hash(),
                        target_description_dict.ToJson().dump());
        logger_->Emit(LogLevel::Error, "{}", msg);
        return ::grpc::Status{::grpc::StatusCode::NOT_FOUND, msg};
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
            logger_->Emit(LogLevel::Error, "{}", error_msg);
            return false;
        }
        return true;
    };

    if (!check_key("repo_key") or !check_key("target_name") or
        !check_key("effective_config")) {
        return ::grpc::Status{::grpc::StatusCode::NOT_FOUND, error_msg};
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
        logger_->Emit(LogLevel::Error, "{}", msg);
        return ::grpc::Status{::grpc::StatusCode::NOT_FOUND, msg};
    }
    ArtifactDigest repo_key_dgst{repo_key->String(), 0, /*is_tree=*/false};
    if (not apis_.local->IsAvailable(repo_key_dgst) and
        not apis_.remote->RetrieveToCas(
            {Artifact::ObjectInfo{.digest = repo_key_dgst,
                                  .type = ObjectType::File}},
            *apis_.local)) {
        auto msg = fmt::format(
            "Could not retrieve blob {} from remote-execution endpoint",
            repo_key->String());
        logger_->Emit(LogLevel::Error, "{}", msg);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, msg};
    }
    auto repo_config_path =
        storage_.CAS().BlobPath(repo_key_dgst, /*is_executable=*/false);
    if (not repo_config_path) {
        // This should not fail unless something went really bad...
        auto msg = fmt::format(
            "Unexpected failure in retrieving blob {} from local CAS",
            repo_key->String());
        logger_->Emit(LogLevel::Error, "{}", msg);
        return ::grpc::Status{::grpc::StatusCode::INTERNAL, msg};
    }

    // populate the RepositoryConfig instance
    RepositoryConfig repository_config{};
    std::string const main_repo{"0"};  // known predefined main repository name
    if (auto msg = DetermineRoots(serve_config_,
                                  storage_config_,
                                  main_repo,
                                  *repo_config_path,
                                  &repository_config,
                                  logger_)) {
        logger_->Emit(LogLevel::Error, "{}", *msg);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, *msg};
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
        logger_->Emit(LogLevel::Error, "{}", msg);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, msg};
    }
    auto target_name = nlohmann::json::object();
    try {
        target_name = nlohmann::json::parse(target_expr->String());
    } catch (std::exception const& ex) {
        auto msg = fmt::format(
            "TargetCacheKey {}: parsing \"target_name\" failed with:\n{}",
            target_cache_key_digest.hash(),
            ex.what());
        logger_->Emit(LogLevel::Error, "{}", msg);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, msg};
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
        logger_->Emit(LogLevel::Error, "{}", msg);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, msg};
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
        logger_->Emit(LogLevel::Error, "{}", msg);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, msg};
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
        logger_->Emit(LogLevel::Error, "{}", error_msg);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION,
                              error_msg};
    }

    BuildMaps::Target::ResultTargetMap result_map{serve_config_.jobs};
    auto configured_target = BuildMaps::Target::ConfiguredTarget{
        .target = std::move(*entity), .config = std::move(config)};

    // setup progress reporting; these instances need to be kept alive for
    // graph traversal, analysis, and build
    Statistics stats{};
    Progress progress{};

    // setup logging for analysis and build; write into a temporary file
    auto tmp_dir = storage_config_.CreateTypedTmpDir("serve-target");
    if (!tmp_dir) {
        auto msg = std::string("Could not create TmpDir");
        logger_->Emit(LogLevel::Error, msg);
        return ::grpc::Status{::grpc::StatusCode::INTERNAL, msg};
    }
    std::filesystem::path tmp_log{tmp_dir->GetPath() / "log"};
    Logger logger{"serve-target", {LogSinkFile::CreateFactory(tmp_log)}};

    AnalyseContext analyse_ctx{.repo_config = &repository_config,
                               .storage = &storage_,
                               .statistics = &stats,
                               .progress = &progress,
                               .serve = serve_};

    // analyse the configured target
    auto result = AnalyseTarget(&analyse_ctx,
                                configured_target,
                                &result_map,
                                serve_config_.jobs,
                                std::nullopt /*request_action_input*/,
                                &logger);

    if (not result) {
        // report failure locally, to keep track of it...
        auto msg = fmt::format("Failed to analyse target {}",
                               configured_target.target.ToString());
        logger_->Emit(LogLevel::Warning, "{}", msg);
        return HandleFailureLog(tmp_log, "analysis", response);
    }
    logger_->Emit(LogLevel::Info, "Analysed target {}", result->id.ToString());

    // get the output artifacts
    auto const [artifacts, runfiles] = ReadOutputArtifacts(result->target);

    // get the result map outputs
    auto const& [actions, blobs, trees] =
        result_map.ToResult(&stats, &progress, &logger);

    // collect cache targets and artifacts for target-level caching
    auto const cache_targets = result_map.CacheTargets();
    auto cache_artifacts = CollectNonKnownArtifacts(cache_targets);

    // Clean up result map, now that it is no longer needed
    {
        TaskSystem ts{serve_config_.jobs};
        result_map.Clear(&ts);
    }

    auto jobs = serve_config_.build_jobs;
    if (jobs == 0) {
        jobs = serve_config_.jobs;
    }

    {
        // setup graph traverser
        GraphTraverser::CommandLineArguments traverser_args{};
        traverser_args.jobs = jobs;
        traverser_args.build.timeout = serve_config_.action_timeout;
        traverser_args.stage = std::nullopt;
        traverser_args.rebuild = std::nullopt;

        // Use a new ApiBundle that knows about local repository config for
        // traversing.
        ApiBundle const local_apis{&storage_config_,
                                   &storage_,
                                   &repository_config,
                                   &apis_.auth,
                                   address};
        GraphTraverser const traverser{
            std::move(traverser_args),
            &repository_config,
            std::move(platform_properties),
            std::move(dispatch_list),
            &stats,
            &progress,
            &local_apis,
            ProgressReporter::Reporter(&stats, &progress, &logger),
            &logger};

        // perform build
        auto build_result = traverser.BuildAndStage(artifacts,
                                                    runfiles,
                                                    actions,
                                                    blobs,
                                                    trees,
                                                    std::move(cache_artifacts));

        if (not build_result) {
            // report failure locally, to keep track of it...
            logger_->Emit(LogLevel::Warning,
                          "Build for target {} failed",
                          configured_target.target.ToString());
            return HandleFailureLog(tmp_log, "build", response);
        }

        WriteTargetCacheEntries(cache_targets,
                                build_result->extra_infos,
                                jobs,
                                local_apis,
                                serve_config_.tc_strategy,
                                tc,
                                &logger,
                                LogLevel::Error);

        if (build_result->failed_artifacts) {
            // report failure locally, to keep track of it...
            logger_->Emit(
                LogLevel::Warning,
                "Build result for target {} contains failed artifacts ",
                configured_target.target.ToString());
            return HandleFailureLog(tmp_log, "artifacts", response);
        }
    }

    // now that the target cache key is in, make sure remote CAS has all
    // required entries
    if (auto target_entry = tc.Read(tc_key); target_entry) {
        // make sure all artifacts referenced in the target cache value are in
        // the remote cas
        std::vector<Artifact::ObjectInfo> tc_artifacts;
        if (!target_entry->first.ToArtifacts(&tc_artifacts)) {
            auto msg = fmt::format(
                "Failed to extract artifacts from target cache entry {}",
                target_entry->second.ToString());
            logger_->Emit(LogLevel::Error, "{}", msg);
            return ::grpc::Status{::grpc::StatusCode::INTERNAL, msg};
        }
        tc_artifacts.emplace_back(target_entry->second);  // add the tc value
        if (not apis_.local->RetrieveToCas(tc_artifacts, *apis_.remote)) {
            auto msg = fmt::format(
                "Failed to upload to remote cas the artifacts referenced in "
                "the target cache entry {}",
                target_entry->second.ToString());
            logger_->Emit(LogLevel::Error, "{}", msg);
            return ::grpc::Status{::grpc::StatusCode::UNAVAILABLE, msg};
        }
        // populate response with the target cache value
        response->mutable_target_value()->CopyFrom(target_entry->second.digest);
        return ::grpc::Status::OK;
    }

    // target cache value missing -- internally something is very wrong
    auto msg = fmt::format("Failed to read TargetCacheKey {} after store",
                           target_cache_key_digest.hash());
    logger_->Emit(LogLevel::Error, "{}", msg);
    return ::grpc::Status{::grpc::StatusCode::INTERNAL, msg};
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
            storage_config_.GitRoot(), root_tree, target_file, logger_)) {
        tree_found = true;
        if (res->first) {
            if (not res->second) {
                // tree exists, but does not contain target file
                auto err = fmt::format(
                    "Target-root {} found, but does not contain targets file "
                    "{}",
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
        for (auto const& path : serve_config_.known_repositories) {
            if (auto res =
                    GetBlobContent(path, root_tree, target_file, logger_)) {
                tree_found = true;
                if (res->first) {
                    if (not res->second) {
                        // tree exists, but does not contain target file
                        auto err = fmt::format(
                            "Target-root {} found, but does not contain "
                            "targets file {}",
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
                fmt::format("Could not read targets file {}", target_file);
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
            "Failed to parse targets file {} as json with error:\n{}",
            target_file,
            e.what());
        logger_->Emit(LogLevel::Error, err);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    if (not map->IsMap()) {
        auto err =
            fmt::format("Targets file {} should contain a map, but found:\n{}",
                        target_file,
                        map->ToString());
        logger_->Emit(LogLevel::Error, "{}", err);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    // do validity checks (target present, is export, flexible_config valid)
    auto target_desc = map->At(target);
    if (not target_desc) {
        // target is not present
        auto err = fmt::format("Missing target {} in targets file {}",
                               nlohmann::json(target).dump(),
                               target_file);
        logger_->Emit(LogLevel::Error, "{}", err);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    auto export_desc = target_desc->get()->At("type");
    if (not export_desc) {
        auto err = fmt::format(
            "Missing \"type\" field for target {} in targets file {}.",
            nlohmann::json(target).dump(),
            target_file);
        logger_->Emit(LogLevel::Error, "{}", err);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    if (not export_desc->get()->IsString()) {
        auto err = fmt::format(
            "Expected field \"type\" for target {} in targets file {} to be a "
            "string, but found:\n{}",
            nlohmann::json(target).dump(),
            target_file,
            export_desc->get()->ToString());
        logger_->Emit(LogLevel::Error, "{}", err);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    if (export_desc->get()->String() != "export") {
        // target is not of "type" : "export"
        auto err = fmt::format(R"(target {} is not of "type" : "export")",
                               nlohmann::json(target).dump());
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    auto flexible_config = target_desc->get()->At("flexible_config");
    if (not flexible_config) {
        // respond with success and an empty flexible_config list
        return ::grpc::Status::OK;
    }
    if (not flexible_config->get()->IsList()) {
        auto err = fmt::format(
            "Field \"flexible_config\" for target {} in targets file {} should "
            "be a list, but found {}",
            nlohmann::json(target).dump(),
            target_file,
            flexible_config->get()->ToString());
        logger_->Emit(LogLevel::Error, "{}", err);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    // populate response with flexible_config list
    auto flexible_config_list = flexible_config->get()->List();
    for (auto const& elem : flexible_config_list) {
        if (not elem->IsString()) {
            auto err = fmt::format(
                "Field \"flexible_config\" for target {} in targets file {} "
                "has non-string entry {}",
                nlohmann::json(target).dump(),
                target_file,
                elem->ToString());
            logger_->Emit(LogLevel::Error, "{}", err);
            response->clear_flexible_config();  // must be unset
            return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
        }
        response->add_flexible_config(elem->String());
    }
    // respond with success
    return ::grpc::Status::OK;
}

auto TargetService::ServeTargetDescription(
    ::grpc::ServerContext* /*context*/,
    const ::justbuild::just_serve::ServeTargetDescriptionRequest* request,
    ::justbuild::just_serve::ServeTargetDescriptionResponse* response)
    -> ::grpc::Status {
    auto const& root_tree{request->root_tree()};
    auto const& target_file{request->target_file()};
    auto const& target{request->target()};
    // retrieve content of target file
    std::optional<std::string> target_file_content{std::nullopt};
    bool tree_found{false};
    // try in local build root Git cache
    if (auto res = GetBlobContent(
            storage_config_.GitRoot(), root_tree, target_file, logger_)) {
        tree_found = true;
        if (res->first) {
            if (not res->second) {
                // tree exists, but does not contain target file
                auto err = fmt::format(
                    "Target-root {} found, but does not contain targets file "
                    "{}",
                    root_tree,
                    target_file);
                logger_->Emit(LogLevel::Error, "{}", err);
                return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION,
                                      err};
            }
            target_file_content = *res->second;
        }
    }
    if (not target_file_content) {
        // try given extra repositories, in order
        for (auto const& path : serve_config_.known_repositories) {
            if (auto res =
                    GetBlobContent(path, root_tree, target_file, logger_)) {
                tree_found = true;
                if (res->first) {
                    if (not res->second) {
                        // tree exists, but does not contain target file
                        auto err = fmt::format(
                            "Target-root {} found, but does not contain "
                            "targets file {}",
                            root_tree,
                            target_file);
                        logger_->Emit(LogLevel::Error, "{}", err);
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
                fmt::format("Could not read targets file {}", target_file);
            logger_->Emit(LogLevel::Error, "{}", err);
            return ::grpc::Status{::grpc::StatusCode::INTERNAL, err};
        }
        // tree not found
        auto err = fmt::format("Missing target-root tree {}", root_tree);
        logger_->Emit(LogLevel::Error, "{}", err);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    // parse target file as json
    ExpressionPtr map{nullptr};
    try {
        map = Expression::FromJson(nlohmann::json::parse(*target_file_content));
    } catch (std::exception const& e) {
        auto err = fmt::format(
            "Failed to parse targets file {} as json with error:\n{}",
            target_file,
            e.what());
        logger_->Emit(LogLevel::Error, "{}", err);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    if (not map->IsMap()) {
        auto err =
            fmt::format("Targets file {} should contain a map, but found:\n{}",
                        target_file,
                        map->ToString());
        logger_->Emit(LogLevel::Error, "{}", err);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    // do validity checks (target is present and is of "type": "export")
    auto target_desc = map->At(target);
    if (not target_desc) {
        // target is not present
        auto err = fmt::format("Missing target {} in targets file {}",
                               nlohmann::json(target).dump(),
                               target_file);
        logger_->Emit(LogLevel::Error, "{}", err);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    auto export_desc = target_desc->get()->At("type");
    if (not export_desc) {
        auto err = fmt::format(
            "Missing \"type\" field for target {} in targets file {}.",
            nlohmann::json(target).dump(),
            target_file);
        logger_->Emit(LogLevel::Error, "{}", err);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    if (not export_desc->get()->IsString()) {
        auto err = fmt::format(
            "Expected field \"type\" for target {} in targets file {} to be a "
            "string, but found:\n{}",
            nlohmann::json(target).dump(),
            target_file,
            export_desc->get()->ToString());
        logger_->Emit(LogLevel::Error, "{}", err);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    if (export_desc->get()->String() != "export") {
        // target is not of "type" : "export"
        auto err = fmt::format(R"(target {} is not of "type" : "export")",
                               nlohmann::json(target).dump());
        logger_->Emit(LogLevel::Error, "{}", err);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    // populate response description object with fields as-is
    auto desc = nlohmann::json::object();
    if (auto doc = target_desc->get()->Get("doc", Expression::none_t{});
        doc.IsNotNull()) {
        desc["doc"] = doc->ToJson();
    }
    if (auto config_doc =
            target_desc->get()->Get("config_doc", Expression::none_t{});
        config_doc.IsNotNull()) {
        desc["config_doc"] = config_doc->ToJson();
    }
    if (auto flexible_config =
            target_desc->get()->Get("flexible_config", Expression::none_t{});
        flexible_config.IsNotNull()) {
        desc["flexible_config"] = flexible_config->ToJson();
    }

    // acquire lock for CAS
    auto lock = GarbageCollector::SharedLock(storage_config_);
    if (!lock) {
        auto error_msg = fmt::format("Could not acquire gc SharedLock");
        logger_->Emit(LogLevel::Error, error_msg);
        return ::grpc::Status{::grpc::StatusCode::INTERNAL, error_msg};
    }

    // store description blob to local CAS and sync with remote CAS;
    // we keep the documentation strings as close to actual as possible, so we
    // do not fail if they contain invalid UTF-8 characters, instead we use the
    // UTF-8 replacement character to solve any decoding errors.
    std::string description_str;
    try {
        description_str =
            desc.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
    } catch (std::exception const& ex) {
        // normally shouldn't happen
        std::string err{"Failed to dump backend JSON description to string"};
        logger_->Emit(LogLevel::Error, err);
        return ::grpc::Status{::grpc::StatusCode::INTERNAL, err};
    }
    if (auto dgst = storage_.CAS().StoreBlob(description_str,
                                             /*is_executable=*/false)) {
        auto const& artifact_dgst = ArtifactDigest{*dgst};
        if (not apis_.local->RetrieveToCas(
                {Artifact::ObjectInfo{.digest = artifact_dgst,
                                      .type = ObjectType::File}},
                *apis_.remote)) {
            auto error_msg = fmt::format(
                "Failed to upload to remote cas the description blob {}",
                artifact_dgst.hash());
            logger_->Emit(LogLevel::Error, "{}", error_msg);
            return ::grpc::Status{::grpc::StatusCode::UNAVAILABLE, error_msg};
        }

        // populate response
        response->mutable_description_id()->CopyFrom(*dgst);
        return ::grpc::Status::OK;
    }
    // failed to store blob
    const auto* const error_msg =
        "Failed to store description blob to local cas";
    logger_->Emit(LogLevel::Error, error_msg);
    return ::grpc::Status{::grpc::StatusCode::INTERNAL, error_msg};
}

#endif  // BOOTSTRAP_BUILD_TOOL
