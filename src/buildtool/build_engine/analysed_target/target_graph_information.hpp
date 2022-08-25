#ifndef INCLUDED_SRC_BUILDTOOL_BUILDENGINE_ANALYSED_TARGET_TARGET_GRAPH_INFORMATION_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILDENGINE_ANALYSED_TARGET_TARGET_GRAPH_INFORMATION_HPP

#include <optional>

#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/target_map/configured_target.hpp"

class TargetGraphInformation {
  public:
    TargetGraphInformation(
        BuildMaps::Target::ConfiguredTargetPtr node,
        std::vector<BuildMaps::Target::ConfiguredTargetPtr> direct,
        std::vector<BuildMaps::Target::ConfiguredTargetPtr> implicit,
        std::vector<BuildMaps::Target::ConfiguredTargetPtr> anonymous)
        : node_{std::move(node)},
          direct_{std::move(direct)},
          implicit_{std::move(implicit)},
          anonymous_{std::move(anonymous)} {}

    static const TargetGraphInformation kSource;

    [[nodiscard]] auto Node() const noexcept
        -> BuildMaps::Target::ConfiguredTargetPtr {
        return node_;
    }

    [[nodiscard]] auto NodeString() const noexcept
        -> std::optional<std::string>;
    [[nodiscard]] auto DepsToJson() const noexcept -> nlohmann::json;

  private:
    BuildMaps::Target::ConfiguredTargetPtr node_;
    std::vector<BuildMaps::Target::ConfiguredTargetPtr> direct_;
    std::vector<BuildMaps::Target::ConfiguredTargetPtr> implicit_;
    std::vector<BuildMaps::Target::ConfiguredTargetPtr> anonymous_;
};

inline const TargetGraphInformation TargetGraphInformation::kSource =
    TargetGraphInformation{nullptr, {}, {}, {}};
#endif
