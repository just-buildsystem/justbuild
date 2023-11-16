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

#ifndef INCLUDED_SRC_BUILDTOOL_COMMON_CLI_HPP
#define INCLUDED_SRC_BUILDTOOL_COMMON_CLI_HPP

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "CLI/CLI.hpp"
#include "fmt/core.h"
#include "gsl/gsl"
#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/expression/evaluator.hpp"
#include "src/buildtool/common/clidefaults.hpp"
#include "src/buildtool/compatibility/compatibility.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/utils/cpp/path.hpp"

constexpr auto kDefaultTimeout = std::chrono::milliseconds{300000};

/// \brief Arguments common to all commands.
struct CommonArguments {
    std::optional<std::filesystem::path> workspace_root{};
    std::optional<std::filesystem::path> repository_config{};
    std::optional<std::string> main{};
    std::size_t jobs{std::max(1U, std::thread::hardware_concurrency())};
};

struct LogArguments {
    std::vector<std::filesystem::path> log_files{};
    LogLevel log_limit{kDefaultLogLevel};
    bool plain_log{false};
    bool log_append{false};
};

/// \brief Arguments required for analysing targets.
struct AnalysisArguments {
    std::optional<std::size_t> expression_log_limit{};
    std::vector<std::string> defines{};
    std::filesystem::path config_file{};
    std::optional<nlohmann::json> target{};
    std::optional<std::string> request_action_input{};
    std::optional<std::string> target_file_name{};
    std::optional<std::string> rule_file_name{};
    std::optional<std::string> expression_file_name{};
    std::optional<std::filesystem::path> target_root{};
    std::optional<std::filesystem::path> rule_root{};
    std::optional<std::filesystem::path> expression_root{};
    std::optional<std::filesystem::path> graph_file{};
    std::optional<std::filesystem::path> artifacts_to_build_file{};
};

/// \brief Arguments required for describing targets/rules.
struct DescribeArguments {
    bool print_json{};
    bool describe_rule{};
};

/// \brief Arguments required for running diagnostics.
struct DiagnosticArguments {
    std::optional<std::string> dump_actions{std::nullopt};
    std::optional<std::string> dump_blobs{std::nullopt};
    std::optional<std::string> dump_trees{std::nullopt};
    std::optional<std::string> dump_vars{std::nullopt};
    std::optional<std::string> dump_targets{std::nullopt};
    std::optional<std::string> dump_export_targets{std::nullopt};
    std::optional<std::string> dump_targets_graph{std::nullopt};
    std::optional<std::string> dump_anonymous{std::nullopt};
    std::optional<std::string> dump_nodes{std::nullopt};
};

/// \brief Arguments required for specifying build endpoint.
struct EndpointArguments {
    std::optional<std::filesystem::path> local_root{};
    std::optional<std::string> remote_execution_address;
    std::vector<std::string> platform_properties;
    std::optional<std::filesystem::path> remote_execution_dispatch_file{};
};

/// \brief Arguments required for building.
struct BuildArguments {
    std::optional<std::vector<std::string>> local_launcher{std::nullopt};
    std::chrono::milliseconds timeout{kDefaultTimeout};
    std::size_t build_jobs{};
    std::optional<std::string> dump_artifacts{std::nullopt};
    std::optional<std::string> print_to_stdout{std::nullopt};
    bool show_runfiles{false};
};

/// \brief Arguments required for staging.
struct StageArguments {
    std::filesystem::path output_dir{};
    bool remember{false};
};

/// \brief Arguments required for rebuilding.
struct RebuildArguments {
    std::optional<std::string> cache_endpoint{};
    std::optional<std::filesystem::path> dump_flaky{};
};

/// \brief Arguments for fetching artifacts from CAS.
struct FetchArguments {
    std::string object_id{};
    std::optional<std::filesystem::path> output_path{};
    std::optional<std::filesystem::path> sub_path{};
    bool remember{false};
    bool raw_tree{};
};

/// \brief Arguments required for running from graph file.
struct GraphArguments {
    nlohmann::json artifacts{};
    std::filesystem::path graph_file{};
    std::optional<std::filesystem::path> git_cas{};
};

// Arguments for authentication methods.

/// \brief Arguments shared by both server and client
struct CommonAuthArguments {
    std::optional<std::filesystem::path> tls_ca_cert{std::nullopt};
};

