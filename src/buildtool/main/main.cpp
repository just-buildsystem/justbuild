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

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "gsl/gsl"
#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name.hpp"
#include "src/buildtool/build_engine/expression/evaluator.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/build_engine/target_map/target_map.hpp"
#include "src/buildtool/common/artifact_description.hpp"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/common/remote/remote_common.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/common/statistics.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/logging/log_config.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/log_sink_cmdline.hpp"
#include "src/buildtool/logging/log_sink_file.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/main/add_to_cas.hpp"
#include "src/buildtool/main/analyse.hpp"
#include "src/buildtool/main/analyse_context.hpp"
#include "src/buildtool/main/build_utils.hpp"
#include "src/buildtool/main/cli.hpp"
#include "src/buildtool/main/constants.hpp"
#include "src/buildtool/main/diagnose.hpp"
#include "src/buildtool/main/exit_codes.hpp"
#include "src/buildtool/main/install_cas.hpp"
#include "src/buildtool/main/version.hpp"
#include "src/buildtool/multithreading/async_map_consumer.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/buildtool/progress_reporting/progress.hpp"
#include "src/buildtool/serve_api/remote/serve_api.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/file_chunker.hpp"
#include "src/buildtool/storage/garbage_collector.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/buildtool/storage/target_cache.hpp"
#include "src/utils/cpp/concepts.hpp"
#include "src/utils/cpp/json.hpp"
#ifndef BOOTSTRAP_BUILD_TOOL
#include "fmt/core.h"
#include "src/buildtool/auth/authentication.hpp"
#include "src/buildtool/common/remote/retry_config.hpp"
#include "src/buildtool/execution_api/common/api_bundle.hpp"
#include "src/buildtool/execution_api/execution_service/server_implementation.hpp"
#include "src/buildtool/execution_api/local/config.hpp"
#include "src/buildtool/execution_api/local/context.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/execution_api/remote/context.hpp"
#include "src/buildtool/execution_engine/executor/context.hpp"
#include "src/buildtool/graph_traverser/graph_traverser.hpp"
#include "src/buildtool/main/describe.hpp"
#include "src/buildtool/main/retry.hpp"
#include "src/buildtool/main/serve.hpp"
#include "src/buildtool/progress_reporting/progress_reporter.hpp"
#include "src/buildtool/serve_api/remote/config.hpp"
#include "src/buildtool/serve_api/serve_service/serve_server_implementation.hpp"
#include "src/buildtool/storage/backend_description.hpp"
#include "src/buildtool/storage/garbage_collector.hpp"
#include "src/utils/cpp/gsl.hpp"
#endif  // BOOTSTRAP_BUILD_TOOL

