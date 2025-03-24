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
#ifndef BOOTSTRAP_BUILD_TOOL

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gsl/gsl"
#include "nlohmann/json.hpp"
#include "src/buildtool/common/action_description.hpp"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_description.hpp"
#include "src/buildtool/common/cli.hpp"
#include "src/buildtool/common/identifier.hpp"
#include "src/buildtool/common/tree.hpp"
#include "src/buildtool/execution_engine/dag/dag.hpp"
#include "src/buildtool/execution_engine/executor/context.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/progress_reporting/base_progress_reporter.hpp"

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
        gsl::not_null<const ExecutionContext*> const& context,
        progress_reporter_t reporter,
        Logger const* logger = nullptr)
        : clargs_{std::move(clargs)},
          context_{*context},
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
        std::vector<ActionDescription::Ptr>&& action_descriptions,
        std::vector<std::string>&& blobs,
        std::vector<Tree::Ptr>&& trees,
        std::vector<ArtifactDescription>&& extra_artifacts = {}) const
        -> std::optional<BuildResult>;

    /// \brief Parses graph description into graph, traverses it and retrieves
    /// outputs specified by command line arguments
    [[nodiscard]] auto BuildAndStage(
        std::filesystem::path const& graph_description,
        nlohmann::json const& artifacts) const -> std::optional<BuildResult>;

  private:
    CommandLineArguments const clargs_;
    ExecutionContext const& context_;
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
            std::tuple<nlohmann::json, nlohmann::json, nlohmann::json>>;

    /// \brief Requires for the executor to upload blobs to CAS. In the case any
    /// of the uploads fails, execution is terminated
    /// \param[in]  blobs   blobs to be uploaded
    [[nodiscard]] auto UploadBlobs(
        std::vector<std::string>&& blobs) const noexcept -> bool;

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
                                   std::vector<ArtifactIdentifier>>>;

    /// \brief Traverses the graph. In case any of the artifact ids
    /// specified by the command line arguments is duplicated, execution is
    /// terminated.
    [[nodiscard]] auto Traverse(
        DependencyGraph const& g,
        std::vector<ArtifactIdentifier> const& artifact_ids) const -> bool;

    [[nodiscard]] auto TraverseRebuild(
        DependencyGraph const& g,
        std::vector<ArtifactIdentifier> const& artifact_ids) const -> bool;

    /// \brief Retrieves nodes corresponding to artifacts with ids in artifacts.
    /// In case any of the identifiers doesn't correspond to a node inside the
    /// graph, we write out error message and stop execution with failure code
    [[nodiscard]] static auto GetArtifactNodes(
        DependencyGraph const& g,
        std::vector<ArtifactIdentifier> const& artifact_ids,
        Logger const* logger) noexcept
        -> std::optional<std::vector<DependencyGraph::ArtifactNode const*>>;

    void LogStatistics() const noexcept;

    [[nodiscard]] auto BuildArtifacts(
        gsl::not_null<DependencyGraph*> const& graph,
        std::map<std::string, ArtifactDescription> const& artifacts,
        std::map<std::string, ArtifactDescription> const& runfiles,
        std::vector<ActionDescription::Ptr>&& actions,
        std::vector<Tree::Ptr>&& trees,
        std::vector<std::string>&& blobs,
        std::vector<ArtifactDescription> const& extra_artifacts = {}) const
        -> std::optional<
            std::tuple<std::vector<std::filesystem::path>,
                       std::vector<DependencyGraph::ArtifactNode const*>,
                       std::vector<DependencyGraph::ArtifactNode const*>>>;

    [[nodiscard]] auto PrepareOutputPaths(
        std::vector<std::filesystem::path> const& rel_paths) const
        -> std::optional<std::vector<std::filesystem::path>>;

    [[nodiscard]] static auto CollectObjectInfos(
        std::vector<DependencyGraph::ArtifactNode const*> const& artifact_nodes,
        Logger const* logger)
        -> std::optional<std::vector<Artifact::ObjectInfo>>;

    /// \brief Asks execution API to copy output artifacts to paths specified by
    /// command line arguments and writes location info. In case the executor
    /// couldn't retrieve any of the outputs, execution is terminated.
    [[nodiscard]] auto RetrieveOutputs(
        std::vector<std::filesystem::path> const& rel_paths,
        std::vector<Artifact::ObjectInfo> const& object_infos) const
        -> std::optional<std::vector<std::filesystem::path>>;

    void PrintOutputs(
        std::string message,
        std::vector<std::filesystem::path> const& paths,
        std::vector<DependencyGraph::ArtifactNode const*> const& artifact_nodes,
        std::map<std::string, ArtifactDescription> const& runfiles) const;

    void MaybePrintToStdout(
        std::vector<std::filesystem::path> const& paths,
        std::vector<DependencyGraph::ArtifactNode const*> const& artifacts,
        std::optional<std::string> const& unique_artifact) const;
};

#endif
#endif  // INCLUDED_SRC_BUILDTOOL_GRAPH_TRAVERSER_GRAPH_TRAVERSER_HPP
