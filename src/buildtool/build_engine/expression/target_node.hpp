#ifndef INCLUDED_SRC_BUILDTOOL_BUILDENGINE_EXPRESSION_TARGET_NODE_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILDENGINE_EXPRESSION_TARGET_NODE_HPP

#include <type_traits>
#include <variant>

#include "src/buildtool/build_engine/expression/expression_ptr.hpp"
#include "src/buildtool/build_engine/expression/target_result.hpp"

class TargetNode {
    using Value = ExpressionPtr;  // store result type

  public:
    struct Abstract {
        std::string node_type;        // arbitrary string that maps to rule
        ExpressionPtr string_fields;  // map to list of strings
        ExpressionPtr target_fields;  // map to list of targets
        [[nodiscard]] auto IsCacheable() const noexcept -> bool;
    };

    template <class NodeType>
    requires(
        std::is_same_v<NodeType, Value> or
        std::is_same_v<NodeType, Abstract>) explicit TargetNode(NodeType node)
        : data_{std::move(node)},
          is_cacheable_{std::get<NodeType>(data_).IsCacheable()} {}

    [[nodiscard]] auto IsCacheable() const noexcept -> bool {
        return is_cacheable_;
    }

    [[nodiscard]] auto IsValue() const noexcept {
        return std::holds_alternative<Value>(data_);
    }

    [[nodiscard]] auto IsAbstract() const noexcept {
        return std::holds_alternative<Abstract>(data_);
    }

    [[nodiscard]] auto GetValue() const -> Value const& {
        return std::get<Value>(data_);
    }

    [[nodiscard]] auto GetAbstract() const -> Abstract const& {
        return std::get<Abstract>(data_);
    }

    [[nodiscard]] auto operator==(TargetNode const& other) const noexcept
        -> bool {
        if (data_.index() == other.data_.index()) {
            try {
                if (IsValue()) {
                    return GetValue() == other.GetValue();
                }
                auto const& abs_l = GetAbstract();
                auto const& abs_r = other.GetAbstract();
                return abs_l.node_type == abs_r.node_type and
                       abs_l.string_fields == abs_r.string_fields and
                       abs_l.target_fields == abs_r.string_fields;
            } catch (...) {
                // should never happen
            }
        }
        return false;
    }

    [[nodiscard]] auto ToString() const noexcept -> std::string {
        try {
            return ToJson().dump();
        } catch (...) {
            // should never happen
        }
        return {};
    }

    [[nodiscard]] auto ToJson() const -> nlohmann::json;

  private:
    std::variant<Value, Abstract> data_;
    bool is_cacheable_;
};

#endif  // INCLUDED_SRC_BUILDTOOL_BUILDENGINE_EXPRESSION_TARGET_NODE_HPP
