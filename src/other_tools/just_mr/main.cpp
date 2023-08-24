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

#include <filesystem>
#include <utility>

#include <unistd.h>

#include "CLI/CLI.hpp"
#include "gsl/gsl"
#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/buildtool/compatibility/compatibility.hpp"
#include "src/buildtool/file_system/git_context.hpp"
#include "src/buildtool/logging/log_config.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/log_sink_cmdline.hpp"
#include "src/buildtool/logging/log_sink_file.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/main/version.hpp"
#include "src/buildtool/storage/garbage_collector.hpp"
#include "src/other_tools/just_mr/cli.hpp"
#include "src/other_tools/just_mr/exit_codes.hpp"
#include "src/other_tools/just_mr/fetch.hpp"
#include "src/other_tools/just_mr/launch.hpp"
#include "src/other_tools/just_mr/setup.hpp"
#include "src/other_tools/just_mr/setup_utils.hpp"
#include "src/other_tools/just_mr/update.hpp"

namespace {

enum class SubCommand {
    kUnknown,
    kMRVersion,
    kFetch,
    kUpdate,
    kSetup,
    kSetupEnv,
    kJustDo,
    kJustSubCmd
};

struct CommandLineArguments {
    SubCommand cmd{SubCommand::kUnknown};
    MultiRepoCommonArguments common;
    MultiRepoLogArguments log;
    MultiRepoSetupArguments setup;
    MultiRepoFetchArguments fetch;
    MultiRepoUpdateArguments update;
    MultiRepoJustSubCmdsArguments just_cmd;
    MultiRepoRemoteAuthArguments auth;
};

/// \brief Setup arguments for just-mr itself, common to all subcommands.
void SetupCommonCommandArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<CommandLineArguments*> const& clargs) {
    SetupMultiRepoCommonArguments(app, &clargs->common);
    SetupMultiRepoLogArguments(app, &clargs->log);
    SetupMultiRepoRemoteAuthArguments(app, &clargs->auth);
}

/// \brief Setup arguments for subcommand "just-mr fetch".
void SetupFetchCommandArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<CommandLineArguments*> const& clargs) {
    SetupMultiRepoSetupArguments(app, &clargs->setup);
    SetupMultiRepoFetchArguments(app, &clargs->fetch);
}

/// \brief Setup arguments for subcommand "just-mr update".
void SetupUpdateCommandArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<CommandLineArguments*> const& clargs) {
    SetupMultiRepoUpdateArguments(app, &clargs->update);
}

/// \brief Setup arguments for subcommand "just-mr setup" and
/// "just-mr setup-env".
void SetupSetupCommandArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<CommandLineArguments*> const& clargs) {
    SetupMultiRepoSetupArguments(app, &clargs->setup);
}

