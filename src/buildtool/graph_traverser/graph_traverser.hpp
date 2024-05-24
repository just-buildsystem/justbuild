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

#ifndef INCLUDED_SRC_BUILDTOOL_GRAPH_TRAVERSER_GRAPH_TRAVERSER_HPP
#define INCLUDED_SRC_BUILDTOOL_GRAPH_TRAVERSER_GRAPH_TRAVERSER_HPP

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "fmt/core.h"
#include "gsl/gsl"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/cli.hpp"
#include "src/buildtool/common/remote/remote_common.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/common/statistics.hpp"
#include "src/buildtool/common/tree.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_blob_container.hpp"
#include "src/buildtool/execution_api/common/artifact_blob_container.hpp"
#include "src/buildtool/execution_api/common/common_api.hpp"
#include "src/buildtool/execution_api/common/create_execution_api.hpp"
#include "src/buildtool/execution_api/local/local_api.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_api.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/execution_api/utils/subobject.hpp"
#include "src/buildtool/execution_engine/dag/dag.hpp"
#include "src/buildtool/execution_engine/executor/executor.hpp"
#include "src/buildtool/execution_engine/traverser/traverser.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/jsonfs.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/log_sink_cmdline.hpp"
#include "src/buildtool/logging/log_sink_file.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/progress_reporting/base_progress_reporter.hpp"
#include "src/utils/cpp/json.hpp"

class GraphTraverser {
  public:
    struct CommandLineArguments {
        std::size_t jobs;
        BuildArguments build;
        std::optional<StageArguments> stage;
        std::optional<RebuildArguments> rebuild;
    };

    struct BuildResult {
        std::vector<std::filesystem::path> output_paths;
        // Object infos of extra artifacts requested to build.
        std::unordered_map<ArtifactDescription, Artifact::ObjectInfo>
            extra_infos;
        bool failed_artifacts;
    };

    explicit GraphTraverser(
        CommandLineArguments clargs,
        gsl::not_null<const RepositoryConfig*> const& repo_config,
        std::map<std::string, std::string> platform_properties,
        std::vector<std::pair<std::map<std::string, std::string>,
                              ServerAddress>> dispatch_list,
        gsl::not_null<Statistics*> const& stats,
        gsl::not_null<Progress*> const& progress)
        : clargs_{std::move(clargs)},
          repo_config_{repo_config},
          platform_properties_{std::move(platform_properties)},
          dispatch_list_{std::move(dispatch_list)},
          stats_{stats},
          progress_{progress},
          local_api_{CreateExecutionApi(std::nullopt,
                                        std::make_optional(repo_config))},
          remote_api_{CreateExecutionApi(RemoteExecutionConfig::RemoteAddress(),
                                         std::make_optional(repo_config))},
          reporter_{[](auto done, auto cv) {}} {}

    explicit GraphTraverser(
        CommandLineArguments clargs,
        gsl::not_null<const RepositoryConfig*> const& repo_config,
        std::map<std::string, std::string> platform_properties,
        std::vector<std::pair<std::map<std::string, std::string>,
                              ServerAddress>> dispatch_list,
        gsl::not_null<Statistics*> const& stats,
        gsl::not_null<Progress*> const& progress,
        progress_reporter_t reporter,
        Logger const* logger = nullptr)
        : clargs_{std::move(clargs)},
          repo_config_{repo_config},
          platform_properties_{std::move(platform_properties)},
          dispatch_list_{std::move(dispatch_list)},
          stats_{stats},
          progress_{progress},
          local_api_{CreateExecutionApi(std::nullopt,
                                        std::make_optional(repo_config))},
          remote_api_{CreateExecutionApi(RemoteExecutionConfig::RemoteAddress(),
                                         std::make_optional(repo_config))},
          reporter_{std::move(reporter)},
          logger_{logger} {}

