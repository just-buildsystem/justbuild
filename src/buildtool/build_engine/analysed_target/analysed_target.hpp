#ifndef INCLUDED_SRC_BUILDTOOL_BUILDENGINE_ANALYSED_TARGET_ANALYSED_TARGET_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILDENGINE_ANALYSED_TARGET_ANALYSED_TARGET_HPP

#include <memory>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/buildtool/build_engine/expression/expression_ptr.hpp"
#include "src/buildtool/build_engine/expression/target_result.hpp"
#include "src/buildtool/common/action_description.hpp"
#include "src/buildtool/common/tree.hpp"

class AnalysedTarget {
  public:
    AnalysedTarget(TargetResult result,
                   std::vector<ActionDescription> actions,
                   std::vector<std::string> blobs,
                   std::vector<Tree> trees,
                   std::unordered_set<std::string> vars,
                   std::set<std::string> tainted)
        : result_{std::move(result)},
          actions_{std::move(actions)},
          blobs_{std::move(blobs)},
          trees_{std::move(trees)},
          vars_{std::move(vars)},
          tainted_{std::move(tainted)} {}

    [[nodiscard]] auto Actions() const& noexcept
        -> std::vector<ActionDescription> const& {
        return actions_;
    }
    [[nodiscard]] auto Actions() && noexcept -> std::vector<ActionDescription> {
        return std::move(actions_);
    }
    [[nodiscard]] auto Artifacts() const& noexcept -> ExpressionPtr const& {
        return result_.artifact_stage;
    }
    [[nodiscard]] auto Artifacts() && noexcept -> ExpressionPtr {
        return std::move(result_.artifact_stage);
    }
    [[nodiscard]] auto RunFiles() const& noexcept -> ExpressionPtr const& {
        return result_.runfiles;
    }
    [[nodiscard]] auto RunFiles() && noexcept -> ExpressionPtr {
        return std::move(result_.runfiles);
    }
    [[nodiscard]] auto Provides() const& noexcept -> ExpressionPtr const& {
        return result_.provides;
    }
    [[nodiscard]] auto Provides() && noexcept -> ExpressionPtr {
        return std::move(result_.provides);
    }
    [[nodiscard]] auto Blobs() const& noexcept
        -> std::vector<std::string> const& {
        return blobs_;
    }
    [[nodiscard]] auto Trees() && noexcept -> std::vector<Tree> {
        return std::move(trees_);
    }
    [[nodiscard]] auto Trees() const& noexcept -> std::vector<Tree> const& {
        return trees_;
    }
    [[nodiscard]] auto Blobs() && noexcept -> std::vector<std::string> {
        return std::move(blobs_);
    }
    [[nodiscard]] auto Vars() const& noexcept
        -> std::unordered_set<std::string> const& {
        return vars_;
    }
    [[nodiscard]] auto Vars() && noexcept -> std::unordered_set<std::string> {
        return std::move(vars_);
    }
    [[nodiscard]] auto Tainted() const& noexcept
        -> std::set<std::string> const& {
        return tainted_;
    }
    [[nodiscard]] auto Tainted() && noexcept -> std::set<std::string> {
        return std::move(tainted_);
    }
    [[nodiscard]] auto Result() const& noexcept -> TargetResult const& {
        return result_;
    }
    [[nodiscard]] auto Result() && noexcept -> TargetResult {
        return std::move(result_);
    }

  private:
    TargetResult result_;
    std::vector<ActionDescription> actions_;
    std::vector<std::string> blobs_;
    std::vector<Tree> trees_;
    std::unordered_set<std::string> vars_;
    std::set<std::string> tainted_;
};

using AnalysedTargetPtr = std::shared_ptr<AnalysedTarget>;

#endif  // INCLUDED_SRC_BUILDTOOL_BUILDENGINE_ANALYSED_TARGET_ANALYSED_TARGET_HPP