[[nodiscard]] auto ParseCommandLineArguments(int argc, char const* const* argv)
    -> CommandLineArguments {
    CLI::App app(
        "just-mr, a multi-repository configuration tool and launcher for just");
    app.option_defaults()->take_last();
    auto* cmd_mrversion = app.add_subcommand(
        "mrversion", "Print version information in JSON format of this tool.");
    auto* cmd_setup =
        app.add_subcommand("setup", "Setup and generate just configuration");
    auto* cmd_setup_env = app.add_subcommand(
        "setup-env", "Setup without workspace root for the main repository.");
    auto* cmd_fetch =
        app.add_subcommand("fetch", "Fetch and store distribution files.");
    auto* cmd_update = app.add_subcommand(
        "update",
        "Advance Git commit IDs and print updated just-mr configuration.");
    auto* cmd_do = app.add_subcommand(
        "do", "Canonical way of specifying just subcommands.");
    cmd_do->set_help_flag();  // disable help flag
    // define just subcommands
    std::vector<CLI::App*> cmd_just_subcmds{};
    cmd_just_subcmds.reserve(kKnownJustSubcommands.size());
    for (auto const& known_subcmd : kKnownJustSubcommands) {
        auto* subcmd = app.add_subcommand(
            known_subcmd.first,
            "Run setup and call \"just " + known_subcmd.first + "\".");
        subcmd->set_help_flag();  // disable help flag
        cmd_just_subcmds.emplace_back(subcmd);
    }
    app.require_subcommand(1);

    CommandLineArguments clargs;
    // first, set the common arguments for just-mr itself
    SetupCommonCommandArguments(&app, &clargs);
    // then, set the arguments for each subcommand
    SetupSetupCommandArguments(cmd_setup, &clargs);
    SetupSetupCommandArguments(cmd_setup_env, &clargs);
    SetupFetchCommandArguments(cmd_fetch, &clargs);
    SetupUpdateCommandArguments(cmd_update, &clargs);

    // for 'just' calls, allow extra arguments
    cmd_do->allow_extras();
    for (auto const& sub_cmd : cmd_just_subcmds) {
        sub_cmd->allow_extras();
    }

    try {
        app.parse(argc, argv);
    } catch (CLI::Error& e) {
        [[maybe_unused]] auto err = app.exit(e);
        std::exit(kExitClargsError);
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error, "Command line parse error: {}", ex.what());
        std::exit(kExitClargsError);
    }

    if (*cmd_mrversion) {
        clargs.cmd = SubCommand::kMRVersion;
    }
    else if (*cmd_setup) {
        clargs.cmd = SubCommand::kSetup;
    }
    else if (*cmd_setup_env) {
        clargs.cmd = SubCommand::kSetupEnv;
    }
    else if (*cmd_fetch) {
        clargs.cmd = SubCommand::kFetch;
    }
    else if (*cmd_update) {
        clargs.cmd = SubCommand::kUpdate;
    }
    else if (*cmd_do) {
        clargs.cmd = SubCommand::kJustDo;
        // get remaining args
        clargs.just_cmd.additional_just_args = cmd_do->remaining();
    }
    else {
        for (auto const& sub_cmd : cmd_just_subcmds) {
            if (*sub_cmd) {
                clargs.cmd = SubCommand::kJustSubCmd;
                clargs.just_cmd.subcmd_name =
                    sub_cmd->get_name();  // get name of subcommand
                // get remaining args
                clargs.just_cmd.additional_just_args = sub_cmd->remaining();
                break;  // no need to go further
            }
        }
    }

    return clargs;
}

void SetupDefaultLogging() {
    LogConfig::SetLogLimit(kDefaultLogLevel);
    LogConfig::SetSinks({LogSinkCmdLine::CreateFactory()});
}

void SetupLogging(MultiRepoLogArguments const& clargs) {
    if (clargs.log_limit) {
        LogConfig::SetLogLimit(*clargs.log_limit);
    }
    else {
        LogConfig::SetLogLimit(kDefaultLogLevel);
    }
    LogConfig::SetSinks({LogSinkCmdLine::CreateFactory(not clargs.plain_log)});
    for (auto const& log_file : clargs.log_files) {
        LogConfig::AddSink(LogSinkFile::CreateFactory(
            log_file,
            clargs.log_append ? LogSinkFile::Mode::Append
                              : LogSinkFile::Mode::Overwrite));
    }
}