    /// \brief Parses actions and blobs into graph, traverses it and retrieves
    /// outputs specified by command line arguments.
    /// \param artifact_descriptions Artifacts to build (and stage).
    /// \param runfile_descriptions  Runfiles to build (and stage).
    /// \param action_descriptions   All required actions for building.
    /// \param blobs                 Blob artifacts to upload before the build.
    /// \param trees                 Tree artifacts to compute graph nodes from.
    /// \param extra_artifacts       Extra artifacts to obtain object infos for.
    [[nodiscard]] auto BuildAndStage(
        std::map<std::string, ArtifactDescription> const& artifact_descriptions,
        std::map<std::string, ArtifactDescription> const& runfile_descriptions,
        std::vector<ActionDescription::Ptr> const& action_descriptions,
        std::vector<std::string> const& blobs,
        std::vector<Tree::Ptr> const& trees,
        std::vector<ArtifactDescription>&& extra_artifacts = {}) const
        -> std::optional<BuildResult> {
        DependencyGraph graph;  // must outlive artifact_nodes
        auto artifacts = BuildArtifacts(&graph,
                                        artifact_descriptions,
                                        runfile_descriptions,
                                        action_descriptions,
                                        trees,
                                        blobs,
                                        extra_artifacts);
        if (not artifacts) {
            return std::nullopt;
        }
        auto [rel_paths, artifact_nodes, extra_nodes] = *artifacts;

        auto const object_infos = CollectObjectInfos(artifact_nodes, logger_);
        auto extra_infos = CollectObjectInfos(extra_nodes, logger_);
        if (not object_infos or not extra_infos) {
            return std::nullopt;
        }

        Expects(extra_artifacts.size() == extra_infos->size());
        std::unordered_map<ArtifactDescription, Artifact::ObjectInfo> infos;
        infos.reserve(extra_infos->size());
        std::transform(
            std::make_move_iterator(extra_artifacts.begin()),
            std::make_move_iterator(extra_artifacts.end()),
            std::make_move_iterator(extra_infos->begin()),
            std::inserter(infos, infos.end()),
            std::make_pair<ArtifactDescription&&, Artifact::ObjectInfo&&>);

        bool failed_artifacts = std::any_of(
            object_infos->begin(), object_infos->end(), [](auto const& info) {
                return info.failed;
            });

        if (not clargs_.stage) {
            PrintOutputs("Artifacts built, logical paths are:",
                         rel_paths,
                         artifact_nodes,
                         runfile_descriptions);
            MaybePrintToStdout(rel_paths, artifact_nodes);
            return BuildResult{.output_paths = std::move(rel_paths),
                               .extra_infos = std::move(infos),
                               .failed_artifacts = failed_artifacts};
        }

        if (clargs_.stage->remember) {
            if (not remote_api_->ParallelRetrieveToCas(
                    *object_infos, GetLocalApi(), clargs_.jobs, true)) {
                Logger::Log(logger_,
                            LogLevel::Warning,
                            "Failed to copy objects to CAS");
            }
        }

        auto output_paths = RetrieveOutputs(rel_paths, *object_infos);
        if (not output_paths) {
            return std::nullopt;
        }
        PrintOutputs("Artifacts can be found in:",
                     *output_paths,
                     artifact_nodes,
                     runfile_descriptions);

        MaybePrintToStdout(rel_paths, artifact_nodes);

        return BuildResult{.output_paths = *output_paths,
                           .extra_infos = std::move(infos),
                           .failed_artifacts = failed_artifacts};
    }

    /// \brief Parses graph description into graph, traverses it and retrieves
    /// outputs specified by command line arguments
    [[nodiscard]] auto BuildAndStage(
        std::filesystem::path const& graph_description,
        nlohmann::json const& artifacts) const -> std::optional<BuildResult> {
        // Read blobs to upload and actions from graph description file
        auto desc = ReadGraphDescription(graph_description, logger_);
        if (not desc) {
            return std::nullopt;
        }
        auto const [blobs, tree_descs, actions] = *desc;

        std::vector<ActionDescription::Ptr> action_descriptions{};
        action_descriptions.reserve(actions.size());
        for (auto const& [id, description] : actions.items()) {
            auto action = ActionDescription::FromJson(id, description);
            if (not action) {
                return std::nullopt;  // Error already logged
            }
            action_descriptions.emplace_back(std::move(*action));
        }

        std::vector<Tree::Ptr> trees{};
        for (auto const& [id, description] : tree_descs.items()) {
            auto tree = Tree::FromJson(id, description);
            if (not tree) {
                return std::nullopt;
            }
            trees.emplace_back(std::move(*tree));
        }

        std::map<std::string, ArtifactDescription> artifact_descriptions{};
        for (auto const& [rel_path, description] : artifacts.items()) {
            auto artifact = ArtifactDescription::FromJson(description);
            if (not artifact) {
                return std::nullopt;  // Error already logged
            }
            artifact_descriptions.emplace(rel_path, std::move(*artifact));
        }

        return BuildAndStage(
            artifact_descriptions, {}, action_descriptions, blobs, trees);
    }

