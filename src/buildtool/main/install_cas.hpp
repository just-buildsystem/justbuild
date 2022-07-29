
#ifndef INCLUDED_SRC_BUILDTOOL_MAIN_INSTALL_CAS_HPP
#define INCLUDED_SRC_BUILDTOOL_MAIN_INSTALL_CAS_HPP

#include <string>

#include <gsl-lite/gsl-lite.h>

#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/cli.hpp"
#ifndef BOOTSTRAP_BUILD_TOOL
#include "src/buildtool/execution_api/common/execution_api.hpp"
#endif

[[nodiscard]] auto ObjectInfoFromLiberalString(std::string const& s) noexcept
    -> Artifact::ObjectInfo;

#ifndef BOOTSTRAP_BUILD_TOOL
[[nodiscard]] auto FetchAndInstallArtifacts(
    gsl::not_null<IExecutionApi*> const& api,
    FetchArguments const& clargs) -> bool;
#endif

#endif  // INCLUDED_SRC_BUILDTOOL_MAIN_INSTALL_CAS_HPP