/// \brief Arguments used by the client
struct ClientAuthArguments {
    std::optional<std::filesystem::path> tls_client_cert{std::nullopt};
    std::optional<std::filesystem::path> tls_client_key{std::nullopt};
};

/// \brief Authentication arguments used by subcommand just execute
struct ServerAuthArguments {
    std::optional<std::filesystem::path> tls_server_cert{std::nullopt};
    std::optional<std::filesystem::path> tls_server_key{std::nullopt};
};

struct ServiceArguments {
    std::optional<int> port{std::nullopt};
    std::optional<std::filesystem::path> info_file{std::nullopt};
    std::optional<std::string> interface{std::nullopt};
    std::optional<std::string> pid_file{std::nullopt};
    std::optional<uint8_t> op_exponent;
};

struct ServeArguments {
    std::filesystem::path config{};
    std::optional<std::string> remote_serve_address{};
    // repositories populated from just-serve config file
    std::vector<std::filesystem::path> repositories{};
};

static inline auto SetupCommonArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<CommonArguments*> const& clargs) {
    app->add_option("-C,--repository-config",
                    clargs->repository_config,
                    "Path to configuration file for multi-repository builds.")
        ->type_name("PATH");
    app->add_option(
           "--main", clargs->main, "The repository to take the target from.")
        ->type_name("NAME");
    app->add_option_function<std::string>(
           "-w,--workspace-root",
           [clargs](auto const& workspace_root_raw) {
               std::filesystem::path root = ToNormalPath(workspace_root_raw);
               if (not root.is_absolute()) {
                   try {
                       root = std::filesystem::absolute(root);
                   } catch (std::exception const& e) {
                       Logger::Log(LogLevel::Error,
                                   "Failed to convert workspace root {} ({})",
                                   workspace_root_raw,
                                   e.what());
                       throw e;
                   }
               }
               clargs->workspace_root = root;
           },
           "Path of the workspace's root directory.")
        ->type_name("PATH");
    app->add_option("-j,--jobs",
                    clargs->jobs,
                    "Number of jobs to run (Default: Number of cores).")
        ->type_name("NUM");
}

static inline auto SetupLogArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<LogArguments*> const& clargs) {
    app->add_option_function<std::string>(
           "-f,--log-file",
           [clargs](auto const& log_file_) {
               clargs->log_files.emplace_back(log_file_);
           },
           "Path to local log file.")
        ->type_name("PATH")
        ->trigger_on_parse();  // run callback on all instances while parsing,
                               // not after all parsing is done
    app->add_option_function<std::underlying_type_t<LogLevel>>(
           "--log-limit",
           [clargs](auto const& limit) {
               clargs->log_limit = ToLogLevel(limit);
           },
           fmt::format("Log limit (higher is more verbose) in interval [{},{}] "
                       "(Default: {}).",
                       static_cast<int>(kFirstLogLevel),
                       static_cast<int>(kLastLogLevel),
                       static_cast<int>(kDefaultLogLevel)))
        ->type_name("NUM");
    app->add_flag("--plain-log",
                  clargs->plain_log,
                  "Do not use ANSI escape sequences to highlight messages.");
    app->add_flag(
        "--log-append",
        clargs->log_append,
        "Append messages to log file instead of overwriting existing.");
}

