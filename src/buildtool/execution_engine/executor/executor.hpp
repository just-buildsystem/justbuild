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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_ENGINE_EXECUTOR_EXECUTOR_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_ENGINE_EXECUTOR_EXECUTOR_HPP

#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <type_traits>
#include <utility>
#include <vector>

#include "gsl/gsl"
#include "src/buildtool/common/remote/remote_common.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/common/statistics.hpp"
#include "src/buildtool/common/tree.hpp"
#include "src/buildtool/compatibility/compatibility.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_api.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/execution_engine/dag/dag.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/progress_reporting/progress.hpp"
#include "src/utils/cpp/hex_string.hpp"

/// \brief Implementations for executing actions and uploading artifacts.
class ExecutorImpl {
  public:
    /// \brief Execute action and obtain response.
    /// \returns std::nullopt for actions without response (e.g., tree actions).
    /// \returns nullptr on error.
    [[nodiscard]] static auto ExecuteAction(
        Logger const& logger,
        gsl::not_null<DependencyGraph::ActionNode const*> const& action,
        gsl::not_null<IExecutionApi*> const& api,
        std::map<std::string, std::string> const& properties,
        std::vector<std::pair<std::map<std::string, std::string>,
                              ServerAddress>> const& dispatch_list,
        std::chrono::milliseconds const& timeout,
        IExecutionAction::CacheFlag cache_flag,
        gsl::not_null<Statistics*> const& stats,
        gsl::not_null<Progress*> const& progress)
        -> std::optional<IExecutionResponse::Ptr> {
        auto const& inputs = action->Dependencies();
        auto const tree_action = action->Content().IsTreeAction();

        logger.Emit(LogLevel::Trace, [&inputs, tree_action]() {
            std::ostringstream oss{};
            oss << "execute " << (tree_action ? "tree " : "") << "action"
                << std::endl;
            for (auto const& [local_path, artifact] : inputs) {
                auto const& info = artifact->Content().Info();
                oss << fmt::format(
                           " - needs {} {}",
                           local_path,
                           info ? info->ToString() : std::string{"[???]"})
                    << std::endl;
            }
            return oss.str();
        });

        auto const root_digest = CreateRootDigest(api, inputs);
        if (not root_digest) {
            Logger::Log(LogLevel::Error,
                        "failed to create root digest for input artifacts.");
            return nullptr;
        }

        if (tree_action) {
            auto const& tree_artifact = action->OutputDirs()[0].node->Content();
            bool failed_inputs = false;
            for (auto const& [local_path, artifact] : inputs) {
                failed_inputs |= artifact->Content().Info()->failed;
            }
            tree_artifact.SetObjectInfo(
                *root_digest, ObjectType::Tree, failed_inputs);
            return std::nullopt;
        }

        // do not count statistics for rebuilder fetching from cache
        if (cache_flag != IExecutionAction::CacheFlag::FromCacheOnly) {
            progress->TaskTracker().Start(action->Content().Id());
            stats->IncrementActionsQueuedCounter();
        }

        auto alternative_api =
            GetAlternativeEndpoint(properties, dispatch_list);
        if (alternative_api) {
            if (not api->ParallelRetrieveToCas(
                    std::vector<Artifact::ObjectInfo>{Artifact::ObjectInfo{
                        *root_digest, ObjectType::Tree, /* failed= */ false}},
                    &(*alternative_api),
                    /* jobs= */ 1,
                    /* use_blob_splitting= */ true)) {
                Logger::Log(LogLevel::Error,
                            "Failed to sync tree {} to dispatch endpoint",
                            root_digest->hash());
                return nullptr;
            }
        }

        auto remote_action = (alternative_api ? &(*alternative_api) : api)
                                 ->CreateAction(*root_digest,
                                                action->Command(),
                                                action->OutputFilePaths(),
                                                action->OutputDirPaths(),
                                                action->Env(),
                                                properties);

        if (remote_action == nullptr) {
            logger.Emit(LogLevel::Error,
                        "failed to create action for execution.");
            return nullptr;
        }

        // set action options
        remote_action->SetCacheFlag(cache_flag);
        remote_action->SetTimeout(timeout);
        auto result = remote_action->Execute(&logger);
        if (alternative_api) {
            if (result) {
                auto const& artifacts = result->Artifacts();
                std::vector<Artifact::ObjectInfo> object_infos{};
                object_infos.reserve(artifacts.size());
                for (auto const& [path, info] : artifacts) {
                    object_infos.emplace_back(info);
                }
                if (not alternative_api->RetrieveToCas(object_infos, api)) {
                    Logger::Log(LogLevel::Warning,
                                "Failed to retrieve back artifacts from "
                                "dispatch endpoint");
                }
            }
        }
        return result;
    }

