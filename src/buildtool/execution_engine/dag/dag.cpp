#include "src/buildtool/execution_engine/dag/dag.hpp"

#include "src/buildtool/common/artifact_description.hpp"
#include "src/buildtool/logging/logger.hpp"

auto DependencyGraph::CreateOutputArtifactNodes(
    std::string const& action_id,
    std::vector<std::string> const& file_paths,
    std::vector<std::string> const& dir_paths,
    bool is_tree_action)
    -> std::pair<std::vector<DependencyGraph::NamedArtifactNodePtr>,
                 std::vector<DependencyGraph::NamedArtifactNodePtr>> {
    if (is_tree_action) {  // create tree artifact
        auto artifact = ArtifactDescription{action_id}.ToArtifact();
        auto const node_id = AddArtifact(std::move(artifact));
        return std::make_pair(std::vector<NamedArtifactNodePtr>{},
                              std::vector<NamedArtifactNodePtr>{
                                  {{}, &(*artifact_nodes_[node_id])}});
    }

    // create action artifacts
    auto node_creator = [this, &action_id](auto* nodes, auto const& paths) {
        for (auto const& artifact_path : paths) {
            auto artifact =
                ArtifactDescription{action_id,
                                    std::filesystem::path{artifact_path}}
                    .ToArtifact();
            auto const node_id = AddArtifact(std::move(artifact));
            nodes->emplace_back(NamedArtifactNodePtr{
                artifact_path, &(*artifact_nodes_[node_id])});
        }
    };

    std::vector<NamedArtifactNodePtr> file_nodes{};
    file_nodes.reserve(file_paths.size());
    node_creator(&file_nodes, file_paths);

    std::vector<NamedArtifactNodePtr> dir_nodes{};
    dir_nodes.reserve(dir_paths.size());
    node_creator(&dir_nodes, dir_paths);

    return std::make_pair(std::move(file_nodes), std::move(dir_nodes));
}

auto DependencyGraph::CreateInputArtifactNodes(
    ActionDescription::inputs_t const& inputs)
    -> std::optional<std::vector<DependencyGraph::NamedArtifactNodePtr>> {
    std::vector<NamedArtifactNodePtr> nodes{};

    for (auto const& [local_path, artifact_desc] : inputs) {
        auto artifact = artifact_desc.ToArtifact();
        auto const node_id = AddArtifact(std::move(artifact));
        nodes.push_back({local_path, &(*artifact_nodes_[node_id])});
    }
    return nodes;
}

auto DependencyGraph::CreateActionNode(Action const& action) noexcept
    -> DependencyGraph::ActionNode* {
    if (action.IsTreeAction() or not action.Command().empty()) {
        auto const node_id = AddAction(action);
        return &(*action_nodes_[node_id]);
    }
    return nullptr;
}

auto DependencyGraph::LinkNodePointers(
    std::vector<NamedArtifactNodePtr> const& output_files,
    std::vector<NamedArtifactNodePtr> const& output_dirs,
    gsl::not_null<ActionNode*> const& action_node,
    std::vector<NamedArtifactNodePtr> const& input_nodes) noexcept -> bool {
    for (auto const& named_file : output_files) {
        if (!named_file.node->AddBuilderActionNode(action_node) ||
            !action_node->AddOutputFile(named_file)) {
            return false;
        }
    }
    for (auto const& named_dir : output_dirs) {
        if (!named_dir.node->AddBuilderActionNode(action_node) ||
            !action_node->AddOutputDir(named_dir)) {
            return false;
        }
    }

    for (auto const& named_node : input_nodes) {
        if (!named_node.node->AddConsumerActionNode(action_node) ||
            !action_node->AddDependency(named_node)) {
            return false;
        }
    }

    action_node->NotifyDoneLinking();

    return true;
}

auto DependencyGraph::Add(std::vector<ActionDescription> const& actions)
    -> bool {
    for (auto const& action : actions) {
        if (not AddAction(action)) {
            return false;
        }
    }
    return true;
}

auto DependencyGraph::AddArtifact(ArtifactDescription const& description)
    -> ArtifactIdentifier {
    auto artifact = description.ToArtifact();
    auto id = artifact.Id();
    [[maybe_unused]] auto const node_id = AddArtifact(std::move(artifact));
    return id;
}

auto DependencyGraph::AddAction(ActionDescription const& description) -> bool {
    auto output_nodes =
        CreateOutputArtifactNodes(description.Id(),
                                  description.OutputFiles(),
                                  description.OutputDirs(),
                                  description.GraphAction().IsTreeAction());
    auto* action_node = CreateActionNode(description.GraphAction());
    auto input_nodes = CreateInputArtifactNodes(description.Inputs());

    if (action_node == nullptr or not input_nodes.has_value() or
        (output_nodes.first.empty() and output_nodes.second.empty())) {
        return false;
    }

    return LinkNodePointers(
        output_nodes.first, output_nodes.second, action_node, *input_nodes);
}

