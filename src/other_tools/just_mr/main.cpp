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

#include <exception>
#include <filesystem>
#include <utility>

#ifdef __unix__
#include <unistd.h>
#else
#error "Non-unix is not supported yet"
#endif

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
#include "src/other_tools/just_mr/rc.hpp"
#include "src/other_tools/just_mr/setup.hpp"
#include "src/other_tools/just_mr/setup_utils.hpp"
#include "src/other_tools/just_mr/update.hpp"

namespace {

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
        "just-mr, a multi-repository configuration tool and launcher for the "
        "build tool");
    app.option_defaults()->take_last();
    auto* cmd_mrversion = app.add_subcommand(
        "mrversion", "Print version information in JSON format of this tool.");
    auto* cmd_setup = app.add_subcommand(
        "setup", "Setup and generate configuration for the build tool");
    auto* cmd_setup_env = app.add_subcommand(
        "setup-env", "Setup without workspace root for the main repository.");
    auto* cmd_fetch =
        app.add_subcommand("fetch", "Fetch and store distribution files.");
    auto* cmd_update = app.add_subcommand(
        "update",
        "Advance Git commit IDs and print updated just-mr configuration.");
    auto* cmd_do = app.add_subcommand(
        "do", "Canonical way of specifying subcommands to be launched.");
    cmd_do->set_help_flag();  // disable help flag
    // define just subcommands
    std::vector<CLI::App*> cmd_just_subcmds{};
    cmd_just_subcmds.reserve(kKnownJustSubcommands.size());
    for (auto const& known_subcmd : kKnownJustSubcommands) {
        auto* subcmd =
            app.add_subcommand(known_subcmd.first,
                               "Run setup and launch the \"" +
                                   known_subcmd.first + "\" subcommand.");
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
    LogConfig::SetSinks({LogSinkCmdLine::CreateFactory(
        not clargs.plain_log, clargs.restrict_stderr_log_limit)});
    for (auto const& log_file : clargs.log_files) {
        LogConfig::AddSink(LogSinkFile::CreateFactory(
            log_file,
            clargs.log_append ? LogSinkFile::Mode::Append
                              : LogSinkFile::Mode::Overwrite));
    }
}

}  // namespace

auto main(int argc, char* argv[]) -> int {
    SetupDefaultLogging();
    std::string my_name{};
    if (argc > 0) {
        try {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            my_name = std::filesystem::path(argv[0]).filename().string();
        } catch (...) {
            // ignore, as my_name is only used for error messages
        }
    }
    try {
        // get the user-defined arguments
        auto arguments = ParseCommandLineArguments(argc, argv);

        if (arguments.cmd == SubCommand::kMRVersion) {
            std::cout << version() << std::endl;
            return kExitSuccess;
        }

        SetupLogging(arguments.log);
        auto config_file = ReadJustMRRC(&arguments);
        // As the rc file can contain logging parameters, reset the logging
        // configuration
        SetupLogging(arguments.log);
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

        // read checkout locations and alternative mirrors
        if (arguments.common.checkout_locations_file) {
            try {
                std::ifstream ifs(*arguments.common.checkout_locations_file);
                auto checkout_locations_json = nlohmann::json::parse(ifs);
                arguments.common.just_mr_paths->git_checkout_locations =
                    checkout_locations_json
                        .value("checkouts", nlohmann::json::object())
                        .value("git", nlohmann::json::object());
                arguments.common.alternative_mirrors->local_mirrors =
                    checkout_locations_json.value("local mirrors",
                                                  nlohmann::json::object());
                arguments.common.alternative_mirrors->preferred_hostnames =
                    checkout_locations_json.value("preferred hostnames",
                                                  nlohmann::json::array());
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
            // check setup configuration arguments for validity
            if (arguments.common.compatible == true and
                arguments.common.fetch_absent) {
                Logger::Log(LogLevel::Error,
                            "Fetching absent repositories only available in "
                            "native mode!");
                return kExitConfigError;
            }
            return CallJust(config_file,
                            arguments.common,
                            arguments.setup,
                            arguments.just_cmd,
                            arguments.log,
                            arguments.auth,
                            arguments.launch_fwd,
                            forward_build_root,
                            my_name);
        }
        auto lock = GarbageCollector::SharedLock();
        if (not lock) {
            return kExitGenericFailure;
        }

        // The remaining options all need the config file
        auto config = JustMR::Utils::ReadConfiguration(
            config_file, arguments.common.absent_repository_file);

        // Run subcommand `setup` or `setup-env`
        if (arguments.cmd == SubCommand::kSetup or
            arguments.cmd == SubCommand::kSetupEnv) {
            // check setup configuration arguments for validity
            if (arguments.common.compatible == true and
                arguments.common.fetch_absent) {
                Logger::Log(LogLevel::Error,
                            "Fetching absent repositories only available in "
                            "native mode!");
                return kExitConfigError;
            }
            auto mr_config_path = MultiRepoSetup(
                config,
                arguments.common,
                arguments.setup,
                arguments.just_cmd,
                arguments.auth,
                /*interactive=*/(arguments.cmd == SubCommand::kSetupEnv),
                my_name);
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
            return MultiRepoUpdate(
                config, arguments.common, arguments.update, my_name);
        }

        // Run subcommand `fetch`
        if (arguments.cmd == SubCommand::kFetch) {
            // check fetch configuration arguments for validity
            if (arguments.common.compatible == true) {
                if (arguments.common.remote_execution_address and
                    arguments.fetch.backup_to_remote) {
                    Logger::Log(
                        LogLevel::Error,
                        "Remote backup for fetched archives only available "
                        "in native mode!");
                    return kExitConfigError;
                }
                if (arguments.common.fetch_absent) {
                    Logger::Log(LogLevel::Error,
                                "Fetching absent repositories only available "
                                "in native mode!");
                    return kExitConfigError;
                }
            }
            return MultiRepoFetch(config,
                                  arguments.common,
                                  arguments.setup,
                                  arguments.fetch,
                                  arguments.auth,
                                  my_name);
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