    /// \brief Ensures the artifact is available to the CAS, either checking
    /// that its existing digest corresponds to that of an object already
    /// available or by uploading it if there is no digest in the artifact. In
    /// the later case, the new digest is saved in the artifact
    /// \param[in] artifact The artifact to process.
    /// \returns True if artifact is available at the point of return, false
    /// otherwise
    [[nodiscard]] static auto VerifyOrUploadArtifact(
        Logger const& logger,
        gsl::not_null<DependencyGraph::ArtifactNode const*> const& artifact,
        gsl::not_null<RepositoryConfig*> const& repo_config,
        gsl::not_null<IExecutionApi*> const& remote_api,
        gsl::not_null<IExecutionApi*> const& local_api) noexcept -> bool {
        auto const object_info_opt = artifact->Content().Info();
        auto const file_path_opt = artifact->Content().FilePath();
        // If there is no object info and no file path, the artifact can not be
        // processed: it means its definition is ill-formed or that it is the
        // output of an action, in which case it shouldn't have reached here
        if (not object_info_opt and not file_path_opt) {
            Logger::Log(LogLevel::Error,
                        "artifact {} can not be processed.",
                        ToHexString(artifact->Content().Id()));
            return false;
        }
        // If the artifact has digest, we check that an object with this digest
        // is available to the execution API
        if (object_info_opt) {
            logger.Emit(LogLevel::Trace, [&object_info_opt]() {
                std::ostringstream oss{};
                oss << fmt::format("upload KNOWN artifact: {}",
                                   object_info_opt->ToString())
                    << std::endl;
                return oss.str();
            });
            if (not remote_api->IsAvailable(object_info_opt->digest)) {
                // Check if requested artifact is available in local CAS and
                // upload to remote CAS in case it is.
                if (local_api->IsAvailable(object_info_opt->digest) and
                    local_api->RetrieveToCas({*object_info_opt}, remote_api)) {
                    return true;
                }

                if (not VerifyOrUploadKnownArtifact(
                        remote_api,
                        artifact->Content().Repository(),
                        repo_config,
                        *object_info_opt)) {
                    Logger::Log(
                        LogLevel::Error,
                        "artifact {} should be present in CAS but is missing.",
                        ToHexString(artifact->Content().Id()));
                    return false;
                }
            }
            return true;
        }

        // Otherwise, we upload the new file to make it available to the
        // execution API
        // Note that we can be sure now that file_path_opt has a value and
        // that the path stored is relative to the workspace dir, so we need to
        // prepend it
        logger.Emit(LogLevel::Trace, [&file_path_opt]() {
            std::ostringstream oss{};
            oss << fmt::format("upload LOCAL artifact: {}",
                               file_path_opt->string())
                << std::endl;
            return oss.str();
        });
        auto repo = artifact->Content().Repository();
        auto new_info =
            UploadFile(remote_api, repo, repo_config, *file_path_opt);
        if (not new_info) {
            Logger::Log(LogLevel::Error,
                        "artifact in {} could not be uploaded to CAS.",
                        file_path_opt->string());
            return false;
        }

        // And we save the digest object type in the artifact
        artifact->Content().SetObjectInfo(*new_info, false);
        return true;
    }

