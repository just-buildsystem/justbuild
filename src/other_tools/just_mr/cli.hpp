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

#ifndef INCLUDED_SRC_OTHER_TOOLS_JUST_MR_CLI_HPP
#define INCLUDED_SRC_OTHER_TOOLS_JUST_MR_CLI_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "CLI/CLI.hpp"
#include "fmt/core.h"
#include "gsl/gsl"
#include "nlohmann/json.hpp"
#include "src/buildtool/common/clidefaults.hpp"
#include "src/buildtool/common/retry_cli.hpp"
#include "src/buildtool/common/user_structs.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/other_tools/just_mr/mirrors.hpp"
#include "src/other_tools/just_mr/utils.hpp"

/// \brief Arguments common to all just-mr subcommands
struct MultiRepoCommonArguments {
    std::optional<std::filesystem::path> repository_config{std::nullopt};
    std::optional<std::filesystem::path> absent_repository_file{std::nullopt};
    std::optional<std::filesystem::path> checkout_locations_file{std::nullopt};
    std::vector<std::string> explicit_distdirs;
    LocalPathsPtr just_mr_paths = std::make_shared<LocalPaths>();
    MirrorsPtr alternative_mirrors = std::make_shared<Mirrors>();
    std::optional<std::vector<std::string>> local_launcher{std::nullopt};
    CAInfoPtr ca_info = std::make_shared<CAInfo>();
    std::optional<std::filesystem::path> just_path{std::nullopt};
    std::optional<std::string> main{std::nullopt};
    std::optional<std::filesystem::path> rc_path{std::nullopt};
    std::optional<std::filesystem::path> git_path{std::nullopt};
    std::optional<std::filesystem::path> dump_rc{std::nullopt};
    bool norc{false};
    std::size_t jobs{std::max(1U, std::thread::hardware_concurrency())};
    std::vector<std::string> defines;
    std::optional<std::string> remote_execution_address;
    bool compatible{false};
    std::optional<std::string> remote_serve_address;
    bool fetch_absent{false};
};

struct MultiRepoLogArguments {
    std::vector<std::filesystem::path> log_files;
    std::optional<LogLevel> log_limit;
    std::optional<LogLevel> restrict_stderr_log_limit;
    bool plain_log{false};
    bool log_append{false};
};

struct MultiRepoSetupArguments {
    std::optional<std::string> sub_main{std::nullopt};
    bool sub_all{false};
};

struct MultiRepoFetchArguments {
    std::optional<std::filesystem::path> fetch_dir{std::nullopt};
    bool backup_to_remote{false};
};

struct MultiRepoUpdateArguments {
    std::vector<std::string> repos_to_update;
};

struct MultiRepoGcArguments {
    bool drop_only{false};
};

struct MultiRepoJustSubCmdsArguments {
    std::optional<std::string> subcmd_name{std::nullopt};
    std::vector<std::string> additional_just_args;
    std::unordered_map<std::string, std::vector<std::string>> just_args;
    std::optional<std::filesystem::path> config;
    std::optional<std::filesystem::path> endpoint_configuration;
};

// corresponding to the similarly-named arguments in 'just'
struct MultiRepoRemoteAuthArguments {
    std::optional<std::filesystem::path> tls_ca_cert{std::nullopt};
    std::optional<std::filesystem::path> tls_client_cert{std::nullopt};
    std::optional<std::filesystem::path> tls_client_key{std::nullopt};
};

struct ForwardOnlyArguments {
    std::vector<std::string> remote_execution_properties;
};

enum class SubCommand : std::uint8_t {
    kUnknown,
    kMRVersion,
    kFetch,
    kUpdate,
    kSetup,
    kSetupEnv,
    kJustDo,
    kJustSubCmd,
    kGcRepo
};

struct CommandLineArguments {
    SubCommand cmd{SubCommand::kUnknown};
    MultiRepoCommonArguments common;
    RetryArguments retry;
    MultiRepoLogArguments log;
    MultiRepoSetupArguments setup;
    MultiRepoFetchArguments fetch;
    MultiRepoUpdateArguments update;
    MultiRepoGcArguments gc;
    MultiRepoJustSubCmdsArguments just_cmd;
    MultiRepoRemoteAuthArguments auth;
    ForwardOnlyArguments launch_fwd;
};

static inline void SetupMultiRepoCommonArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<MultiRepoCommonArguments*> const& clargs) {
    // repository config is mandatory
    app->add_option_function<std::string>(
           "-C, --repository-config",
           [clargs](auto const& repository_config_raw) {
               clargs->repository_config = std::filesystem::weakly_canonical(
                   std::filesystem::absolute(repository_config_raw));
           },
           "Repository-description file to use.")
        ->type_name("FILE");
    app->add_option_function<std::string>(
           "--absent",
           [clargs](auto const& file_raw) {
               clargs->absent_repository_file =
                   std::filesystem::weakly_canonical(
                       std::filesystem::absolute(file_raw));
           },
           "File specifying the repositories to consider absent (overrides the "
           "pragma in the config file).")
        ->type_name("FILE");
    app->add_option_function<std::string>(
           "--local-build-root",
           [clargs](auto const& local_build_root_raw) {
               clargs->just_mr_paths->root = std::filesystem::weakly_canonical(
                   std::filesystem::absolute(local_build_root_raw));
           },
           "Root for CAS, repository space, etc.")
        ->type_name("PATH");
    app->add_option_function<std::string>(
           "--checkout-locations",
           [clargs](auto const& checkout_locations_raw) {
               clargs->checkout_locations_file =
                   std::filesystem::weakly_canonical(
                       std::filesystem::absolute(checkout_locations_raw));
           },
           "Specification file for checkout locations.")
        ->type_name("CHECKOUT_LOCATIONS");
    app->add_option_function<std::string>(
           "-L, --local-launcher",
           [clargs](auto const& launcher_raw) {
               clargs->local_launcher =
                   nlohmann::json::parse(launcher_raw)
                       .template get<std::vector<std::string>>();
           },
           "JSON array with the list of strings representing the launcher to "
           "prepend actions' commands before being executed locally.")
        ->type_name("JSON");
    app->add_option_function<std::string>(
           "--distdir",
           [clargs](auto const& distdir_raw) {
               clargs->explicit_distdirs.emplace_back(
                   std::filesystem::weakly_canonical(std::filesystem::absolute(
                       std::filesystem::path(distdir_raw))));
           },
           "Directory to look for distfiles before fetching.")
        ->type_name("PATH")
        ->trigger_on_parse();  // run callback on all instances while parsing,
                               // not after all parsing is done
    app->add_flag("--no-fetch-ssl-verify",
                  clargs->ca_info->no_ssl_verify,
                  "Do not perform SSL verification when fetching archives from "
                  "remote.");
    app->add_option_function<std::string>(
           "--fetch-cacert",
           [clargs](auto const& cacert_raw) {
               clargs->ca_info->ca_bundle = std::filesystem::weakly_canonical(
                   std::filesystem::absolute(cacert_raw));
           },
           "CA certificate bundle to use for SSL verification when fetching "
           "archives from remote.")
        ->type_name("CA_BUNDLE");
    app->add_option("--just",
                    clargs->just_path,
                    fmt::format("The build tool to be launched (default: {}).",
                                kDefaultJustPath))
        ->type_name("PATH");
    app->add_option("--main",
                    clargs->main,
                    "Main repository to consider from the configuration.")
        ->type_name("MAIN");
    app->add_option_function<std::string>(
           "--rc",
           [clargs](auto const& rc_path_raw) {
               clargs->rc_path = std::filesystem::weakly_canonical(
                   std::filesystem::absolute(rc_path_raw));
           },
           "Use just-mrrc file from custom path.")
        ->type_name("RCFILE");
    app->add_option("--git",
                    clargs->git_path,
                    fmt::format("Path to the git binary. (Default: {})",
                                kDefaultGitPath))
        ->type_name("PATH");
    app->add_option(
           "--dump-rc", clargs->dump_rc, "Dump the effective rc value.")
        ->type_name("PATH");
    app->add_flag("--norc", clargs->norc, "Do not use any just-mrrc file.");
    app->add_option("-j, --jobs",
                    clargs->jobs,
                    "Number of jobs to run (Default: Number of cores).")
        ->type_name("NUM");
    app->add_option_function<std::string>(
           "-D,--defines",
           [clargs](auto const& d) { clargs->defines.emplace_back(d); },
           "Define overlay configuration to be forwarded to the invocation of"
           " just, in case the subcommand supports it; otherwise ignored.")
        ->type_name("JSON")
        ->trigger_on_parse();  // run callback on all instances while parsing,
                               // not after all parsing is done
    app->add_option("-r,--remote-execution-address",
                    clargs->remote_execution_address,
                    "Address of a remote-execution service.")
        ->type_name("NAME:PORT");
    app->add_flag(
        "--compatible",
        clargs->compatible,
        "At increased computational effort, be compatible with the original "
        "remote build execution protocol. As the change affects identifiers, "
        "the flag must be used consistently for all related invocations.");
    app->add_option("-R,--remote-serve-address",
                    clargs->remote_serve_address,
                    "Address of a remote 'serve' service.")
        ->type_name("NAME:PORT");
    app->add_flag("--fetch-absent",
                  clargs->fetch_absent,
                  "Do not produce absent roots. For Git repositories, try to "
                  "fetch served commit trees from the remote execution "
                  "endpoint before reverting to the network.");
}