    [[nodiscard]] auto GetLocalApi() const -> gsl::not_null<IExecutionApi*> {
        return &(*local_api_);
    }

    [[nodiscard]] auto GetRemoteApi() const -> gsl::not_null<IExecutionApi*> {
        return &(*remote_api_);
    }

  private:
    CommandLineArguments const clargs_;
    gsl::not_null<const RepositoryConfig*> repo_config_;
    std::map<std::string, std::string> platform_properties_;
    std::vector<std::pair<std::map<std::string, std::string>, ServerAddress>>
        dispatch_list_;
    gsl::not_null<Statistics*> stats_;
    gsl::not_null<Progress*> progress_;
    gsl::not_null<IExecutionApi::Ptr> const local_api_;
    gsl::not_null<IExecutionApi::Ptr> const remote_api_;
    progress_reporter_t reporter_;
    Logger const* logger_{nullptr};

    /// \brief Reads contents of graph description file as json object. In case
    /// the description is missing "blobs" or "actions" key/value pairs or they
    /// can't be retrieved with the appropriate types, execution is terminated
    /// after logging error
    /// \returns A pair containing the blobs to upload (as a vector of strings)
    /// and the actions as a json object.
    [[nodiscard]] static auto ReadGraphDescription(
        std::filesystem::path const& graph_description,
        Logger const* logger)
        -> std::optional<
            std::tuple<nlohmann::json, nlohmann::json, nlohmann::json>> {
        auto const graph_description_opt = Json::ReadFile(graph_description);
        if (not graph_description_opt.has_value()) {
            Logger::Log(logger,
                        LogLevel::Error,
                        "parsing graph from {}",
                        graph_description.string());
            return std::nullopt;
        }
        auto blobs_opt = ExtractValueAs<std::vector<std::string>>(
            *graph_description_opt, "blobs", [logger](std::string const& s) {
                Logger::Log(logger,
                            LogLevel::Error,
                            "{}\ncan not retrieve value for \"blobs\" from "
                            "graph description.",
                            s);
            });
        auto trees_opt = ExtractValueAs<nlohmann::json>(
            *graph_description_opt, "trees", [logger](std::string const& s) {
                Logger::Log(logger,
                            LogLevel::Error,
                            "{}\ncan not retrieve value for \"trees\" from "
                            "graph description.",
                            s);
            });
        auto actions_opt = ExtractValueAs<nlohmann::json>(
            *graph_description_opt, "actions", [logger](std::string const& s) {
                Logger::Log(logger,
                            LogLevel::Error,
                            "{}\ncan not retrieve value for \"actions\" from "
                            "graph description.",
                            s);
            });
        if (not blobs_opt or not trees_opt or not actions_opt) {
            return std::nullopt;
        }
        return std::make_tuple(std::move(*blobs_opt),
                               std::move(*trees_opt),
                               std::move(*actions_opt));
    }

