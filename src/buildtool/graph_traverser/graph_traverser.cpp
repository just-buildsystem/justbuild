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

#include "src/buildtool/graph_traverser/graph_traverser.hpp"

#ifndef BOOTSTRAP_BUILD_TOOL
#ifdef __unix__
#include <unistd.h>
#else
#error "Non-unix is not supported yet"
#endif

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <thread>
#include <unordered_set>

#include "fmt/core.h"
#include "src/buildtool/common/artifact_blob.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/statistics.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/common/api_bundle.hpp"
#include "src/buildtool/execution_api/common/common_api.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/execution_api/utils/subobject.hpp"
#include "src/buildtool/execution_engine/executor/executor.hpp"
#include "src/buildtool/execution_engine/traverser/traverser.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/jsonfs.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/utils/cpp/expected.hpp"
#include "src/utils/cpp/json.hpp"
#include "src/utils/cpp/path.hpp"

auto GraphTraverser::BuildAndStage(
    std::map<std::string, ArtifactDescription> const& artifact_descriptions,
    std::map<std::string, ArtifactDescription> const& runfile_descriptions,
    std::vector<ActionDescription::Ptr>&& action_descriptions,
    std::vector<std::string>&& blobs,
    std::vector<Tree::Ptr>&& trees,
    std::vector<ArtifactDescription>&& extra_artifacts) const
    -> std::optional<BuildResult> {
    DependencyGraph graph;  // must outlive artifact_nodes
    auto artifacts = BuildArtifacts(&graph,
                                    artifact_descriptions,
                                    runfile_descriptions,
                                    std::move(action_descriptions),
                                    std::move(trees),
                                    std::move(blobs),
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
        MaybePrintToStdout(
            rel_paths,
            artifact_nodes,
            artifact_descriptions.size() == 1
                ? std::optional<std::string>{artifact_descriptions.begin()
                                                 ->first}
                : std::nullopt);
        return BuildResult{.output_paths = std::move(rel_paths),
                           .extra_infos = std::move(infos),
                           .failed_artifacts = failed_artifacts};
    }

    if (clargs_.stage->remember) {
        if (not context_.apis->remote->ParallelRetrieveToCas(
                *object_infos, *context_.apis->local, clargs_.jobs, true)) {
            Logger::Log(
                logger_, LogLevel::Warning, "Failed to copy objects to CAS");
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

    MaybePrintToStdout(
        rel_paths,
        artifact_nodes,
        artifact_descriptions.size() == 1
            ? std::optional<std::string>{artifact_descriptions.begin()->first}
            : std::nullopt);

    return BuildResult{.output_paths = *output_paths,
                       .extra_infos = std::move(infos),
                       .failed_artifacts = failed_artifacts};
}

auto GraphTraverser::BuildAndStage(
    std::filesystem::path const& graph_description,
    nlohmann::json const& artifacts) const -> std::optional<BuildResult> {
    // Read blobs to upload and actions from graph description file
    auto desc = ReadGraphDescription(graph_description, logger_);
    if (not desc) {
        return std::nullopt;
    }
    auto [blobs, tree_descs, actions] = *std::move(desc);

    HashFunction::Type const hash_type = context_.apis->local->GetHashType();
    std::vector<ActionDescription::Ptr> action_descriptions{};
    action_descriptions.reserve(actions.size());
    for (auto const& [id, description] : actions.items()) {
        auto action = ActionDescription::FromJson(hash_type, id, description);
        if (not action) {
            return std::nullopt;  // Error already logged
        }
        action_descriptions.emplace_back(std::move(*action));
    }

    std::vector<Tree::Ptr> trees{};
    for (auto const& [id, description] : tree_descs.items()) {
        auto tree = Tree::FromJson(hash_type, id, description);
        if (not tree) {
            return std::nullopt;
        }
        trees.emplace_back(std::move(*tree));
    }

    std::map<std::string, ArtifactDescription> artifact_descriptions{};
    for (auto const& [rel_path, description] : artifacts.items()) {
        auto artifact = ArtifactDescription::FromJson(hash_type, description);
        if (not artifact) {
            return std::nullopt;  // Error already logged
        }
        artifact_descriptions.emplace(rel_path, std::move(*artifact));
    }

    return BuildAndStage(artifact_descriptions,
                         {},
                         std::move(action_descriptions),
                         std::move(blobs),
                         std::move(trees));
}

auto GraphTraverser::ReadGraphDescription(
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
    return std::make_tuple(
        std::move(*blobs_opt), std::move(*trees_opt), std::move(*actions_opt));
}

auto GraphTraverser::UploadBlobs(
    std::vector<std::string>&& blobs) const noexcept -> bool {
    std::unordered_set<ArtifactBlob> container;
    HashFunction const hash_function{context_.apis->remote->GetHashType()};
    for (auto& content : blobs) {
        auto blob = ArtifactBlob::FromMemory(
            hash_function, ObjectType::File, std::move(content));
        if (not blob.has_value()) {
            logger_->Emit(LogLevel::Trace, "Failed to create ArtifactBlob");
            return false;
        }
        Logger::Log(logger_, LogLevel::Trace, [&]() {
            return fmt::format(
                "Will upload blob, its digest has id {} and size {}.",
                blob->GetDigest().hash(),
                blob->GetDigest().size());
        });
        // Store and/or upload blob, taking into account the maximum
        // transfer size.
        if (not UpdateContainerAndUpload(
                &container,
                *std::move(blob),
                /*exception_is_fatal=*/true,
                [&api = context_.apis->remote](
                    std::unordered_set<ArtifactBlob>&& blobs) {
                    return api->Upload(std::move(blobs));
                },
                logger_)) {
            return false;
        }
    }
    // Upload remaining blobs.
    auto result = context_.apis->remote->Upload(std::move(container));
    Logger::Log(logger_, LogLevel::Trace, [&]() {
        std::stringstream msg{};
        msg << (result ? "Finished" : "Failed") << " upload of\n";
        for (auto const& blob : blobs) {
            msg << " - " << nlohmann::json(blob).dump() << "\n";
        }
        return msg.str();
    });
    return result;
}

auto GraphTraverser::AddArtifactsToRetrieve(
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

auto GraphTraverser::Traverse(
    DependencyGraph const& g,
    std::vector<ArtifactIdentifier> const& artifact_ids) const -> bool {
    Executor executor{&context_, logger_, clargs_.build.timeout};
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

auto GraphTraverser::TraverseRebuild(
    DependencyGraph const& g,
    std::vector<ArtifactIdentifier> const& artifact_ids) const -> bool {
    Rebuilder executor{&context_, clargs_.build.timeout};
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

auto GraphTraverser::GetArtifactNodes(
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

void GraphTraverser::LogStatistics() const noexcept {
    auto& stats = *context_.statistics;
    if (clargs_.rebuild) {
        std::stringstream ss{};
        ss << stats.RebuiltActionComparedCounter()
           << " actions compared with cache";
        if (stats.ActionsFlakyCounter() > 0) {
            ss << ", " << stats.ActionsFlakyCounter() << " flaky actions found";
            ss << " (" << stats.ActionsFlakyTaintedCounter()
               << " of which tainted)";
        }
        if (stats.RebuiltActionMissingCounter() > 0) {
            ss << ", no cache entry found for "
               << stats.RebuiltActionMissingCounter() << " actions";
        }
        ss << ".";
        Logger::Log(logger_, LogLevel::Info, ss.str());
    }
    else {
        Logger::Log(logger_,
                    LogLevel::Info,
                    "Processed {} actions, {} cache hits.",
                    stats.ActionsQueuedCounter(),
                    stats.ActionsCachedCounter());
    }
}

auto GraphTraverser::BuildArtifacts(
    gsl::not_null<DependencyGraph*> const& graph,
    std::map<std::string, ArtifactDescription> const& artifacts,
    std::map<std::string, ArtifactDescription> const& runfiles,
    std::vector<ActionDescription::Ptr>&& actions,
    std::vector<Tree::Ptr>&& trees,
    std::vector<std::string>&& blobs,
    std::vector<ArtifactDescription> const& extra_artifacts) const
    -> std::optional<
        std::tuple<std::vector<std::filesystem::path>,
                   std::vector<DependencyGraph::ArtifactNode const*>,
                   std::vector<DependencyGraph::ArtifactNode const*>>> {
    if (not UploadBlobs(std::move(blobs))) {
        return std::nullopt;
    }

    auto artifact_infos = AddArtifactsToRetrieve(graph, artifacts, runfiles);
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
    auto const it_extra =
        std::next(artifact_nodes->begin(),
                  static_cast<std::int64_t>(output_paths.size()));
    auto extra_nodes = std::vector<DependencyGraph::ArtifactNode const*>{
        std::make_move_iterator(it_extra),
        std::make_move_iterator(artifact_nodes->end())};
    artifact_nodes->erase(it_extra, artifact_nodes->end());

    return std::make_tuple(std::move(output_paths),
                           std::move(*artifact_nodes),
                           std::move(extra_nodes));
}

auto GraphTraverser::PrepareOutputPaths(
    std::vector<std::filesystem::path> const& rel_paths) const
    -> std::optional<std::vector<std::filesystem::path>> {
    std::vector<std::filesystem::path> output_paths{};
    output_paths.reserve(rel_paths.size());
    for (auto const& rel_path : rel_paths) {
        output_paths.emplace_back(clargs_.stage->output_dir / rel_path);
    }
    return output_paths;
}

auto GraphTraverser::CollectObjectInfos(
    std::vector<DependencyGraph::ArtifactNode const*> const& artifact_nodes,
    Logger const* logger) -> std::optional<std::vector<Artifact::ObjectInfo>> {
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

auto GraphTraverser::RetrieveOutputs(
    std::vector<std::filesystem::path> const& rel_paths,
    std::vector<Artifact::ObjectInfo> const& object_infos) const
    -> std::optional<std::vector<std::filesystem::path>> {
    // Create output directory
    if (not FileSystemManager::CreateDirectory(clargs_.stage->output_dir)) {
        return std::nullopt;  // Message logged in the file system manager
    }

    auto output_paths = PrepareOutputPaths(rel_paths);

    if (not output_paths or
        not context_.apis->remote->RetrieveToPaths(
            object_infos, *output_paths, &*context_.apis->local)) {
        Logger::Log(logger_, LogLevel::Error, "Could not retrieve outputs.");
        return std::nullopt;
    }

    return output_paths;
}

void GraphTraverser::PrintOutputs(
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

    if (not clargs_.build.show_runfiles and not runfiles.empty()) {
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

void GraphTraverser::MaybePrintToStdout(
    std::vector<std::filesystem::path> const& paths,
    std::vector<DependencyGraph::ArtifactNode const*> const& artifacts,
    std::optional<std::string> const& unique_artifact) const {
    if (clargs_.build.print_to_stdout) {
        auto const& remote = *context_.apis->remote;
        for (std::size_t i = 0; i < paths.size(); i++) {
            if (paths[i] == *(clargs_.build.print_to_stdout)) {
                auto info = artifacts[i]->Content().Info();
                if (info) {
                    if (not remote.RetrieveToFds({*info},
                                                 {dup(fileno(stdout))},
                                                 /*raw_tree=*/false,
                                                 &*context_.apis->local)) {
                        Logger::Log(logger_,
                                    LogLevel::Error,
                                    "Failed to retrieve {}",
                                    *(clargs_.build.print_to_stdout));
                    }
                }
                else {
                    Logger::Log(logger_,
                                LogLevel::Error,
                                "Failed to obtain object information for {}",
                                *(clargs_.build.print_to_stdout));
                }
                return;
            }
        }
        // Not directly an artifact, hence check if the path is contained in
        // some artifact
        auto target_path =
            ToNormalPath(std::filesystem::path{*clargs_.build.print_to_stdout})
                .relative_path();
        for (std::size_t i = 0; i < paths.size(); i++) {
            auto const& path = paths[i];
            auto relpath = target_path.lexically_relative(path);
            if ((not relpath.empty()) and *relpath.begin() != "..") {
                Logger::Log(logger_,
                            LogLevel::Info,
                            "'{}' not a direct logical path of the specified "
                            "target; will take subobject '{}' of '{}'",
                            *(clargs_.build.print_to_stdout),
                            relpath.string(),
                            path.string());
                auto info = artifacts[i]->Content().Info();
                if (info) {
                    auto new_info =
                        RetrieveSubPathId(*info, *context_.apis, relpath);
                    if (new_info) {
                        if (not remote.RetrieveToFds({*new_info},
                                                     {dup(fileno(stdout))},
                                                     /*raw_tree=*/false,
                                                     &*context_.apis->local)) {
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
                    Logger::Log(logger_,
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
    else if (clargs_.build.print_unique) {
        if (unique_artifact) {
            auto const& remote = *context_.apis->remote;
            std::optional<Artifact::ObjectInfo> info = std::nullopt;
            for (std::size_t i = 0; i < paths.size(); i++) {
                if (paths[i] == unique_artifact) {
                    info = artifacts[i]->Content().Info();
                }
            }
            if (info) {
                if (not remote.RetrieveToFds({*info},
                                             {dup(fileno(stdout))},
                                             /*raw_tree=*/false,
                                             &*context_.apis->local)) {
                    Logger::Log(logger_,
                                LogLevel::Error,
                                "Failed to retrieve {}",
                                *unique_artifact);
                }
            }
            else {
                Logger::Log(logger_,
                            LogLevel::Error,
                            "Failed to obtain object information for {}",
                            *unique_artifact);
            }
            return;
        }
        Logger::Log(logger_,
                    LogLevel::Info,
                    "Target does not have precisely one artifact.");
    }
}

#endif
