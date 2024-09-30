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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_ENGINE_DAG_DAG_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_ENGINE_DAG_DAG_HPP

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>  // std::move
#include <variant>
#include <vector>

#include "gsl/gsl"
#include "src/buildtool/common/action.hpp"
#include "src/buildtool/common/action_description.hpp"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_description.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/hex_string.hpp"
#include "src/utils/cpp/transformed_range.hpp"
#include "src/utils/cpp/type_safe_arithmetic.hpp"

/// \brief Plain DirectedAcyclicGraph.
/// Additional properties (e.g. bipartiteness) can be encoded in nodes.
/// Deliberately not using \ref DirectedAcyclicGraph::Node anywhere to avoid
/// vtable lookups. For now, it does not hold any data.
class DirectedAcyclicGraph {
  public:
    /// \brief Abstract class for DAG nodes.
    /// \tparam T_Content  Type of content.
    /// \tparam T_Other    Type of neighboring nodes.
    /// TODO: once we have hashes, require sub classes to implement generating
    /// IDs depending on its unique content.
    template <typename T_Content, typename T_Other>
    class Node {
      public:
        using OtherNode = T_Other;
        using OtherNodePtr = gsl::not_null<OtherNode*>;

        explicit Node(T_Content&& content) noexcept
            : content_{std::move(content)} {}

        // NOLINTNEXTLINE(modernize-pass-by-value)
        explicit Node(T_Content const& content) noexcept : content_{content} {}

        Node(T_Content const& content,
             std::vector<OtherNodePtr> const& parents,
             std::vector<OtherNodePtr> const& children) noexcept
            : content_{content}, parents_{parents}, children_{children} {}

        // No copies and no moves are allowed. The only way to "copy" an
        // instance is to copy its raw pointer.
        Node(Node const&) = delete;
        Node(Node&&) = delete;
        auto operator=(Node const&) -> Node& = delete;
        auto operator=(Node&&) -> Node& = delete;
        ~Node() = default;

        [[nodiscard]] auto Content() const& noexcept -> T_Content const& {
            return content_;
        }

        [[nodiscard]] auto Content() && noexcept -> T_Content {
            return std::move(content_);
        }

        [[nodiscard]] auto Parents() const& noexcept
            -> std::vector<OtherNodePtr> const& {
            return parents_;
        }

        [[nodiscard]] auto Children() const& noexcept
            -> std::vector<OtherNodePtr> const& {
            return children_;
        }

        auto AddParent(OtherNodePtr const& parent) noexcept -> bool {
            parents_.push_back(parent);
            return true;
        }

        auto AddParent(OtherNodePtr&& parent) noexcept -> bool {
            parents_.push_back(std::move(parent));
            return true;
        }

        auto AddChild(OtherNodePtr const& child) noexcept -> bool {
            children_.push_back(child);
            return true;
        }

        auto AddChild(OtherNodePtr&& child) noexcept -> bool {
            children_.push_back(std::move(child));
            return true;
        }

      private:
        T_Content content_{};
        std::vector<OtherNodePtr> parents_{};
        std::vector<OtherNodePtr> children_{};
    };

    /// \brief Lock-free class for basic traversal state data
    /// Provides the following atomic operations:
    ///  - Retrieve (previous) state and mark as discovered, which will allow us
    ///  to know whether we should queue a visit the node or not at the same
    ///  time that we mark that its visit should not be queued by other threads,
    ///  since it is being queued by the current caller to this method or it has
    ///  already been queued by a previous caller.
    /// Note that "discovered" refers to "queued for visit" here.
    ///  - Retrieve (previous) state and mark as queued to be processed, which
    ///  will allow us to ensure that processing a node is queued at most once.
    class NodeTraversalState {
      public:
        NodeTraversalState() noexcept = default;
        NodeTraversalState(NodeTraversalState const&) = delete;
        NodeTraversalState(NodeTraversalState&&) = delete;
        auto operator=(NodeTraversalState const&) -> NodeTraversalState& =
                                                         delete;
        auto operator=(NodeTraversalState&&) -> NodeTraversalState& = delete;
        ~NodeTraversalState() noexcept = default;

        /// \brief Sets traversal state as discovered
        /// \returns True if it was already discovered, false otherwise
        /// Note: this is an atomic, lock-free operation
        [[nodiscard]] auto GetAndMarkDiscovered() noexcept -> bool {
            return std::atomic_exchange(&has_been_discovered_, true);
        }

        /// \brief Sets traversal state as queued to be processed
        /// \returns True if it was already queued to be processed, false
        /// otherwise
        /// Note: this is an atomic, lock-free operation
        [[nodiscard]] auto GetAndMarkQueuedToBeProcessed() noexcept -> bool {
            return std::atomic_exchange(&is_queued_to_be_processed_, true);
        }