    /// \brief Requires for the executor to upload blobs to CAS. In the case any
    /// of the uploads fails, execution is terminated
    /// \param[in]  blobs   blobs to be uploaded
    [[nodiscard]] auto UploadBlobs(
        std::vector<std::string> const& blobs) const noexcept -> bool {
        ArtifactBlobContainer container;
        for (auto const& blob : blobs) {
            auto digest = ArtifactDigest::Create<ObjectType::File>(blob);
            Logger::Log(logger_, LogLevel::Trace, [&]() {
                return fmt::format(
                    "Uploaded blob {}, its digest has id {} and size {}.",
                    nlohmann::json(blob).dump(),
                    digest.hash(),
                    digest.size());
            });
            // Store and/or upload blob, taking into account the maximum
            // transfer size.
            if (not UpdateContainerAndUpload<ArtifactDigest>(
                    &container,
                    ArtifactBlob{std::move(digest), blob, /*is_exec=*/false},
                    /*exception_is_fatal=*/true,
                    [&api = remote_api_](ArtifactBlobContainer&& blobs) {
                        return api->Upload(std::move(blobs));
                    },
                    logger_)) {
                return false;
            }
        }
        // Upload remaining blobs.
        return remote_api_->Upload(std::move(container));
    }

    /// \brief Adds the artifacts to be retrieved to the graph
    /// \param[in]  g   dependency graph
    /// \param[in]  artifacts   output artifact map
    /// \param[in]  runfiles    output runfile map
    /// \returns    pair of vectors where the first vector contains the absolute
    /// paths to which the artifacts will be retrieved and the second one
    /// contains the ids of the artifacts to be retrieved
    [[nodiscard]] static auto AddArtifactsToRetrieve(
        gsl::not_null<DependencyGraph*> const& g,
        std::map<std::string, ArtifactDescription> const& artifacts,
        std::map<std::string, ArtifactDescription> const& runfiles)
        -> std::optional<std::pair<std::vector<std::filesystem::path>,
                                   std::vector<ArtifactIdentifier>>> {
        std::vector<std::filesystem::path> rel_paths;
        std::vector<ArtifactIdentifier> ids;
        auto total_size = artifacts.size() + runfiles.size();
        rel_paths.reserve(total_size);
        ids.reserve(total_size);
        auto add_and_get_info =
            [&g, &rel_paths, &ids](
                std::map<std::string, ArtifactDescription> const& descriptions)
            -> bool {
            for (auto const& [rel_path, artifact] : descriptions) {
                rel_paths.emplace_back(rel_path);
                ids.emplace_back(g->AddArtifact(artifact));
            }
            return true;
        };
        if (add_and_get_info(artifacts) and add_and_get_info(runfiles)) {
            return std::make_pair(std::move(rel_paths), std::move(ids));
        }
        return std::nullopt;
    }

    /// \brief Traverses the graph. In case any of the artifact ids
    /// specified by the command line arguments is duplicated, execution is
    /// terminated.
    [[nodiscard]] auto Traverse(
        DependencyGraph const& g,
        std::vector<ArtifactIdentifier> const& artifact_ids) const -> bool {
        Executor executor{repo_config_,
                          &(*local_api_),
                          &(*remote_api_),
                          platform_properties_,
                          dispatch_list_,
                          stats_,
                          progress_,
                          logger_,
                          clargs_.build.timeout};
        bool traversing{};
        std::atomic<bool> done = false;
        std::atomic<bool> failed = false;
        std::condition_variable cv{};
        auto observer =
            std::thread([this, &done, &cv]() { reporter_(&done, &cv); });
        {
            Traverser t{executor, g, clargs_.jobs, &failed};
            traversing =
                t.Traverse({std::begin(artifact_ids), std::end(artifact_ids)});
        }
        done = true;
        cv.notify_all();
        observer.join();
        return traversing and not failed;
    }

    [[nodiscard]] auto TraverseRebuild(
        DependencyGraph const& g,
        std::vector<ArtifactIdentifier> const& artifact_ids) const -> bool {
        // setup rebuilder with api for cache endpoint
        auto api_cached =
            CreateExecutionApi(RemoteExecutionConfig::CacheAddress(),
                               std::make_optional(repo_config_));
        Rebuilder executor{repo_config_,
                           &(*local_api_),
                           &(*remote_api_),
                           &(*api_cached),
                           platform_properties_,
                           dispatch_list_,
                           stats_,
                           progress_,
                           clargs_.build.timeout};
        bool traversing{false};
        std::atomic<bool> done = false;
        std::atomic<bool> failed = false;
        std::condition_variable cv{};
        auto observer =
            std::thread([this, &done, &cv]() { reporter_(&done, &cv); });
        {
            Traverser t{executor, g, clargs_.jobs, &failed};
            traversing =
                t.Traverse({std::begin(artifact_ids), std::end(artifact_ids)});
        }
        done = true;
        cv.notify_all();
        observer.join();

        if (traversing and not failed and clargs_.rebuild->dump_flaky) {
            std::ofstream file{*clargs_.rebuild->dump_flaky};
            file << executor.DumpFlakyActions().dump(2);
        }
        return traversing and not failed;
    }

