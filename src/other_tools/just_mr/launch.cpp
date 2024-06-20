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

#include "src/other_tools/just_mr/launch.hpp"

#include <filesystem>
#include <utility>

#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/buildtool/storage/garbage_collector.hpp"
#include "src/other_tools/just_mr/exit_codes.hpp"
#include "src/other_tools/just_mr/setup.hpp"
#include "src/other_tools/just_mr/setup_utils.hpp"
#include "src/utils/cpp/file_locking.hpp"

auto CallJust(std::optional<std::filesystem::path> const& config_file,
              MultiRepoCommonArguments const& common_args,
              MultiRepoSetupArguments const& setup_args,
              MultiRepoJustSubCmdsArguments const& just_cmd_args,
              MultiRepoLogArguments const& log_args,
              MultiRepoRemoteAuthArguments const& auth_args,
              RetryArguments const& retry_args,
              ForwardOnlyArguments const& launch_fwd,
              bool forward_build_root,
              std::string multi_repo_tool_name) -> int {
    // check if subcmd_name can be taken from additional args
    auto additional_args_offset = 0U;
    auto subcommand = just_cmd_args.subcmd_name;
    if (not subcommand and not just_cmd_args.additional_just_args.empty()) {
        subcommand = just_cmd_args.additional_just_args[0];
        additional_args_offset++;
    }

    bool use_config{false};
    bool use_build_root{false};
    bool use_launcher{false};
    bool supports_defines{false};
    bool supports_remote{false};
    bool supports_remote_properties{false};
    bool supports_serve{false};
    bool supports_dispatch{false};
    std::optional<std::filesystem::path> mr_config_path{std::nullopt};

    std::optional<LockFile> lock{};
    if (subcommand and kKnownJustSubcommands.contains(*subcommand)) {
        // Read the config file if needed
        if (kKnownJustSubcommands.at(*subcommand).config) {
            lock = GarbageCollector::SharedLock();
            if (not lock) {
                return kExitGenericFailure;
            }
            auto config = JustMR::Utils::ReadConfiguration(
                config_file, common_args.absent_repository_file);

            use_config = true;
            mr_config_path = MultiRepoSetup(config,
                                            common_args,
                                            setup_args,
                                            just_cmd_args,
                                            auth_args,
                                            /*interactive=*/false,
                                            std::move(multi_repo_tool_name));
            if (not mr_config_path) {
                Logger::Log(LogLevel::Error,
                            "Failed to setup config for calling \"{} {}\"",
                            common_args.just_path
                                ? common_args.just_path->string()
                                : kDefaultJustPath,
                            *subcommand);
                return kExitSetupError;
            }
        }
        use_build_root = kKnownJustSubcommands.at(*subcommand).build_root;
        use_launcher = kKnownJustSubcommands.at(*subcommand).launch;
        supports_defines = kKnownJustSubcommands.at(*subcommand).defines;
        supports_remote = kKnownJustSubcommands.at(*subcommand).remote;
        supports_remote_properties =
            kKnownJustSubcommands.at(*subcommand).remote_props;
        supports_serve = kKnownJustSubcommands.at(*subcommand).serve;
        supports_dispatch = kKnownJustSubcommands.at(*subcommand).dispatch;
    }
    // build just command
    std::vector<std::string> cmd = {common_args.just_path->string()};
    if (subcommand) {
        cmd.emplace_back(*subcommand);
    }
    if (use_config) {
        cmd.emplace_back("-C");
        cmd.emplace_back(mr_config_path->string());
    }
    if (use_build_root and forward_build_root) {
        cmd.emplace_back("--local-build-root");
        cmd.emplace_back(*common_args.just_mr_paths->root);
    }
    if (use_launcher and common_args.local_launcher and
        (common_args.local_launcher != kDefaultLauncher)) {
        cmd.emplace_back("--local-launcher");
        cmd.emplace_back(nlohmann::json(*common_args.local_launcher).dump());
    }
    // forward logging arguments
    if (not log_args.log_files.empty()) {
        cmd.emplace_back("--log-append");
        for (auto const& log_file : log_args.log_files) {
            cmd.emplace_back("-f");
            cmd.emplace_back(log_file.string());
        }
    }
    if (log_args.log_limit and *log_args.log_limit != kDefaultLogLevel) {
        cmd.emplace_back("--log-limit");
        cmd.emplace_back(
            std::to_string(static_cast<std::underlying_type<LogLevel>::type>(
                *log_args.log_limit)));
    }
    if (log_args.restrict_stderr_log_limit) {
        cmd.emplace_back("--restrict-stderr-log-limit");
        cmd.emplace_back(
            std::to_string(static_cast<std::underlying_type<LogLevel>::type>(
                *log_args.restrict_stderr_log_limit)));
    }
    if (log_args.plain_log) {
        cmd.emplace_back("--plain-log");
    }
    if (supports_defines) {
        if (just_cmd_args.config) {
            cmd.emplace_back("-c");
            cmd.emplace_back(just_cmd_args.config->string());
        }
        auto overlay_config = Configuration();
        for (auto const& s : common_args.defines) {
            try {
                auto map = Expression::FromJson(nlohmann::json::parse(s));
                if (not map->IsMap()) {
                    Logger::Log(LogLevel::Error,
                                "Defines entry {} does not contain a map.",
                                nlohmann::json(s).dump());
                    std::exit(kExitClargsError);
                }
                overlay_config = overlay_config.Update(map);
            } catch (std::exception const& e) {
                Logger::Log(LogLevel::Error,
                            "Parsing defines entry {} failed with error:\n{}",
                            nlohmann::json(s).dump(),
                            e.what());
                std::exit(kExitClargsError);
            }
        }
        if (not overlay_config.Expr()->Map().empty()) {
            cmd.emplace_back("-D");
            cmd.emplace_back(overlay_config.ToString());
        }
    }
    // forward remote execution and mutual TLS arguments
    if (supports_remote) {
        if (common_args.compatible) {
            cmd.emplace_back("--compatible");
        }
        if (common_args.remote_execution_address) {
            cmd.emplace_back("-r");
            cmd.emplace_back(*common_args.remote_execution_address);
        }
        if (auth_args.tls_ca_cert) {
            cmd.emplace_back("--tls-ca-cert");
            cmd.emplace_back(auth_args.tls_ca_cert->string());
        }
        if (auth_args.tls_client_cert) {
            cmd.emplace_back("--tls-client-cert");
            cmd.emplace_back(auth_args.tls_client_cert->string());
        }
        if (auth_args.tls_client_key) {
            cmd.emplace_back("--tls-client-key");
            cmd.emplace_back(auth_args.tls_client_key->string());
        }
        if (retry_args.max_attempts) {
            cmd.emplace_back("--max-attempts");
            cmd.emplace_back(std::to_string(*retry_args.max_attempts));
        }
        if (retry_args.initial_backoff_seconds) {
            cmd.emplace_back("--initial-backoff-seconds");
            cmd.emplace_back(
                std::to_string(*retry_args.initial_backoff_seconds));
        }
        if (retry_args.max_backoff_seconds) {
            cmd.emplace_back("--max-backoff-seconds");
            cmd.emplace_back(std::to_string(*retry_args.max_backoff_seconds));
        }
    }
    if (supports_dispatch and just_cmd_args.endpoint_configuration) {
        cmd.emplace_back("--endpoint-configuration");
        cmd.emplace_back(*just_cmd_args.endpoint_configuration);
    }
    if (supports_serve and common_args.remote_serve_address) {
        cmd.emplace_back("-R");
        cmd.emplace_back(*common_args.remote_serve_address);
    }
    // forward-only arguments, still to come before the just-arguments
    if (supports_remote_properties) {
        for (auto const& prop : launch_fwd.remote_execution_properties) {
            cmd.emplace_back("--remote-execution-property");
            cmd.emplace_back(prop);
        }
    }
    // add args read from just-mrrc
    if (subcommand and just_cmd_args.just_args.contains(*subcommand)) {
        for (auto const& subcmd_arg : just_cmd_args.just_args.at(*subcommand)) {
            cmd.emplace_back(subcmd_arg);
        }
    }
    // add (remaining) args given by user as clargs
    for (auto it = just_cmd_args.additional_just_args.begin() +
                   additional_args_offset;
         it != just_cmd_args.additional_just_args.end();
         ++it) {
        cmd.emplace_back(*it);
    }

    Logger::Log(
        LogLevel::Info, "Setup finished, exec {}", nlohmann::json(cmd).dump());

    // create argv
    std::vector<char*> argv{};
    std::transform(std::begin(cmd),
                   std::end(cmd),
                   std::back_inserter(argv),
                   [](auto& str) { return str.data(); });
    argv.push_back(nullptr);
    // run execvp; will only return if failure
    [[maybe_unused]] auto res =
        execvp(argv[0], static_cast<char* const*>(argv.data()));
    // execvp returns only if command errored out
    Logger::Log(LogLevel::Error, "execvp failed with error code {}", errno);
    return kExitExecError;
}