    /// \brief Uploads the content of a git tree recursively to the CAS. It is
    /// first checked which elements of a directory are not available in the
    /// CAS and the missing elements are uploaded accordingly. This ensures the
    /// invariant that if a git tree is known to the CAS all its content is also
    /// existing in the CAS.
    /// \param[in] api      The remote execution API of the CAS.
    /// \param[in] tree     The git tree to be uploaded.
    /// \returns True if the upload was successful, False in case of any error.
    // NOLINTNEXTLINE(misc-no-recursion)
    [[nodiscard]] static auto VerifyOrUploadTree(
        gsl::not_null<IExecutionApi*> const& api,
        GitTree const& tree) noexcept -> bool {

        // create list of digests for batch check of CAS availability
        std::vector<ArtifactDigest> digests;
        std::unordered_map<ArtifactDigest, gsl::not_null<GitTreeEntryPtr>>
            entry_map;
        for (auto const& [path, entry] : tree) {
            auto digest =
                ArtifactDigest{entry->Hash(), *entry->Size(), entry->IsTree()};
            digests.emplace_back(digest);
            try {
                entry_map.emplace(std::move(digest), entry);
            } catch (...) {
                return false;
            }
        }

        Logger::Log(LogLevel::Trace, [&tree]() {
            std::ostringstream oss{};
            oss << "upload directory content of " << tree.Hash() << std::endl;
            for (auto const& [path, entry] : tree) {
                oss << fmt::format(" - {}: {}", path, entry->Hash())
                    << std::endl;
            }
            return oss.str();
        });

        // find missing digests
        auto missing_digests = api->IsAvailable(digests);

        // process missing trees
        for (auto const& digest : missing_digests) {
            if (auto it = entry_map.find(digest); it != entry_map.end()) {
                auto const& entry = it->second;
                if (entry->IsTree()) {
                    auto const& tree = entry->Tree();
                    if (not tree or not VerifyOrUploadTree(api, *tree)) {
                        return false;
                    }
                }
            }
        }

        // upload missing entries (blobs or trees)
        BlobContainer container;
        for (auto const& digest : missing_digests) {
            if (auto it = entry_map.find(digest); it != entry_map.end()) {
                auto const& entry = it->second;
                auto content = entry->RawData();
                if (not content) {
                    return false;
                }
                try {
                    container.Emplace(std::move(
                        BazelBlob{digest,
                                  std::move(*content),
                                  IsExecutableObject(entry->Type())}));
                } catch (std::exception const& ex) {
                    Logger::Log(LogLevel::Error,
                                "failed to create blob with: ",
                                ex.what());
                    return false;
                }
            }
        }

        return api->Upload(container, /*skip_find_missing=*/true);
    }

    /// \brief Lookup blob via digest in local git repositories and upload.
    /// \param api          The endpoint used for uploading
    /// \param repo         The global repository name, the artifact belongs to
    /// \param info         The info of the object
    /// \param hash         The git-sha1 hash of the object
    /// \returns true on success
    [[nodiscard]] static auto VerifyOrUploadGitArtifact(
        gsl::not_null<IExecutionApi*> const& api,
        std::string const& repo,
        gsl::not_null<RepositoryConfig*> const& repo_config,
        Artifact::ObjectInfo const& info,
        std::string const& hash) noexcept -> bool {
        std::optional<std::string> content;
        if (NativeSupport::IsTree(
                static_cast<bazel_re::Digest>(info.digest).hash())) {
            // if known tree is not available, recursively upload its content
            auto tree = ReadGitTree(repo, repo_config, hash);
            if (not tree) {
                Logger::Log(
                    LogLevel::Error, "failed to read git tree {}", hash);
                return false;
            }
            if (not VerifyOrUploadTree(api, *tree)) {
                Logger::Log(LogLevel::Error,
                            "failed to verifyorupload git tree {} [{}]",
                            tree->Hash(),
                            hash);
                return false;
            }
            content = tree->RawData();
        }
        else {
            // if known blob is not available, read and upload it
            content = ReadGitBlob(repo, repo_config, hash);
        }
        if (not content) {
            Logger::Log(LogLevel::Error, "failed to get content");
            return false;
        }

        // upload artifact content
        auto container = BlobContainer{{BazelBlob{
            info.digest, std::move(*content), IsExecutableObject(info.type)}}};
        return api->Upload(container, /*skip_find_missing=*/true);
    }

    [[nodiscard]] static auto ReadGitBlob(
        std::string const& repo,
        gsl::not_null<RepositoryConfig*> const& repo_config,
        std::string const& hash) noexcept -> std::optional<std::string> {
        std::optional<std::string> blob{};
        if (auto const* ws_root = repo_config->WorkspaceRoot(repo)) {
            // try to obtain blob from local workspace's Git CAS, if any
            blob = ws_root->ReadBlob(hash);
        }
        if (not blob) {
            // try to obtain blob from global Git CAS, if any
            blob = repo_config->ReadBlobFromGitCAS(hash);
        }
        return blob;
    }

