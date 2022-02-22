#ifndef INCLUDED_SRC_BUILDTOOL_BUILDENGINE_EXPRESSION_TARGET_RESULT_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILDENGINE_EXPRESSION_TARGET_RESULT_HPP

#include "src/buildtool/build_engine/expression/expression_ptr.hpp"
#include "src/utils/cpp/hash_combine.hpp"

struct TargetResult {
    ExpressionPtr artifact_stage{};
    ExpressionPtr provides{};
    ExpressionPtr runfiles{};
    bool is_cacheable{provides.IsCacheable()};

    [[nodiscard]] auto operator==(TargetResult const& other) const noexcept
        -> bool {
        return artifact_stage == other.artifact_stage and
               provides == other.provides and runfiles == other.runfiles;
    }
};

namespace std {
template <>
struct std::hash<TargetResult> {
    [[nodiscard]] auto operator()(TargetResult const& r) noexcept
        -> std::size_t {
        auto seed = std::hash<ExpressionPtr>{}(r.artifact_stage);
        hash_combine(&seed, std::hash<ExpressionPtr>{}(r.provides));
        hash_combine(&seed, std::hash<ExpressionPtr>{}(r.runfiles));
        return seed;
    }
};
}  // namespace std

#endif  // INCLUDED_SRC_BUILDTOOL_BUILDENGINE_EXPRESSION_TARGET_RESULT_HPP