    /// \brief Retrieves nodes corresponding to artifacts with ids in artifacts.
    /// In case any of the identifiers doesn't correspond to a node inside the
    /// graph, we write out error message and stop execution with failure code
    [[nodiscard]] static auto GetArtifactNodes(
        DependencyGraph const& g,
        std::vector<ArtifactIdentifier> const& artifact_ids,
        Logger const* logger) noexcept
        -> std::optional<std::vector<DependencyGraph::ArtifactNode const*>> {
        std::vector<DependencyGraph::ArtifactNode const*> nodes{};

        for (auto const& art_id : artifact_ids) {
            auto const* node = g.ArtifactNodeWithId(art_id);
            if (node == nullptr) {
                Logger::Log(logger,
                            LogLevel::Error,
                            "Artifact {} not found in graph.",
                            art_id);
                return std::nullopt;
            }
            nodes.push_back(node);
        }
        return nodes;
    }

    void LogStatistics() const noexcept {
        if (clargs_.rebuild) {
            std::stringstream ss{};
            ss << stats_->RebuiltActionComparedCounter()
               << " actions compared with cache";
            if (stats_->ActionsFlakyCounter() > 0) {
                ss << ", " << stats_->ActionsFlakyCounter()
                   << " flaky actions found";
                ss << " (" << stats_->ActionsFlakyTaintedCounter()
                   << " of which tainted)";
            }
            if (stats_->RebuiltActionMissingCounter() > 0) {
                ss << ", no cache entry found for "
                   << stats_->RebuiltActionMissingCounter() << " actions";
            }
            ss << ".";
            Logger::Log(logger_, LogLevel::Info, ss.str());
        }
        else {
            Logger::Log(logger_,
                        LogLevel::Info,
                        "Processed {} actions, {} cache hits.",
                        stats_->ActionsQueuedCounter(),
                        stats_->ActionsCachedCounter());
        }
    }

    [[nodiscard]] auto BuildArtifacts(
        gsl::not_null<DependencyGraph*> const& graph,
        std::map<std::string, ArtifactDescription> const& artifacts,
        std::map<std::string, ArtifactDescription> const& runfiles,
        std::vector<ActionDescription::Ptr> const& actions,
        std::vector<Tree::Ptr> const& trees,
        std::vector<std::string> const& blobs,
        std::vector<ArtifactDescription> const& extra_artifacts = {}) const
        -> std::optional<
            std::tuple<std::vector<std::filesystem::path>,
                       std::vector<DependencyGraph::ArtifactNode const*>,
                       std::vector<DependencyGraph::ArtifactNode const*>>> {
        if (not UploadBlobs(blobs)) {
            return std::nullopt;
        }

        auto artifact_infos =
            AddArtifactsToRetrieve(graph, artifacts, runfiles);
        if (not artifact_infos) {
            return std::nullopt;
        }
        auto& [output_paths, artifact_ids] = *artifact_infos;

        // Add extra artifacts to ids to build
        artifact_ids.reserve(artifact_ids.size() + extra_artifacts.size());
        for (auto const& artifact : extra_artifacts) {
            artifact_ids.emplace_back(graph->AddArtifact(artifact));
        }

        std::vector<ActionDescription> tree_actions{};
        tree_actions.reserve(trees.size());
        for (auto const& tree : trees) {
            tree_actions.emplace_back(tree->Action());
        }

        if (not graph->Add(actions) or not graph->Add(tree_actions)) {
            Logger::Log(logger_, LogLevel::Error, [&actions]() {
                auto json = nlohmann::json::array();
                for (auto const& desc : actions) {
                    json.push_back(desc->ToJson());
                }
                return fmt::format(
                    "could not build the dependency graph from the actions "
                    "described in {}.",
                    json.dump());
            });
            return std::nullopt;
        }

        if (clargs_.rebuild ? not TraverseRebuild(*graph, artifact_ids)
                            : not Traverse(*graph, artifact_ids)) {
            Logger::Log(logger_, LogLevel::Error, "Build failed.");
            return std::nullopt;
        }

        LogStatistics();

        auto artifact_nodes = GetArtifactNodes(*graph, artifact_ids, logger_);
        if (not artifact_nodes) {
            return std::nullopt;
        }

        // split extra artifacts' nodes from artifact nodes
        auto extra_nodes = std::vector<DependencyGraph::ArtifactNode const*>{
            std::make_move_iterator(artifact_nodes->begin() +
                                    output_paths.size()),
            std::make_move_iterator(artifact_nodes->end())};
        artifact_nodes->erase(artifact_nodes->begin() + output_paths.size(),
                              artifact_nodes->end());

        return std::make_tuple(std::move(output_paths),
                               std::move(*artifact_nodes),
                               std::move(extra_nodes));
    }