static inline auto SetupAnalysisArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<AnalysisArguments*> const& clargs,
    bool with_graph = true) {
    app->add_option("--expression-log-limit",
                    clargs->expression_log_limit,
                    fmt::format("Maximal size for logging a single expression "
                                "in error messages (Default {})",
                                Evaluator::kDefaultExpressionLogLimit))
        ->type_name("NUM");
    app->add_option_function<std::string>(
           "-D,--defines",
           [clargs](auto const& d) { clargs->defines.emplace_back(d); },
           "Define an overlay configuration via an in-line JSON object."
           " Multiple options overlay.")
        ->type_name("JSON")
        ->trigger_on_parse();  // run callback on all instances while parsing,
                               // not after all parsing is done
    app->add_option(
           "-c,--config", clargs->config_file, "Path to configuration file.")
        ->type_name("PATH");
    app->add_option(
           "--request-action-input",
           clargs->request_action_input,
           "Instead of the target result, request input for this action.")
        ->type_name("ACTION");
    app->add_option_function<std::vector<std::string>>(
        "target",
        [clargs](auto const& target_raw) {
            if (target_raw.size() == 1) {
                clargs->target = nlohmann::json(target_raw[0]);
            }
            else {
                clargs->target = nlohmann::json(target_raw);
            }
        },
        "Module and target name to build.\n"
        "Assumes current module if module name is omitted.");
    app->add_option("--target-root",
                    clargs->target_root,
                    "Path of the target files' root directory.\n"
                    "Default: Same as --workspace-root")
        ->type_name("PATH");
    app->add_option("--rule-root",
                    clargs->rule_root,
                    "Path of the rule files' root directory.\n"
                    "Default: Same as --target-root")
        ->type_name("PATH");
    app->add_option("--expression-root",
                    clargs->expression_root,
                    "Path of the expression files' root directory.\n"
                    "Default: Same as --rule-root")
        ->type_name("PATH");
    app->add_option("--target-file-name",
                    clargs->target_file_name,
                    "Name of the targets file.");
    app->add_option(
        "--rule-file-name", clargs->rule_file_name, "Name of the rules file.");
    app->add_option("--expression-file-name",
                    clargs->expression_file_name,
                    "Name of the expressions file.");
    if (with_graph) {
        app->add_option(
               "--dump-graph",
               clargs->graph_file,
               "File path for writing the action graph description to.")
            ->type_name("PATH");
        app->add_option("--dump-artifacts-to-build",
                        clargs->artifacts_to_build_file,
                        "File path for writing the artifacts to build to.")
            ->type_name("PATH");
    }
}

static inline auto SetupDescribeArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<DescribeArguments*> const& clargs) {
    app->add_flag("--json", clargs->print_json, "Print description as JSON.");
    app->add_flag("--rule",
                  clargs->describe_rule,
                  "Positional arguments refer to rule instead of target.");
}

static inline auto SetupDiagnosticArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<DiagnosticArguments*> const& clargs) {
    app->add_option("--dump-actions",
                    clargs->dump_actions,
                    "Dump actions to file (use - for stdout).")
        ->type_name("PATH");
    app->add_option("--dump-trees",
                    clargs->dump_trees,
                    "Dump trees to file (use - for stdout).")
        ->type_name("PATH");
    app->add_option("--dump-blobs",
                    clargs->dump_blobs,
                    "Dump blobs to file (use - for stdout).")
        ->type_name("PATH");
    app->add_option("--dump-vars",
                    clargs->dump_vars,
                    "Dump domain of the effective configuration to file (use - "
                    "for stdout).")
        ->type_name("PATH");
    app->add_option("--dump-targets",
                    clargs->dump_targets,
                    "Dump targets to file (use - for stdout).")
        ->type_name("PATH");
    app->add_option("--dump-export-targets",
                    clargs->dump_export_targets,
                    "Dump \"export\" targets to file (use - for stdout).")
        ->type_name("PATH");
    app->add_option("--dump-targets-graph",
                    clargs->dump_targets_graph,
                    "Dump the graph of the configured targets to file.")
        ->type_name("PATH");
    app->add_option("--dump-anonymous",
                    clargs->dump_anonymous,
                    "Dump anonymous targets to file (use - for stdout).")
        ->type_name("PATH");
    app->add_option("--dump-nodes",
                    clargs->dump_nodes,
                    "Dump nodes of target to file (use - for stdout).")
        ->type_name("PATH");
}

static inline auto SetupCacheArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<EndpointArguments*> const& clargs) {
    app->add_option_function<std::string>(
           "--local-build-root",
           [clargs](auto const& build_root_raw) {
               std::filesystem::path root = ToNormalPath(build_root_raw);
               if (!root.is_absolute()) {
                   try {
                       root = std::filesystem::absolute(root);
                   } catch (std::exception const& e) {
                       Logger::Log(
                           LogLevel::Error,
                           "Failed to convert local build root {} ({}).",
                           build_root_raw,
                           e.what());
                       throw e;
                   }
               }
               clargs->local_root = root;
           },
           "Root for local CAS, cache, and build directories.")
        ->type_name("PATH");
}

