#include "src/buildtool/build_engine/analysed_target/analysed_target.hpp"

namespace {

// NOLINTNEXTLINE(misc-no-recursion)
void CollectNonKnownArtifacts(
    ExpressionPtr const& expr,
    gsl::not_null<std::vector<ArtifactDescription>*> const& artifacts,
    gsl::not_null<std::unordered_set<ExpressionPtr>*> const& traversed) {
    if (not traversed->contains(expr)) {
        if (expr->IsMap()) {
            auto result_map = Expression::map_t::underlying_map_t{};
            for (auto const& [key, val] : expr->Map()) {
                CollectNonKnownArtifacts(val, artifacts, traversed);
            }
        }
        else if (expr->IsList()) {
            auto result_list = Expression::list_t{};
            result_list.reserve(expr->List().size());
            for (auto const& val : expr->List()) {
                CollectNonKnownArtifacts(val, artifacts, traversed);
            }
        }
        else if (expr->IsNode()) {
            auto const& node = expr->Node();
            if (node.IsAbstract()) {
                CollectNonKnownArtifacts(
                    node.GetAbstract().target_fields, artifacts, traversed);
            }
            else {
                // value node
                CollectNonKnownArtifacts(node.GetValue(), artifacts, traversed);
            }
        }
        else if (expr->IsResult()) {
            auto const& result = expr->Result();
            CollectNonKnownArtifacts(
                result.artifact_stage, artifacts, traversed);
            CollectNonKnownArtifacts(result.runfiles, artifacts, traversed);
            CollectNonKnownArtifacts(result.provides, artifacts, traversed);
        }
        else if (expr->IsArtifact()) {
            auto const& artifact = expr->Artifact();
            if (not artifact.IsKnown()) {
                artifacts->emplace_back(artifact);
            }
        }
        traversed->emplace(expr);
    }
}

}  // namespace

auto AnalysedTarget::ContainedNonKnownArtifacts() const
    -> std::vector<ArtifactDescription> {
    auto artifacts = std::vector<ArtifactDescription>{};
    auto traversed = std::unordered_set<ExpressionPtr>{};
    CollectNonKnownArtifacts(Artifacts(), &artifacts, &traversed);
    CollectNonKnownArtifacts(RunFiles(), &artifacts, &traversed);
    CollectNonKnownArtifacts(Provides(), &artifacts, &traversed);
    return artifacts;
}