    [[nodiscard]] static auto ReadGitTree(
        std::string const& repo,
        gsl::not_null<RepositoryConfig*> const& repo_config,
        std::string const& hash) noexcept -> std::optional<GitTree> {
        std::optional<GitTree> tree{};
        if (auto const* ws_root = repo_config->WorkspaceRoot(repo)) {
            // try to obtain tree from local workspace's Git CAS, if any
            tree = ws_root->ReadTree(hash);
        }
        if (not tree) {
            // try to obtain tree from global Git CAS, if any
            tree = repo_config->ReadTreeFromGitCAS(hash);
        }
        return tree;
    }

    /// \brief Lookup blob via digest in local git repositories and upload.
    /// \param api          The endpoint used for uploading
    /// \param repo         The global repository name, the artifact belongs to
    /// \param repo_config  Configuration specifying the workspace root
    /// \param info         The info of the object
    /// \returns true on success
    [[nodiscard]] static auto VerifyOrUploadKnownArtifact(
        gsl::not_null<IExecutionApi*> const& api,
        std::string const& repo,
        gsl::not_null<RepositoryConfig*> const& repo_config,
        Artifact::ObjectInfo const& info) noexcept -> bool {
        if (Compatibility::IsCompatible()) {
            auto opt = Compatibility::GetGitEntry(info.digest.hash());
            if (opt) {
                auto const& [git_sha1_hash, comp_repo] = *opt;
                return VerifyOrUploadGitArtifact(
                    api, comp_repo, repo_config, info, git_sha1_hash);
            }
            return false;
        }
        return VerifyOrUploadGitArtifact(
            api, repo, repo_config, info, info.digest.hash());
    }

    /// \brief Lookup file via path in local workspace root and upload.
    /// \param api          The endpoint used for uploading
    /// \param repo         The global repository name, the artifact belongs to
    /// \param repo_config  Configuration specifying the workspace root
    /// \param file_path    The path of the file to be read
    /// \returns The computed object info on success
    [[nodiscard]] static auto UploadFile(
        gsl::not_null<IExecutionApi*> const& api,
        std::string const& repo,
        gsl::not_null<RepositoryConfig*> const& repo_config,
        std::filesystem::path const& file_path) noexcept
        -> std::optional<Artifact::ObjectInfo> {
        auto const* ws_root = repo_config->WorkspaceRoot(repo);
        if (ws_root == nullptr) {
            return std::nullopt;
        }
        auto const object_type = ws_root->BlobType(file_path);
        if (not object_type) {
            return std::nullopt;
        }
        auto content = ws_root->ReadContent(file_path);
        if (not content.has_value()) {
            return std::nullopt;
        }
        auto digest = ArtifactDigest::Create<ObjectType::File>(*content);
        if (not api->Upload(
                BlobContainer{{BazelBlob{digest,
                                         std::move(*content),
                                         IsExecutableObject(*object_type)}}})) {
            return std::nullopt;
        }
        return Artifact::ObjectInfo{.digest = std::move(digest),
                                    .type = *object_type};
    }

    /// \brief Add digests and object type to artifact nodes for all outputs of
    /// the action that was run
    void static SaveObjectInfo(
        IExecutionResponse::ArtifactInfos const& artifacts,
        gsl::not_null<DependencyGraph::ActionNode const*> const& action,
        bool fail_artifacts) noexcept {
        for (auto const& [name, node] : action->OutputFiles()) {
            node->Content().SetObjectInfo(artifacts.at(name), fail_artifacts);
        }
        for (auto const& [name, node] : action->OutputDirs()) {
            node->Content().SetObjectInfo(artifacts.at(name), fail_artifacts);
        }
    }