static inline auto SetupEndpointArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<EndpointArguments*> const& clargs) {
    app->add_option("-r,--remote-execution-address",
                    clargs->remote_execution_address,
                    "Address of the remote-execution service.")
        ->type_name("NAME:PORT");
    app->add_option("--endpoint-configuration",
                    clargs->remote_execution_dispatch_file,
                    "File with dispatch instructions to use different "
                    "remote-execution services, depending on the properties")
        ->type_name("PATH");
    app->add_option(
           "--remote-execution-property",
           clargs->platform_properties,
           "Property for remote execution as key-value pair. Specifying this "
           "option multiple times will accumulate pairs (latest wins).")
        ->type_name("KEY:VAL")
        ->allow_extra_args(false)
        ->expected(1, 1);
}

static inline auto SetupServeEndpointArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<ServeArguments*> const& clargs) {
    app->add_option("-R,--remote-serve-address",
                    clargs->remote_serve_address,
                    "Address of the serve service.")
        ->type_name("NAME:PORT");
}

static inline auto SetupCommonBuildArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<BuildArguments*> const& clargs) {
    app->add_option_function<std::string>(
           "-L,--local-launcher",
           [clargs](auto const& launcher_raw) {
               clargs->local_launcher =
                   nlohmann::json::parse(launcher_raw)
                       .template get<std::vector<std::string>>();
           },
           "JSON array with the list of strings representing the launcher to "
           "prepend actions' commands before being executed locally.")
        ->type_name("JSON")
        ->default_val(nlohmann::json(kDefaultLauncher).dump());
}

static inline auto SetupBuildArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<BuildArguments*> const& clargs) {

    app->add_option_function<unsigned int>(
           "--action-timeout",
           [clargs](auto const& seconds) {
               clargs->timeout = seconds * std::chrono::seconds{1};
           },
           "Action timeout in seconds. (Default: 300).")
        ->type_name("NUM");

    app->add_option(
           "-J,--build-jobs",
           clargs->build_jobs,
           "Number of jobs to run during build phase (Default: same as jobs).")
        ->type_name("NUM");
    app->add_option("--dump-artifacts",
                    clargs->dump_artifacts,
                    "Dump artifacts to file (use - for stdout).")
        ->type_name("PATH");

    app->add_flag("-s,--show-runfiles",
                  clargs->show_runfiles,
                  "Do not omit runfiles in build report.");

    app->add_option("-P,--print-to-stdout",
                    clargs->print_to_stdout,
                    "After building, print the specified artifact to stdout.")
        ->type_name("LOGICAL_PATH");
}

static inline auto SetupStageArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<StageArguments*> const& clargs) {
    app->add_option_function<std::string>(
           "-o,--output-dir",
           [clargs](auto const& output_dir_raw) {
               std::filesystem::path out = ToNormalPath(output_dir_raw);
               if (not out.is_absolute()) {
                   try {
                       out = std::filesystem::absolute(out);
                   } catch (std::exception const& e) {
                       Logger::Log(
                           LogLevel::Error,
                           "Failed to convert output directory {} ({}).",
                           output_dir_raw,
                           e.what());
                       throw e;
                   }
               }
               clargs->output_dir = out;
           },
           "Path of the directory where outputs will be copied.")
        ->type_name("PATH")
        ->required();

    app->add_flag(
        "--remember", clargs->remember, "Copy object to local CAS first");
}

static inline auto SetupRebuildArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<RebuildArguments*> const& clargs) {
    app->add_option_function<std::string>(
           "--vs",
           [clargs](auto const& cache_endpoint) {
               clargs->cache_endpoint = cache_endpoint;
           },
           "Cache endpoint to compare against (use \"local\" for local cache).")
        ->type_name("NAME:PORT|\"local\"");

    app->add_option(
           "--dump-flaky", clargs->dump_flaky, "Dump flaky actions to file.")
        ->type_name("PATH");
}

