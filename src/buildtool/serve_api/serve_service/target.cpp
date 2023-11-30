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

#include <string>
#include <utility>

#include "fmt/core.h"
#include "fmt/format.h"
#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/build_engine/expression/expression_ptr.hpp"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/file_system/git_cas.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/serve_api/remote/config.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/buildtool/storage/target_cache_key.hpp"
#include "src/utils/cpp/verify_hash.hpp"

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

    auto const& target_description_dict =
        nlohmann::json::parse(*target_description_str);

    // utility function to check the correctness of the TargetCacheKey
    [[maybe_unused]] std::string error_msg;
    auto check_key = [&target_description_dict,
                      this,
                      &target_cache_key_digest,
                      &error_msg](std::string const& key) -> bool {
        if (!target_description_dict.contains(key)) {
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

    // TODO(asartori): coordinate the build on the remote
    // for now we return an error
    const auto* msg = "orchestration of remote build not yet implemented";
    logger_->Emit(LogLevel::Error, msg);
    return grpc::Status{grpc::StatusCode::UNIMPLEMENTED, msg};
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