[[nodiscard]] auto ReadLocation(
    nlohmann::json const& location,
    std::optional<std::filesystem::path> const& ws_root)
    -> std::optional<std::pair<std::filesystem::path, std::filesystem::path>> {
    if (not location.contains("path") or not location.contains("root")) {
        Logger::Log(LogLevel::Error,
                    "Malformed location object: {}",
                    location.dump(-1));
        std::exit(kExitConfigError);
    }
    auto root = location["root"].get<std::string>();
    auto path = location["path"].get<std::string>();
    auto base = location.contains("base") ? location["base"].get<std::string>()
                                          : std::string(".");

    std::filesystem::path root_path{};
    if (root == "workspace") {
        if (not ws_root) {
            Logger::Log(LogLevel::Warning,
                        "Not in workspace root, ignoring location {}.",
                        location.dump(-1));
            return std::nullopt;
        }
        root_path = *ws_root;
    }
    if (root == "home") {
        root_path = StorageConfig::GetUserHome();
    }
    if (root == "system") {
        root_path = FileSystemManager::GetCurrentDirectory().root_path();
    }
    return std::make_pair(std::filesystem::weakly_canonical(
                              std::filesystem::absolute(root_path / path)),
                          std::filesystem::weakly_canonical(
                              std::filesystem::absolute(root_path / base)));
}

[[nodiscard]] auto ReadLocation(
    ExpressionPtr const& location,
    std::optional<std::filesystem::path> const& ws_root)
    -> std::optional<std::pair<std::filesystem::path, std::filesystem::path>> {
    if (location.IsNotNull()) {
        auto root = location->Get("root", Expression::none_t{});
        auto path = location->Get("path", Expression::none_t{});
        auto base = location->Get("base", std::string("."));

        if (not path->IsString() or not root->IsString() or
            not kLocationTypes.contains(root->String())) {
            Logger::Log(LogLevel::Error,
                        "Malformed location object: {}",
                        location.ToJson().dump(-1));
            std::exit(kExitConfigError);
        }
        auto root_str = root->String();
        std::filesystem::path root_path{};
        if (root_str == "workspace") {
            if (not ws_root) {
                Logger::Log(LogLevel::Warning,
                            "Not in workspace root, ignoring location {}.",
                            location.ToJson().dump(-1));
                return std::nullopt;
            }
            root_path = *ws_root;
        }
        if (root_str == "home") {
            root_path = StorageConfig::GetUserHome();
        }
        if (root_str == "system") {
            root_path = FileSystemManager::GetCurrentDirectory().root_path();
        }
        return std::make_pair(
            std::filesystem::weakly_canonical(
                std::filesystem::absolute(root_path / path->String())),
            std::filesystem::weakly_canonical(
                std::filesystem::absolute(root_path / base->String())));
    }
    return std::nullopt;
}