        /// \brief Check if a node is required to be processed or not
        [[nodiscard]] auto IsRequired() const noexcept -> bool {
            return is_required_;
        }

        /// \brief Mark node as required to be executed
        /// Note: this should be upon node discovery (visit) while traversing
        /// the graph
        void MarkRequired() noexcept { is_required_ = true; }

      private:
        std::atomic<bool> has_been_discovered_{false};
        std::atomic<bool> is_queued_to_be_processed_{false};
        std::atomic<bool> is_required_{false};
    };
};

class DependencyGraph : DirectedAcyclicGraph {
  public:
    // Forward declaration
    class ArtifactNode;

    // Node identifier for actions
    struct ActionNodeIdentifierTag : type_safe_arithmetic_tag<std::size_t> {};
    using ActionNodeIdentifier = type_safe_arithmetic<ActionNodeIdentifierTag>;

    // Node identifier for artifacts
    struct ArtifactNodeIdentifierTag : type_safe_arithmetic_tag<std::size_t> {};
    using ArtifactNodeIdentifier =
        type_safe_arithmetic<ArtifactNodeIdentifierTag>;

    /// \brief Class for traversal state data specific for ActionNode's
    /// Provides the following atomic operations (listed on the public methods):
    class ActionNodeTraversalState : public NodeTraversalState {
      public:
        ActionNodeTraversalState() noexcept = default;
        ActionNodeTraversalState(ActionNodeTraversalState const&) = delete;
        ActionNodeTraversalState(ActionNodeTraversalState&&) = delete;
        auto operator=(ActionNodeTraversalState const&)
            -> ActionNodeTraversalState& = delete;
        auto operator=(ActionNodeTraversalState&&)
            -> ActionNodeTraversalState& = delete;
        ~ActionNodeTraversalState() noexcept = default;

        /// \brief Acknowledge that a dependency was made available and return
        /// whether the action is ready to be executed
        [[nodiscard]] auto NotifyAvailableDepAndCheckReady() noexcept -> bool {
            return std::atomic_fetch_sub(&unavailable_deps_, 1) == 1;
        }

        /// \brief Check whether the action can be now executed or not.
        /// Note: checking state without modifying (unlike
        /// NotifyAvailableDepAndCheckReady()) is useful in the case that when
        /// the action node is visited all its dependencies were already
        /// available
        [[nodiscard]] auto IsReady() const noexcept -> bool {
            return unavailable_deps_ == 0;
        }

        /// \brief Initialise number of unavailable dependencies
        /// \param[in]  count    Number of unavailable dependencies
        /// Note: this method should be called previous to the start of the
        /// traversal (once the action node is built)
        void InitUnavailableDeps(std::size_t count) noexcept {
            unavailable_deps_ = static_cast<int>(count);
        }

      private:
        std::atomic<int> unavailable_deps_{-1};
    };

    /// \brief Class for traversal state data specific for ArtifactNode's
    /// Provides the following atomic operations:
    ///  - Mark the artifact in this node as available
    ///  - Check whether the artifact in this node is available or not
    class ArtifactNodeTraversalState : public NodeTraversalState {
      public:
        ArtifactNodeTraversalState() noexcept = default;
        ArtifactNodeTraversalState(ArtifactNodeTraversalState const&) = delete;
        ArtifactNodeTraversalState(ArtifactNodeTraversalState&&) = delete;
        auto operator=(ArtifactNodeTraversalState const&)
            -> ArtifactNodeTraversalState& = delete;
        auto operator=(ArtifactNodeTraversalState&&)
            -> ArtifactNodeTraversalState& = delete;
        ~ArtifactNodeTraversalState() noexcept = default;

        [[nodiscard]] auto IsAvailable() const noexcept -> bool {
            return is_available_;
        }

        void MakeAvailable() noexcept { is_available_ = true; }

      private:
        std::atomic<bool> is_available_{false};
    };

    /// \brief Action node (bipartite)
    /// Cannot be entry.
    class ActionNode final : public Node<Action, ArtifactNode> {
        using base = Node<Action, ArtifactNode>;

      public:
        using base::base;
        using Ptr = std::unique_ptr<ActionNode>;
        struct NamedOtherNodePtr {
            Action::LocalPath path;
            base::OtherNodePtr node;
        };
        using LocalPaths =
            TransformedRange<std::vector<NamedOtherNodePtr>::const_iterator,
                             Action::LocalPath>;

        [[nodiscard]] static auto Create(Action const& content) noexcept
            -> Ptr {
            return std::make_unique<ActionNode>(content);
        }