static inline auto SetupFetchArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<FetchArguments*> const& clargs) {
    app->add_option(
           "object_id",
           clargs->object_id,
           "Object identifier with the format '[<hash>:<size>:<type>]'.")
        ->required();

    app->add_option_function<std::string>(
           "-o,--output-path",
           [clargs](auto const& output_path_raw) {
               std::filesystem::path out = ToNormalPath(output_path_raw);
               if (not out.is_absolute()) {
                   try {
                       out = std::filesystem::absolute(out);
                   } catch (std::exception const& e) {
                       Logger::Log(LogLevel::Error,
                                   "Failed to convert output path {} ({})",
                                   output_path_raw,
                                   e.what());
                       throw e;
                   }
               }
               clargs->output_path = out;
           },
           "Install path for the artifact. (omit to dump to stdout)")
        ->type_name("PATH");

    app->add_option_function<std::string>(
           "-P,--sub-object-path",
           [clargs](auto const& rel_path) {
               clargs->sub_path = ToNormalPath(rel_path).relative_path();
           },
           "Select the sub-object at the specified path (if artifact is a "
           "tree).")
        ->type_name("PATH");

    app->add_flag("--raw-tree",
                  clargs->raw_tree,
                  "Dump raw tree object (omit pretty printing)");

    app->add_flag(
        "--remember", clargs->remember, "Copy object to local CAS first");
}

static inline auto SetupGraphArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<GraphArguments*> const& clargs) {
    app->add_option_function<std::string>(
        "-a,--artifacts",
        [clargs](auto const& artifact_map_raw) {
            clargs->artifacts = nlohmann::json::parse(artifact_map_raw);
        },
        "Json object with key/value pairs formed by the relative path in which "
        "artifact is to be copied and the description of the artifact as json "
        "object as well.");

    app->add_option("-g,--graph-file",
                    clargs->graph_file,
                    "Path of the file containing the description of the "
                    "actions.")
        ->required();

    app->add_option("--git-cas",
                    clargs->git_cas,
                    "Path to a Git repository, containing blobs of potentially "
                    "missing KNOWN artifacts.");
}

static inline auto SetupCompatibilityArguments(
    gsl::not_null<CLI::App*> const& app) {
    app->add_flag_function(
        "--compatible",
        [](auto /*unused*/) { Compatibility::SetCompatible(); },
        "At increased computational effort, be compatible with the original "
        "remote build execution protocol. As the change affects identifiers, "
        "the flag must be used consistently for all related invocations.");
}

static inline auto SetupCommonAuthArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<CommonAuthArguments*> const& authargs) {
    app->add_option("--tls-ca-cert",
                    authargs->tls_ca_cert,
                    "Path to a TLS CA certificate that is trusted to sign the "
                    "server certificate.");
}

static inline auto SetupClientAuthArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<ClientAuthArguments*> const& authargs) {
    app->add_option("--tls-client-cert",
                    authargs->tls_client_cert,
                    "Path to the TLS client certificate.");
    app->add_option("--tls-client-key",
                    authargs->tls_client_key,
                    "Path to the TLS client key.");
}

static inline auto SetupServerAuthArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<ServerAuthArguments*> const& authargs) {
    app->add_option("--tls-server-cert",
                    authargs->tls_server_cert,
                    "Path to the TLS server certificate.");
    app->add_option("--tls-server-key",
                    authargs->tls_server_key,
                    "Path to the TLS server key.");
}

static inline auto SetupServiceArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<ServiceArguments*> const& service_args) {
    app->add_option("-p,--port",
                    service_args->port,
                    "The service will listen to this port. If unset, the "
                    "service will listen to the first available one.");
    app->add_option("--info-file",
                    service_args->info_file,
                    "Write the used port, interface, and pid to this file in "
                    "JSON format. If the file exists, it "
                    "will be overwritten.");
    app->add_option("-i,--interface",
                    service_args->interface,
                    "Interface to use. If unset, the loopback device is used.");
    app->add_option(
        "--pid-file",
        service_args->pid_file,
        "Write pid to this file in plain txt. If the file exists, it "
        "will be overwritten.");

    app->add_option(
        "--log-operations-threshold",
        service_args->op_exponent,
        "Once the number of operations stored exceeds twice 2^n, where n is "
        "given by the option --log-operations-threshold, at most 2^n "
        "operations will be removed, in a FIFO scheme. If unset, defaults to "
        "14. Must be in the range [0,255]");
}

static inline auto SetupServeArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<ServeArguments*> const& serve_args) {
    app->add_option("config",
                    serve_args->config,
                    "Configuration file for the subcommand.")
        ->required();
}

#endif  // INCLUDED_SRC_BUILDTOOL_COMMON_CLI_HPP
