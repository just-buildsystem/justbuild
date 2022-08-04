#ifndef INCLUDED_SRC_BUILDOOL_MAIN_ANALYSE_HPP
#define INCLUDED_SRC_BUILDOOL_MAIN_ANALYSE_HPP

#include "src/buildtool/build_engine/analysed_target/analysed_target.hpp"
#include "src/buildtool/build_engine/target_map/configured_target.hpp"
#include "src/buildtool/build_engine/target_map/result_map.hpp"
#include "src/buildtool/common/cli.hpp"

struct AnalysisResult {
    BuildMaps::Target::ConfiguredTarget id;
    AnalysedTargetPtr target;
    std::optional<std::string> modified;
};

[[nodiscard]] auto AnalyseTarget(
    const BuildMaps::Target::ConfiguredTarget& id,
    gsl::not_null<BuildMaps::Target::ResultTargetMap*> const& result_map,
    std::size_t jobs,
    AnalysisArguments const& clargs) -> std::optional<AnalysisResult>;
#endif