        [[nodiscard]] static auto Create(Action&& content) noexcept -> Ptr {
            return std::make_unique<ActionNode>(std::move(content));
        }

        [[nodiscard]] auto AddOutputFile(
            NamedOtherNodePtr const& output) noexcept -> bool {
            base::AddParent(output.node);
            output_files_.push_back(output);
            return true;
        }

        [[nodiscard]] auto AddOutputFile(NamedOtherNodePtr&& output) noexcept
            -> bool {
            base::AddParent(output.node);
            output_files_.push_back(std::move(output));
            return true;
        }

        [[nodiscard]] auto AddOutputDir(
            NamedOtherNodePtr const& output) noexcept -> bool {
            base::AddParent(output.node);
            output_dirs_.push_back(output);
            return true;
        }

        [[nodiscard]] auto AddOutputDir(NamedOtherNodePtr&& output) noexcept
            -> bool {
            base::AddParent(output.node);
            output_dirs_.push_back(std::move(output));
            return true;
        }

        [[nodiscard]] auto AddDependency(
            NamedOtherNodePtr const& dependency) noexcept -> bool {
            base::AddChild(dependency.node);
            dependencies_.push_back(dependency);
            return true;
        }

        [[nodiscard]] auto AddDependency(
            NamedOtherNodePtr&& dependency) noexcept -> bool {
            base::AddChild(dependency.node);
            dependencies_.push_back(std::move(dependency));
            return true;
        }

        [[nodiscard]] auto OutputFiles() const& noexcept
            -> std::vector<NamedOtherNodePtr> const& {
            return output_files_;
        }

        [[nodiscard]] auto OutputDirs() const& noexcept
            -> std::vector<NamedOtherNodePtr> const& {
            return output_dirs_;
        }

        [[nodiscard]] auto Dependencies() const& noexcept
            -> std::vector<NamedOtherNodePtr> const& {
            return dependencies_;
        }

        [[nodiscard]] auto Command() const noexcept
            -> std::vector<std::string> const& {
            return Content().Command();
        }

        [[nodiscard]] auto Env() const noexcept
            -> std::map<std::string, std::string> const& {
            return Content().Env();
        }

        [[nodiscard]] auto MayFail() const noexcept
            -> std::optional<std::string> const& {
            return Content().MayFail();
        }

        [[nodiscard]] auto TimeoutScale() const noexcept -> double {
            return Content().TimeoutScale();
        }

        [[nodiscard]] auto ExecutionProperties() const noexcept
            -> std::map<std::string, std::string> const& {
            return Content().ExecutionProperties();
        }

        [[nodiscard]] auto NoCache() const noexcept -> bool {
            return Content().NoCache();
        }

        [[nodiscard]] auto OutputFilePaths() const& noexcept -> LocalPaths {
            return NodePaths(&output_files_);
        }

        [[nodiscard]] auto OutputDirPaths() const& noexcept -> LocalPaths {
            return NodePaths(&output_dirs_);
        }

        [[nodiscard]] auto DependencyPaths() const& noexcept -> LocalPaths {
            return NodePaths(&dependencies_);
        }

        // To initialise the action traversal specific data before traversing
        // the graph
        void NotifyDoneLinking() const noexcept {
            traversal_state_->InitUnavailableDeps(Children().size());
        }

        [[nodiscard]] auto TraversalState() const noexcept
            -> ActionNodeTraversalState* {
            return traversal_state_.get();
        }

      private:
        std::vector<NamedOtherNodePtr> output_files_;
        std::vector<NamedOtherNodePtr> output_dirs_;
        std::vector<NamedOtherNodePtr> dependencies_;
        std::unique_ptr<ActionNodeTraversalState> traversal_state_{
            std::make_unique<ActionNodeTraversalState>()};

        [[nodiscard]] static auto NodePaths(
            gsl::not_null<std::vector<NamedOtherNodePtr> const*> const& nodes)
            -> LocalPaths {
            return TransformedRange{
                nodes->begin(),
                nodes->end(),
                [](NamedOtherNodePtr const& node) { return node.path; }};
        }
    };

    /// \brief Artifact node (bipartite)
    /// Can be entry or leaf and can only have single child (see \ref AddChild)
    class ArtifactNode final : public Node<Artifact, ActionNode> {
        using base = Node<Artifact, ActionNode>;

      public:
        using base::base;
        using typename base::OtherNode;
        using typename base::OtherNodePtr;
        using Ptr = std::unique_ptr<ArtifactNode>;

        [[nodiscard]] static auto Create(Artifact const& content) noexcept
            -> Ptr {
            return std::make_unique<ArtifactNode>(content);
        }

