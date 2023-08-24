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

#include "src/other_tools/just_mr/setup_utils.hpp"

#include <fstream>
#include <unordered_set>

#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/other_tools/just_mr/exit_codes.hpp"

namespace JustMR::Utils {

void ReachableRepositories(
    ExpressionPtr const& repos,
    std::string const& main,
    std::shared_ptr<JustMR::SetupRepos> const& setup_repos) {
    // use temporary sets to avoid duplicates
    std::unordered_set<std::string> include_repos_set{};
    if (repos->IsMap()) {
        // traversal of bindings
        std::function<void(std::string const&)> traverse =
            [&](std::string const& repo_name) {
                if (not include_repos_set.contains(repo_name)) {
                    // if not found, add it and repeat for its bindings
                    include_repos_set.insert(repo_name);
                    // check bindings
                    auto repos_repo_name =
                        repos->Get(repo_name, Expression::none_t{});
                    if (not repos_repo_name.IsNotNull()) {
                        return;
                    }
                    auto bindings =
                        repos_repo_name->Get("bindings", Expression::none_t{});
                    if (bindings.IsNotNull() and bindings->IsMap()) {
                        for (auto const& bound : bindings->Map().Values()) {
                            if (bound.IsNotNull() and bound->IsString()) {
                                traverse(bound->String());
                            }
                        }
                    }
                }
            };
        traverse(main);  // traverse all bindings of main repository

        // Add overlay repositories
        std::unordered_set<std::string> setup_repos_set{include_repos_set};
        for (auto const& repo : include_repos_set) {
            auto repos_repo = repos->Get(repo, Expression::none_t{});
            if (repos_repo.IsNotNull()) {
                // copy over any present alternative root dirs
                for (auto const& layer : kAltDirs) {
                    auto layer_val =
                        repos_repo->Get(layer, Expression::none_t{});
                    if (layer_val.IsNotNull() and layer_val->IsString()) {
                        auto repo_name = layer_val->String();
                        setup_repos_set.insert(repo_name);
                    }
                }
            }
        }

        // copy to vectors
        setup_repos->to_setup.clear();
        setup_repos->to_setup.reserve(setup_repos_set.size());
        std::copy(
            setup_repos_set.begin(),
            setup_repos_set.end(),
            std::inserter(setup_repos->to_setup, setup_repos->to_setup.end()));
        setup_repos->to_include.clear();
        setup_repos->to_include.reserve(include_repos_set.size());
        std::copy(include_repos_set.begin(),
                  include_repos_set.end(),
                  std::inserter(setup_repos->to_include,
                                setup_repos->to_include.end()));
    }
}

void DefaultReachableRepositories(
    ExpressionPtr const& repos,
    std::shared_ptr<JustMR::SetupRepos> const& setup_repos) {
    if (repos.IsNotNull() and repos->IsMap()) {
        setup_repos->to_setup = repos->Map().Keys();
        setup_repos->to_include = setup_repos->to_setup;
    }
}

auto ReadConfiguration(
    std::optional<std::filesystem::path> const& config_file_opt) noexcept
    -> std::shared_ptr<Configuration> {
    if (not config_file_opt) {
        Logger::Log(LogLevel::Error, "Cannot find repository configuration.");
        std::exit(kExitConfigError);
    }
    auto const& config_file = *config_file_opt;

    std::shared_ptr<Configuration> config{nullptr};
    if (not FileSystemManager::IsFile(config_file)) {
        Logger::Log(LogLevel::Error,
                    "Cannot read config file {}.",
                    config_file.string());
        std::exit(kExitConfigError);
    }
    try {
        std::ifstream fs(config_file);
        auto map = Expression::FromJson(nlohmann::json::parse(fs));
        if (not map->IsMap()) {
            Logger::Log(LogLevel::Error,
                        "Config file {} does not contain a JSON object.",
                        config_file.string());
            std::exit(kExitConfigError);
        }
        config = std::make_shared<Configuration>(map);
    } catch (std::exception const& e) {
        Logger::Log(LogLevel::Error,
                    "Parsing config file {} failed with error:\n{}",
                    config_file.string(),
                    e.what());
        std::exit(kExitConfigError);
    }
    return config;
}

}  // namespace JustMR::Utils
