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

#include "src/other_tools/just_mr/rc.hpp"

#include <cmath>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>  // std::move
#include <vector>

#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/build_engine/expression/expression_ptr.hpp"
#include "src/buildtool/common/clidefaults.hpp"
#include "src/buildtool/common/location.hpp"
#include "src/buildtool/common/retry_cli.hpp"
#include "src/buildtool/common/user_structs.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/other_tools/just_mr/exit_codes.hpp"
#include "src/other_tools/just_mr/rc_merge.hpp"
#include "src/other_tools/just_mr/utils.hpp"
#include "src/utils/cpp/expected.hpp"

namespace {

/// \brief Overlay for ReadLocationObject accepting an ExpressionPtr and that
/// can std::exit
[[nodiscard]] auto ReadLocation(
    ExpressionPtr const& location,
    std::optional<std::filesystem::path> const& ws_root)
    -> std::optional<std::pair<std::filesystem::path, std::filesystem::path>> {
    if (location.IsNotNull()) {
        auto res = ReadLocationObject(location->ToJson(), ws_root);
        if (not res) {
            Logger::Log(LogLevel::Error, res.error());
            std::exit(kExitConfigError);
        }
        return *res;
    }
    return std::nullopt;
}

/// \brief Overlay of ReadLocationObject that can std::exit
[[nodiscard]] auto ReadLocation(
    nlohmann::json const& location,
    std::optional<std::filesystem::path> const& ws_root)
    -> std::optional<std::pair<std::filesystem::path, std::filesystem::path>> {
    auto res = ReadLocationObject(location, ws_root);
    if (not res) {
        Logger::Log(LogLevel::Error, res.error());
        std::exit(kExitConfigError);
    }
    return *res;
}

[[nodiscard]] auto ReadOptionalLocationList(
    ExpressionPtr const& location_list,
    std::optional<std::filesystem::path> const& ws_root,
    std::string const& argument_name) -> std::optional<std::filesystem::path> {
    if (location_list->IsNone()) {
        return std::nullopt;
    }
    if (not location_list->IsList()) {
        Logger::Log(LogLevel::Error,
                    "Argument {} has to be a list, but found {}",
                    argument_name,
                    location_list->ToString());
        std::exit(kExitConfigError);
    }
    for (auto const& location : location_list->List()) {
        auto p = ReadLocation(location, ws_root);
        if (p and FileSystemManager::IsFile(p->first)) {
            return p->first;
        }
    }
    return std::nullopt;
}

[[nodiscard]] auto ObtainRCConfig(
    gsl::not_null<CommandLineArguments*> const& clargs) -> Configuration {
    Configuration rc_config{};
    auto rc_path = clargs->common.rc_path;
    // set default if rcpath not given
    if (not clargs->common.norc) {
        if (not rc_path) {
            rc_path = std::filesystem::weakly_canonical(kDefaultRCPath);
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
    return rc_config;
}

}  // namespace

/// \brief Read just-mrrc file and set up various configs. Return the path to
/// the repository config file, if any is provided.
[[nodiscard]] auto ReadJustMRRC(
    gsl::not_null<CommandLineArguments*> const& clargs)
    -> std::optional<std::filesystem::path> {
    Configuration rc_config = ObtainRCConfig(clargs);

    // Merge in the rc-files to overlay
    auto extra_rc_files = rc_config["rc files"];
    if (not extra_rc_files->IsNone()) {
        if (not extra_rc_files->IsList()) {
            Logger::Log(
                LogLevel::Error,
                "'rc files' has to be a list of location objects, but found {}",
                extra_rc_files->ToString());
            std::exit(kExitConfigError);
        }
        for (auto const& entry : extra_rc_files->List()) {
            auto extra_rc_location = ReadLocation(
                entry, clargs->common.just_mr_paths->workspace_root);
            if (extra_rc_location) {
                auto const& extra_rc_path = extra_rc_location->first;
                if (FileSystemManager::IsFile(extra_rc_path)) {
                    Configuration extra_rc_config{};
                    try {
                        std::ifstream fs(extra_rc_path);
                        auto map =
                            Expression::FromJson(nlohmann::json::parse(fs));
                        if (not map->IsMap()) {
                            Logger::Log(LogLevel::Error,
                                        "In extra RC file {}: expected an "
                                        "object but found:\n{}",
                                        extra_rc_path.string(),
                                        map->ToString());
                            std::exit(kExitConfigError);
                        }
                        extra_rc_config = Configuration{map};
                    } catch (std::exception const& e) {
                        Logger::Log(LogLevel::Error,
                                    "Parsing extra RC file {} as JSON failed "
                                    "with error:\n{}",
                                    extra_rc_path.string(),
                                    e.what());
                        std::exit(kExitConfigError);
                    }
                    rc_config = MergeMRRC(rc_config, extra_rc_config);
                }
            }
        }
    }

    // If requested, dump effective rc
    if (clargs->common.dump_rc) {
        auto dump_json = rc_config.ToJson();
        dump_json.erase("rc files");
        std::ofstream os(*clargs->common.dump_rc);
        os << dump_json.dump(2) << std::endl;
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
    // read the just file-arguments
    auto just_files = rc_config["just files"];
    if (just_files.IsNotNull()) {
        if (not just_files->IsMap()) {
            Logger::Log(LogLevel::Error,
                        "Configuration-file provided 'just files' has to be a "
                        "map, but found {}.",
                        just_files->ToString());
            std::exit(kExitConfigError);
        }
        auto files = Configuration(just_files);
        clargs->just_cmd.config = ReadOptionalLocationList(
            files["config"],
            clargs->common.just_mr_paths->workspace_root,
            "'config' in 'just files'");
        clargs->just_cmd.endpoint_configuration = ReadOptionalLocationList(
            files["endpoint-configuration"],
            clargs->common.just_mr_paths->workspace_root,
            "'endpoint-configuration' in 'just files'");
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
                if (not arg->IsString()) {
                    Logger::Log(
                        LogLevel::Error,
                        "Configuration-file provided 'just' argument key {} "
                        "must have strings in its list value, but found {}",
                        cmd_name,
                        arg->ToString());
                    std::exit(kExitConfigError);
                }
                args.emplace_back(arg->String());
            }
            clargs->just_cmd.just_args[cmd_name] = std::move(args);
        }
    }
    // read remote-execution properties, used for extending the launch
    // command-line (not settable on just-mr's command line).
    auto re_props = rc_config["remote-execution properties"];
    if (re_props.IsNotNull()) {
        if (not re_props->IsList()) {
            Logger::Log(LogLevel::Error,
                        "Configuration-file provided remote-execution "
                        "properties have to be a list of strings, but found {}",
                        re_props->ToString());
            std::exit(kExitConfigError);
        }
        for (auto const& entry : re_props->List()) {
            if (not entry->IsString()) {
                Logger::Log(
                    LogLevel::Error,
                    "Configuration-file provided remote-execution properties "
                    "have to be a list of strings, but found entry {}",
                    entry->ToString());
                std::exit(kExitConfigError);
            }
            clargs->launch_fwd.remote_execution_properties.emplace_back(
                entry->String());
        }
    }
    // read the defaults for the retry parameters
    if (not clargs->retry.max_attempts) {
        auto max_attempts = rc_config["max attempts"];
        if (max_attempts.IsNotNull()) {
            if (not max_attempts->IsNumber()) {
                Logger::Log(LogLevel::Error,
                            "Configuration-file provided \"max attempts\" has "
                            "to be a number, but found {}",
                            max_attempts->ToString());
                std::exit(kExitConfigError);
            }
            clargs->retry.max_attempts =
                static_cast<unsigned int>(std::lround(max_attempts->Number()));
        }
    }
    if (not clargs->retry.initial_backoff_seconds) {
        auto initial_backoff_seconds = rc_config["initial backoff seconds"];
        if (initial_backoff_seconds.IsNotNull()) {
            if (not initial_backoff_seconds->IsNumber()) {
                Logger::Log(LogLevel::Error,
                            "Configuration-file provided \"initial backoff "
                            "seconds\" has to be a number, but found {}",
                            initial_backoff_seconds->ToString());
                std::exit(kExitConfigError);
            }
            clargs->retry.initial_backoff_seconds = static_cast<unsigned int>(
                std::lround(initial_backoff_seconds->Number()));
        }
    }
    if (not clargs->retry.max_backoff_seconds) {
        auto max_backoff_seconds = rc_config["max backoff seconds"];
        if (max_backoff_seconds.IsNotNull()) {
            if (not max_backoff_seconds->IsNumber()) {
                Logger::Log(LogLevel::Error,
                            "Configuration-file provided \"max backoff "
                            "seconds\" has to be a number, but found {}",
                            max_backoff_seconds->ToString());
                std::exit(kExitConfigError);
            }
            clargs->retry.max_backoff_seconds = static_cast<unsigned int>(
                std::lround(max_backoff_seconds->Number()));
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
        }
    }
    // Set restrict stderr log limit, if specified and not set on the command
    // line
    if (not clargs->log.restrict_stderr_log_limit) {
        auto limit = rc_config["restrict stderr log limit"];
        if (limit.IsNotNull()) {
            if (not limit->IsNumber()) {
                Logger::Log(LogLevel::Error,
                            "Configuration-file specified log-limit has to be "
                            "a number, but found {}",
                            limit->ToString());
                std::exit(kExitConfigError);
            }
            clargs->log.restrict_stderr_log_limit = ToLogLevel(limit->Number());
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
        auto const& files_list = log_files->List();
        clargs->log.log_files.reserve(clargs->log.log_files.size() +
                                      files_list.size());
        for (auto const& log_file : files_list) {
            auto path = ReadLocation(
                log_file, clargs->common.just_mr_paths->workspace_root);
            if (path) {
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
        if (not clargs->common.remote_execution_address) {
            auto addr = remote->Get("address", Expression::none_t{});
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
            auto compat = remote->Get("compatible", Expression::none_t{});
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
    // read remote exec args; overwritten if user provided it already
    auto serve = rc_config["remote serve"];
    if (serve.IsNotNull()) {
        if (not serve->IsMap()) {
            Logger::Log(LogLevel::Error,
                        "Configuration-provided remote serve service arguments "
                        "has to be a map, but found {}",
                        serve->ToString());
            std::exit(kExitConfigError);
        }
        if (not clargs->common.remote_serve_address) {
            auto addr = serve->Get("address", Expression::none_t{});
            if (addr.IsNotNull()) {
                if (not addr->IsString()) {
                    Logger::Log(LogLevel::Error,
                                "Configuration-provided remote serve service "
                                "address has to be a string, but found {}",
                                addr->ToString());
                    std::exit(kExitConfigError);
                }
                clargs->common.remote_serve_address = addr->String();
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
        if (not clargs->auth.tls_ca_cert) {
            auto v =
                ReadLocation(auth_args->Get("ca cert", Expression::none_t{}),
                             clargs->common.just_mr_paths->workspace_root);
            if (v) {
                clargs->auth.tls_ca_cert = v->first;
            }
        }
        if (not clargs->auth.tls_client_cert) {
            auto v = ReadLocation(
                auth_args->Get("client cert", Expression::none_t{}),
                clargs->common.just_mr_paths->workspace_root);
            if (v) {
                clargs->auth.tls_client_cert = v->first;
            }
        }
        if (not clargs->auth.tls_client_key) {
            auto v =
                ReadLocation(auth_args->Get("client key", Expression::none_t{}),
                             clargs->common.just_mr_paths->workspace_root);
            if (v) {
                clargs->auth.tls_client_key = v->first;
            }
        }
    }
    // read path to absent repository specification if not already provided by
    // the user
    if (not clargs->common.absent_repository_file) {
        auto absent_order = rc_config["absent"];
        if (absent_order.IsNotNull() and absent_order->IsList()) {
            for (auto const& entry : absent_order->List()) {
                auto path = ReadLocation(
                    entry, clargs->common.just_mr_paths->workspace_root);
                if (path and FileSystemManager::IsFile(path->first)) {
                    clargs->common.absent_repository_file = path->first;
                    break;
                }
            }
        }
    }
    // read invocation log argumnets
    auto invocation_log = rc_config["invocation log"];
    if (invocation_log.IsNotNull()) {
        if (not invocation_log->IsMap()) {
            Logger::Log(
                LogLevel::Error,
                "Value of \"invocation log\" has to be a map, but found {}",
                invocation_log->ToString());
            std::exit(kExitConfigError);
        }
        auto dir =
            ReadLocation(invocation_log->Get("directory", Expression::none_t{}),
                         clargs->common.just_mr_paths->workspace_root);
        if (dir) {
            clargs->invocation_log.directory = dir->first;
            // Parse the remaining entries, only if  directory is specified
            auto proj_id =
                invocation_log->Get("project id", Expression::none_t{});
            if (proj_id->IsString()) {
                clargs->invocation_log.project_id = proj_id->String();
            }
            auto metadata =
                invocation_log->Get("metadata", Expression::none_t{});
            if (metadata->IsString()) {
                clargs->invocation_log.metadata = metadata->String();
            }
            auto graph_file =
                invocation_log->Get("--dump-graph", Expression::none_t{});
            if (graph_file->IsString()) {
                clargs->invocation_log.graph_file = graph_file->String();
            }
            auto graph_file_plain =
                invocation_log->Get("--dump-plain-graph", Expression::none_t{});
            if (graph_file_plain->IsString()) {
                clargs->invocation_log.graph_file = graph_file_plain->String();
            }
            auto dump_artifacts_to_build = invocation_log->Get(
                "--dump-artifacts-to-build", Expression::none_t{});
            if (dump_artifacts_to_build->IsString()) {
                clargs->invocation_log.dump_artifacts_to_build =
                    dump_artifacts_to_build->String();
            }
            auto dump_artifacts =
                invocation_log->Get("--dump-artifacts", Expression::none_t{});
            if (dump_artifacts->IsString()) {
                clargs->invocation_log.dump_artifacts =
                    dump_artifacts->String();
            }
            auto profile =
                invocation_log->Get("--profile", Expression::none_t{});
            if (profile->IsString()) {
                clargs->invocation_log.profile = profile->String();
            }
            auto msg =
                invocation_log->Get("invocation message", Expression::none_t{});
            if (msg->IsString()) {
                clargs->invocation_log.invocation_msg = msg->String();
            }
            auto context_vars =
                invocation_log->Get("context variables", Expression::none_t{});
            if (context_vars->IsList()) {
                for (auto const& env_var : context_vars->List()) {
                    if (env_var->IsString()) {
                        clargs->invocation_log.context_vars.emplace_back(
                            env_var->String());
                    }
                }
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