    /// \brief Create root tree digest for input artifacts.
    /// \param api          The endpoint required for uploading
    /// \param artifacts    The artifacts to create the root tree digest from
    [[nodiscard]] static auto CreateRootDigest(
        gsl::not_null<IExecutionApi*> const& api,
        std::vector<DependencyGraph::NamedArtifactNodePtr> const&
            artifacts) noexcept -> std::optional<ArtifactDigest> {
        if (artifacts.size() == 1 and
            (artifacts.at(0).path == "." or artifacts.at(0).path.empty())) {
            auto const& info = artifacts.at(0).node->Content().Info();
            if (info and IsTreeObject(info->type)) {
                // Artifact list contains single tree with path "." or "". Reuse
                // the existing tree artifact by returning its digest.
                return info->digest;
            }
        }
        return api->UploadTree(artifacts);
    }
    /// \brief Check that all outputs expected from the action description
    /// are present in the artifacts map
    [[nodiscard]] static auto CheckOutputsExist(
        IExecutionResponse::ArtifactInfos const& artifacts,
        std::vector<Action::LocalPath> const& outputs) noexcept -> bool {
        return std::all_of(
            outputs.begin(), outputs.end(), [&artifacts](auto const& output) {
                return artifacts.contains(output);
            });
    }

    /// \brief Parse response and write object info to DAG's artifact nodes.
    /// \returns false on non-zero exit code or if output artifacts are missing
    [[nodiscard]] static auto ParseResponse(
        Logger const& logger,
        IExecutionResponse::Ptr const& response,
        gsl::not_null<DependencyGraph::ActionNode const*> const& action,
        gsl::not_null<Statistics*> const& stats,
        gsl::not_null<Progress*> const& progress,
        bool count_as_executed = false) -> bool {
        logger.Emit(LogLevel::Trace, "finished execution");

        if (!response) {
            logger.Emit(LogLevel::Trace, "response is empty");
            return false;
        }

        if (not count_as_executed and response->IsCached()) {
            logger.Emit(LogLevel::Trace, " - served from cache");
            stats->IncrementActionsCachedCounter();
        }
        else {
            stats->IncrementActionsExecutedCounter();
        }
        progress->TaskTracker().Stop(action->Content().Id());

        PrintInfo(logger, action->Command(), response);
        bool should_fail_outputs = false;
        for (auto const& [local_path, node] : action->Dependencies()) {
            should_fail_outputs |= node->Content().Info()->failed;
        }
        if (response->ExitCode() != 0) {
            if (action->MayFail()) {
                logger.Emit(LogLevel::Warning,
                            "{} (exit code {})",
                            *(action->MayFail()),
                            response->ExitCode());
                should_fail_outputs = true;
            }
            else {
                logger.Emit(LogLevel::Error,
                            "action returned non-zero exit code {}",
                            response->ExitCode());
                PrintError(logger, action, progress);
                return false;
            }
        }

        auto artifacts = response->Artifacts();
        auto output_files = action->OutputFilePaths();
        auto output_dirs = action->OutputDirPaths();

        if (artifacts.empty() or
            not CheckOutputsExist(artifacts, output_files) or
            not CheckOutputsExist(artifacts, output_dirs)) {
            logger.Emit(LogLevel::Error, [&] {
                std::string message{
                    "action executed with missing outputs.\n"
                    " Action outputs should be the following artifacts:"};
                for (auto const& output : output_files) {
                    message += "\n  - " + output;
                }
                return message;
            });
            PrintError(logger, action, progress);
            return false;
        }

        SaveObjectInfo(artifacts, action, should_fail_outputs);

        return true;
    }

    /// \brief Write out if response is empty and otherwise, write out
    /// standard error/output if they are present
    void static PrintInfo(Logger const& logger,
                          std::vector<std::string> const& command,
                          IExecutionResponse::Ptr const& response) noexcept {
        if (!response) {
            logger.Emit(LogLevel::Error, "response is empty");
            return;
        }
        auto const has_err = response->HasStdErr();
        auto const has_out = response->HasStdOut();
        auto build_message =
            [has_err, has_out, &logger, &command, &response]() {
                using namespace std::string_literals;
                auto message = ""s;
                if (has_err or has_out) {
                    message += (has_err and has_out ? "Stdout and stderr"s
                                : has_out           ? "Stdout"s
                                                    : "Stderr"s) +
                               " of command: ";
                }
                message += nlohmann::json(command).dump() + "\n";
                if (response->HasStdOut()) {
                    message += response->StdOut();
                }
                if (response->HasStdErr()) {
                    message += response->StdErr();
                }
                return message;
            };
        logger.Emit((has_err or has_out) ? LogLevel::Info : LogLevel::Debug,
                    std::move(build_message));
    }

