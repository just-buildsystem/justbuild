// Copyright 2023 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILD_SERVE_API_SERVE_SERVICE_TARGET_UTILS_HPP
#define INCLUDED_SRC_BUILD_SERVE_API_SERVE_SERVICE_TARGET_UTILS_HPP

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "gsl/gsl"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/serve_api/remote/config.hpp"
#include "src/buildtool/storage/config.hpp"

// Methods used by ServeTarget remote service

/// \brief Check if tree exists in the given repository.
[[nodiscard]] auto IsTreeInRepo(std::string const& tree_id,
                                std::filesystem::path const& repo_path,
                                std::shared_ptr<Logger> const& logger) -> bool;

/// \brief For a given tree id, find the known repository that can serve it.
[[nodiscard]] auto GetServingRepository(RemoteServeConfig const& serve_config,
                                        StorageConfig const& storage_config,
                                        std::string const& tree_id,
                                        std::shared_ptr<Logger> const& logger)
    -> std::optional<std::filesystem::path>;

/// \brief Parse the stored repository configuration blob and populate the
/// RepositoryConfig instance.
/// \returns nullopt on success, error message as a string otherwise.
[[nodiscard]] auto DetermineRoots(
    RemoteServeConfig const& serve_config,
    gsl::not_null<StorageConfig const*> storage_config,
    std::string const& main_repo,
    std::filesystem::path const& repo_config_path,
    gsl::not_null<RepositoryConfig*> const& repository_config,
    std::shared_ptr<Logger> const& logger) -> std::optional<std::string>;

// Methods used by ServeTargetVariables remote service

/// \brief Get the blob content at given path inside a Git tree.
/// \returns If tree found, pair of "no-internal-errors" flag and content of
/// blob at the path specified if blob exists, nullopt otherwise.
[[nodiscard]] auto GetBlobContent(std::filesystem::path const& repo_path,
                                  std::string const& tree_id,
                                  std::string const& rel_path,
                                  std::shared_ptr<Logger> const& logger)
    -> std::optional<std::pair<bool, std::optional<std::string>>>;

#endif  // INCLUDED_SRC_BUILD_SERVE_API_SERVE_SERVICE_TARGET_UTILS_HPP
