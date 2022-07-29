
#ifndef INCLUDED_SRC_BUILDTOOL_MAIN_DESCRIBE_HPP
#define INCLUDED_SRC_BUILDTOOL_MAIN_DESCRIBE_HPP

#include "src/buildtool/build_engine/base_maps/entity_name.hpp"
#include "src/buildtool/build_engine/target_map/configured_target.hpp"

[[nodiscard]] auto DescribeTarget(BuildMaps::Target::ConfiguredTarget const& id,
                                  std::size_t jobs,
                                  bool print_json) -> int;

[[nodiscard]] auto DescribeUserDefinedRule(
    BuildMaps::Base::EntityName const& rule_name,
    std::size_t jobs,
    bool print_json) -> int;

#endif  // INCLUDED_SRC_BUILDTOOL_MAIN_DESCRIBE_HPP