    void static PrintError(
        Logger const& logger,
        gsl::not_null<DependencyGraph::ActionNode const*> const& action,
        gsl::not_null<Progress*> const& progress) noexcept {
        std::ostringstream msg{};
        msg << "Failed to execute command ";
        msg << nlohmann::json(action->Command()).dump();
        auto const& origin_map = progress->OriginMap();
        auto origins = origin_map.find(action->Content().Id());
        if (origins != origin_map.end() and !origins->second.empty()) {
            msg << "\nrequested by";
            for (auto const& origin : origins->second) {
                msg << "\n - ";
                msg << origin.first.ToString();
                msg << "#";
                msg << origin.second;
            }
        }
        logger.Emit(LogLevel::Error, "{}", msg.str());
    }

    [[nodiscard]] static inline auto ScaleTime(std::chrono::milliseconds t,
                                               double f)
        -> std::chrono::milliseconds {
        return std::chrono::milliseconds(std::lround(t.count() * f));
    }

    [[nodiscard]] static inline auto MergeProperties(
        const std::map<std::string, std::string>& base,
        const std::map<std::string, std::string>& overlay) {
        std::map<std::string, std::string> result = base;
        for (auto const& [k, v] : overlay) {
            result[k] = v;
        }
        return result;
    }

  private:
    [[nodiscard]] static inline auto GetAlternativeEndpoint(
        const std::map<std::string, std::string>& properties,
        const std::vector<std::pair<std::map<std::string, std::string>,
                                    ServerAddress>>& dispatch_list)
        -> std::unique_ptr<BazelApi> {
        for (auto const& [pred, endpoint] : dispatch_list) {
            bool match = true;
            for (auto const& [k, v] : pred) {
                auto v_it = properties.find(k);
                if (not(v_it != properties.end() and v_it->second == v)) {
                    match = false;
                }
            }
            if (match) {
                Logger::Log(LogLevel::Debug, [endpoint = endpoint] {
                    return fmt::format("Dispatching action to endpoint {}",
                                       endpoint.ToJson().dump());
                });
                ExecutionConfiguration config;
                return std::make_unique<BazelApi>(
                    "alternative remote execution",
                    endpoint.host,
                    endpoint.port,
                    config);
            }
        }
        return nullptr;
    }
};

/// \brief Executor for using concrete Execution API.
class Executor {
    using Impl = ExecutorImpl;
    using CF = IExecutionAction::CacheFlag;

  public:
    explicit Executor(
        gsl::not_null<RepositoryConfig*> const& repo_config,
        gsl::not_null<IExecutionApi*> const& local_api,
        gsl::not_null<IExecutionApi*> const& remote_api,
        std::map<std::string, std::string> properties,
        std::vector<std::pair<std::map<std::string, std::string>,
                              ServerAddress>> dispatch_list,
        gsl::not_null<Statistics*> const& stats,
        gsl::not_null<Progress*> const& progress,
        std::chrono::milliseconds timeout = IExecutionAction::kDefaultTimeout)
        : repo_config_{repo_config},
          local_api_{local_api},
          remote_api_{remote_api},
          properties_{std::move(properties)},
          dispatch_list_{std::move(dispatch_list)},
          stats_{stats},
          progress_{progress},
          timeout_{timeout} {}

    /// \brief Run an action in a blocking manner
    /// This method must be thread-safe as it could be called in parallel
    /// \param[in] action The action to execute.
    /// \returns True if execution was successful, false otherwise
    [[nodiscard]] auto Process(
        gsl::not_null<DependencyGraph::ActionNode const*> const& action)
        const noexcept -> bool {
        Logger logger("action:" + action->Content().Id());

        auto const response = Impl::ExecuteAction(
            logger,
            action,
            remote_api_,
            Impl::MergeProperties(properties_, action->ExecutionProperties()),
            dispatch_list_,
            Impl::ScaleTime(timeout_, action->TimeoutScale()),
            action->NoCache() ? CF::DoNotCacheOutput : CF::CacheOutput,
            stats_,
            progress_);

        // check response and save digests of results
        return not response or
               Impl::ParseResponse(
                   logger, *response, action, stats_, progress_);
    }

