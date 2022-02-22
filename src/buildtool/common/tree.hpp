#ifndef INCLUDED_SRC_BUILDTOOL_COMMON_TREE_HPP
#define INCLUDED_SRC_BUILDTOOL_COMMON_TREE_HPP

#include <string>
#include <unordered_map>

#include "nlohmann/json.hpp"
#include "src/buildtool/common/action_description.hpp"
#include "src/buildtool/common/artifact_description.hpp"

// Describes tree, its inputs, output (tree artifact), and action (tree action).
class Tree {
    using inputs_t = ActionDescription::inputs_t;

  public:
    explicit Tree(inputs_t&& inputs)
        : id_{ComputeId(inputs)}, inputs_{std::move(inputs)} {}

    [[nodiscard]] auto Id() const& -> std::string const& { return id_; }
    [[nodiscard]] auto Id() && -> std::string { return std::move(id_); }

    [[nodiscard]] auto ToJson() const -> nlohmann::json {
        return ComputeDescription(inputs_);
    }

    [[nodiscard]] auto Inputs() const -> inputs_t { return inputs_; }

    [[nodiscard]] auto Action() const -> ActionDescription {
        return {
            {/*unused*/}, {/*unused*/}, Action::CreateTreeAction(id_), inputs_};
    }

    [[nodiscard]] auto Output() const -> ArtifactDescription {
        return ArtifactDescription{id_};
    }

    [[nodiscard]] static auto FromJson(std::string const& id,
                                       nlohmann::json const& json)
        -> std::optional<Tree> {
        auto inputs = inputs_t{};
        inputs.reserve(json.size());
        for (auto const& [path, artifact] : json.items()) {
            auto artifact_desc = ArtifactDescription::FromJson(artifact);
            if (not artifact_desc) {
                return std::nullopt;
            }
            inputs.emplace(path, std::move(*artifact_desc));
        }
        return Tree{id, std::move(inputs)};
    }

  private:
    std::string id_;
    inputs_t inputs_;

    Tree(std::string id, inputs_t&& inputs)
        : id_{std::move(id)}, inputs_{std::move(inputs)} {}

    static auto ComputeDescription(inputs_t const& inputs) -> nlohmann::json {
        auto json = nlohmann::json::object();
        for (auto const& [path, artifact] : inputs) {
            json[path] = artifact.ToJson();
        }
        return json;
    }

    static auto ComputeId(inputs_t const& inputs) -> std::string {
        return ComputeHash(ComputeDescription(inputs).dump());
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_COMMON_TREE_HPP
