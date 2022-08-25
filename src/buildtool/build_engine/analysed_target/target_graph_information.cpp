#include "src/buildtool/build_engine/analysed_target/target_graph_information.hpp"

auto TargetGraphInformation::NodeString() const noexcept
    -> std::optional<std::string> {
    if (node_) {
        return node_->ToString();
    }
    return std::nullopt;
}

namespace {
auto NodesToString(
    std::vector<BuildMaps::Target::ConfiguredTargetPtr> const& nodes)
    -> std::vector<std::string> {
    std::vector<std::string> result{};
    result.reserve(nodes.size());
    for (auto const& n : nodes) {
        if (n) {
            result.emplace_back(n->ToString());
        }
    }
    return result;
}

}  // namespace

auto TargetGraphInformation::DepsToJson() const noexcept -> nlohmann::json {
    auto result = nlohmann::json::object();
    result["declared"] = NodesToString(direct_);
    result["implicit"] = NodesToString(implicit_);
    result["anonymous"] = NodesToString(anonymous_);
    return result;
}