/// \brief Read just-mrrc file and set up various configs. Return the path to
/// the repository config file, if any is provided.
[[nodiscard]] auto ReadJustMRRC(
    gsl::not_null<CommandLineArguments*> const& clargs)
    -> std::optional<std::filesystem::path> {
    Configuration rc_config{};
    auto rc_path = clargs->common.rc_path;
    // set default if rcpath not given
    if (not clargs->common.norc) {
        if (not rc_path) {
            rc_path = std::filesystem::weakly_canonical(
                std::filesystem::absolute(kDefaultRCPath));
        }
        else {
            if (not FileSystemManager::IsFile(*rc_path)) {
                Logger::Log(LogLevel::Error,
                            "Cannot read RC file {}.",
                            rc_path->string());
                std::exit(kExitConfigError);
            }
        }
        if (FileSystemManager::IsFile(*rc_path)) {
            // json::parse may throw
            try {
                std::ifstream fs(*rc_path);
                auto map = Expression::FromJson(nlohmann::json::parse(fs));
                if (not map->IsMap()) {
                    Logger::Log(
                        LogLevel::Error,
                        "In RC file {}: expected an object but found:\n{}",
                        rc_path->string(),
                        map->ToString());
                    std::exit(kExitConfigError);
                }
                rc_config = Configuration{map};
            } catch (std::exception const& e) {
                Logger::Log(LogLevel::Error,
                            "Parsing RC file {} as JSON failed with error:\n{}",
                            rc_path->string(),
                            e.what());
                std::exit(kExitConfigError);
            }
        }
    }
    // read local build root; overwritten if user provided it already
    if (not clargs->common.just_mr_paths->root) {
        auto build_root =
            ReadLocation(rc_config["local build root"],
                         clargs->common.just_mr_paths->workspace_root);
        if (build_root) {
            clargs->common.just_mr_paths->root = build_root->first;
        }
    }
    // read checkout locations file; overwritten if user provided it already
    if (not clargs->common.checkout_locations_file) {
        auto checkout =
            ReadLocation(rc_config["checkout locations"],
                         clargs->common.just_mr_paths->workspace_root);
        if (checkout) {
            if (not FileSystemManager::IsFile(checkout->first)) {
                Logger::Log(LogLevel::Error,
                            "Cannot find checkout locations file {}.",
                            checkout->first.string());
                std::exit(kExitConfigError);
            }
            clargs->common.checkout_locations_file = checkout->first;
        }
    }
    // read distdirs; user can append, but does not overwrite
    auto distdirs = rc_config["distdirs"];
    if (distdirs.IsNotNull()) {
        if (not distdirs->IsList()) {
            Logger::Log(LogLevel::Error,
                        "Configuration-file provided distdirs has to be a list "
                        "of strings, but found {}",
                        distdirs->ToString());
            std::exit(kExitConfigError);
        }
        auto const& distdirs_list = distdirs->List();
        for (auto const& l : distdirs_list) {
            auto paths =
                ReadLocation(l, clargs->common.just_mr_paths->workspace_root);
            if (paths) {
                if (FileSystemManager::IsDirectory(paths->first)) {
                    clargs->common.just_mr_paths->distdirs.emplace_back(
                        paths->first);
                }
                else {
                    Logger::Log(LogLevel::Warning,
                                "Ignoring non-existing distdir {}.",
                                paths->first.string());
                }
            }
        }
    }
    // read just path; overwritten if user provided it already
    if (not clargs->common.just_path) {
        auto just = ReadLocation(rc_config["just"],
                                 clargs->common.just_mr_paths->workspace_root);
        if (just) {
            clargs->common.just_path = just->first;
        }
    }
    // read git binary path; overwritten if user provided it already
    if (not clargs->common.git_path) {
        auto git = ReadLocation(rc_config["git"],
                                clargs->common.just_mr_paths->workspace_root);
        if (git) {
            clargs->common.git_path = git->first;
        }
    }
    // read additional just args; user can append, but does not overwrite
    auto just_args = rc_config["just args"];
    if (just_args.IsNotNull()) {
        if (not just_args->IsMap()) {
            Logger::Log(LogLevel::Error,
                        "Configuration-file provided 'just' arguments has to "
                        "be a map, but found {}",
                        just_args->ToString());
            std::exit(kExitConfigError);
        }
        for (auto const& [cmd_name, cmd_args] : just_args->Map()) {
            // get list of string args for current command
            std::vector<std::string> args{};
            if (not cmd_args->IsList()) {
                Logger::Log(
                    LogLevel::Error,
                    "Configuration-file provided 'just' argument key {} has to "
                    "have as value a list of strings, but found {}",
                    cmd_name,
                    cmd_args->ToString());
                std::exit(kExitConfigError);
            }
            auto const& args_list = cmd_args->List();
            args.reserve(args_list.size());
            for (auto const& arg : args_list) {
                args.emplace_back(arg->String());
            }
            clargs->just_cmd.just_args[cmd_name] = std::move(args);
        }
    }
    // read default for local launcher
    if (not clargs->common.local_launcher) {
        auto launcher = rc_config["local launcher"];
        if (launcher.IsNotNull()) {
            if (not launcher->IsList()) {
                Logger::Log(LogLevel::Error,
                            "Configuration-file provided launcher has to be a "
                            "list of strings, but found {}",
                            launcher->ToString());
                std::exit(kExitConfigError);
            }
            std::vector<std::string> default_launcher{};
            default_launcher.reserve(launcher->List().size());
            for (auto const& entry : launcher->List()) {
                if (not entry->IsString()) {
                    Logger::Log(LogLevel::Error,
                                "Configuration-file provided launcher {} is "
                                "not a list of strings",
                                launcher->ToString());
                    std::exit(kExitConfigError);
                }
                default_launcher.emplace_back(entry->String());
            }
            clargs->common.local_launcher = default_launcher;
        }
        else {
            clargs->common.local_launcher = kDefaultLauncher;
        }
    }
    // Set log limit, if specified and not set on the command line
    if (not clargs->log.log_limit) {
        auto limit = rc_config["log limit"];
        if (limit.IsNotNull()) {
            if (not limit->IsNumber()) {
                Logger::Log(LogLevel::Error,
                            "Configuration-file specified log-limit has to be "
                            "a number, but found {}",
                            limit->ToString());
                std::exit(kExitConfigError);
            }
            clargs->log.log_limit = ToLogLevel(limit->Number());
            LogConfig::SetLogLimit(*clargs->log.log_limit);
        }
    }
    // Add additional log sinks specified in the rc file.
    auto log_files = rc_config["log files"];
    if (log_files.IsNotNull()) {
        if (not log_files->IsList()) {
            Logger::Log(LogLevel::Error,
                        "Configuration-provided log files have to be a list of "
                        "location objects, but found {}",
                        log_files->ToString());
            std::exit(kExitConfigError);
        }
        for (auto const& log_file : log_files->List()) {
            auto path =
                ReadLocation(log_file->ToJson(),
                             clargs->common.just_mr_paths->workspace_root);
            if (path) {
                LogConfig::AddSink(LogSinkFile::CreateFactory(
                    path->first,
                    clargs->log.log_append ? LogSinkFile::Mode::Append
                                           : LogSinkFile::Mode::Overwrite));
                clargs->log.log_files.emplace_back(path->first);
            }
        }
    }
    // read remote exec args; overwritten if user provided it already
    auto remote = rc_config["remote execution"];
    if (remote.IsNotNull()) {
        if (not remote->IsMap()) {
            Logger::Log(LogLevel::Error,
                        "Configuration-provided remote execution arguments has "
                        "to be a map, but found {}",
                        remote->ToString());
            std::exit(kExitConfigError);
        }
        auto const& remote_map = remote->Map();
        if (not clargs->common.remote_execution_address) {
            auto addr = remote_map.at("address");
            if (addr.IsNotNull()) {
                if (not addr->IsString()) {
                    Logger::Log(LogLevel::Error,
                                "Configuration-provided remote execution "
                                "address has to be a string, but found {}",
                                addr->ToString());
                    std::exit(kExitConfigError);
                }
                clargs->common.remote_execution_address = addr->String();
            }
        }
        if (not clargs->common.compatible) {
            auto compat = remote_map.at("compatible");
            if (compat.IsNotNull()) {
                if (not compat->IsBool()) {
                    Logger::Log(LogLevel::Error,
                                "Configuration-provided remote execution "
                                "compatibility has to be a flag, but found {}",
                                compat->ToString());
                    std::exit(kExitConfigError);
                }
                clargs->common.compatible = compat->Bool();
            }
        }
    }
    // read authentication args; overwritten if user provided it already
    auto auth_args = rc_config["authentication"];
    if (auth_args.IsNotNull()) {
        if (not auth_args->IsMap()) {
            Logger::Log(LogLevel::Error,
                        "Configuration-provided authentication arguments has "
                        "to be a map, but found {}",
                        auth_args->ToString());
            std::exit(kExitConfigError);
        }
        auto const& auth_map = auth_args->Map();
        if (not clargs->auth.tls_ca_cert) {
            auto v = ReadLocation(auth_map.at("ca cert"),
                                  clargs->common.just_mr_paths->workspace_root);
            if (v) {
                clargs->auth.tls_ca_cert = v->first;
            }
        }
        if (not clargs->auth.tls_client_cert) {
            auto v = ReadLocation(auth_map.at("client cert"),
                                  clargs->common.just_mr_paths->workspace_root);
            if (v) {
                clargs->auth.tls_client_cert = v->first;
            }
        }
        if (not clargs->auth.tls_client_key) {
            auto v = ReadLocation(auth_map.at("client key"),
                                  clargs->common.just_mr_paths->workspace_root);
            if (v) {
                clargs->auth.tls_client_key = v->first;
            }
        }
    }
    // read config lookup order
    auto config_lookup_order = rc_config["config lookup order"];
    if (config_lookup_order.IsNotNull()) {
        for (auto const& entry : config_lookup_order->List()) {
            auto paths = ReadLocation(
                entry, clargs->common.just_mr_paths->workspace_root);
            if (paths and FileSystemManager::IsFile(paths->first)) {
                clargs->common.just_mr_paths->setup_root = paths->second;
                return paths->first;
            }
        }
    }
    else {
        for (auto const& entry : kDefaultConfigLocations) {
            auto paths = ReadLocation(
                entry, clargs->common.just_mr_paths->workspace_root);
            if (paths and FileSystemManager::IsFile(paths->first)) {
                clargs->common.just_mr_paths->setup_root = paths->second;
                return paths->first;
            }
        }
    }
    return std::nullopt;  // default return value
}

}  // namespace