    [[nodiscard]] auto PrepareOutputPaths(
        std::vector<std::filesystem::path> const& rel_paths) const
        -> std::optional<std::vector<std::filesystem::path>> {
        std::vector<std::filesystem::path> output_paths{};
        output_paths.reserve(rel_paths.size());
        for (auto const& rel_path : rel_paths) {
            output_paths.emplace_back(clargs_.stage->output_dir / rel_path);
        }
        return output_paths;
    }

    [[nodiscard]] static auto CollectObjectInfos(
        std::vector<DependencyGraph::ArtifactNode const*> const& artifact_nodes,
        Logger const* logger)
        -> std::optional<std::vector<Artifact::ObjectInfo>> {
        std::vector<Artifact::ObjectInfo> object_infos;
        object_infos.reserve(artifact_nodes.size());
        for (auto const* art_ptr : artifact_nodes) {
            auto const& info = art_ptr->Content().Info();
            if (info) {
                object_infos.push_back(*info);
            }
            else {
                Logger::Log(logger,
                            LogLevel::Error,
                            "artifact {} could not be retrieved, it can not be "
                            "found in CAS.",
                            art_ptr->Content().Id());
                return std::nullopt;
            }
        }
        return object_infos;
    }

    /// \brief Asks execution API to copy output artifacts to paths specified by
    /// command line arguments and writes location info. In case the executor
    /// couldn't retrieve any of the outputs, execution is terminated.
    [[nodiscard]] auto RetrieveOutputs(
        std::vector<std::filesystem::path> const& rel_paths,
        std::vector<Artifact::ObjectInfo> const& object_infos) const
        -> std::optional<std::vector<std::filesystem::path>> {
        // Create output directory
        if (not FileSystemManager::CreateDirectory(clargs_.stage->output_dir)) {
            return std::nullopt;  // Message logged in the file system manager
        }

        auto output_paths = PrepareOutputPaths(rel_paths);

        if (not output_paths or
            not remote_api_->RetrieveToPaths(
                object_infos, *output_paths, &(*GetLocalApi()))) {
            Logger::Log(
                logger_, LogLevel::Error, "Could not retrieve outputs.");
            return std::nullopt;
        }

        return std::move(*output_paths);
    }