namespace {

namespace Base = BuildMaps::Base;
namespace Target = BuildMaps::Target;

void SetupDefaultLogging() {
    LogConfig::SetLogLimit(kDefaultLogLevel);
    LogConfig::SetSinks({LogSinkCmdLine::CreateFactory()});
}

void SetupLogging(LogArguments const& clargs) {
    LogConfig::SetLogLimit(clargs.log_limit);
    LogConfig::SetSinks({LogSinkCmdLine::CreateFactory(
        not clargs.plain_log, clargs.restrict_stderr_log_limit)});
    for (auto const& log_file : clargs.log_files) {
        LogConfig::AddSink(LogSinkFile::CreateFactory(
            log_file,
            clargs.log_append ? LogSinkFile::Mode::Append
                              : LogSinkFile::Mode::Overwrite));
    }
}

[[nodiscard]] auto CreateStorageConfig(
    EndpointArguments const& eargs,
    HashFunction::Type hash_type,
    std::optional<ServerAddress> const& remote_address = std::nullopt,
    ExecutionProperties const& remote_platform_properties = {},
    std::vector<DispatchEndpoint> const& remote_dispatch = {}) noexcept
    -> std::optional<StorageConfig> {
    StorageConfig::Builder builder;
    if (eargs.local_root.has_value()) {
        builder.SetBuildRoot(*eargs.local_root);
    }

    auto config =
        builder.SetHashType(hash_type)
            .SetRemoteExecutionArgs(
                remote_address, remote_platform_properties, remote_dispatch)
            .Build();
    if (config) {
        return *std::move(config);
    }
    Logger::Log(LogLevel::Error, config.error());
    return std::nullopt;
}

#ifndef BOOTSTRAP_BUILD_TOOL
[[nodiscard]] auto CreateLocalExecutionConfig(
    BuildArguments const& bargs) noexcept
    -> std::optional<LocalExecutionConfig> {
    LocalExecutionConfig::Builder builder;
    if (bargs.local_launcher.has_value()) {
        builder.SetLauncher(*bargs.local_launcher);
    }

    auto config = builder.Build();
    if (config) {
        return *std::move(config);
    }
    Logger::Log(LogLevel::Error, config.error());
    return std::nullopt;
}

[[nodiscard]] auto CreateRemoteExecutionConfig(EndpointArguments const& eargs,
                                               RebuildArguments const& rargs)
    -> std::optional<RemoteExecutionConfig> {
    RemoteExecutionConfig::Builder builder;
    builder.SetRemoteAddress(eargs.remote_execution_address)
        .SetRemoteExecutionDispatch(eargs.remote_execution_dispatch_file)
        .SetPlatformProperties(eargs.platform_properties)
        .SetCacheAddress(rargs.cache_endpoint);

    auto config = builder.Build();
    if (config) {
        return *std::move(config);
    }
    Logger::Log(LogLevel::Error, config.error());
    return std::nullopt;
}

[[nodiscard]] auto CreateServeConfig(ServeArguments const& srvargs,
                                     CommonArguments const& cargs,
                                     BuildArguments const& bargs,
                                     TCArguments const& tc) noexcept
    -> std::optional<RemoteServeConfig> {
    RemoteServeConfig::Builder builder;
    builder.SetRemoteAddress(srvargs.remote_serve_address)
        .SetKnownRepositories(srvargs.repositories)
        .SetJobs(cargs.jobs)
        .SetActionTimeout(bargs.timeout)
        .SetTCStrategy(tc.target_cache_write_strategy);

    if (bargs.build_jobs > 0) {
        builder.SetBuildJobs(bargs.build_jobs);
    }

    auto config = builder.Build();
    if (config) {
        if (config->tc_strategy == TargetCacheWriteStrategy::Disable) {
            Logger::Log(
                LogLevel::Info,
                "Target-level cache writing of serve service is disabled.");
        }
        return *std::move(config);
    }

    Logger::Log(LogLevel::Error, config.error());
    return std::nullopt;
}

[[nodiscard]] auto CreateAuthConfig(
    CommonAuthArguments const& authargs,
    ClientAuthArguments const& client_authargs,
    ServerAuthArguments const& server_authargs) noexcept
    -> std::optional<Auth> {
    Auth::TLS::Builder tls_builder;
    tls_builder.SetCACertificate(authargs.tls_ca_cert)
        .SetClientCertificate(client_authargs.tls_client_cert)
        .SetClientKey(client_authargs.tls_client_key)
        .SetServerCertificate(server_authargs.tls_server_cert)
        .SetServerKey(server_authargs.tls_server_key);

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

void SetupFileChunker() {
    FileChunker::Initialize();
}

/// \brief Write backend description (which determines the target cache shard)
/// to CAS.
void StoreTargetCacheShard(
    StorageConfig const& storage_config,
    Storage const& storage,
    RemoteExecutionConfig const& remote_exec_config) noexcept {
    auto backend_description =
        DescribeBackend(remote_exec_config.remote_address,
                        remote_exec_config.platform_properties,
                        remote_exec_config.dispatch);
    if (not backend_description) {
        Logger::Log(LogLevel::Error, backend_description.error());
        std::exit(kExitFailure);
    }
    [[maybe_unused]] auto id = storage.CAS().StoreBlob(*backend_description);
    EnsuresAudit(id and id->hash() == storage_config.backend_description_id);
}

#endif  // BOOTSTRAP_BUILD_TOOL

// returns path relative to `root`.
[[nodiscard]] auto FindRoot(std::filesystem::path const& subdir,
                            FileRoot const& root,
                            std::vector<std::string> const& markers)
    -> std::optional<std::filesystem::path> {
    Expects(subdir.is_relative());
    auto current = subdir;
    while (true) {
        for (auto const& marker : markers) {
            if (root.Exists(current / marker)) {
                return current;
            }
        }
        if (current.empty()) {
            break;
        }
        current = current.parent_path();
    }
    return std::nullopt;
}

[[nodiscard]] auto ReadConfiguration(AnalysisArguments const& clargs) noexcept
    -> Configuration {
    Configuration config{};
    if (not clargs.config_file.empty()) {
        if (not FileSystemManager::Exists(clargs.config_file)) {
            Logger::Log(LogLevel::Error,
                        "Config file {} does not exist.",
                        clargs.config_file.string());
            std::exit(kExitFailure);
        }
        try {
            std::ifstream fs(clargs.config_file);
            auto map = Expression::FromJson(nlohmann::json::parse(fs));
            if (not map->IsMap()) {
                Logger::Log(LogLevel::Error,
                            "Config file {} does not contain a map.",
                            clargs.config_file.string());
                std::exit(kExitFailure);
            }
            config = Configuration{map};
        } catch (std::exception const& e) {
            Logger::Log(LogLevel::Error,
                        "Parsing config file {} failed with error:\n{}",
                        clargs.config_file.string(),
                        e.what());
            std::exit(kExitFailure);
        }
    }

    for (auto const& s : clargs.defines) {
        try {
            auto map = Expression::FromJson(nlohmann::json::parse(s));
            if (not map->IsMap()) {
                Logger::Log(LogLevel::Error,
                            "Defines entry {} does not contain a map.",
                            nlohmann::json(s).dump());
                std::exit(kExitFailure);
            }
            config = config.Update(map);
        } catch (std::exception const& e) {
            Logger::Log(LogLevel::Error,
                        "Parsing defines entry {} failed with error:\n{}",
                        nlohmann::json(s).dump(),
                        e.what());
            std::exit(kExitFailure);
        }
    }

    return config;
}

[[nodiscard]] auto DetermineCurrentModule(
    std::filesystem::path const& workspace_root,
    FileRoot const& target_root,
    std::string const& target_file_name) -> std::string {
    std::filesystem::path cwd{};
    try {
        cwd = std::filesystem::current_path();
    } catch (std::exception const& e) {
        Logger::Log(LogLevel::Warning,
                    "Cannot determine current working directory ({}), assuming "
                    "top-level module is requested",
                    e.what());
        return ".";
    }

    auto subdir = std::filesystem::proximate(cwd, workspace_root);
    if (subdir.is_relative() and (*subdir.begin() != "..")) {
        // cwd is subdir of workspace_root
        if (auto root_dir = FindRoot(subdir, target_root, {target_file_name})) {
            return root_dir->string();
        }
    }
    return ".";
}

[[nodiscard]] auto ReadConfiguredTarget(
    std::string const& main_repo,
    std::optional<std::filesystem::path> const& main_ws_root,
    gsl::not_null<const RepositoryConfig*> const& repo_config,
    AnalysisArguments const& clargs) -> Target::ConfiguredTarget {
    auto const* target_root = repo_config->TargetRoot(main_repo);
    if (target_root == nullptr) {
        Logger::Log(LogLevel::Error,
                    "Cannot obtain target root for main repo {}.",
                    main_repo);
        std::exit(kExitFailure);
    }
    auto current_module = std::string{"."};
    std::string target_file_name = *repo_config->TargetFileName(main_repo);
    if (main_ws_root) {
        // module detection only works if main workspace is on the file system
        current_module = DetermineCurrentModule(
            *main_ws_root, *target_root, target_file_name);
    }
    auto config = ReadConfiguration(clargs);
    if (clargs.target) {
        auto entity = Base::ParseEntityNameFromJson(
            *clargs.target,
            Base::EntityName{Base::NamedTarget{main_repo, current_module, ""}},
            repo_config,
            [&clargs](std::string const& parse_err) {
                Logger::Log(LogLevel::Error,
                            "Parsing target name {} failed with:\n{}",
                            clargs.target->dump(),
                            parse_err);
            });
        if (not entity) {
            std::exit(kExitFailure);
        }
        return Target::ConfiguredTarget{.target = std::move(*entity),
                                        .config = std::move(config)};
    }
#ifndef BOOTSTRAP_BUILD_TOOL
    if (target_root->IsAbsent()) {
        // since the user has not specified the target to build, we use the most
        // reasonable default value:
        //
        // module -> "." (i.e., current module)
        // target -> ""  (i.e., firstmost lexicographical target name)

        auto target = nlohmann::json::parse(R"([".",""])");
        Logger::Log(LogLevel::Debug,
                    "Detected absent target root for repo {} and no target was "
                    "given. Assuming default target {}",
                    main_repo,
                    target.dump());
        auto entity = Base::ParseEntityNameFromJson(
            target,
            Base::EntityName{Base::NamedTarget{main_repo, current_module, ""}},
            repo_config,
            [&target](std::string const& parse_err) {
                Logger::Log(LogLevel::Error,
                            "Parsing target name {} failed with:\n{}",
                            target.dump(),
                            parse_err);
            });
        if (not entity) {
            std::exit(kExitFailure);
        }
        return Target::ConfiguredTarget{.target = std::move(*entity),
                                        .config = std::move(config)};
    }
#endif
    auto const target_file =
        (std::filesystem::path{current_module} / target_file_name).string();
    if (not target_root->IsFile(target_file)) {
        Logger::Log(LogLevel::Error, "Expected file at {}.", target_file);
        std::exit(kExitFailure);
    }
    auto file_content = target_root->ReadContent(target_file);
    if (not file_content) {
        Logger::Log(LogLevel::Error, "Cannot read file {}.", target_file);
        std::exit(kExitFailure);
    }
    auto json = nlohmann::json();
    try {
        json = nlohmann::json::parse(*file_content);
    } catch (std::exception const& e) {
        Logger::Log(LogLevel::Error,
                    "While searching for the default target in {}:\n"
                    "Failed to parse json with error {}",
                    target_file,
                    e.what());
        std::exit(kExitFailure);
    }
    if (not json.is_object()) {
        Logger::Log(
            LogLevel::Error, "Invalid content in target file {}.", target_file);
        std::exit(kExitFailure);
    }
    if (json.empty()) {
        Logger::Log(LogLevel::Error,
                    "Missing target descriptions in file {}.",
                    target_file);
        std::exit(kExitFailure);
    }
    return Target::ConfiguredTarget{
        .target = Base::EntityName{Base::NamedTarget{
            main_repo, current_module, json.begin().key()}},
        .config = std::move(config)};
}

[[nodiscard]] auto DetermineWorkspaceRootByLookingForMarkers() noexcept
    -> std::filesystem::path {
    std::filesystem::path cwd{};
    try {
        cwd = std::filesystem::current_path();
    } catch (std::exception const& e) {
        Logger::Log(LogLevel::Warning,
                    "Failed to determine current working directory ({})",
                    e.what());
        Logger::Log(LogLevel::Error, "Could not determine workspace root.");
        std::exit(kExitFailure);
    }
    auto root = cwd.root_path();
    cwd = std::filesystem::relative(cwd, root);
    auto root_dir = FindRoot(cwd, FileRoot{root}, kRootMarkers);
    if (not root_dir) {
        Logger::Log(LogLevel::Error, "Could not determine workspace root.");
        std::exit(kExitFailure);
    }
    return root / *root_dir;
}

// Set all roots and name mappings from the command-line arguments and
// return the name of the main repository and main workspace path if local.
auto DetermineRoots(gsl::not_null<RepositoryConfig*> const& repository_config,
                    CommonArguments const& cargs,
                    AnalysisArguments const& aargs)
    -> std::pair<std::string, std::optional<std::filesystem::path>> {
    std::optional<std::filesystem::path> main_ws_root;
    auto repo_config = nlohmann::json::object();
    if (cargs.repository_config) {
        try {
            std::ifstream fs(*cargs.repository_config);
            repo_config = nlohmann::json::parse(fs);
            if (not repo_config.is_object()) {
                Logger::Log(
                    LogLevel::Error,
                    "Repository configuration file {} does not contain a map.",
                    (*cargs.repository_config).string());
                std::exit(kExitFailure);
            }
        } catch (std::exception const& e) {
            Logger::Log(LogLevel::Error,
                        "Parsing repository configuration file {} failed with "
                        "error:\n{}",
                        (*cargs.repository_config).string(),
                        e.what());
            std::exit(kExitFailure);
        }
    }

    std::string main_repo;

    if (cargs.main) {
        main_repo = *cargs.main;
    }
    else if (auto main_it = repo_config.find("main");
             main_it != repo_config.end()) {
        if (not main_it->is_string()) {
            Logger::Log(LogLevel::Error,
                        "Repository config: main has to be a string");
            std::exit(kExitFailure);
        }
        main_repo = *main_it;
    }
    else if (auto repos_it = repo_config.find("repositories");
             repos_it != repo_config.end() and not repos_it->empty()) {
        // take lexicographical first key as main (nlohmann::json is sorted)
        main_repo = repos_it->begin().key();
    }

    auto repos = nlohmann::json::object();
    auto repos_it = repo_config.find("repositories");
    if (repos_it != repo_config.end()) {
        if (not repos_it->is_object()) {
            Logger::Log(LogLevel::Error,
                        "Repository config: repositories has to be a map");
            std::exit(kExitFailure);
        }
        repos = *repos_it;
    }
    if (not repos.contains(main_repo)) {
        repos[main_repo] = nlohmann::json::object();
    }

    std::string error_msg;
    for (auto const& [repo, desc] : repos.items()) {
        std::optional<FileRoot> ws_root{};
        bool const is_main_repo{repo == main_repo};
        auto it_ws = desc.find("workspace_root");
        if (it_ws != desc.end()) {
            if (auto parsed_root = FileRoot::ParseRoot(
                    repo, "workspace_root", *it_ws, &error_msg)) {
                ws_root = std::move(parsed_root->first);
                if (is_main_repo and parsed_root->second.has_value()) {
                    main_ws_root = std::move(parsed_root->second);
                }
            }
            else {
                Logger::Log(LogLevel::Error, error_msg);
                std::exit(kExitFailure);
            }
        }
        if (is_main_repo) {
            // command line argument always overwrites what is eventually found
            // in the config file
            if (cargs.workspace_root) {
                main_ws_root = *cargs.workspace_root;
            }
            else if (not ws_root) {
                main_ws_root = DetermineWorkspaceRootByLookingForMarkers();
            }
            if (main_ws_root.has_value()) {
                ws_root = FileRoot{*main_ws_root};
            }
        }
        if (not ws_root) {
            Logger::Log(LogLevel::Error,
                        "Unknown workspace root for repository {}",
                        repo);
            std::exit(kExitFailure);
        }
        auto info = RepositoryConfig::RepositoryInfo{std::move(*ws_root)};
        auto parse_keyword_root = [&desc = desc,
                                   &repo = repo,
                                   &error_msg = error_msg,
                                   is_main_repo](FileRoot* keyword_root,
                                                 std::string const& keyword,
                                                 auto const& keyword_carg) {
            auto it = desc.find(keyword);
            if (it != desc.end()) {
                if (auto parsed_root =
                        FileRoot::ParseRoot(repo, keyword, *it, &error_msg)) {
                    (*keyword_root) = parsed_root->first;
                }
                else {
                    Logger::Log(LogLevel::Error, error_msg);
                    std::exit(kExitFailure);
                }
            }

            if (is_main_repo and keyword_carg) {
                *keyword_root = FileRoot{*keyword_carg};
            }
        };

        info.target_root = info.workspace_root;
        parse_keyword_root(&info.target_root, "target_root", aargs.target_root);

        info.rule_root = info.target_root;
        parse_keyword_root(&info.rule_root, "rule_root", aargs.rule_root);

        info.expression_root = info.rule_root;
        parse_keyword_root(
            &info.expression_root, "expression_root", aargs.expression_root);

        auto it_bindings = desc.find("bindings");
        if (it_bindings != desc.end()) {
            if (not it_bindings->is_object()) {
                Logger::Log(
                    LogLevel::Error,
                    "bindings has to be a string-string map, but found {}",
                    it_bindings->dump());
                std::exit(kExitFailure);
            }
            for (auto const& [local_name, global_name] : it_bindings->items()) {
                if (not repos.contains(global_name)) {
                    Logger::Log(LogLevel::Error,
                                "Binding {} for {} in {} does not refer to a "
                                "defined repository.",
                                global_name,
                                local_name,
                                repo);
                    std::exit(kExitFailure);
                }
                info.name_mapping[local_name] = global_name;
            }
        }
        auto parse_keyword_file_name = [&desc = desc, is_main_repo](
                                           std::string* keyword_file_name,
                                           std::string const& keyword,
                                           auto const& keyword_carg) {
            auto it = desc.find(keyword);
            if (it != desc.end()) {
                *keyword_file_name = *it;
            }

            if (is_main_repo and keyword_carg) {
                *keyword_file_name = *keyword_carg;
            }
        };

        parse_keyword_file_name(
            &info.target_file_name, "target_file_name", aargs.target_file_name);

        parse_keyword_file_name(
            &info.rule_file_name, "rule_file_name", aargs.rule_file_name);

        parse_keyword_file_name(&info.expression_file_name,
                                "expression_file_name",
                                aargs.expression_file_name);

        repository_config->SetInfo(repo, std::move(info));
    }

    return {main_repo, main_ws_root};
}

void ReportTaintedness(const AnalysisResult& result) {
    if (result.target->Tainted().empty()) {
        // Never report untainted targets
        return;
    }

    // To ensure proper quoting, go through json.
    nlohmann::json tainted{};
    for (auto const& s : result.target->Tainted()) {
        tainted.push_back(s);
    }
    Logger::Log(LogLevel::Info, "Target tainted {}.", tainted.dump());
}

auto DetermineNonExplicitTarget(
    std::string const& main_repo,
    std::optional<std::filesystem::path> const& main_ws_root,
    gsl::not_null<const RepositoryConfig*> const& repo_config,
    AnalysisArguments const& clargs)
    -> std::optional<BuildMaps::Target::ConfiguredTarget> {
    auto id =
        ReadConfiguredTarget(main_repo, main_ws_root, repo_config, clargs);
    switch (id.target.GetNamedTarget().reference_t) {
        case Base::ReferenceType::kFile:
            std::cout << id.ToString() << " is a source file." << std::endl;
            return std::nullopt;
        case Base::ReferenceType::kTree:
            std::cout << id.ToString() << " is a tree." << std::endl;
            return std::nullopt;
        case Base::ReferenceType::kGlob:
            std::cout << id.ToString() << " is a glob." << std::endl;
            return std::nullopt;
        case Base::ReferenceType::kSymlink:
            std::cout << id.ToString() << " is a symlink." << std::endl;
            return std::nullopt;
        case Base::ReferenceType::kTarget:
            return id;
    }
    return std::nullopt;  // make gcc happy
}

void DumpArtifactsToBuild(
    std::map<std::string, ArtifactDescription> const& artifacts,
    std::map<std::string, ArtifactDescription> const& runfiles,
    const std::filesystem::path& file_path) {
    nlohmann::json to_build{};
    for (auto const& [path, artifact] : runfiles) {
        to_build[path] = artifact.ToJson();
    }
    for (auto const& [path, artifact] : artifacts) {
        to_build[path] = artifact.ToJson();
    }
    auto const dump_string = IndentListsOnlyUntilDepth(to_build, 2, 1);
    std::ofstream os(file_path);
    os << dump_string << std::endl;
}

}  // namespace

auto main(int argc, char* argv[]) -> int {
    SetupDefaultLogging();
    try {
        auto arguments = ParseCommandLineArguments(argc, argv);

        if (arguments.cmd == SubCommand::kVersion) {
            std::cout << version() << std::endl;
            return kExitSuccess;
        }

        // just serve configures all from its config file, so parse that before
        // doing further setup steps
#ifndef BOOTSTRAP_BUILD_TOOL
        if (arguments.cmd == SubCommand::kServe) {
            ReadJustServeConfig(&arguments);
        }
#endif  // BOOTSTRAP_BUILD_TOOL

        SetupLogging(arguments.log);
        if (arguments.analysis.expression_log_limit) {
            Evaluator::SetExpressionLogLimit(
                *arguments.analysis.expression_log_limit);
        }

        // global repository configuration
        RepositoryConfig repo_config{};

#ifndef BOOTSTRAP_BUILD_TOOL
        /**
         * The current implementation of libgit2 uses pthread_key_t incorrectly
         * on POSIX systems to handle thread-specific data, which requires us to
         * explicitly make sure the main thread is the first one to call
         * git_libgit2_init. Future versions of libgit2 will hopefully fix this.
         */
        GitContext::Create();

        SetupFileChunker();

        if (arguments.cmd == SubCommand::kGc) {
            // Set up storage for GC, as we have all the config args we need.
            auto const storage_config = CreateStorageConfig(
                arguments.endpoint, arguments.protocol.hash_type);
            if (not storage_config) {
                return kExitFailure;
            }

            if (GarbageCollector::TriggerGarbageCollection(
                    *storage_config, arguments.gc.no_rotate)) {
                return kExitSuccess;
            }
            return kExitFailure;
        }

        auto local_exec_config = CreateLocalExecutionConfig(arguments.build);
        if (not local_exec_config) {
            return kExitFailure;
        }

        auto auth_config =
            CreateAuthConfig(arguments.auth, arguments.cauth, arguments.sauth);
        if (not auth_config) {
            return kExitFailure;
        }

        if (arguments.cmd == SubCommand::kExecute) {
            auto execution_server =
                ServerImpl::Create(arguments.service.interface,
                                   arguments.service.port,
                                   arguments.service.info_file,
                                   arguments.service.pid_file);

            if (execution_server) {
                RetryConfig
                    retry_config{};  // default is enough, as remote is not used
                // Use default remote configuration.
                RemoteExecutionConfig remote_exec_config{};

                // Set up storage for local execution.
                auto const storage_config = CreateStorageConfig(
                    arguments.endpoint, arguments.protocol.hash_type);
                if (not storage_config) {
                    return kExitFailure;
                }
                auto const storage = Storage::Create(&*storage_config);
                StoreTargetCacheShard(
                    *storage_config, storage, remote_exec_config);

                // pack the local context instances to be passed as needed
                LocalContext const local_context{
                    .exec_config = &*local_exec_config,
                    .storage_config = &*storage_config,
                    .storage = &storage};
                // pack the remote context instances to be passed as needed
                RemoteContext const remote_context{
                    .auth = &*auth_config,
                    .retry_config = &retry_config,
                    .exec_config = &remote_exec_config};

                auto const exec_apis =
                    ApiBundle::Create(&local_context,
                                      &remote_context,
                                      /*repo_config=*/nullptr);

                return execution_server->Run(&local_context,
                                             &remote_context,
                                             exec_apis,
                                             arguments.service.op_exponent)
                           ? kExitSuccess
                           : kExitFailure;
            }
            return kExitFailure;
        }

        auto serve_config = CreateServeConfig(
            arguments.serve, arguments.common, arguments.build, arguments.tc);
        if (not serve_config) {
            return kExitFailure;
        }

        // Set up the retry arguments, needed only for the client-side logic of
        // remote execution, i.e., just serve and the regular just client.
        auto retry_config = CreateRetryConfig(arguments.retry);
        if (not retry_config) {
            return kExitFailure;
        }

        if (arguments.cmd == SubCommand::kServe) {
            auto serve_server =
                ServeServerImpl::Create(arguments.service.interface,
                                        arguments.service.port,
                                        arguments.service.info_file,
                                        arguments.service.pid_file);
            if (serve_server) {
                // Set up remote execution config.
                auto remote_exec_config = CreateRemoteExecutionConfig(
                    arguments.endpoint, arguments.rebuild);
                if (not remote_exec_config) {
                    return kExitFailure;
                }

                // Set up storage for serve operation.
                auto const storage_config =
                    CreateStorageConfig(arguments.endpoint,
                                        arguments.protocol.hash_type,
                                        remote_exec_config->remote_address,
                                        remote_exec_config->platform_properties,
                                        remote_exec_config->dispatch);
                if (not storage_config) {
                    return kExitFailure;
                }
                auto const storage = Storage::Create(&*storage_config);
                StoreTargetCacheShard(
                    *storage_config, storage, *remote_exec_config);

                // pack the local context instances to be passed as needed
                LocalContext const local_context{
                    .exec_config = &*local_exec_config,
                    .storage_config = &*storage_config,
                    .storage = &storage};
                // pack the remote context instances to be passed as needed
                RemoteContext const remote_context{
                    .auth = &*auth_config,
                    .retry_config = &*retry_config,
                    .exec_config = &*remote_exec_config};

                auto const serve_apis =
                    ApiBundle::Create(&local_context,
                                      &remote_context,
                                      /*repo_config=*/nullptr);
                auto serve = ServeApi::Create(*serve_config,
                                              &local_context,
                                              &remote_context,
                                              &serve_apis);

                bool with_execute =
                    not remote_exec_config->remote_address.has_value();
                // Operation cache only relevant for just execute
                auto const op_exponent =
                    with_execute ? arguments.service.op_exponent : std::nullopt;

                return serve_server->Run(*serve_config,
                                         &local_context,
                                         &remote_context,
                                         serve,
                                         serve_apis,
                                         op_exponent,
                                         with_execute)
                           ? kExitSuccess
                           : kExitFailure;
            }
            return kExitFailure;
        }

        // If no execution endpoint was given, the client should default to the
        // serve endpoint, if given.
        if (not arguments.endpoint.remote_execution_address.has_value() and
            arguments.serve.remote_serve_address.has_value()) {
            // replace the remote execution address
            arguments.endpoint.remote_execution_address =
                *arguments.serve.remote_serve_address;
            // Inform user of the change
            Logger::Log(LogLevel::Info,
                        "Using '{}' as the remote execution endpoint.",
                        *arguments.serve.remote_serve_address);
        }

        // Set up remote execution config.
        auto remote_exec_config =
            CreateRemoteExecutionConfig(arguments.endpoint, arguments.rebuild);
        if (not remote_exec_config) {
            return kExitFailure;
        }

        // Set up storage for client-side operation. This needs to have all the
        // correct remote endpoint info in order to instantiate the
        // correctly-sharded target cache.
        auto const storage_config =
            CreateStorageConfig(arguments.endpoint,
                                arguments.protocol.hash_type,
                                remote_exec_config->remote_address,
                                remote_exec_config->platform_properties,
                                remote_exec_config->dispatch);
#else
        // For bootstrapping the TargetCache sharding is not needed, so we can
        // default all execution arguments.
        auto const storage_config = CreateStorageConfig(
            arguments.endpoint, arguments.protocol.hash_type);
#endif  // BOOTSTRAP_BUILD_TOOL
        if (not storage_config) {
            return kExitFailure;
        }
        auto const storage = Storage::Create(&*storage_config);

#ifndef BOOTSTRAP_BUILD_TOOL
        StoreTargetCacheShard(*storage_config, storage, *remote_exec_config);
#endif  // BOOTSTRAP_BUILD_TOOL

        auto jobs = arguments.build.build_jobs > 0 ? arguments.build.build_jobs
                                                   : arguments.common.jobs;

        auto stage_args = arguments.cmd == SubCommand::kInstall or
                                  arguments.cmd == SubCommand::kInstallCas or
                                  arguments.cmd == SubCommand::kTraverse
                              ? std::make_optional(std::move(arguments.stage))
                              : std::nullopt;

        auto rebuild_args =
            arguments.cmd == SubCommand::kRebuild
                ? std::make_optional(std::move(arguments.rebuild))
                : std::nullopt;

        // statistics and progress instances; need to be kept alive
        // used also in bootstrapped just
        Statistics stats{};
        Progress progress{};

#ifndef BOOTSTRAP_BUILD_TOOL
        // pack the local context instances to be passed to ApiBundle
        LocalContext const local_context{.exec_config = &*local_exec_config,
                                         .storage_config = &*storage_config,
                                         .storage = &storage};
        // pack the remote context instances to be passed as needed
        RemoteContext const remote_context{.auth = &*auth_config,
                                           .retry_config = &*retry_config,
                                           .exec_config = &*remote_exec_config};

        auto const main_apis =
            ApiBundle::Create(&local_context, &remote_context, &repo_config);
        ExecutionContext const exec_context{.repo_config = &repo_config,
                                            .apis = &main_apis,
                                            .remote_context = &remote_context,
                                            .statistics = &stats,
                                            .progress = &progress};
        GraphTraverser const traverser{
            {jobs,
             std::move(arguments.build),
             std::move(stage_args),
             std::move(rebuild_args)},
            &exec_context,
            ProgressReporter::Reporter(&stats, &progress)};

        if (arguments.cmd == SubCommand::kInstallCas) {
            if (not repo_config.SetGitCAS(storage_config->GitRoot())) {
                Logger::Log(LogLevel::Debug,
                            "Failed set Git CAS {}.",
                            storage_config->GitRoot().string());
            }
            return FetchAndInstallArtifacts(
                       main_apis, arguments.fetch, remote_context)
                       ? kExitSuccess
                       : kExitFailure;
        }
        if (arguments.cmd == SubCommand::kAddToCas) {
            return AddArtifactsToCas(arguments.to_add, storage, main_apis)
                       ? kExitSuccess
                       : kExitFailure;
        }
#endif  // BOOTSTRAP_BUILD_TOOL

        auto [main_repo, main_ws_root] =
            DetermineRoots(&repo_config, arguments.common, arguments.analysis);

#ifndef BOOTSTRAP_BUILD_TOOL
        std::optional<ServeApi> serve = ServeApi::Create(
            *serve_config, &local_context, &remote_context, &main_apis);
#else
        std::optional<ServeApi> serve;
#endif  // BOOTSTRAP_BUILD_TOOL

#ifndef BOOTSTRAP_BUILD_TOOL
        auto lock = GarbageCollector::SharedLock(*storage_config);
        if (not lock) {
            return kExitFailure;
        }

        if (arguments.cmd == SubCommand::kTraverse) {
            if (arguments.graph.git_cas) {
                if (not ProtocolTraits::IsNative(
                        arguments.protocol.hash_type)) {
                    Logger::Log(LogLevel::Error,
                                "Command line options {} and {} cannot be used "
                                "together.",
                                "--git-cas",
                                "--compatible");
                    std::exit(EXIT_FAILURE);
                }
                if (not repo_config.SetGitCAS(*arguments.graph.git_cas)) {
                    Logger::Log(LogLevel::Warning,
                                "Failed set Git CAS {}.",
                                arguments.graph.git_cas->string());
                }
            }
            if (traverser.BuildAndStage(arguments.graph.graph_file,
                                        arguments.graph.artifacts)) {
                return kExitSuccess;
            }
        }
        else if (arguments.cmd == SubCommand::kDescribe) {
            if (auto id = DetermineNonExplicitTarget(main_repo,
                                                     main_ws_root,
                                                     &repo_config,
                                                     arguments.analysis)) {
                return arguments.describe.describe_rule
                           ? DescribeUserDefinedRule(
                                 id->target,
                                 &repo_config,
                                 arguments.common.jobs,
                                 arguments.describe.print_json)
                           : DescribeTarget(*id,
                                            &repo_config,
                                            serve,
                                            main_apis,
                                            arguments.common.jobs,
                                            arguments.describe.print_json);
            }
            return kExitFailure;
        }

        else {
#endif  // BOOTSTRAP_BUILD_TOOL
            BuildMaps::Target::ResultTargetMap result_map{
                arguments.common.jobs};
            auto id = ReadConfiguredTarget(
                main_repo, main_ws_root, &repo_config, arguments.analysis);
            auto serve_errors = nlohmann::json::array();
            std::mutex serve_errors_access{};
            BuildMaps::Target::ServeFailureLogReporter collect_serve_errors =
                [&serve_errors, &serve_errors_access](auto target, auto blob) {
                    std::unique_lock lock(serve_errors_access);
                    auto target_desc = nlohmann::json::array();
                    target_desc.push_back(target.target.ToJson());
                    target_desc.push_back(target.config.ToJson());
                    auto entry = nlohmann::json::array();
                    entry.push_back(target_desc);
                    entry.push_back(blob);
                    serve_errors.push_back(entry);
                };

            // create progress tracker for export targets
            Progress exports_progress{};
            AnalyseContext analyse_ctx{.repo_config = &repo_config,
                                       .storage = &storage,
                                       .statistics = &stats,
                                       .progress = &exports_progress,
                                       .serve = serve ? &*serve : nullptr};

            auto result = AnalyseTarget(&analyse_ctx,
                                        id,
                                        &result_map,
                                        arguments.common.jobs,
                                        arguments.analysis.request_action_input,
                                        /*logger=*/nullptr,
                                        &collect_serve_errors);
            if (arguments.analysis.serve_errors_file) {
                Logger::Log(
                    serve_errors.empty() ? LogLevel::Debug : LogLevel::Info,
                    "Dumping serve-error information to {}",
                    arguments.analysis.serve_errors_file->string());
                std::ofstream os(*arguments.analysis.serve_errors_file);
                os << serve_errors.dump() << std::endl;
            }
            if (result) {
                Logger::Log(LogLevel::Info,
                            "Analysed target {}",
                            result->id.ToShortString());

                {
                    auto cached = stats.ExportsCachedCounter();
                    auto served = stats.ExportsServedCounter();
                    auto uncached = stats.ExportsUncachedCounter();
                    auto not_eligible = stats.ExportsNotEligibleCounter();
                    Logger::Log(
                        served + cached + uncached + not_eligible > 0
                            ? LogLevel::Info
                            : LogLevel::Debug,
                        "Export targets found: {} cached, {}{} uncached, "
                        "{} not eligible for caching",
                        cached,
                        served > 0 ? fmt::format("{} served, ", served) : "",
                        uncached,
                        not_eligible);
                }

                if (arguments.analysis.graph_file) {
                    result_map.ToFile(
                        *arguments.analysis.graph_file, &stats, &progress);
                }
                auto const [artifacts, runfiles] =
                    ReadOutputArtifacts(result->target);
                if (arguments.analysis.artifacts_to_build_file) {
                    DumpArtifactsToBuild(
                        artifacts,
                        runfiles,
                        *arguments.analysis.artifacts_to_build_file);
                }
                if (arguments.cmd == SubCommand::kAnalyse) {
                    DiagnoseResults(*result, result_map, arguments.diagnose);
                    ReportTaintedness(*result);
                    // Clean up in parallel
                    {
                        TaskSystem ts{arguments.common.jobs};
                        result_map.Clear(&ts);
                    }
                    return kExitSuccess;
                }
#ifndef BOOTSTRAP_BUILD_TOOL
                ReportTaintedness(*result);
                auto const& [actions, blobs, trees] =
                    result_map.ToResult(&stats, &progress);

                // collect cache targets and artifacts for target-level caching
                auto const cache_targets = result_map.CacheTargets();
                auto cache_artifacts = CollectNonKnownArtifacts(cache_targets);

                // Clean up result map, now that it is no longer needed
                {
                    TaskSystem ts{arguments.common.jobs};
                    result_map.Clear(&ts);
                }

                Logger::Log(
                    LogLevel::Info,
                    "{}ing{} {}.",
                    arguments.cmd == SubCommand::kRebuild ? "Rebuild" : "Build",
                    result->modified ? fmt::format(" input of action {} of",
                                                   *(result->modified))
                                     : "",
                    result->id.ToShortString());

                auto build_result =
                    traverser.BuildAndStage(artifacts,
                                            runfiles,
                                            actions,
                                            blobs,
                                            trees,
                                            std::move(cache_artifacts));
                if (build_result) {
                    WriteTargetCacheEntries(
                        cache_targets,
                        build_result->extra_infos,
                        jobs,
                        main_apis,
                        arguments.tc.target_cache_write_strategy,
                        storage.TargetCache(),
                        nullptr,
                        arguments.serve.remote_serve_address
                            ? LogLevel::Performance
                            : LogLevel::Warning);

                    // Repeat taintedness message to make the user aware that
                    // the artifacts are not for production use.
                    ReportTaintedness(*result);
                    if (build_result->failed_artifacts) {
                        Logger::Log(LogLevel::Warning,
                                    "Build result contains failed artifacts.");
                    }
                    return build_result->failed_artifacts
                               ? kExitSuccessFailedArtifacts
                               : kExitSuccess;
                }
            }
#endif  // BOOTSTRAP_BUILD_TOOL
        }
    } catch (std::exception const& ex) {
        Logger::Log(
            LogLevel::Error, "Caught exception with message: {}", ex.what());
    }
    return kExitFailure;
}