auto main(int argc, char* argv[]) -> int {
    SetupDefaultLogging();
    try {
        // get the user-defined arguments
        auto arguments = ParseCommandLineArguments(argc, argv);

        if (arguments.cmd == SubCommand::kMRVersion) {
            std::cout << version() << std::endl;
            return kExitSuccess;
        }

        SetupLogging(arguments.log);
        auto config_file = ReadJustMRRC(&arguments);
        if (arguments.common.repository_config) {
            config_file = arguments.common.repository_config;
        }

        // if optional args were not read from just-mrrc or given by user, use
        // the defaults
        if (not arguments.common.just_path) {
            arguments.common.just_path = kDefaultJustPath;
        }
        if (not arguments.common.git_path) {
            arguments.common.git_path = kDefaultGitPath;
        }
        bool forward_build_root = true;
        if (not arguments.common.just_mr_paths->root) {
            forward_build_root = false;
            arguments.common.just_mr_paths->root =
                std::filesystem::weakly_canonical(
                    std::filesystem::absolute(kDefaultBuildRoot));
        }
        if (not arguments.common.checkout_locations_file and
            FileSystemManager::IsFile(std::filesystem::weakly_canonical(
                std::filesystem::absolute(kDefaultCheckoutLocationsFile)))) {
            arguments.common.checkout_locations_file =
                std::filesystem::weakly_canonical(
                    std::filesystem::absolute(kDefaultCheckoutLocationsFile));
        }
        if (arguments.common.just_mr_paths->distdirs.empty()) {
            arguments.common.just_mr_paths->distdirs.emplace_back(
                kDefaultDistdirs);
        }

        // read checkout locations
        if (arguments.common.checkout_locations_file) {
            try {
                std::ifstream ifs(*arguments.common.checkout_locations_file);
                auto checkout_locations_json = nlohmann::json::parse(ifs);
                arguments.common.just_mr_paths->git_checkout_locations =
                    checkout_locations_json
                        .value("checkouts", nlohmann::json::object())
                        .value("git", nlohmann::json::object());
            } catch (std::exception const& e) {
                Logger::Log(
                    LogLevel::Error,
                    "Parsing checkout locations file {} failed with error:\n{}",
                    arguments.common.checkout_locations_file->string(),
                    e.what());
                std::exit(kExitConfigError);
            }
        }

        // append explicitly-given distdirs
        arguments.common.just_mr_paths->distdirs.insert(
            arguments.common.just_mr_paths->distdirs.end(),
            arguments.common.explicit_distdirs.begin(),
            arguments.common.explicit_distdirs.end());

        // Setup LocalStorageConfig to store the local_build_root properly
        // and make the cas and git cache roots available
        if (not StorageConfig::SetBuildRoot(
                *arguments.common.just_mr_paths->root)) {
            Logger::Log(LogLevel::Error,
                        "Failed to configure local build root.");
            return kExitGenericFailure;
        }

        // check for conflicts in main repo name
        if ((not arguments.setup.sub_all) and arguments.common.main and
            arguments.setup.sub_main and
            (arguments.common.main != arguments.setup.sub_main)) {
            Logger::Log(LogLevel::Warning,
                        "Conflicting options for main repository, selecting {}",
                        *arguments.setup.sub_main);
        }
        if (arguments.setup.sub_main) {
            arguments.common.main = arguments.setup.sub_main;
        }

        // check for errors in setting up local launcher arg
        if (not arguments.common.local_launcher) {
            Logger::Log(LogLevel::Error,
                        "Failed to configure local execution.");
            return kExitGenericFailure;
        }

        // set remote execution protocol compatibility
        if (arguments.common.compatible == true) {
            Compatibility::SetCompatible();
        }

        /**
         * The current implementation of libgit2 uses pthread_key_t incorrectly
         * on POSIX systems to handle thread-specific data, which requires us to
         * explicitly make sure the main thread is the first one to call
         * git_libgit2_init. Future versions of libgit2 will hopefully fix this.
         */
        GitContext::Create();

        // Run subcommands known to just and `do`
        if (arguments.cmd == SubCommand::kJustDo or
            arguments.cmd == SubCommand::kJustSubCmd) {
            return CallJust(config_file,
                            arguments.common,
                            arguments.setup,
                            arguments.just_cmd,
                            arguments.log,
                            arguments.auth,
                            forward_build_root);
        }
        auto lock = GarbageCollector::SharedLock();
        if (not lock) {
            return kExitGenericFailure;
        }

        // The remaining options all need the config file
        auto config = JustMR::Utils::ReadConfiguration(config_file);

        // Run subcommand `setup` or `setup-env`
        if (arguments.cmd == SubCommand::kSetup or
            arguments.cmd == SubCommand::kSetupEnv) {
            auto mr_config_path = MultiRepoSetup(
                config,
                arguments.common,
                arguments.setup,
                arguments.just_cmd,
                arguments.auth,
                /*interactive=*/(arguments.cmd == SubCommand::kSetupEnv));
            // dump resulting config to stdout
            if (not mr_config_path) {
                return kExitSetupError;
            }
            // report success
            Logger::Log(LogLevel::Info, "Setup completed");
            // print config file to stdout
            std::cout << mr_config_path->string() << std::endl;
            return kExitSuccess;
        }

        // Run subcommand `update`
        if (arguments.cmd == SubCommand::kUpdate) {
            return MultiRepoUpdate(config, arguments.common, arguments.update);
        }

        // Run subcommand `fetch`
        if (arguments.cmd == SubCommand::kFetch) {
            // check fetch configuration arguments for validity
            if (arguments.common.remote_execution_address and
                arguments.fetch.backup_to_remote and
                Compatibility::IsCompatible()) {
                Logger::Log(LogLevel::Error,
                            "Remote backup for fetched archives only available "
                            "in native mode!");
                return kExitConfigError;
            }
            return MultiRepoFetch(config,
                                  arguments.common,
                                  arguments.setup,
                                  arguments.fetch,
                                  arguments.auth);
        }

        // Unknown subcommand should fail
        Logger::Log(LogLevel::Error, "Unknown subcommand provided.");
        return kExitUnknownCommand;
    } catch (std::exception const& ex) {
        Logger::Log(
            LogLevel::Error, "Caught exception with message: {}", ex.what());
    }
    return kExitGenericFailure;
}