        [[nodiscard]] static auto Create(Artifact&& content) noexcept -> Ptr {
            return std::make_unique<ArtifactNode>(std::move(content));
        }

        [[nodiscard]] auto AddBuilderActionNode(
            OtherNodePtr const& action) noexcept -> bool {
            if (base::Children().empty()) {
                return base::AddChild(action);
            }
            Logger::Log(LogLevel::Error,
                        "cannot set a second builder for artifact {}",
                        ToHexString(Content().Id()));
            return false;
        }

        [[nodiscard]] auto AddConsumerActionNode(
            OtherNodePtr const& action) noexcept -> bool {
            return base::AddParent(action);
        }

        [[nodiscard]] auto HasBuilderAction() const noexcept -> bool {
            return not base::Children().empty();
        }

        [[nodiscard]] auto BuilderActionNode() const noexcept
            -> ActionNode const* {
            return HasBuilderAction() ? base::Children()[0].get() : nullptr;
        }

        [[nodiscard]] auto TraversalState() const noexcept
            -> ArtifactNodeTraversalState* {
            return traversal_state_.get();
        }

      private:
        std::unique_ptr<ArtifactNodeTraversalState> traversal_state_{
            std::make_unique<ArtifactNodeTraversalState>()};
    };

    using NamedArtifactNodePtr = ActionNode::NamedOtherNodePtr;

    DependencyGraph() noexcept = default;

    // DependencyGraph should not be copyable or movable. This could be changed
    // in the case we want to make the graph construction to be functional
    DependencyGraph(DependencyGraph const&) = delete;
    DependencyGraph(DependencyGraph&&) = delete;
    auto operator=(DependencyGraph const&) -> DependencyGraph& = delete;
    auto operator=(DependencyGraph&&) -> DependencyGraph& = delete;
    ~DependencyGraph() noexcept = default;

    [[nodiscard]] auto Add(std::vector<ActionDescription> const& actions)
        -> bool;

    [[nodiscard]] auto Add(std::vector<ActionDescription::Ptr> const& actions)
        -> bool;

    [[nodiscard]] auto AddAction(ActionDescription const& description) -> bool;

    [[nodiscard]] auto AddArtifact(ArtifactDescription const& description)
        -> ArtifactIdentifier;

    [[nodiscard]] auto ArtifactIdentifiers() const noexcept
        -> std::unordered_set<ArtifactIdentifier>;

    [[nodiscard]] auto ArtifactNodeWithId(
        ArtifactIdentifier const& id) const noexcept -> ArtifactNode const*;

    [[nodiscard]] auto ActionNodeWithId(
        ActionIdentifier const& id) const noexcept -> ActionNode const*;

  private:
    // List of action nodes we already created
    std::vector<ActionNode::Ptr> action_nodes_;

    // List of artifact nodes we already created
    std::vector<ArtifactNode::Ptr> artifact_nodes_;

    // Associates global action identifier to local node id
    std::unordered_map<ActionIdentifier, ActionNodeIdentifier> action_ids_;

    // Associates global artifact identifier to local node id
    std::unordered_map<ArtifactIdentifier, ArtifactNodeIdentifier>
        artifact_ids_;

    [[nodiscard]] auto CreateOutputArtifactNodes(
        std::string const& action_id,
        std::vector<std::string> const& file_paths,
        std::vector<std::string> const& dir_paths,
        bool is_tree_action)
        -> std::pair<std::vector<DependencyGraph::NamedArtifactNodePtr>,
                     std::vector<DependencyGraph::NamedArtifactNodePtr>>;

    [[nodiscard]] auto CreateInputArtifactNodes(
        ActionDescription::inputs_t const& inputs)
        -> std::optional<std::vector<NamedArtifactNodePtr>>;

    [[nodiscard]] auto CreateActionNode(Action const& action) noexcept
        -> ActionNode*;

    [[nodiscard]] static auto LinkNodePointers(
        std::vector<NamedArtifactNodePtr> const& output_files,
        std::vector<NamedArtifactNodePtr> const& output_dirs,
        gsl::not_null<ActionNode*> const& action_node,
        std::vector<NamedArtifactNodePtr> const& input_nodes) noexcept -> bool;

    [[nodiscard]] auto AddAction(Action const& a) noexcept
        -> ActionNodeIdentifier;
    [[nodiscard]] auto AddAction(Action&& a) noexcept -> ActionNodeIdentifier;
    [[nodiscard]] auto AddArtifact(Artifact const& a) noexcept
        -> ArtifactNodeIdentifier;
    [[nodiscard]] auto AddArtifact(Artifact&& a) noexcept
        -> ArtifactNodeIdentifier;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_ENGINE_DAG_DAG_HPP