    void PrintOutputs(
        std::string message,
        std::vector<std::filesystem::path> const& paths,
        std::vector<DependencyGraph::ArtifactNode const*> const& artifact_nodes,
        std::map<std::string, ArtifactDescription> const& runfiles) const {
        std::string msg_dbg{"Artifact ids:"};
        std::string msg_failed{"Failed artifacts:"};
        bool failed{false};
        nlohmann::json json{};
        for (std::size_t pos = 0; pos < paths.size(); ++pos) {
            auto path = paths[pos].string();
            auto id = IdentifierToString(artifact_nodes[pos]->Content().Id());
            if (clargs_.build.show_runfiles or
                not runfiles.contains(clargs_.stage
                                          ? std::filesystem::proximate(
                                                path, clargs_.stage->output_dir)
                                                .string()
                                          : path)) {
                auto info = artifact_nodes[pos]->Content().Info();
                if (info) {
                    message += fmt::format("\n  {} {}", path, info->ToString());
                    if (info->failed) {
                        msg_failed +=
                            fmt::format("\n  {} {}", path, info->ToString());
                        failed = true;
                    }
                    if (clargs_.build.dump_artifacts) {
                        json[path] = info->ToJson();
                    }
                }
                else {
                    Logger::Log(logger_,
                                LogLevel::Error,
                                "Missing info for artifact {}.",
                                id);
                }
            }
            msg_dbg += fmt::format("\n  {}: {}", path, id);
        }

        if (not clargs_.build.show_runfiles and !runfiles.empty()) {
            message += fmt::format("\n({} runfiles omitted.)", runfiles.size());
        }

        Logger::Log(logger_, LogLevel::Info, "{}", message);
        Logger::Log(logger_, LogLevel::Debug, "{}", msg_dbg);
        if (failed) {
            Logger::Log(logger_, LogLevel::Info, "{}", msg_failed);
        }

        if (clargs_.build.dump_artifacts) {
            if (*clargs_.build.dump_artifacts == "-") {
                std::cout << std::setw(2) << json << std::endl;
            }
            else {
                std::ofstream os(*clargs_.build.dump_artifacts);
                os << std::setw(2) << json << std::endl;
            }
        }
    }

    void MaybePrintToStdout(
        std::vector<std::filesystem::path> const& paths,
        std::vector<DependencyGraph::ArtifactNode const*> const& artifacts)
        const {
        if (clargs_.build.print_to_stdout) {
            for (std::size_t i = 0; i < paths.size(); i++) {
                if (paths[i] == *(clargs_.build.print_to_stdout)) {
                    auto info = artifacts[i]->Content().Info();
                    if (info) {
                        if (not remote_api_->RetrieveToFds(
                                {*info},
                                {dup(fileno(stdout))},
                                /*raw_tree=*/false)) {
                            Logger::Log(logger_,
                                        LogLevel::Error,
                                        "Failed to retrieve {}",
                                        *(clargs_.build.print_to_stdout));
                        }
                    }
                    else {
                        Logger::Log(
                            logger_,
                            LogLevel::Error,
                            "Failed to obtain object information for {}",
                            *(clargs_.build.print_to_stdout));
                    }
                    return;
                }
            }
            // Not directly an artifact, hence check if the path is contained in
            // some artifact
            auto target_path = ToNormalPath(std::filesystem::path{
                                                *clargs_.build.print_to_stdout})
                                   .relative_path();
            auto remote = GetRemoteApi();
            for (std::size_t i = 0; i < paths.size(); i++) {
                auto const& path = paths[i];
                auto relpath = target_path.lexically_relative(path);
                if ((not relpath.empty()) and *relpath.begin() != "..") {
                    Logger::Log(
                        logger_,
                        LogLevel::Info,
                        "'{}' not a direct logical path of the specified "
                        "target; will take subobject '{}' of '{}'",
                        *(clargs_.build.print_to_stdout),
                        relpath.string(),
                        path.string());
                    auto info = artifacts[i]->Content().Info();
                    if (info) {
                        auto new_info =
                            RetrieveSubPathId(*info, remote, relpath);
                        if (new_info) {
                            if (not remote_api_->RetrieveToFds(
                                    {*new_info},
                                    {dup(fileno(stdout))},
                                    /*raw_tree=*/false)) {
                                Logger::Log(logger_,
                                            LogLevel::Error,
                                            "Failed to retrieve artifact {} at "
                                            "path '{}' of '{}'",
                                            new_info->ToString(),
                                            relpath.string(),
                                            path.string());
                            }
                        }
                    }
                    else {
                        Logger::Log(
                            logger_,
                            LogLevel::Error,
                            "Failed to obtain object information for {}",
                            *(clargs_.build.print_to_stdout));
                    }
                    return;
                }
            }
            Logger::Log(logger_,
                        LogLevel::Warning,
                        "{} not a logical path of the specified target",
                        *(clargs_.build.print_to_stdout));
        }
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_GRAPH_TRAVERSER_GRAPH_TRAVERSER_HPP