static inline auto SetupMultiRepoLogArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<MultiRepoLogArguments*> const& clargs) {
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
    app->add_option_function<std::underlying_type_t<LogLevel>>(
           "--restrict-stderr-log-limit",
           [clargs](auto const& limit) {
               clargs->restrict_stderr_log_limit = ToLogLevel(limit);
           },
           "Restrict logging on console to the minimum of the specified "
           "--log-limit and this value")
        ->type_name("NUM");
    app->add_flag("--plain-log",
                  clargs->plain_log,
                  "Do not use ANSI escape sequences to highlight messages.");
    app->add_flag(
        "--log-append",
        clargs->log_append,
        "Append messages to log file instead of overwriting existing.");
}

static inline void SetupMultiRepoSetupArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<MultiRepoSetupArguments*> const& clargs) {
    app->add_option("main-repo",
                    clargs->sub_main,
                    "Main repository to consider from the configuration.")
        ->type_name("");
    app->add_flag("--all",
                  clargs->sub_all,
                  "Consider all repositories in the configuration.");
}

static inline void SetupMultiRepoFetchArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<MultiRepoFetchArguments*> const& clargs) {
    app->add_option_function<std::string>(
           "-o",
           [clargs](auto const& fetch_dir_raw) {
               clargs->fetch_dir = std::filesystem::weakly_canonical(
                   std::filesystem::absolute(fetch_dir_raw));
           },
           "Directory to write distfiles when fetching.")
        ->type_name("PATH");
    app->add_flag("--backup-to-remote",
                  clargs->backup_to_remote,
                  "Backup fetched archives to a remote CAS, if a "
                  "remote-execution service is provided.");
}

static inline void SetupMultiRepoUpdateArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<MultiRepoUpdateArguments*> const& clargs) {
    // take all remaining args as positional
    app->add_option("repo", clargs->repos_to_update, "Repository to update.")
        ->type_name("");
}

static inline void SetupMultiRepoGcArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<MultiRepoGcArguments*> const& clargs) {
    app->add_flag("--drop-only",
                  clargs->drop_only,
                  "Only drop old repository generations");
}

static inline auto SetupMultiRepoRemoteAuthArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<MultiRepoRemoteAuthArguments*> const& authargs) {
    app->add_option("--tls-ca-cert",
                    authargs->tls_ca_cert,
                    "Path to a TLS CA certificate that is trusted to sign the "
                    "server certificate.");
    app->add_option("--tls-client-cert",
                    authargs->tls_client_cert,
                    "Path to the TLS client certificate.");
    app->add_option("--tls-client-key",
                    authargs->tls_client_key,
                    "Path to the TLS client key.");
}

#endif  // INCLUDED_SRC_OTHER_TOOLS_JUST_MR_CLI_HPP