auto DependencyGraph::AddAction(Action const& a) noexcept
    -> DependencyGraph::ActionNodeIdentifier {
    auto id = a.Id();
    auto const action_it = action_ids_.find(id);
    if (action_it != action_ids_.end()) {
        return action_it->second;
    }
    action_nodes_.emplace_back(ActionNode::Create(a));
    ActionNodeIdentifier node_id{action_nodes_.size() - 1};
    action_ids_[id] = node_id;
    return node_id;
}

auto DependencyGraph::AddAction(Action&& a) noexcept
    -> DependencyGraph::ActionNodeIdentifier {
    auto const& id = a.Id();
    auto const action_it = action_ids_.find(id);
    if (action_it != action_ids_.end()) {
        return action_it->second;
    }
    action_nodes_.emplace_back(ActionNode::Create(std::move(a)));
    ActionNodeIdentifier node_id{action_nodes_.size() - 1};
    action_ids_[id] = node_id;
    return node_id;
}

auto DependencyGraph::AddArtifact(Artifact const& a) noexcept
    -> DependencyGraph::ArtifactNodeIdentifier {
    auto const& id = a.Id();
    auto const artifact_it = artifact_ids_.find(id);
    if (artifact_it != artifact_ids_.end()) {
        return artifact_it->second;
    }
    artifact_nodes_.emplace_back(ArtifactNode::Create(a));
    ArtifactNodeIdentifier node_id{artifact_nodes_.size() - 1};
    artifact_ids_[id] = node_id;
    return node_id;
}

auto DependencyGraph::AddArtifact(Artifact&& a) noexcept
    -> DependencyGraph::ArtifactNodeIdentifier {
    auto id = a.Id();
    auto const artifact_it = artifact_ids_.find(id);
    if (artifact_it != artifact_ids_.end()) {
        return artifact_it->second;
    }
    artifact_nodes_.emplace_back(ArtifactNode::Create(std::move(a)));
    ArtifactNodeIdentifier node_id{artifact_nodes_.size() - 1};
    artifact_ids_[id] = node_id;
    return node_id;
}

auto DependencyGraph::ArtifactIdentifiers() const noexcept
    -> std::unordered_set<ArtifactIdentifier> {
    std::unordered_set<ArtifactIdentifier> ids;
    std::transform(
        std::begin(artifact_ids_),
        std::end(artifact_ids_),
        std::inserter(ids, std::begin(ids)),
        [](auto const& artifact_id_pair) { return artifact_id_pair.first; });
    return ids;
}

auto DependencyGraph::ArtifactNodeWithId(ArtifactIdentifier const& id)
    const noexcept -> DependencyGraph::ArtifactNode const* {
    auto it_to_artifact = artifact_ids_.find(id);
    if (it_to_artifact == artifact_ids_.end()) {
        return nullptr;
    }
    return &(*artifact_nodes_[it_to_artifact->second]);
}

auto DependencyGraph::ActionNodeWithId(ActionIdentifier const& id)
    const noexcept -> DependencyGraph::ActionNode const* {
    auto it_to_action = action_ids_.find(id);
    if (it_to_action == action_ids_.end()) {
        return nullptr;
    }
    return &(*action_nodes_[it_to_action->second]);
}

auto DependencyGraph::ActionNodeOfArtifactWithId(ArtifactIdentifier const& id)
    const noexcept -> DependencyGraph::ActionNode const* {
    auto const* node = ArtifactNodeWithId(id);
    if (node != nullptr) {
        auto const& children = node->Children();
        if (children.empty()) {
            return nullptr;
        }
        return children[0];
    }
    return nullptr;
}

auto DependencyGraph::ArtifactWithId(
    ArtifactIdentifier const& id) const noexcept -> std::optional<Artifact> {
    auto const* node = ArtifactNodeWithId(id);
    if (node != nullptr) {
        return node->Content();
    }
    return std::nullopt;
}

[[nodiscard]] auto DependencyGraph::ActionWithId(
    ActionIdentifier const& id) const noexcept -> std::optional<Action> {
    auto const* node = ActionNodeWithId(id);
    if (node != nullptr) {
        return node->Content();
    }
    return std::nullopt;
}

auto DependencyGraph::ActionOfArtifactWithId(
    ArtifactIdentifier const& artifact_id) const noexcept
    -> std::optional<Action> {
    auto const* node = ActionNodeOfArtifactWithId(artifact_id);
    if (node != nullptr) {
        return node->Content();
    }
    return std::nullopt;
}

auto DependencyGraph::ActionIdOfArtifactWithId(
    ArtifactIdentifier const& artifact_id) const noexcept
    -> std::optional<ActionIdentifier> {
    auto const& action = ActionOfArtifactWithId(artifact_id);
    if (action) {
        return action->Id();
    }
    return std::nullopt;
}