    /// \brief Check artifact is available to the CAS or upload it.
    /// \param[in] artifact The artifact to process.
    /// \param[in] repo_config The repository configuration to use
    /// \returns True if artifact is available or uploaded, false otherwise
    [[nodiscard]] auto Process(
        gsl::not_null<DependencyGraph::ArtifactNode const*> const& artifact)
        const noexcept -> bool {
        Logger logger("artifact:" + ToHexString(artifact->Content().Id()));
        return Impl::VerifyOrUploadArtifact(
            logger, artifact, repo_config_, remote_api_, local_api_);
    }

  private:
    gsl::not_null<RepositoryConfig*> repo_config_;
    gsl::not_null<IExecutionApi*> local_api_;
    gsl::not_null<IExecutionApi*> remote_api_;
    std::map<std::string, std::string> properties_;
    std::vector<std::pair<std::map<std::string, std::string>, ServerAddress>>
        dispatch_list_;
    gsl::not_null<Statistics*> stats_;
    gsl::not_null<Progress*> progress_;
    std::chrono::milliseconds timeout_;
};

/// \brief Rebuilder for running and comparing actions of two API endpoints.
class Rebuilder {
    using Impl = ExecutorImpl;
    using CF = IExecutionAction::CacheFlag;

  public:
    /// \brief Create rebuilder for action comparision of two endpoints.
    /// \param api          Rebuild endpoint, executes without action cache.
    /// \param api_cached   Reference endpoint, serves everything from cache.
    /// \param properties   Platform properties for execution.
    /// \param timeout      Timeout for action execution.
    Rebuilder(
        gsl::not_null<RepositoryConfig*> const& repo_config,
        gsl::not_null<IExecutionApi*> const& local_api,
        gsl::not_null<IExecutionApi*> const& remote_api,
        gsl::not_null<IExecutionApi*> const& api_cached,
        std::map<std::string, std::string> properties,
        std::vector<std::pair<std::map<std::string, std::string>,
                              ServerAddress>> dispatch_list,
        gsl::not_null<Statistics*> const& stats,
        gsl::not_null<Progress*> const& progress,
        std::chrono::milliseconds timeout = IExecutionAction::kDefaultTimeout)
        : repo_config_{repo_config},
          local_api_{local_api},
          remote_api_{remote_api},
          api_cached_{api_cached},
          properties_{std::move(properties)},
          dispatch_list_{std::move(dispatch_list)},
          stats_{stats},
          progress_{progress},
          timeout_{timeout} {}

    [[nodiscard]] auto Process(
        gsl::not_null<DependencyGraph::ActionNode const*> const& action)
        const noexcept -> bool {
        auto const& action_id = action->Content().Id();
        Logger logger("rebuild:" + action_id);
        auto response = Impl::ExecuteAction(
            logger,
            action,
            remote_api_,
            Impl::MergeProperties(properties_, action->ExecutionProperties()),
            dispatch_list_,
            Impl::ScaleTime(timeout_, action->TimeoutScale()),
            CF::PretendCached,
            stats_,
            progress_);

        if (not response) {
            return true;  // action without response (e.g., tree action)
        }

        Logger logger_cached("cached:" + action_id);
        auto response_cached = Impl::ExecuteAction(
            logger_cached,
            action,
            api_cached_,
            Impl::MergeProperties(properties_, action->ExecutionProperties()),
            dispatch_list_,
            Impl::ScaleTime(timeout_, action->TimeoutScale()),
            CF::FromCacheOnly,
            stats_,
            progress_);

        if (not response_cached) {
            logger_cached.Emit(LogLevel::Error,
                               "expected regular action with response");
            return false;
        }

        DetectFlakyAction(*response, *response_cached, action->Content());
        return Impl::ParseResponse(logger,
                                   *response,
                                   action,
                                   stats_,
                                   progress_,
                                   /*count_as_executed=*/true);
    }

    [[nodiscard]] auto Process(
        gsl::not_null<DependencyGraph::ArtifactNode const*> const& artifact)
        const noexcept -> bool {
        Logger logger("artifact:" + ToHexString(artifact->Content().Id()));
        return Impl::VerifyOrUploadArtifact(
            logger, artifact, repo_config_, remote_api_, local_api_);
    }

