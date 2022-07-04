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

#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/buildtool/logging/log_config.hpp"
#include "src/buildtool/logging/log_sink_cmdline.hpp"
#include "src/other_tools/just_mr/cli.hpp"
#include "src/other_tools/just_mr/exit_codes.hpp"

namespace {

enum class SubCommand {
    kUnknown,
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
    MultiRepoSetupArguments setup;
    MultiRepoFetchArguments fetch;
    MultiRepoUpdateArguments update;
    MultiRepoJustSubCmdsArguments just_cmd;
};

struct SetupRepos {
    std::vector<std::string> to_setup;
    std::vector<std::string> to_include;
};

/// \brief Setup arguments for just-mr itself, common to all subcommands.
void SetupCommonCommandArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<CommandLineArguments*> const& clargs) {
    SetupMultiRepoCommonArguments(app, &clargs->common);
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

void SetupDefaultLogging() {
    LogConfig::SetLogLimit(kDefaultLogLevel);
    LogConfig::SetSinks({LogSinkCmdLine::CreateFactory()});
}

[[nodiscard]] auto ParseCommandLineArguments(int argc, char const* const* argv)
    -> CommandLineArguments {
    CLI::App app("just-mr");
    app.option_defaults()->take_last();
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
        "do", "Canonical way of specifying just subcommands. ");
    cmd_do->set_help_flag();  // disable help flag
    // define just subcommands
    std::vector<CLI::App*> cmd_just_subcmds{};
    cmd_just_subcmds.reserve(kKnownJustSubcommands.size());
    for (auto const& known_subcmd : kKnownJustSubcommands) {
        auto* subcmd = app.add_subcommand(
            known_subcmd.first,
            "Run setup and call \'just " + known_subcmd.first + "\'.");
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

    if (*cmd_setup) {
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
        root_path = LocalExecutionConfig::GetUserHome();
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

        if (not path.IsNotNull() or not root.IsNotNull() or
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
            root_path = LocalExecutionConfig::GetUserHome();
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
            if (not FileSystemManager::IsFile(kDefaultRCPath)) {
                return std::nullopt;
            }
            rc_path = kDefaultRCPath;
        }
        else {

            if (not FileSystemManager::IsFile(*rc_path)) {
                Logger::Log(LogLevel::Error,
                            "Cannot read RC file {}.",
                            rc_path->string());
                std::exit(kExitConfigError);
            }
        }
        try {
            std::ifstream fs(*rc_path);
            auto map = Expression::FromJson(nlohmann::json::parse(fs));
            if (not map->IsMap()) {
                Logger::Log(LogLevel::Error,
                            "RC file {} does not contain a JSON object.",
                            rc_path->string());
                std::exit(kExitConfigError);
            }
            rc_config = Configuration{map};
        } catch (std::exception const& e) {
            Logger::Log(LogLevel::Error,
                        "Parsing RC file {} failed with error:\n{}",
                        rc_path->string(),
                        e.what());
            std::exit(kExitConfigError);
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
    // read additional just args; user can append, but does not overwrite
    auto just_args = rc_config["just args"];
    if (just_args.IsNotNull()) {
        for (auto const& [cmd_name, cmd_args] : just_args->Map()) {
            // get list of string args for current command
            clargs->just_cmd.just_args[cmd_name] =
                [&cmd_args = cmd_args]() -> std::vector<std::string> {
                std::vector<std::string> args{};
                auto const& args_list = cmd_args->List();
                args.resize(args_list.size());
                for (auto const& arg : args_list) {
                    args.emplace_back(arg->String());
                }
                return args;
            }();
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

[[nodiscard]] auto ReadConfiguration(
    std::filesystem::path const& config_file) noexcept
    -> std::shared_ptr<Configuration> {
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

}  // namespace

auto main(int argc, char* argv[]) -> int {
    SetupDefaultLogging();
    try {
        // get the user-defined arguments
        auto arguments = ParseCommandLineArguments(argc, argv);

        auto config_file = ReadJustMRRC(&arguments);
        if (arguments.common.repository_config) {
            config_file = arguments.common.repository_config;
        }
        if (not config_file) {
            Logger::Log(LogLevel::Error,
                        "Cannot find repository configuration.");
            std::exit(kExitConfigError);
        }

        auto config = ReadConfiguration(*config_file);

        // if optional args were not read from just-mrrc or given by user, use
        // the defaults
        if (not arguments.common.just_path) {
            arguments.common.just_path = kDefaultJustPath;
        }
        if (not arguments.common.just_mr_paths->root) {
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

        // Setup LocalExecutionConfig to store the local_build_root properly
        // and make the cas and git cache roots available
        if (not LocalExecutionConfig::SetBuildRoot(
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

        if (arguments.cmd == SubCommand::kJustDo or
            arguments.cmd == SubCommand::kJustSubCmd) {
            return kExitSuccess;
        }

        if (arguments.cmd == SubCommand::kSetup or
            arguments.cmd == SubCommand::kSetupEnv) {
            return kExitSuccess;
        }

        if (arguments.cmd == SubCommand::kUpdate) {
            return kExitSuccess;
        }

        if (arguments.cmd == SubCommand::kFetch) {
            return kExitSuccess;
        }

        // default exit code for unknown subcommand
        return kExitUnknownCommand;
    } catch (std::exception const& ex) {
        Logger::Log(
            LogLevel::Error, "Caught exception with message: {}", ex.what());
    }
    return kExitGenericFailure;
}
