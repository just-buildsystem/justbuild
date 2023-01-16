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

#include <nlohmann/json.hpp>
#include <unistd.h>

#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/buildtool/logging/log_config.hpp"
#include "src/buildtool/logging/log_sink_cmdline.hpp"
#include "src/other_tools/just_mr/cli.hpp"
#include "src/other_tools/just_mr/exit_codes.hpp"
#include "src/other_tools/ops_maps/git_update_map.hpp"
#include "src/other_tools/ops_maps/repo_fetch_map.hpp"
#include "src/other_tools/repo_map/repos_to_setup_map.hpp"

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

void ReachableRepositories(ExpressionPtr const& repos,
                           std::string const& main,
                           std::shared_ptr<SetupRepos> const& setup_repos) {
    // use temporary sets to avoid duplicates
    std::unordered_set<std::string> include_repos_set{};
    if (repos->IsMap()) {
        // traversal of bindings
        std::function<void(std::string const&)> traverse =
            [&](std::string const& repo_name) {
                if (not include_repos_set.contains(repo_name)) {
                    // if not found, add it and repeat for its bindings
                    include_repos_set.insert(repo_name);
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
        for (auto const& repo : setup_repos->to_include) {
            auto repos_repo = repos->Get(repo, Expression::none_t{});
            if (repos_repo.IsNotNull()) {
                // copy over any present alternative root dirs
                for (auto const& layer : kAltDirs) {
                    auto layer_val =
                        repos_repo->Get(layer, Expression::none_t{});
                    if (layer_val.IsNotNull() and layer_val->IsString()) {
                        setup_repos_set.insert(layer_val->String());
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
    std::shared_ptr<SetupRepos> const& setup_repos) {
    if (repos.IsNotNull() and repos->IsMap()) {
        setup_repos->to_setup = repos->Map().Keys();
        setup_repos->to_include = setup_repos->to_setup;
    }
}

[[nodiscard]] auto MultiRepoFetch(std::shared_ptr<Configuration> const& config,
                                  CommandLineArguments const& arguments)
    -> int {
    // find fetch dir
    auto fetch_dir = arguments.fetch.fetch_dir;
    if (not fetch_dir) {
        for (auto const& d : arguments.common.just_mr_paths->distdirs) {
            if (FileSystemManager::IsDirectory(d)) {
                fetch_dir = std::filesystem::weakly_canonical(
                    std::filesystem::absolute(d));
                break;
            }
        }
    }
    if (not fetch_dir) {
        auto considered =
            nlohmann::json(arguments.common.just_mr_paths->distdirs);
        Logger::Log(LogLevel::Error,
                    "No directory found to fetch to, considered {}",
                    considered.dump());
        return kExitFetchError;
    }

    auto repos = (*config)["repositories"];
    if (not repos.IsNotNull()) {
        Logger::Log(LogLevel::Error,
                    "Config: mandatory key \"repositories\" "
                    "missing");
        return kExitFetchError;
    }
    auto fetch_repos =
        std::make_shared<SetupRepos>();  // repos to setup and include
    DefaultReachableRepositories(repos, fetch_repos);

    if (not arguments.setup.sub_all) {
        auto main = arguments.common.main;
        if (not main and not fetch_repos->to_include.empty()) {
            main = *std::min_element(fetch_repos->to_include.begin(),
                                     fetch_repos->to_include.end());
        }
        if (main) {
            ReachableRepositories(repos, *main, fetch_repos);
        }

        std::function<bool(std::filesystem::path const&,
                           std::filesystem::path const&)>
            is_subpath = [](std::filesystem::path const& path,
                            std::filesystem::path const& base) {
                return (std::filesystem::proximate(path, base) == base);
            };

        // warn if fetch_dir is in invocation workspace
        if (arguments.common.just_mr_paths->workspace_root and
            is_subpath(*fetch_dir,
                       *arguments.common.just_mr_paths->workspace_root)) {
            auto repo_desc = repos->Get(*main, Expression::none_t{});
            auto repo = repo_desc->Get("repository", Expression::none_t{});
            auto repo_path = repo->Get("path", Expression::none_t{});
            auto repo_type = repo->Get("type", Expression::none_t{});
            if (repo_path->IsString() and repo_type->IsString() and
                (repo_type->String() == "file")) {
                auto repo_path_as_path =
                    std::filesystem::path(repo_path->String());
                if (not repo_path_as_path.is_absolute()) {
                    repo_path_as_path = std::filesystem::weakly_canonical(
                        std::filesystem::absolute(
                            arguments.common.just_mr_paths->setup_root /
                            repo_path_as_path));
                }
                // only warn if repo workspace differs to invocation workspace
                if (not is_subpath(
                        repo_path_as_path,
                        *arguments.common.just_mr_paths->workspace_root)) {
                    Logger::Log(
                        LogLevel::Warning,
                        "Writing distributiona files to workspace location {}, "
                        "which is different to the workspace of the requested "
                        "main repository {}.",
                        fetch_dir->string(),
                        repo_path_as_path.string());
                }
            }
        }
    }

    Logger::Log(LogLevel::Info, "Fetching to {}", fetch_dir->string());

    // gather all repos to be fetched
    std::vector<ArchiveRepoInfo> repos_to_fetch{};
    repos_to_fetch.reserve(
        fetch_repos->to_include.size());  // pre-reserve a maximum size
    for (auto const& repo_name : fetch_repos->to_include) {
        auto repo_desc = repos->At(repo_name);
        if (not repo_desc) {
            Logger::Log(LogLevel::Error,
                        "Config: missing config entry for repository {}",
                        repo_name);
            return kExitFetchError;
        }
        auto repo = repo_desc->get()->At("repository");
        if (repo) {
            auto resolved_repo_desc =
                JustMR::Utils::ResolveRepo(repo->get(), repos);
            if (not resolved_repo_desc) {
                Logger::Log(LogLevel::Error,
                            "Config: found cyclic dependency for "
                            "repository {}",
                            repo_name);
                return kExitFetchError;
            }
            // get repo_type
            auto repo_type = (*resolved_repo_desc)->At("type");
            if (not repo_type) {
                Logger::Log(LogLevel::Error,
                            "Config: mandatory key \"type\" missing "
                            "for repository {}",
                            repo_name);
                return kExitFetchError;
            }
            if (not repo_type->get()->IsString()) {
                Logger::Log(LogLevel::Error,
                            "Config: unsupported value for key \'type\' for "
                            "repository {}",
                            repo_name);
                return kExitFetchError;
            }
            auto repo_type_str = repo_type->get()->String();
            if (not kCheckoutTypeMap.contains(repo_type_str)) {
                Logger::Log(LogLevel::Error,
                            "Unknown repository type {} for {}",
                            repo_type_str,
                            repo_name);
                return kExitFetchError;
            }
            // only do work if repo is archive type
            if (kCheckoutTypeMap.at(repo_type_str) == CheckoutType::Archive) {
                // check mandatory fields
                auto repo_desc_content = (*resolved_repo_desc)->At("content");
                if (not repo_desc_content) {
                    Logger::Log(LogLevel::Error,
                                "Mandatory field \"content\" is missing");
                    return kExitFetchError;
                }
                if (not repo_desc_content->get()->IsString()) {
                    Logger::Log(
                        LogLevel::Error,
                        "Unsupported value for mandatory field \'content\'");
                    return kExitFetchError;
                }
                auto repo_desc_fetch = (*resolved_repo_desc)->At("fetch");
                if (not repo_desc_fetch) {
                    Logger::Log(LogLevel::Error,
                                "Mandatory field \"fetch\" is missing");
                    return kExitFetchError;
                }
                if (not repo_desc_fetch->get()->IsString()) {
                    Logger::Log(LogLevel::Error,
                                "ArchiveCheckout: Unsupported value for "
                                "mandatory field \'fetch\'");
                    return kExitFetchError;
                }
                auto repo_desc_subdir =
                    (*resolved_repo_desc)->Get("subdir", Expression::none_t{});
                auto subdir =
                    std::filesystem::path(repo_desc_subdir->IsString()
                                              ? repo_desc_subdir->String()
                                              : "")
                        .lexically_normal();
                auto repo_desc_distfile =
                    (*resolved_repo_desc)
                        ->Get("distfile", Expression::none_t{});
                auto repo_desc_sha256 =
                    (*resolved_repo_desc)->Get("sha256", Expression::none_t{});
                auto repo_desc_sha512 =
                    (*resolved_repo_desc)->Get("sha512", Expression::none_t{});
                ArchiveRepoInfo archive_info = {
                    {
                        repo_desc_content->get()->String(), /* content */
                        repo_desc_distfile->IsString()
                            ? repo_desc_distfile->String()
                            : "",                         /* distfile */
                        repo_desc_fetch->get()->String(), /* fetch_url */
                        repo_desc_sha256->IsString()
                            ? repo_desc_sha256->String()
                            : "", /* sha256 */
                        repo_desc_sha512->IsString()
                            ? repo_desc_sha512->String()
                            : "" /* sha512 */
                    },           /* archive */
                    repo_type_str,
                    subdir.empty() ? "." : subdir.string()};
                // add to list
                repos_to_fetch.emplace_back(std::move(archive_info));
            }
        }
        else {
            Logger::Log(LogLevel::Error,
                        "Config: missing repository description for {}",
                        repo_name);
            return kExitFetchError;
        }
    }
    // create async maps
    auto content_cas_map = CreateContentCASMap(arguments.common.just_mr_paths,
                                               arguments.common.jobs);
    auto repo_fetch_map =
        CreateRepoFetchMap(&content_cas_map, *fetch_dir, arguments.common.jobs);
    // do the fetch
    bool failed{false};
    {
        TaskSystem ts{arguments.common.jobs};
        repo_fetch_map.ConsumeAfterKeysReady(
            &ts,
            repos_to_fetch,
            [&failed](auto const& values) {
                // report any fetch fails
                for (auto const& val : values) {
                    if (not *val) {
                        failed = true;
                        break;
                    }
                }
            },
            [&failed](auto const& msg, bool fatal) {
                Logger::Log(fatal ? LogLevel::Error : LogLevel::Warning,
                            "While performing just-mr fetch:\n{}",
                            msg);
                failed = failed or fatal;
            });
    }
    if (failed) {
        return kExitFetchError;
    }
    return kExitSuccess;
}

[[nodiscard]] auto MultiRepoUpdate(std::shared_ptr<Configuration> const& config,
                                   CommandLineArguments const& arguments)
    -> int {
    // Check trivial case
    if (arguments.update.repos_to_update.empty()) {
        std::cout << config->ToJson().dump(2) << std::endl;
        return kExitSuccess;
    }
    auto repos = (*config)["repositories"];
    if (not repos.IsNotNull()) {
        Logger::Log(LogLevel::Error,
                    "Config: mandatory key \"repositories\" "
                    "missing");
        return kExitUpdateError;
    }
    // gather repos to update
    std::vector<std::pair<std::string, std::string>> repos_to_update{};
    repos_to_update.reserve(arguments.update.repos_to_update.size());
    for (auto const& repo_name : arguments.update.repos_to_update) {
        auto repo_desc_parent = repos->At(repo_name);
        if (not repo_desc_parent) {
            Logger::Log(LogLevel::Error,
                        "Config: missing config entry for repository {}",
                        repo_name);
            return kExitUpdateError;
        }
        auto repo_desc = repo_desc_parent->get()->At("repository");
        if (repo_desc) {
            auto resolved_repo_desc =
                JustMR::Utils::ResolveRepo(repo_desc->get(), repos);
            if (not resolved_repo_desc) {
                Logger::Log(LogLevel::Error,
                            fmt::format("Config: found cyclic dependency for "
                                        "repository {}",
                                        repo_name));
                return kExitUpdateError;
            }
            // get repo_type
            auto repo_type = (*resolved_repo_desc)->At("type");
            if (not repo_type) {
                Logger::Log(LogLevel::Error,
                            "Config: mandatory key \"type\" missing "
                            "for repository {}",
                            repo_name);
                return kExitUpdateError;
            }
            if (not repo_type->get()->IsString()) {
                Logger::Log(LogLevel::Error,
                            "Config: unsupported value for key \'type\' for "
                            "repository {}",
                            repo_name);
                return kExitUpdateError;
            }
            auto repo_type_str = repo_type->get()->String();
            if (not kCheckoutTypeMap.contains(repo_type_str)) {
                Logger::Log(LogLevel::Error,
                            "Unknown repository type {} for {}",
                            repo_type_str,
                            repo_name);
                return kExitUpdateError;
            }
            // only do work if repo is git type
            if (kCheckoutTypeMap.at(repo_type_str) == CheckoutType::Git) {
                auto repo_desc_repository =
                    (*resolved_repo_desc)->At("repository");
                if (not repo_desc_repository) {
                    Logger::Log(LogLevel::Error,
                                "Mandatory field \"repository\" is missing");
                    return kExitUpdateError;
                }
                if (not repo_desc_repository->get()->IsString()) {
                    Logger::Log(
                        LogLevel::Error,
                        "Config: unsupported value for key \'repository\' for "
                        "repository {}",
                        repo_name);
                    return kExitUpdateError;
                }
                auto repo_desc_branch = (*resolved_repo_desc)->At("branch");
                if (not repo_desc_branch) {
                    Logger::Log(LogLevel::Error,
                                "Mandatory field \"branch\" is missing");
                    return kExitUpdateError;
                }
                if (not repo_desc_branch->get()->IsString()) {
                    Logger::Log(
                        LogLevel::Error,
                        "Config: unsupported value for key \'branch\' for "
                        "repository {}",
                        repo_name);
                    return kExitUpdateError;
                }
                repos_to_update.emplace_back(
                    std::make_pair(repo_desc_repository->get()->String(),
                                   repo_desc_branch->get()->String()));
            }
            else {
                Logger::Log(LogLevel::Error,
                            "Config: argument {} is not the name of a git type "
                            "repository",
                            repo_name);
                return kExitUpdateError;
            }
        }
        else {
            Logger::Log(LogLevel::Error,
                        "Config: missing repository description for {}",
                        repo_name);
            return kExitUpdateError;
        }
    }
    // Create fake repo for the anonymous remotes
    auto tmp_dir = JustMR::Utils::CreateTypedTmpDir("update");
    if (not tmp_dir) {
        Logger::Log(LogLevel::Error, "Failed to create commit update tmp dir");
        return kExitUpdateError;
    }
    // Init and open git repo
    auto git_repo = GitRepo::InitAndOpen(tmp_dir->GetPath(), /*is_bare=*/true);
    if (not git_repo) {
        Logger::Log(LogLevel::Error,
                    "Failed to initialize repository in tmp dir {} for git "
                    "commit update",
                    tmp_dir->GetPath().string());
        return kExitUpdateError;
    }
    // Initialize resulting config to be updated
    auto mr_config = config->ToJson();
    // Create and call git commit update map
    auto git_update_map =
        CreateGitUpdateMap(git_repo->GetGitCAS(), arguments.common.jobs);
    bool failed{false};
    {
        TaskSystem ts{arguments.common.jobs};
        git_update_map.ConsumeAfterKeysReady(
            &ts,
            repos_to_update,
            [&mr_config,
             repos_to_update_names =
                 arguments.update.repos_to_update](auto const& values) {
                for (auto const& repo_name : repos_to_update_names) {
                    auto i = static_cast<size_t>(
                        &repo_name - &repos_to_update_names[0]);  // get index
                    mr_config["repositories"][repo_name]["repository"]
                             ["commit"] = *values[i];
                }
            },
            [&failed](auto const& msg, bool fatal) {
                Logger::Log(fatal ? LogLevel::Error : LogLevel::Warning,
                            "While performing just-mr update:\n{}",
                            msg);
                failed = failed or fatal;
            });
    }
    if (failed) {
        return kExitUpdateError;
    }
    // print mr_config to stdout
    std::cout << mr_config.dump(2) << std::endl;
    return kExitSuccess;
}

[[nodiscard]] auto MultiRepoSetup(std::shared_ptr<Configuration> const& config,
                                  CommandLineArguments const& arguments,
                                  bool interactive)
    -> std::optional<std::filesystem::path> {
    // set anchor dir to setup_root; current dir will be reverted when anchor
    // goes out of scope
    auto cwd_anchor = FileSystemManager::ChangeDirectory(
        arguments.common.just_mr_paths->setup_root);

    auto repos = (*config)["repositories"];
    auto setup_repos =
        std::make_shared<SetupRepos>();  // repos to setup and include
    nlohmann::json mr_config{};          // output config to populate

    auto main =
        arguments.common.main;  // get local copy of updated clarg 'main', as
                                // it might be updated again from config

    // check if config provides main repo name
    if (not main) {
        auto main_from_config = (*config)["main"];
        if (main_from_config.IsNotNull()) {
            if (main_from_config->IsString()) {
                main = main_from_config->String();
            }
            else {
                Logger::Log(
                    LogLevel::Error,
                    "Unsupported value for field 'main' in configuration.");
            }
        }
    }
    // pass on main that was explicitly set via command line or config
    if (main) {
        mr_config["main"] = *main;
    }
    // get default repos to setup and to include
    DefaultReachableRepositories(repos, setup_repos);
    // check if main is to be taken as first repo name lexicographically
    if (not main and not setup_repos->to_setup.empty()) {
        main = *std::min_element(setup_repos->to_setup.begin(),
                                 setup_repos->to_setup.end());
    }
    // final check on which repos are to be set up
    if (main and not arguments.setup.sub_all) {
        ReachableRepositories(repos, *main, setup_repos);
    }

    // setup the required async maps
    auto crit_git_op_ptr = std::make_shared<CriticalGitOpGuard>();
    auto critical_git_op_map = CreateCriticalGitOpMap(crit_git_op_ptr);
    auto content_cas_map = CreateContentCASMap(arguments.common.just_mr_paths,
                                               arguments.common.jobs);
    auto import_to_git_map =
        CreateImportToGitMap(&critical_git_op_map, arguments.common.jobs);

    auto commit_git_map = CreateCommitGitMap(&critical_git_op_map,
                                             arguments.common.just_mr_paths,
                                             arguments.common.jobs);
    auto content_git_map = CreateContentGitMap(&content_cas_map,
                                               &import_to_git_map,
                                               &critical_git_op_map,
                                               arguments.common.jobs);
    auto fpath_git_map = CreateFilePathGitMap(arguments.just_cmd.subcmd_name,
                                              &critical_git_op_map,
                                              &import_to_git_map,
                                              arguments.common.jobs);
    auto distdir_git_map = CreateDistdirGitMap(&content_cas_map,
                                               &import_to_git_map,
                                               &critical_git_op_map,
                                               arguments.common.jobs);
    auto repos_to_setup_map = CreateReposToSetupMap(config,
                                                    main,
                                                    interactive,
                                                    &commit_git_map,
                                                    &content_git_map,
                                                    &fpath_git_map,
                                                    &content_cas_map,
                                                    &distdir_git_map,
                                                    arguments.common.jobs);

    // Populate workspace_root and TAKE_OVER fields
    bool failed{false};
    {
        TaskSystem ts{arguments.common.jobs};
        repos_to_setup_map.ConsumeAfterKeysReady(
            &ts,
            setup_repos->to_setup,
            [&mr_config, config, setup_repos, main, interactive](
                auto const& values) {
                nlohmann::json mr_repos{};
                for (auto const& repo : setup_repos->to_setup) {
                    auto i = static_cast<size_t>(
                        &repo - &setup_repos->to_setup[0]);  // get index
                    mr_repos[repo] = *values[i];
                }
                // populate ALT_DIRS
                auto repos = (*config)["repositories"];
                if (repos.IsNotNull()) {
                    for (auto const& repo : setup_repos->to_include) {
                        auto desc = repos->Get(repo, Expression::none_t{});
                        if (desc.IsNotNull()) {
                            if (not((main and (repo == *main)) and
                                    interactive)) {
                                for (auto const& key : kAltDirs) {
                                    auto val =
                                        desc->Get(key, Expression::none_t{});
                                    if (val.IsNotNull() and
                                        not((main and val->IsString() and
                                             (val->String() == *main)) and
                                            interactive)) {
                                        mr_repos[repo][key] =
                                            mr_repos[val->String()]
                                                    ["workspace_root"];
                                    }
                                }
                            }
                        }
                    }
                }
                // retain only the repos we need
                for (auto const& repo : setup_repos->to_include) {
                    mr_config["repositories"][repo] = mr_repos[repo];
                }
            },
            [&failed, interactive](auto const& msg, bool fatal) {
                Logger::Log(fatal ? LogLevel::Error : LogLevel::Warning,
                            "While performing just-mr {}:\n{}",
                            interactive ? "setup-env" : "setup",
                            msg);
                failed = failed or fatal;
            });
    }
    if (failed) {
        return std::nullopt;
    }
    // if successful, return the output config
    return JustMR::Utils::AddToCAS(mr_config.dump(2));
}

/// \brief Runs execvp for given command. Only returns if execvp fails.
[[nodiscard]] auto CallJust(std::shared_ptr<Configuration> const& config,
                            CommandLineArguments const& arguments) -> int {
    // check if subcmd_name can be taken from additional args
    auto additional_args_offset = 0U;
    auto subcommand = arguments.just_cmd.subcmd_name;
    if (not subcommand and
        not arguments.just_cmd.additional_just_args.empty()) {
        subcommand = arguments.just_cmd.additional_just_args[0];
        additional_args_offset++;
    }

    bool use_config{false};
    bool use_build_root{false};
    std::optional<std::filesystem::path> mr_config_path{std::nullopt};

    if (subcommand and kKnownJustSubcommands.contains(*subcommand)) {
        if (kKnownJustSubcommands.at(*subcommand).config) {
            use_config = true;
            mr_config_path =
                MultiRepoSetup(config, arguments, /*interactive=*/false);
            if (not mr_config_path) {
                Logger::Log(LogLevel::Error,
                            "Failed to setup config while calling \'just {}\'",
                            *subcommand);
                return kExitSetupError;
            }
        }
        use_build_root = kKnownJustSubcommands.at(*subcommand).build_root;
    }
    // build just command
    std::vector<std::string> cmd = {arguments.common.just_path->string()};
    if (subcommand) {
        cmd.emplace_back(*subcommand);
    }
    if (use_config) {
        cmd.emplace_back("-C");
        cmd.emplace_back(mr_config_path->string());
    }
    if (use_build_root) {
        cmd.emplace_back("--local-build-root");
        cmd.emplace_back(*arguments.common.just_mr_paths->root);
    }
    // add args read from just-mrrc
    if (subcommand and arguments.just_cmd.just_args.contains(*subcommand)) {
        for (auto const& subcmd_arg :
             arguments.just_cmd.just_args.at(*subcommand)) {
            cmd.emplace_back(subcmd_arg);
        }
    }
    // add (remaining) args given by user as clargs
    for (auto it = arguments.just_cmd.additional_just_args.begin() +
                   additional_args_offset;
         it != arguments.just_cmd.additional_just_args.end();
         ++it) {
        cmd.emplace_back(*it);
    }

    Logger::Log(LogLevel::Info,
                "Setup finished, exec \'{}\'",
                nlohmann::json(cmd).dump());

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

        // Run subcommands known to just and `do`
        if (arguments.cmd == SubCommand::kJustDo or
            arguments.cmd == SubCommand::kJustSubCmd) {
            return CallJust(config, arguments);
        }

        // Run subcommand `setup` or `setup-env`
        if (arguments.cmd == SubCommand::kSetup or
            arguments.cmd == SubCommand::kSetupEnv) {
            auto mr_config_path = MultiRepoSetup(
                config,
                arguments,
                /*interactive=*/(arguments.cmd == SubCommand::kSetupEnv));
            // dump resulting config to stdout
            if (not mr_config_path) {
                return kExitSetupError;
            }
            std::cout << mr_config_path->string() << std::endl;
            return kExitSuccess;
        }

        // Run subcommand `update`
        if (arguments.cmd == SubCommand::kUpdate) {
            return MultiRepoUpdate(config, arguments);
        }

        // Run subcommand `fetch`
        if (arguments.cmd == SubCommand::kFetch) {
            return MultiRepoFetch(config, arguments);
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
