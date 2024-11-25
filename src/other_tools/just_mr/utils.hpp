// Copyright 2022 Huawei Cloud Computing Technology Co., Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef INCLUDED_SRC_OTHER_TOOLS_JUST_MR_UTILS_HPP
#define INCLUDED_SRC_OTHER_TOOLS_JUST_MR_UTILS_HPP

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "gsl/gsl"
#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/expression/expression_ptr.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/storage/config.hpp"

/* Paths and constants required by just-mr */

auto const kDefaultJustPath = "just";
auto const kDefaultGitPath = "git";
auto const kDefaultRCPath = FileSystemManager::GetUserHome() / ".just-mrrc";
auto const kDefaultBuildRoot = StorageConfig::kDefaultBuildRoot;
auto const kDefaultCheckoutLocationsFile =
    FileSystemManager::GetUserHome() / ".just-local.json";
auto const kDefaultDistdirs = FileSystemManager::GetUserHome() / ".distfiles";

std::vector<std::string> const kTakeOver = {"bindings",
                                            "target_file_name",
                                            "rule_file_name",
                                            "expression_file_name"};

struct JustSubCmdFlags {
    bool config;        // requires setup
    bool build_root;    // supports the local build root arg
    bool launch;        // supports the local launcher arg
    bool defines;       // supports defines arg
    bool remote;        // supports remote exec args, including client-side auth
    bool remote_props;  // supports remote-execution properties
    bool serve;         // supports a serve endpoint
    bool dispatch;      // supports dispatching of the remote-execution endpoint
};

// ordered, so that we have replicability
std::map<std::string, JustSubCmdFlags> const kKnownJustSubcommands{
    {"version",
     {.config = false,
      .build_root = false,
      .launch = false,
      .defines = false,
      .remote = false,
      .remote_props = false,
      .serve = false,
      .dispatch = false}},
    {"describe",
     {.config = true,
      .build_root = true,
      .launch = false,
      .defines = true,
      .remote = true,
      .remote_props = false,
      .serve = true,
      .dispatch = false}},
    {"analyse",
     {.config = true,
      .build_root = true,
      .launch = false,
      .defines = true,
      .remote = true,
      .remote_props = true,
      .serve = true,
      .dispatch = true}},
    {"build",
     {.config = true,
      .build_root = true,
      .launch = true,
      .defines = true,
      .remote = true,
      .remote_props = true,
      .serve = true,
      .dispatch = true}},
    {"install",
     {.config = true,
      .build_root = true,
      .launch = true,
      .defines = true,
      .remote = true,
      .remote_props = true,
      .serve = true,
      .dispatch = true}},
    {"rebuild",
     {.config = true,
      .build_root = true,
      .launch = true,
      .defines = true,
      .remote = true,
      .remote_props = true,
      .serve = true,
      .dispatch = true}},
    {"add-to-cas",
     {.config = false,
      .build_root = true,
      .launch = false,
      .defines = false,
      .remote = true,
      .remote_props = false,
      .serve = false,
      .dispatch = false}},
    {"install-cas",
     {.config = false,
      .build_root = true,
      .launch = false,
      .defines = false,
      .remote = true,
      .remote_props = false,
      .serve = false,
      .dispatch = false}},
    {"gc",
     {.config = false,
      .build_root = true,
      .launch = false,
      .defines = false,
      .remote = false,
      .remote_props = false,
      .serve = false,
      .dispatch = false}}};

nlohmann::json const kDefaultConfigLocations = nlohmann::json::array(
    {{{"root", "workspace"}, {"path", "repos.json"}},
     {{"root", "workspace"}, {"path", "etc/repos.json"}},
     {{"root", "home"}, {"path", ".just-repos.json"}},
     {{"root", "system"}, {"path", "etc/just-repos.json"}}});

/// \brief Checkout type enum
enum class CheckoutType : std::uint8_t {
    Git,
    Archive,
    ForeignFile,
    File,
    Distdir,
    GitTree,
    Computed
};

/// \brief Checkout type map
std::unordered_map<std::string, CheckoutType> const kCheckoutTypeMap = {
    {"git", CheckoutType::Git},
    {"archive", CheckoutType::Archive},
    {"zip", CheckoutType::Archive},  // treated the same as "archive"
    {"foreign file", CheckoutType::ForeignFile},
    {"file", CheckoutType::File},
    {"distdir", CheckoutType::Distdir},
    {"git tree", CheckoutType::GitTree},
    {"computed", CheckoutType::Computed}};
namespace JustMR::Utils {

/// \brief Recursive part of the ResolveRepo function.
/// Keeps track of repository names to detect any cyclic dependencies.
/// \param repos ExpressionPtr of Map type.
[[nodiscard]] auto ResolveRepo(
    ExpressionPtr const& repo_desc,
    ExpressionPtr const& repos,
    gsl::not_null<std::unordered_set<std::string>*> const& seen)
    -> std::optional<ExpressionPtr>;

/// \brief Resolves any cyclic dependency issues and follows the repository
/// dependencies until the one containing the workspace root is found.
/// Returns a repository entry as an ExpressionPtr, or nullopt if cyclic
/// dependency found.
/// \param repos ExpressionPtr of Map type.
[[nodiscard]] auto ResolveRepo(ExpressionPtr const& repo_desc,
                               ExpressionPtr const& repos) noexcept
    -> std::optional<ExpressionPtr>;

}  // namespace JustMR::Utils

#endif  // INCLUDED_SRC_OTHER_TOOLS_JUST_MR_UTILS_HPP