    [[nodiscard]] auto DumpFlakyActions() const noexcept -> nlohmann::json {
        std::unique_lock lock{m_};
        auto actions = nlohmann::json::object();
        for (auto const& [action_id, outputs] : flaky_actions_) {
            for (auto const& [path, infos] : outputs) {
                actions[action_id][path]["rebuilt"] = infos.first.ToJson();
                actions[action_id][path]["cached"] = infos.second.ToJson();
            }
        }
        return {{"flaky actions", actions}, {"cache misses", cache_misses_}};
    }

  private:
    gsl::not_null<RepositoryConfig*> repo_config_;
    gsl::not_null<IExecutionApi*> local_api_;
    gsl::not_null<IExecutionApi*> remote_api_;
    gsl::not_null<IExecutionApi*> api_cached_;
    std::map<std::string, std::string> properties_;
    std::vector<std::pair<std::map<std::string, std::string>, ServerAddress>>
        dispatch_list_;
    gsl::not_null<Statistics*> stats_;
    gsl::not_null<Progress*> progress_;
    std::chrono::milliseconds timeout_;
    mutable std::mutex m_;
    mutable std::vector<std::string> cache_misses_{};
    mutable std::unordered_map<
        std::string,
        std::unordered_map<
            std::string,
            std::pair<Artifact::ObjectInfo, Artifact::ObjectInfo>>>
        flaky_actions_{};

    void DetectFlakyAction(IExecutionResponse::Ptr const& response,
                           IExecutionResponse::Ptr const& response_cached,
                           Action const& action) const noexcept {
        if (response and response_cached and
            response_cached->ActionDigest() == response->ActionDigest()) {
            stats_->IncrementRebuiltActionComparedCounter();
            auto artifacts = response->Artifacts();
            auto artifacts_cached = response_cached->Artifacts();
            std::ostringstream msg{};
            for (auto const& [path, info] : artifacts) {
                auto const& info_cached = artifacts_cached[path];
                if (info != info_cached) {
                    RecordFlakyAction(&msg, action, path, info, info_cached);
                }
            }
            if (msg.tellp() > 0) {
                stats_->IncrementActionsFlakyCounter();
                bool tainted = action.MayFail() or action.NoCache();
                if (tainted) {
                    stats_->IncrementActionsFlakyTaintedCounter();
                }
                Logger::Log(tainted ? LogLevel::Debug : LogLevel::Warning,
                            "{}",
                            msg.str());
            }
        }
        else {
            stats_->IncrementRebuiltActionMissingCounter();
            std::unique_lock lock{m_};
            cache_misses_.emplace_back(action.Id());
        }
    }

    void RecordFlakyAction(gsl::not_null<std::ostringstream*> const& msg,
                           Action const& action,
                           std::string const& path,
                           Artifact::ObjectInfo const& rebuilt,
                           Artifact::ObjectInfo const& cached) const noexcept {
        auto const& action_id = action.Id();
        if (msg->tellp() <= 0) {
            bool tainted = action.MayFail() or action.NoCache();
            auto cmd = GetCmdString(action);
            (*msg) << "Found flaky " << (tainted ? "tainted " : "")
                   << "action:" << std::endl
                   << " - id: " << action_id << std::endl
                   << " - cmd: " << cmd << std::endl;
        }
        (*msg) << " - output '" << path << "' differs:" << std::endl
               << "   - " << rebuilt.ToString() << " (rebuilt)" << std::endl
               << "   - " << cached.ToString() << " (cached)" << std::endl;

        std::unique_lock lock{m_};
        auto& object_map = flaky_actions_[action_id];
        try {
            object_map.emplace(path, std::make_pair(rebuilt, cached));
        } catch (std::exception const& ex) {
            Logger::Log(LogLevel::Error,
                        "recoding flaky action failed with: {}",
                        ex.what());
        }
    }

    static auto GetCmdString(Action const& action) noexcept -> std::string {
        try {
            return nlohmann::json(action.Command()).dump();
        } catch (std::exception const& ex) {
            return fmt::format("<exception: {}>", ex.what());
        }
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_ENGINE_EXECUTOR_EXECUTOR_HPP
