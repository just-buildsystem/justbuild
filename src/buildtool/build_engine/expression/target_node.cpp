#include "src/buildtool/build_engine/expression/target_node.hpp"

#include "src/buildtool/build_engine/expression/expression.hpp"

auto TargetNode::Abstract::IsCacheable() const noexcept -> bool {
    return target_fields->IsCacheable();
}

auto TargetNode::ToJson() const -> nlohmann::json {
    if (IsValue()) {
        return {{"type", "VALUE_NODE"}, {"result", GetValue()->ToJson()}};
    }
    auto const& data = GetAbstract();
    return {{"type", "ABSTRACT_NODE"},
            {"node_type", data.node_type},
            {"string_fields", data.string_fields->ToJson()},
            {"target_fields",
             data.target_fields->ToJson(
                 Expression::JsonMode::SerializeAllButNodes)}};
}
