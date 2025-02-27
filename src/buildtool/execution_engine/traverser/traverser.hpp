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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_ENGINE_TRAVERSER_TRAVERSER_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_ENGINE_TRAVERSER_TRAVERSER_HPP

#include <atomic>
#include <concepts>
#include <cstddef>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

#include "gsl/gsl"
#include "src/buildtool/common/identifier.hpp"
#include "src/buildtool/execution_engine/dag/dag.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/multithreading/task_system.hpp"

/// \brief Concept required for Runners used by the Traverser.
template <class T>
concept Runnable = requires(T const r,
                            DependencyGraph::ActionNode const* action,
                            DependencyGraph::ArtifactNode const* artifact) {
    { r.Process(action) } -> std::same_as<bool>;
    { r.Process(artifact) } -> std::same_as<bool>;
};

/// \brief Class to traverse the dependency graph executing necessary actions
/// \tparam Executor    Type of the executor
//  Traversal of the graph and execution of actions are concurrent, using
/// the //src/buildtool/execution_engine/task_system.
/// Graph remains constant and the only parts of the nodes that are modified are
/// their traversal state
template <Runnable Executor>
class Traverser {
  public:
    explicit Traverser(Executor const& r,
                       DependencyGraph const& graph,
                       std::size_t jobs,
                       gsl::not_null<std::atomic<bool>*> const& fail_flag)
        : runner_{r}, graph_{graph}, failed_{fail_flag}, tasker_{jobs} {}
    Traverser() = delete;
    Traverser(Traverser const&) = delete;
    Traverser(Traverser&&) = delete;
    auto operator=(Traverser const&) -> Traverser& = delete;
    auto operator=(Traverser&&) -> Traverser& = delete;
    ~Traverser() = default;

    // Traverse the whole graph
    [[nodiscard]] auto Traverse() noexcept -> bool {
        auto const& ids = graph_.ArtifactIdentifiers();
        return Traverse(ids);
    };

    // Traverse starting by the artifacts with the given identifiers, avoiding
    // executing actions that are not strictly needed to build the given
    // artifacts
    [[nodiscard]] auto Traverse(std::unordered_set<ArtifactIdentifier> const&
                                    target_ids) noexcept -> bool;

  private:
    Executor const& runner_{};
    DependencyGraph const& graph_;
    gsl::not_null<std::atomic<bool>*> failed_;
    TaskSystem tasker_;  // THIS SHOULD BE THE LAST MEMBER VARIABLE

    // Visits discover nodes and queue visits to their children nodes.
    void Visit(gsl::not_null<DependencyGraph::ArtifactNode const*>
                   artifact_node) noexcept;
    void Visit(
        gsl::not_null<DependencyGraph::ActionNode const*> action_node) noexcept;

    // Notify all actions that have it as a dependency that it is available and
    // queue execution of those that become ready (that were only waiting for
    // this artifact)
    void NotifyAvailable(
        gsl::not_null<DependencyGraph::ArtifactNode const*> const&
            artifact_node) noexcept;

    // Calls NotifyAvailable on all the action's outputs
    void NotifyAvailable(
        gsl::not_null<DependencyGraph::ActionNode const*> const&
            action_node) noexcept;

    // Visit to nodes are queued only once
    template <typename NodeTypePtr>
    void QueueVisit(NodeTypePtr node) noexcept {
        // in case the node was already discovered, there is no need to queue
        // the visit
        if (failed_->load() or node->TraversalState()->GetAndMarkDiscovered()) {
            return;
        }
        tasker_.QueueTask([this, node]() noexcept { Visit(node); });
    }

    // Queue task to process the node by the executor after making sure that the
    // node is required and that it was not yet queued to be processed. The task
    // queued will call notify that the node is available in case processing it
    // was successful
    template <typename NodeTypePtr>
    void QueueProcessing(NodeTypePtr node) noexcept {
        if (failed_->load() or not node->TraversalState()->IsRequired() or
            node->TraversalState()->GetAndMarkQueuedToBeProcessed()) {
            return;
        }

        auto process_node = [this, node]() {
            if (runner_.Process(node)) {
                NotifyAvailable(node);
            }
            else {
                Abort();
            }
        };
        tasker_.QueueTask(process_node);
    }

    void Abort() noexcept {
        failed_->store(true);
        tasker_.Shutdown();  // skip execution of pending tasks
    }
};

template <Runnable Executor>
auto Traverser<Executor>::Traverse(
    std::unordered_set<ArtifactIdentifier> const& target_ids) noexcept -> bool {
    for (auto artifact_id : target_ids) {
        auto const* artifact_node = graph_.ArtifactNodeWithId(artifact_id);
        if (artifact_node != nullptr) {
            QueueVisit(artifact_node);
        }
        else {
            Abort();
            Logger::Log(
                LogLevel::Error,
                "artifact with id {} can not be found in dependency graph.",
                artifact_id);
            return false;
        }
    }
    return true;
}

template <Runnable Executor>
void Traverser<Executor>::Visit(
    gsl::not_null<DependencyGraph::ArtifactNode const*>
        artifact_node) noexcept {
    artifact_node->TraversalState()->MarkRequired();
    // Visits are queued only once per artifact node, but it could be that the
    // builder action had multiple outputs and was queued and executed through
    // the visit to another of the outputs, in which case the current artifact
    // would be available and there is nothing else to do
    if (artifact_node->TraversalState()->IsAvailable()) {
        return;
    }

    if (artifact_node->HasBuilderAction()) {
        QueueVisit(gsl::not_null<DependencyGraph::ActionNode const*>(
            artifact_node->BuilderActionNode()));
    }
    else {
        QueueProcessing(artifact_node);
    }
}

template <Runnable Executor>
void Traverser<Executor>::Visit(
    gsl::not_null<DependencyGraph::ActionNode const*> action_node) noexcept {
    action_node->TraversalState()->MarkRequired();
    for (auto const& dep : action_node->Children()) {
        if (not dep->TraversalState()->IsAvailable()) {
            QueueVisit(dep);
        }
    }

    if (action_node->TraversalState()->IsReady()) {
        QueueProcessing(action_node);
    }
}

template <Runnable Executor>
void Traverser<Executor>::NotifyAvailable(
    gsl::not_null<DependencyGraph::ArtifactNode const*> const&
        artifact_node) noexcept {
    artifact_node->TraversalState()->MakeAvailable();
    for (auto const& action_node : artifact_node->Parents()) {
        if (action_node->TraversalState()->NotifyAvailableDepAndCheckReady()) {
            QueueProcessing(action_node);
        }
    }
}

template <Runnable Executor>
void Traverser<Executor>::NotifyAvailable(
    gsl::not_null<DependencyGraph::ActionNode const*> const&
        action_node) noexcept {
    for (auto const& output : action_node->Parents()) {
        NotifyAvailable(output);
    }
}

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_ENGINE_TRAVERSER_TRAVERSER_HPP
