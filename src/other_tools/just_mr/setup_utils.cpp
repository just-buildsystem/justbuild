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

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iterator>
#include <unordered_set>
#include <utility>
#include <variant>

#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/other_tools/just_mr/exit_codes.hpp"
#include "src/utils/cpp/expected.hpp"

namespace {

void WarnUnknownKeys(std::string const& name, ExpressionPtr const& repo_def) {
    if (not repo_def->IsMap()) {
        return;
    }
    for (auto const& [key, value] : repo_def->Map()) {
        if (not kRepositoryExpectedFields.contains(key)) {
            Logger::Log(std::any_of(kRepositoryPossibleFieldTrunks.begin(),
                                    kRepositoryPossibleFieldTrunks.end(),
                                    [k = key](auto const& trunk) {
                                        return k.find(trunk) !=
                                               std::string::npos;
                                    })
                            ? LogLevel::Debug
                            : LogLevel::Warning,
                        "Ignoring unknown field {} in repository {}",
                        key,
                        name);
        }
    }
}

}  // namespace

namespace JustMR::Utils {

void ReachableRepositories(
    ExpressionPtr const& repos,
    std::string const& main,
    std::shared_ptr<JustMR::SetupRepos> const& setup_repos) {
    // use temporary sets to avoid duplicates
    std::unordered_set<std::string> include_repos_set{};
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
                WarnUnknownKeys(repo_name, repos_repo_name);
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
                auto layer_val = repos_repo->Get(layer, Expression::none_t{});
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
    std::copy(
        include_repos_set.begin(),
        include_repos_set.end(),
        std::inserter(setup_repos->to_include, setup_repos->to_include.end()));
}

void DefaultReachableRepositories(
    ExpressionPtr const& repos,
    std::shared_ptr<JustMR::SetupRepos> const& setup_repos) {
    setup_repos->to_setup = repos->Map().Keys();
    setup_repos->to_include = setup_repos->to_setup;
}

auto ReadConfiguration(
    std::optional<std::filesystem::path> const& config_file_opt,
    std::optional<std::filesystem::path> const& absent_file_opt)
    -> std::shared_ptr<Configuration> {
    if (not config_file_opt) {
        Logger::Log(LogLevel::Error, "Cannot find repository configuration.");
        std::exit(kExitConfigError);
    }
    auto const& config_file = *config_file_opt;

    auto config = nlohmann::json::object();
    if (not FileSystemManager::IsFile(config_file)) {
        Logger::Log(LogLevel::Error,
                    "Cannot read config file {}.",
                    config_file.string());
        std::exit(kExitConfigError);
    }
    try {
        std::ifstream fs(config_file);
        config = nlohmann::json::parse(fs);
        if (not config.is_object()) {
            Logger::Log(LogLevel::Error,
                        "Config file {} does not contain a JSON object.",
                        config_file.string());
            std::exit(kExitConfigError);
        }
    } catch (std::exception const& e) {
        Logger::Log(LogLevel::Error,
                    "Parsing config file {} failed with error:\n{}",
                    config_file.string(),
                    e.what());
        std::exit(kExitConfigError);
    }

    if (absent_file_opt) {
        if (not FileSystemManager::IsFile(*absent_file_opt)) {
            Logger::Log(LogLevel::Error,
                        "Not file specifying the absent repositories: {}",
                        absent_file_opt->string());
            std::exit(kExitConfigError);
        }
        try {
            std::ifstream fs(*absent_file_opt);
            auto absent = nlohmann::json::parse(fs);
            if (not absent.is_array()) {
                Logger::Log(LogLevel::Error,
                            "Expected {} to contain a list of repository "
                            "names, but found {}",
                            absent_file_opt->string(),
                            absent.dump());
                std::exit(kExitConfigError);
            }
            std::unordered_set<std::string> absent_set{};
            for (auto const& repo : absent) {
                if (not repo.is_string()) {
                    Logger::Log(LogLevel::Error,
                                "Repositories names have to be strings, but "
                                "found entry {} in {}",
                                repo.dump(),
                                absent_file_opt->string());
                    std::exit(kExitConfigError);
                }
                absent_set.insert(repo);
            }
            auto new_repos = nlohmann::json::object();
            auto repos = config.value("repositories", nlohmann::json::object());
            for (auto const& [key, val] : repos.items()) {
                new_repos[key] = val;
                auto ws = val.value("repository", nlohmann::json::object());
                if (ws.is_object()) {
                    auto pragma = ws.value("pragma", nlohmann::json::object());
                    pragma["absent"] = absent_set.contains(key);
                    ws["pragma"] = pragma;
                    new_repos[key]["repository"] = ws;
                }
            }
            config["repositories"] = new_repos;
        } catch (std::exception const& e) {
            Logger::Log(LogLevel::Error,
                        "Parsing absent-repos file {} failed with error:\n{}",
                        absent_file_opt->string(),
                        e.what());
            std::exit(kExitConfigError);
        }
    }

    try {
        return std::make_shared<Configuration>(Expression::FromJson(config));
    } catch (std::exception const& e) {
        Logger::Log(LogLevel::Error,
                    "Parsing configuration file failed with error:\n{}",
                    e.what());
        std::exit(kExitConfigError);
    }
}

auto CreateAuthConfig(MultiRepoRemoteAuthArguments const& authargs) noexcept
    -> std::optional<Auth> {

    Auth::TLS::Builder tls_builder;
    tls_builder.SetCACertificate(authargs.tls_ca_cert)
        .SetClientCertificate(authargs.tls_client_cert)
        .SetClientKey(authargs.tls_client_key);

    // create auth config (including validation)
    auto result = tls_builder.Build();
    if (result) {
        if (*result) {
            // correctly configured TLS/SSL certification
            return *std::move(*result);
        }
        Logger::Log(LogLevel::Error, result->error());
        return std::nullopt;
    }

    // no TLS/SSL configuration was given, and we currently support no other
    // certification method, so return an empty config (no certification)
    return Auth{};
}

auto CreateLocalExecutionConfig(MultiRepoCommonArguments const& cargs) noexcept
    -> std::optional<LocalExecutionConfig> {

    LocalExecutionConfig::Builder builder;
    if (cargs.local_launcher.has_value()) {
        builder.SetLauncher(*cargs.local_launcher);
    }

    auto config = builder.Build();
    if (config) {
        return *std::move(config);
    }
    Logger::Log(LogLevel::Error, config.error());
    return std::nullopt;
}

auto CreateRemoteExecutionConfig(
    std::optional<std::string> const& remote_exec_addr,
    std::optional<std::string> const& remote_serve_addr) noexcept
    -> std::optional<RemoteExecutionConfig> {
    // if only a serve endpoint address is given, we assume it is one that acts
    // also as remote-execution
    auto remote_addr = remote_exec_addr ? remote_exec_addr : remote_serve_addr;

    RemoteExecutionConfig::Builder builder;
    auto config = builder.SetRemoteAddress(remote_addr).Build();

    if (config) {
        return *std::move(config);
    }

    Logger::Log(LogLevel::Error, config.error());
    return std::nullopt;
}

auto CreateServeConfig(
    std::optional<std::string> const& remote_serve_addr) noexcept
    -> std::optional<RemoteServeConfig> {
    RemoteServeConfig::Builder builder;
    auto config = builder.SetRemoteAddress(remote_serve_addr).Build();

    if (config) {
        return *std::move(config);
    }

    Logger::Log(LogLevel::Error, config.error());
    return std::nullopt;
}

}  // namespace JustMR::Utils
