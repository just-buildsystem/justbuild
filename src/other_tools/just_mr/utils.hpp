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

#include <unordered_set>

#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/buildtool/main/constants.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/utils/cpp/tmp_dir.hpp"

/* Paths and constants required by just-mr */

std::unordered_set<std::string> const kLocationTypes{"workspace",
                                                     "home",
                                                     "system"};
auto const kDefaultJustPath = "just";
auto const kDefaultGitPath = "git";
auto const kDefaultRCPath = StorageConfig::GetUserHome() / ".just-mrrc";
auto const kDefaultBuildRoot = StorageConfig::kDefaultBuildRoot;
auto const kDefaultCheckoutLocationsFile =
    StorageConfig::GetUserHome() / ".just-local.json";
auto const kDefaultDistdirs = StorageConfig::GetUserHome() / ".distfiles";

std::vector<std::string> const kAltDirs = {"target_root",
                                           "rule_root",
                                           "expression_root"};

std::vector<std::string> const kTakeOver = {"bindings",
                                            "target_file_name",
                                            "rule_file_name",
                                            "expression_file_name"};

struct JustSubCmdFlags {
    bool config;
    bool build_root;
    bool launch;
};

// ordered, so that we have replicability
std::map<std::string, JustSubCmdFlags> const kKnownJustSubcommands{
    {"version", {false /*config*/, false /*build_root*/, false /*launch*/}},
    {"describe", {true /*config*/, false /*build_root*/, false /*launch*/}},
    {"analyse", {true /*config*/, true /*build_root*/, false /*launch*/}},
    {"build", {true /*config*/, true /*build_root*/, true /*launch*/}},
    {"install", {true /*config*/, true /*build_root*/, true /*launch*/}},
    {"rebuild", {true /*config*/, true /*build_root*/, true /*launch*/}},
    {"install-cas", {false /*config*/, true /*build_root*/, false /*launch*/}},
    {"gc", {false /*config*/, true /*build_root*/, false /*launch*/}}};

nlohmann::json const kDefaultConfigLocations = nlohmann::json::array(
    {{{"root", "workspace"}, {"path", "repos.json"}},
     {{"root", "workspace"}, {"path", "etc/repos.json"}},
     {{"root", "home"}, {"path", ".just-repos.json"}},
     {{"root", "system"}, {"path", "etc/just-repos.json"}}});

/// \brief Checkout type enum
enum class CheckoutType : std::uint8_t { Git, Archive, File, Distdir, GitTree };

/// \brief Checkout type map
std::unordered_map<std::string, CheckoutType> const kCheckoutTypeMap = {
    {"git", CheckoutType::Git},
    {"archive", CheckoutType::Archive},
    {"zip", CheckoutType::Archive},  // treated the same as "archive"
    {"file", CheckoutType::File},
    {"distdir", CheckoutType::Distdir},
    {"git tree", CheckoutType::GitTree}};

namespace JustMR {

struct Paths {
    // user-defined locations
    std::optional<std::filesystem::path> root{std::nullopt};
    std::filesystem::path setup_root{FileSystemManager::GetCurrentDirectory()};
    std::optional<std::filesystem::path> workspace_root{
        // find workspace root
        []() -> std::optional<std::filesystem::path> {
            std::function<bool(std::filesystem::path const&)>
                is_workspace_root = [&](std::filesystem::path const& path) {
                    return std::any_of(
                        kRootMarkers.begin(),
                        kRootMarkers.end(),
                        [&path](auto const& marker) {
                            return FileSystemManager::Exists(path / marker);
                        });
                };
            // default path to current dir
            auto path = FileSystemManager::GetCurrentDirectory();
            auto root_path = path.root_path();
            while (true) {
                if (is_workspace_root(path)) {
                    return path;
                }
                if (path == root_path) {
                    return std::nullopt;
                }
                path = path.parent_path();
            }
        }()};
    nlohmann::json git_checkout_locations{};
    std::vector<std::filesystem::path> distdirs{};
};

struct CAInfo {
    bool no_ssl_verify{false};
    std::optional<std::filesystem::path> ca_bundle{std::nullopt};
};

using PathsPtr = std::shared_ptr<JustMR::Paths>;
using CAInfoPtr = std::shared_ptr<JustMR::CAInfo>;

namespace Utils {

/// \brief Get the Git cache root path.
[[nodiscard]] auto GetGitCacheRoot() noexcept -> std::filesystem::path;

/// \brief Get location of Git repository. Defaults to the Git cache root when
/// no better location is found.
[[nodiscard]] auto GetGitRoot(JustMR::PathsPtr const& just_mr_paths,
                              std::string const& repo_url) noexcept
    -> std::filesystem::path;

/// \brief Create a tmp directory with controlled lifetime for specific
/// operations (archive, zip, file, distdir checkouts; fetch; update).
[[nodiscard]] auto CreateTypedTmpDir(std::string const& type) noexcept
    -> TmpDirPtr;

/// \brief Get the path to the file storing the tree id of an archive
/// content.
[[nodiscard]] auto GetArchiveTreeIDFile(std::string const& repo_type,
                                        std::string const& content) noexcept
    -> std::filesystem::path;

/// \brief Get the path to the file storing the tree id of a distdir list
/// content.
[[nodiscard]] auto GetDistdirTreeIDFile(std::string const& content) noexcept
    -> std::filesystem::path;

/// \brief Write a tree id to file. The parent folder of the file must exist!
[[nodiscard]] auto WriteTreeIDFile(std::filesystem::path const& tree_id_file,
                                   std::string const& tree_id) noexcept -> bool;

/// \brief Add data to file CAS.
/// Returns the path to the file added to CAS, or nullopt if not added.
[[nodiscard]] auto AddToCAS(std::string const& data) noexcept
    -> std::optional<std::filesystem::path>;

/// \brief Try to add distfile to CAS.
void AddDistfileToCAS(std::filesystem::path const& distfile,
                      JustMR::PathsPtr const& just_mr_paths) noexcept;

/// \brief Recursive part of the ResolveRepo function.
/// Keeps track of repository names to detect any cyclic dependencies.
auto ResolveRepo(ExpressionPtr const& repo_desc,
                 ExpressionPtr const& repos,
                 gsl::not_null<std::unordered_set<std::string>*> const& seen)
    -> std::optional<ExpressionPtr>;

/// \brief Resolves any cyclic dependency issues and follows the repository
/// dependencies until the one containing the WS root is found.
/// Returns a repository entry as an ExpressionPtr, or nullopt if cyclic
/// dependency found.
auto ResolveRepo(ExpressionPtr const& repo_desc,
                 ExpressionPtr const& repos) noexcept
    -> std::optional<ExpressionPtr>;

}  // namespace Utils

}  // namespace JustMR

#endif  // INCLUDED_SRC_OTHER_TOOLS_JUST_MR_UTILS_HPP
