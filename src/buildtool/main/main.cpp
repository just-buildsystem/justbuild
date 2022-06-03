#include "src/buildtool/main/main.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "src/buildtool/build_engine/base_maps/directory_map.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name.hpp"
#include "src/buildtool/build_engine/base_maps/expression_map.hpp"
#include "src/buildtool/build_engine/base_maps/rule_map.hpp"
#include "src/buildtool/build_engine/base_maps/source_map.hpp"
#include "src/buildtool/build_engine/base_maps/targets_file_map.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/build_engine/target_map/target_map.hpp"
#include "src/buildtool/common/artifact_description.hpp"
#include "src/buildtool/common/cli.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/compatibility/compatibility.hpp"
#ifndef BOOTSTRAP_BUILD_TOOL
#include "src/buildtool/graph_traverser/graph_traverser.hpp"
#include "src/buildtool/progress_reporting/base_progress_reporter.hpp"
#endif
#include "src/buildtool/logging/log_config.hpp"
#include "src/buildtool/logging/log_sink_cmdline.hpp"
#include "src/buildtool/logging/log_sink_file.hpp"
#include "src/buildtool/multithreading/async_map_consumer.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/utils/cpp/concepts.hpp"
#include "src/utils/cpp/json.hpp"

namespace {

namespace Base = BuildMaps::Base;
namespace Target = BuildMaps::Target;

enum class SubCommand {
    kUnknown,
    kDescribe,
    kAnalyse,
    kBuild,
    kInstall,
    kRebuild,
    kInstallCas,
    kTraverse
};

struct CommandLineArguments {
    SubCommand cmd{SubCommand::kUnknown};
    CommonArguments common;
    AnalysisArguments analysis;
    DiagnosticArguments diagnose;
    EndpointArguments endpoint;
    BuildArguments build;
    StageArguments stage;
    RebuildArguments rebuild;
    FetchArguments fetch;
    GraphArguments graph;
};

/// \brief Setup arguments for sub command "just describe".
auto SetupDescribeCommandArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<CommandLineArguments*> const& clargs) {
    SetupCommonArguments(app, &clargs->common);
    SetupAnalysisArguments(app, &clargs->analysis, false);
}

/// \brief Setup arguments for sub command "just analyse".
auto SetupAnalyseCommandArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<CommandLineArguments*> const& clargs) {
    SetupCommonArguments(app, &clargs->common);
    SetupAnalysisArguments(app, &clargs->analysis);
    SetupDiagnosticArguments(app, &clargs->diagnose);
    SetupCompatibilityArguments(app);
}

/// \brief Setup arguments for sub command "just build".
auto SetupBuildCommandArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<CommandLineArguments*> const& clargs) {
    SetupCommonArguments(app, &clargs->common);
    SetupAnalysisArguments(app, &clargs->analysis);
    SetupEndpointArguments(app, &clargs->endpoint);
    SetupBuildArguments(app, &clargs->build);
    SetupCompatibilityArguments(app);
}

/// \brief Setup arguments for sub command "just install".
auto SetupInstallCommandArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<CommandLineArguments*> const& clargs) {
    SetupBuildCommandArguments(app, clargs);   // same as build
    SetupStageArguments(app, &clargs->stage);  // plus stage
}

/// \brief Setup arguments for sub command "just rebuild".
auto SetupRebuildCommandArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<CommandLineArguments*> const& clargs) {
    SetupBuildCommandArguments(app, clargs);       // same as build
    SetupRebuildArguments(app, &clargs->rebuild);  // plus rebuild
}

/// \brief Setup arguments for sub command "just install-cas".
auto SetupInstallCasCommandArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<CommandLineArguments*> const& clargs) {
    SetupCompatibilityArguments(app);
    SetupEndpointArguments(app, &clargs->endpoint);
    SetupFetchArguments(app, &clargs->fetch);
}

/// \brief Setup arguments for sub command "just traverse".
auto SetupTraverseCommandArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<CommandLineArguments*> const& clargs) {
    SetupCommonArguments(app, &clargs->common);
    SetupEndpointArguments(app, &clargs->endpoint);
    SetupGraphArguments(app, &clargs->graph);  // instead of analysis
    SetupBuildArguments(app, &clargs->build);
    SetupStageArguments(app, &clargs->stage);
    SetupCompatibilityArguments(app);
}

auto ParseCommandLineArguments(int argc, char const* const* argv)
    -> CommandLineArguments {
    CLI::App app("just");
    app.option_defaults()->take_last();

    auto* cmd_describe = app.add_subcommand(
        "describe", "Describe the rule generating a target.");
    auto* cmd_analyse =
        app.add_subcommand("analyse", "Analyse specified targets.");
    auto* cmd_build = app.add_subcommand("build", "Build specified targets.");
    auto* cmd_install =
        app.add_subcommand("install", "Build and stage specified targets.");
    auto* cmd_rebuild = app.add_subcommand(
        "rebuild", "Rebuild and compare artifacts to cached build.");
    auto* cmd_install_cas =
        app.add_subcommand("install-cas", "Fetch and stage artifact from CAS.");
    auto* cmd_traverse =
        app.group("")  // group for creating hidden options
            ->add_subcommand("traverse",
                             "Build and stage artifacts from graph file.");
    app.require_subcommand(1);

    CommandLineArguments clargs;
    SetupDescribeCommandArguments(cmd_describe, &clargs);
    SetupAnalyseCommandArguments(cmd_analyse, &clargs);
    SetupBuildCommandArguments(cmd_build, &clargs);
    SetupInstallCommandArguments(cmd_install, &clargs);
    SetupRebuildCommandArguments(cmd_rebuild, &clargs);
    SetupInstallCasCommandArguments(cmd_install_cas, &clargs);
    SetupTraverseCommandArguments(cmd_traverse, &clargs);

    try {
        app.parse(argc, argv);
    } catch (CLI::Error& e) {
        std::exit(app.exit(e));
    }

    if (*cmd_describe) {
        clargs.cmd = SubCommand::kDescribe;
    }
    else if (*cmd_analyse) {
        clargs.cmd = SubCommand::kAnalyse;
    }
    else if (*cmd_build) {
        clargs.cmd = SubCommand::kBuild;
    }
    else if (*cmd_install) {
        clargs.cmd = SubCommand::kInstall;
    }
    else if (*cmd_rebuild) {
        clargs.cmd = SubCommand::kRebuild;
    }
    else if (*cmd_install_cas) {
        clargs.cmd = SubCommand::kInstallCas;
    }
    else if (*cmd_traverse) {
        clargs.cmd = SubCommand::kTraverse;
    }

    return clargs;
}

void SetupLogging(CommonArguments const& clargs) {
    LogConfig::SetLogLimit(clargs.log_limit);
    LogConfig::SetSinks({LogSinkCmdLine::CreateFactory()});
    if (clargs.log_file) {
        LogConfig::AddSink(LogSinkFile::CreateFactory(
            *clargs.log_file, LogSinkFile::Mode::Overwrite));
    }
}

#ifndef BOOTSTRAP_BUILD_TOOL
void SetupLocalExecution(EndpointArguments const& eargs,
                         BuildArguments const& bargs) {
    using LocalConfig = LocalExecutionConfig;
    if (not LocalConfig::SetKeepBuildDir(bargs.persistent_build_dir) or
        not(not eargs.local_root or
            (LocalConfig::SetBuildRoot(*eargs.local_root))) or
        not(not bargs.local_launcher or
            LocalConfig::SetLauncher(*bargs.local_launcher))) {
        Logger::Log(LogLevel::Error, "failed to configure local execution.");
    }
}

void SetupHashGenerator() {
    if (Compatibility::IsCompatible()) {
        HashGenerator::SetHashGenerator(HashGenerator::HashType::SHA256);
    }
    else {
        HashGenerator::SetHashGenerator(HashGenerator::HashType::GIT);
    }
}

#endif

// returns path relative to `root`.
[[nodiscard]] auto FindRoot(std::filesystem::path const& subdir,
                            FileRoot const& root,
                            std::vector<std::string> const& markers)
    -> std::optional<std::filesystem::path> {
    gsl_Expects(subdir.is_relative());
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
        if (not std::filesystem::exists(clargs.config_file)) {
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

    if (not clargs.defines.empty()) {
        try {
            auto map =
                Expression::FromJson(nlohmann::json::parse(clargs.defines));
            if (not map->IsMap()) {
                Logger::Log(LogLevel::Error,
                            "Defines {} does not contain a map.",
                            clargs.defines);
                std::exit(kExitFailure);
            }
            config = config.Update(map);
        } catch (std::exception const& e) {
            Logger::Log(LogLevel::Error,
                        "Parsing defines {} failed with error:\n{}",
                        clargs.defines,
                        e.what());
            std::exit(kExitFailure);
        }
    }

    return config;
}

[[nodiscard]] auto DetermineCurrentModule(
    std::filesystem::path const& workspace_root,
    FileRoot const& target_root,
    std::optional<std::string> const& target_file_name_opt) -> std::string {
    auto cwd = std::filesystem::current_path();
    auto subdir = std::filesystem::proximate(cwd, workspace_root);
    if (subdir.is_relative() and (*subdir.begin() != "..")) {
        // cwd is subdir of workspace_root
        std::string target_file_name =
            target_file_name_opt ? *target_file_name_opt : "TARGETS";
        if (auto root_dir = FindRoot(subdir, target_root, {target_file_name})) {
            return root_dir->string();
        }
    }
    return ".";
}

[[nodiscard]] auto ReadConfiguredTarget(
    AnalysisArguments const& clargs,
    std::string const& main_repo,
    std::optional<std::filesystem::path> const& main_ws_root)
    -> Target::ConfiguredTarget {
    auto const* target_root =
        RepositoryConfig::Instance().TargetRoot(main_repo);
    if (target_root == nullptr) {
        Logger::Log(LogLevel::Error,
                    "Cannot obtain target root for main repo {}.",
                    main_repo);
        std::exit(kExitFailure);
    }
    auto current_module = std::string{"."};
    if (main_ws_root) {
        // module detection only works if main workspace is on the file system
        current_module = DetermineCurrentModule(
            *main_ws_root, *target_root, clargs.target_file_name);
    }
    auto config = ReadConfiguration(clargs);
    if (clargs.target) {
        auto entity = Base::ParseEntityNameFromJson(
            *clargs.target,
            Base::EntityName{Base::NamedTarget{main_repo, current_module, ""}},
            [&clargs](std::string const& parse_err) {
                Logger::Log(LogLevel::Error,
                            "Parsing target name {} failed with:\n{}.",
                            clargs.target->dump(),
                            parse_err);
            });
        if (not entity) {
            std::exit(kExitFailure);
        }
        return Target::ConfiguredTarget{std::move(*entity), std::move(config)};
    }
    std::string target_file_name =
        clargs.target_file_name ? *clargs.target_file_name : "TARGETS";
    auto const target_file =
        (std::filesystem::path{current_module} / target_file_name).string();
    auto file_content = target_root->ReadFile(target_file);
    if (not file_content) {
        Logger::Log(LogLevel::Error, "Cannot read file {}.", target_file);
        std::exit(kExitFailure);
    }
    auto const json = nlohmann::json::parse(*file_content);
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
        Base::EntityName{
            Base::NamedTarget{main_repo, current_module, json.begin().key()}},
        std::move(config)};
}

template <HasToString K, typename V>
[[nodiscard]] auto DetectAndReportCycle(std::string const& name,
                                        AsyncMapConsumer<K, V> const& map)
    -> bool {
    using namespace std::string_literals;
    auto cycle = map.DetectCycle();
    if (cycle) {
        bool found{false};
        std::ostringstream oss{};
        oss << fmt::format("Cycle detected in {}:", name) << std::endl;
        for (auto const& k : *cycle) {
            auto match = (k == cycle->back());
            auto prefix{match   ? found ? "`-- "s : ".-> "s
                        : found ? "|   "s
                                : "    "s};
            oss << prefix << k.ToString() << std::endl;
            found = found or match;
        }
        Logger::Log(LogLevel::Error, "{}", oss.str());
        return true;
    }
    return false;
}

template <HasToString K, typename V>
void DetectAndReportPending(std::string const& name,
                            AsyncMapConsumer<K, V> const& map) {
    using namespace std::string_literals;
    auto keys = map.GetPendingKeys();
    if (not keys.empty()) {
        std::ostringstream oss{};
        oss << fmt::format("Internal error, failed to evaluate pending {}:",
                           name)
            << std::endl;
        for (auto const& k : keys) {
            oss << "  " << k.ToString() << std::endl;
        }
        Logger::Log(LogLevel::Error, "{}", oss.str());
    }
}

[[nodiscard]] auto DetermineWorkspaceRootByLookingForMarkers()
    -> std::filesystem::path {
    auto cwd = std::filesystem::current_path();
    auto root = cwd.root_path();
    cwd = std::filesystem::relative(cwd, root);
    auto root_dir =
        FindRoot(cwd, FileRoot{root}, {"ROOT", "WORKSPACE", ".git"});
    if (not root_dir) {
        Logger::Log(LogLevel::Error, "Could not determine workspace root.");
        std::exit(kExitFailure);
    }
    return root / *root_dir;
}

// returns FileRoot and optional local path, if the root is local
auto ParseRoot(nlohmann::json desc,
               const std::string& repo,
               const std::string& keyword)
    -> std::pair<FileRoot, std::optional<std::filesystem::path>> {
    nlohmann::json root = desc[keyword];
    if ((not root.is_array()) or root.empty()) {
        Logger::Log(LogLevel::Error,
                    "Expected {} for {} to be of the form [<scheme>, ...], but "
                    "found {}",
                    keyword,
                    repo,
                    root.dump());
        std::exit(kExitFailure);
    }
    if (root[0] == "file") {
        if (root.size() != 2 or (not root[1].is_string())) {
            Logger::Log(LogLevel::Error,
                        "\"file\" scheme expects precisely one string "
                        "argument, but found {} for {} of repository {}",
                        root.dump(),
                        keyword,
                        repo);
            std::exit(kExitFailure);
        }
        auto path = std::filesystem::path{root[1]};
        return {FileRoot{path}, std::move(path)};
    }
    if (root[0] == "git tree") {
        if (root.size() != 3 or (not root[1].is_string()) or
            (not root[2].is_string())) {
            Logger::Log(LogLevel::Error,
                        "\"git tree\" scheme expects two string arguments, "
                        "but found {} for {} of repository {}",
                        root.dump(),
                        keyword,
                        repo);
            std::exit(kExitFailure);
        }
        if (auto git_root = FileRoot::FromGit(root[2], root[1])) {
            return {std::move(*git_root), std::nullopt};
        }
        Logger::Log(LogLevel::Error,
                    "Could not create file root for git repository {} and tree "
                    "id {}",
                    root[2],
                    root[1]);
        std::exit(kExitFailure);
    }
    Logger::Log(LogLevel::Error,
                "Unknown scheme in the specification {} of {} of repository {}",
                root.dump(),
                keyword,
                repo);
    std::exit(kExitFailure);
}

// Set all roots and name mappings from the command-line arguments and
// return the name of the main repository and main workspace path if local.
auto DetermineRoots(CommonArguments cargs, AnalysisArguments aargs)
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

    auto main_it = repo_config.find("main");
    if (main_it != repo_config.end()) {
        if (not main_it->is_string()) {
            Logger::Log(LogLevel::Error,
                        "Repository config: main has to be a string");
            std::exit(kExitFailure);
        }
        main_repo = *main_it;
    }
    if (cargs.main) {
        main_repo = *cargs.main;
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

    for (auto const& [repo, desc] : repos.items()) {
        std::optional<FileRoot> ws_root{};
        bool const is_main_repo{repo == main_repo};
        if (desc.contains("workspace_root")) {
            auto [root, path] = ParseRoot(desc, repo, "workspace_root");
            ws_root = std::move(root);
            if (is_main_repo) {
                main_ws_root = std::move(path);
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
            ws_root = FileRoot{*main_ws_root};
        }
        if (not ws_root) {
            Logger::Log(
                LogLevel::Error, "Unknown root for repository {}", repo);
            std::exit(kExitFailure);
        }
        auto info = RepositoryConfig::RepositoryInfo{std::move(*ws_root)};
        if (desc.contains("target_root")) {
            info.target_root = ParseRoot(desc, repo, "target_root").first;
        }
        if (is_main_repo && aargs.target_root) {
            info.target_root = FileRoot{*aargs.target_root};
        }
        info.rule_root = info.target_root;
        if (desc.contains("rule_root")) {
            info.rule_root = ParseRoot(desc, repo, "rule_root").first;
        }
        if (is_main_repo && aargs.rule_root) {
            info.rule_root = FileRoot{*aargs.rule_root};
        }
        info.expression_root = info.rule_root;
        if (desc.contains("expression_root")) {
            info.expression_root =
                ParseRoot(desc, repo, "expression_root").first;
        }
        if (is_main_repo && aargs.expression_root) {
            info.expression_root = FileRoot{*aargs.expression_root};
        }

        if (desc.contains("bindings")) {
            if (not desc["bindings"].is_object()) {
                Logger::Log(
                    LogLevel::Error,
                    "bindings has to be a string-string map, but found {}",
                    desc["bindings"].dump());
                std::exit(kExitFailure);
            }
            for (auto const& [local_name, global_name] :
                 desc["bindings"].items()) {
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

        if (desc.contains("target_file_name")) {
            info.target_file_name = desc["target_file_name"];
        }
        if (is_main_repo && aargs.target_file_name) {
            info.target_file_name = *aargs.target_file_name;
        }
        if (desc.contains("rule_file_name")) {
            info.rule_file_name = desc["rule_file_name"];
        }
        if (is_main_repo && aargs.rule_file_name) {
            info.rule_file_name = *aargs.rule_file_name;
        }
        if (desc.contains("expression_file_name")) {
            info.expression_file_name = desc["expression_file_name"];
        }
        if (is_main_repo && aargs.expression_file_name) {
            info.expression_file_name = *aargs.expression_file_name;
        }

        RepositoryConfig::Instance().SetInfo(repo, std::move(info));
    }

    return {main_repo, main_ws_root};
}

struct AnalysisResult {
    Target::ConfiguredTarget id;
    AnalysedTargetPtr target;
};

[[nodiscard]] auto AnalyseTarget(
    gsl::not_null<Target::ResultTargetMap*> const& result_map,
    std::string const& main_repo,
    std::optional<std::filesystem::path> const& main_ws_root,
    std::size_t jobs,
    AnalysisArguments const& clargs) -> std::optional<AnalysisResult> {
    auto directory_entries = Base::CreateDirectoryEntriesMap(jobs);
    auto expressions_file_map = Base::CreateExpressionFileMap(jobs);
    auto rule_file_map = Base::CreateRuleFileMap(jobs);
    auto targets_file_map = Base::CreateTargetsFileMap(jobs);
    auto expr_map = Base::CreateExpressionMap(&expressions_file_map, jobs);
    auto rule_map = Base::CreateRuleMap(&rule_file_map, &expr_map, jobs);
    auto source_targets = Base::CreateSourceTargetMap(&directory_entries, jobs);
    auto target_map = Target::CreateTargetMap(&source_targets,
                                              &targets_file_map,
                                              &rule_map,
                                              &directory_entries,
                                              result_map,
                                              jobs);

    auto id = ReadConfiguredTarget(clargs, main_repo, main_ws_root);
    Logger::Log(LogLevel::Info, "Requested target is {}", id.ToString());
    std::shared_ptr<AnalysedTarget> target{};

    bool failed{false};
    {
        TaskSystem ts{jobs};
        target_map.ConsumeAfterKeysReady(
            &ts,
            {id},
            [&target](auto values) { target = *values[0]; },
            [&failed](auto const& msg, bool fatal) {
                Logger::Log(fatal ? LogLevel::Error : LogLevel::Warning,
                            "While processing targets:\n{}",
                            msg);
                failed = failed or fatal;
            });
    }

    if (failed) {
        return std::nullopt;
    }

    if (not target) {
        Logger::Log(
            LogLevel::Error, "Failed to analyse target: {}", id.ToString());
        if (not(DetectAndReportCycle("expression imports", expr_map) or
                DetectAndReportCycle("target dependencies", target_map))) {
            DetectAndReportPending("expressions", expr_map);
            DetectAndReportPending("targets", expr_map);
        }
        return std::nullopt;
    }

    // Clean up in parallel what is no longer needed
    {
        TaskSystem ts{jobs};
        target_map.Clear(&ts);
        source_targets.Clear(&ts);
        directory_entries.Clear(&ts);
        expressions_file_map.Clear(&ts);
        rule_file_map.Clear(&ts);
        targets_file_map.Clear(&ts);
        expr_map.Clear(&ts);
        rule_map.Clear(&ts);
    }

    return AnalysisResult{id, target};
}

[[nodiscard]] auto ResultToJson(TargetResult const& result) -> nlohmann::json {
    return nlohmann::ordered_json{
        {"artifacts",
         result.artifact_stage->ToJson(
             Expression::JsonMode::SerializeAllButNodes)},
        {"runfiles",
         result.runfiles->ToJson(Expression::JsonMode::SerializeAllButNodes)},
        {"provides",
         result.provides->ToJson(Expression::JsonMode::SerializeAllButNodes)}};
}

[[nodiscard]] auto TargetActionsToJson(AnalysedTargetPtr const& target)
    -> nlohmann::json {
    auto actions = nlohmann::json::array();
    std::for_each(target->Actions().begin(),
                  target->Actions().end(),
                  [&actions](auto const& action) {
                      actions.push_back(action->ToJson());
                  });
    return actions;
}

[[nodiscard]] auto TreesToJson(AnalysedTargetPtr const& target)
    -> nlohmann::json {
    auto trees = nlohmann::json::object();
    std::for_each(
        target->Trees().begin(),
        target->Trees().end(),
        [&trees](auto const& tree) { trees[tree->Id()] = tree->ToJson(); });

    return trees;
}

void DumpActions(std::string const& file_path, AnalysisResult const& result) {
    auto const dump_string =
        IndentListsOnlyUntilDepth(TargetActionsToJson(result.target), 2, 1);
    if (file_path == "-") {
        Logger::Log(
            LogLevel::Info, "Actions for target {}:", result.id.ToString());
        std::cout << dump_string << std::endl;
    }
    else {
        Logger::Log(LogLevel::Info,
                    "Dumping actions for target {} to file '{}'.",
                    result.id.ToString(),
                    file_path);
        std::ofstream os(file_path);
        os << dump_string << std::endl;
    }
}

void DumpBlobs(std::string const& file_path, AnalysisResult const& result) {
    auto blobs = nlohmann::json::array();
    for (auto const& s : result.target->Blobs()) {
        blobs.push_back(s);
    }
    auto const dump_string = blobs.dump(2);
    if (file_path == "-") {
        Logger::Log(
            LogLevel::Info, "Blobs for target {}:", result.id.ToString());
        std::cout << dump_string << std::endl;
    }
    else {
        Logger::Log(LogLevel::Info,
                    "Dumping blobs for target {} to file '{}'.",
                    result.id.ToString(),
                    file_path);
        std::ofstream os(file_path);
        os << dump_string << std::endl;
    }
}

void DumpTrees(std::string const& file_path, AnalysisResult const& result) {
    auto const dump_string = TreesToJson(result.target).dump(2);
    if (file_path == "-") {
        Logger::Log(
            LogLevel::Info, "Trees for target {}:", result.id.ToString());
        std::cout << dump_string << std::endl;
    }
    else {
        Logger::Log(LogLevel::Info,
                    "Dumping trees for target {} to file '{}'.",
                    result.id.ToString(),
                    file_path);
        std::ofstream os(file_path);
        os << dump_string << std::endl;
    }
}

void DumpTargets(std::string const& file_path,
                 std::vector<Target::ConfiguredTarget> const& target_ids) {
    auto repo_map = nlohmann::json::object();
    auto conf_list =
        [&repo_map](Base::EntityName const& ref) -> nlohmann::json& {
        if (ref.IsAnonymousTarget()) {
            auto const& anon = ref.GetAnonymousTarget();
            auto& anon_map = repo_map[Base::EntityName::kAnonymousMarker];
            auto& rule_map = anon_map[anon.rule_map.ToIdentifier()];
            return rule_map[anon.target_node.ToIdentifier()];
        }
        auto const& named = ref.GetNamedTarget();
        auto& location_map = repo_map[Base::EntityName::kLocationMarker];
        auto& module_map = location_map[named.repository];
        auto& target_map = module_map[named.module];
        return target_map[named.name];
    };
    std::for_each(
        target_ids.begin(), target_ids.end(), [&conf_list](auto const& id) {
            conf_list(id.target).push_back(id.config.ToJson());
        });
    auto const dump_string = IndentListsOnlyUntilDepth(repo_map, 2);
    if (file_path == "-") {
        Logger::Log(LogLevel::Info, "List of analysed targets:");
        std::cout << dump_string << std::endl;
    }
    else {
        Logger::Log(LogLevel::Info,
                    "Dumping list of analysed targets to file '{}'.",
                    file_path);
        std::ofstream os(file_path);
        os << dump_string << std::endl;
    }
}

auto DumpExpressionToMap(gsl::not_null<nlohmann::json*> const& map,
                         ExpressionPtr const& expr) -> bool {
    auto const& id = expr->ToIdentifier();
    if (not map->contains(id)) {
        (*map)[id] = expr->ToJson();
        return true;
    }
    return false;
}

// NOLINTNEXTLINE(misc-no-recursion)
void DumpNodesInExpressionToMap(gsl::not_null<nlohmann::json*> const& map,
                                ExpressionPtr const& expr) {
    if (expr->IsNode()) {
        if (DumpExpressionToMap(map, expr)) {
            auto const& node = expr->Node();
            if (node.IsAbstract()) {
                DumpNodesInExpressionToMap(map,
                                           node.GetAbstract().target_fields);
            }
            else if (node.IsValue()) {
                DumpNodesInExpressionToMap(map, node.GetValue());
            }
        }
    }
    else if (expr->IsList()) {
        for (auto const& entry : expr->List()) {
            DumpNodesInExpressionToMap(map, entry);
        }
    }
    else if (expr->IsMap()) {
        for (auto const& [_, value] : expr->Map()) {
            DumpNodesInExpressionToMap(map, value);
        }
    }
    else if (expr->IsResult()) {
        DumpNodesInExpressionToMap(map, expr->Result().provides);
    }
}

void DumpAnonymous(std::string const& file_path,
                   std::vector<Target::ConfiguredTarget> const& target_ids) {
    auto anon_map = nlohmann::json{{"nodes", nlohmann::json::object()},
                                   {"rule_maps", nlohmann::json::object()}};
    std::for_each(
        target_ids.begin(), target_ids.end(), [&anon_map](auto const& id) {
            if (id.target.IsAnonymousTarget()) {
                auto const& anon_t = id.target.GetAnonymousTarget();
                DumpExpressionToMap(&anon_map["rule_maps"], anon_t.rule_map);
                DumpNodesInExpressionToMap(&anon_map["nodes"],
                                           anon_t.target_node);
            }
        });
    auto const dump_string = IndentListsOnlyUntilDepth(anon_map, 2);
    if (file_path == "-") {
        Logger::Log(LogLevel::Info, "List of anonymous target data:");
        std::cout << dump_string << std::endl;
    }
    else {
        Logger::Log(LogLevel::Info,
                    "Dumping list of anonymous target data to file '{}'.",
                    file_path);
        std::ofstream os(file_path);
        os << dump_string << std::endl;
    }
}

void DumpNodes(std::string const& file_path, AnalysisResult const& result) {
    auto node_map = nlohmann::json::object();
    DumpNodesInExpressionToMap(&node_map, result.target->Provides());
    auto const dump_string = IndentListsOnlyUntilDepth(node_map, 2);
    if (file_path == "-") {
        Logger::Log(
            LogLevel::Info, "Target nodes of target {}:", result.id.ToString());
        std::cout << dump_string << std::endl;
    }
    else {
        Logger::Log(LogLevel::Info,
                    "Dumping target nodes of target {} to file '{}'.",
                    result.id.ToString(),
                    file_path);
        std::ofstream os(file_path);
        os << dump_string << std::endl;
    }
}

[[nodiscard]] auto DiagnoseResults(AnalysisResult const& result,
                                   Target::ResultTargetMap const& result_map,
                                   DiagnosticArguments const& clargs) {
    Logger::Log(
        LogLevel::Info,
        "Result of target {}: {}",
        result.id.ToString(),
        IndentOnlyUntilDepth(
            ResultToJson(result.target->Result()),
            2,
            2,
            std::unordered_map<std::string, std::size_t>{{"/provides", 3}}));
    if (clargs.dump_actions) {
        DumpActions(*clargs.dump_actions, result);
    }
    if (clargs.dump_blobs) {
        DumpBlobs(*clargs.dump_blobs, result);
    }
    if (clargs.dump_trees) {
        DumpTrees(*clargs.dump_trees, result);
    }
    if (clargs.dump_targets) {
        DumpTargets(*clargs.dump_targets, result_map.ConfiguredTargets());
    }
    if (clargs.dump_anonymous) {
        DumpAnonymous(*clargs.dump_anonymous, result_map.ConfiguredTargets());
    }
    if (clargs.dump_nodes) {
        DumpNodes(*clargs.dump_nodes, result);
    }
}

// Return disjoint maps for artifacts and runfiles
[[nodiscard]] auto ReadOutputArtifacts(AnalysedTargetPtr const& target)
    -> std::pair<std::map<std::string, ArtifactDescription>,
                 std::map<std::string, ArtifactDescription>> {
    std::map<std::string, ArtifactDescription> artifacts{};
    std::map<std::string, ArtifactDescription> runfiles{};
    for (auto const& [path, artifact] : target->Artifacts()->Map()) {
        artifacts.emplace(path, artifact->Artifact());
    }
    for (auto const& [path, artifact] : target->RunFiles()->Map()) {
        if (not artifacts.contains(path)) {
            runfiles.emplace(path, artifact->Artifact());
        }
    }
    return {artifacts, runfiles};
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

#ifndef BOOTSTRAP_BUILD_TOOL
[[nodiscard]] auto FetchAndInstallArtifacts(
    gsl::not_null<IExecutionApi*> const& api,
    FetchArguments const& clargs) -> bool {
    auto object_info = Artifact::ObjectInfo::FromString(clargs.object_id);
    if (not object_info) {
        Logger::Log(
            LogLevel::Error, "failed to parse object id {}.", clargs.object_id);
        return false;
    }

    if (clargs.output_path) {
        auto output_path = (*clargs.output_path / "").parent_path();
        if (FileSystemManager::IsDirectory(output_path)) {
            output_path /= object_info->digest.hash();
        }

        if (not FileSystemManager::CreateDirectory(output_path.parent_path()) or
            not api->RetrieveToPaths({*object_info}, {output_path})) {
            Logger::Log(LogLevel::Error, "failed to retrieve artifact.");
            return false;
        }

        Logger::Log(LogLevel::Info,
                    "artifact {} was installed to {}",
                    object_info->ToString(),
                    output_path.string());
    }
    else {  // dump to stdout
        if (not api->RetrieveToFds({*object_info}, {dup(fileno(stdout))})) {
            Logger::Log(LogLevel::Error, "failed to dump artifact.");
            return false;
        }
    }

    return true;
}
#endif

void PrintDoc(const nlohmann::json& doc, const std::string& indent) {
    if (not doc.is_array()) {
        return;
    }
    for (auto const& line : doc) {
        if (line.is_string()) {
            std::cout << indent << line.get<std::string>() << "\n";
        }
    }
}

void PrintFields(nlohmann::json const& fields,
                 const nlohmann::json& fdoc,
                 const std::string& indent_field,
                 const std::string& indent_field_doc) {
    for (auto const& f : fields) {
        std::cout << indent_field << f << "\n";
        auto doc = fdoc.find(f);
        if (doc != fdoc.end()) {
            PrintDoc(*doc, indent_field_doc);
        }
    }
}

auto DescribeTarget(std::string const& main_repo,
                    std::optional<std::filesystem::path> const& main_ws_root,
                    std::size_t jobs,
                    AnalysisArguments const& clargs) -> int {
    auto id = ReadConfiguredTarget(clargs, main_repo, main_ws_root);
    if (id.target.GetNamedTarget().reference_t == Base::ReferenceType::kFile) {
        std::cout << id.ToString() << " is a source file." << std::endl;
        return kExitSuccess;
    }
    auto targets_file_map = Base::CreateTargetsFileMap(jobs);
    nlohmann::json targets_file{};
    bool failed{false};
    {
        TaskSystem ts{jobs};
        targets_file_map.ConsumeAfterKeysReady(
            &ts,
            {id.target.ToModule()},
            [&targets_file](auto values) { targets_file = *values[0]; },
            [&failed](auto const& msg, bool fatal) {
                Logger::Log(fatal ? LogLevel::Error : LogLevel::Warning,
                            "While searching for target description:\n{}",
                            msg);
                failed = failed or fatal;
            });
    }
    if (failed) {
        return kExitFailure;
    }
    auto desc_it = targets_file.find(id.target.GetNamedTarget().name);
    if (desc_it == targets_file.end()) {
        std::cout << id.ToString() << " is implicitly a source file."
                  << std::endl;
        return kExitSuccess;
    }
    nlohmann::json desc = *desc_it;
    auto rule_it = desc.find("type");
    if (rule_it == desc.end()) {
        Logger::Log(LogLevel::Error,
                    "{} is a target without specified type.",
                    id.ToString());
        return kExitFailure;
    }
    if (BuildMaps::Target::IsBuiltInRule(*rule_it)) {
        std::cout << id.ToString() << " is defined by built-in rule "
                  << rule_it->dump() << "." << std::endl;
        if (*rule_it == "export") {
            // export targets may have doc fields of their own.
            auto doc = desc.find("doc");
            if (doc != desc.end()) {
                PrintDoc(*doc, " | ");
            }
            auto config_doc = nlohmann::json::object();
            auto config_doc_it = desc.find("config_doc");
            if (config_doc_it != desc.end() and config_doc_it->is_object()) {
                config_doc = *config_doc_it;
            }
            auto flexible_config = desc.find("flexible_config");
            if (flexible_config != desc.end() and
                (not flexible_config->empty())) {
                std::cout << " Flexible configuration variables\n";
                PrintFields(*flexible_config, config_doc, " - ", "   | ");
            }
        }
        return kExitSuccess;
    }
    auto rule_name = BuildMaps::Base::ParseEntityNameFromJson(
        *rule_it, id.target, [&rule_it, &id](std::string const& parse_err) {
            Logger::Log(LogLevel::Error,
                        "Parsing rule name {} for target {} failed with:\n{}.",
                        rule_it->dump(),
                        id.ToString(),
                        parse_err);
        });
    if (not rule_name) {
        return kExitFailure;
    }
    auto rule_file_map = Base::CreateRuleFileMap(jobs);
    nlohmann::json rules_file;
    {
        TaskSystem ts{jobs};
        rule_file_map.ConsumeAfterKeysReady(
            &ts,
            {rule_name->ToModule()},
            [&rules_file](auto values) { rules_file = *values[0]; },
            [&failed](auto const& msg, bool fatal) {
                Logger::Log(fatal ? LogLevel::Error : LogLevel::Warning,
                            "While searching for rule definition:\n{}",
                            msg);
                failed = failed or fatal;
            });
    }
    if (failed) {
        return kExitFailure;
    }
    auto ruledesc_it = rules_file.find(rule_name->GetNamedTarget().name);
    if (ruledesc_it == rules_file.end()) {
        Logger::Log(LogLevel::Error,
                    "Rule definition of {} is missing",
                    rule_name->ToString());
        return kExitFailure;
    }
    std::cout << id.ToString() << " is defined by user-defined rule "
              << rule_name->ToString() << ".\n\n";
    auto const& rdesc = *ruledesc_it;
    auto doc = rdesc.find("doc");
    if (doc != rdesc.end()) {
        PrintDoc(*doc, " | ");
    }
    auto field_doc = nlohmann::json::object();
    auto field_doc_it = rdesc.find("field_doc");
    if (field_doc_it != rdesc.end() and field_doc_it->is_object()) {
        field_doc = *field_doc_it;
    }
    auto string_fields = rdesc.find("string_fields");
    if (string_fields != rdesc.end() and (not string_fields->empty())) {
        std::cout << " String fields\n";
        PrintFields(*string_fields, field_doc, " - ", "   | ");
    }
    auto target_fields = rdesc.find("target_fields");
    if (target_fields != rdesc.end() and (not target_fields->empty())) {
        std::cout << " Target fields\n";
        PrintFields(*target_fields, field_doc, " - ", "   | ");
    }
    auto config_fields = rdesc.find("config_fields");
    if (config_fields != rdesc.end() and (not config_fields->empty())) {
        std::cout << " Config fields\n";
        PrintFields(*config_fields, field_doc, " - ", "   | ");
    }
    auto config_doc = nlohmann::json::object();
    auto config_doc_it = rdesc.find("config_doc");
    if (config_doc_it != rdesc.end() and config_doc_it->is_object()) {
        config_doc = *config_doc_it;
    }
    auto config_vars = rdesc.find("config_vars");
    if (config_vars != rdesc.end() and (not config_vars->empty())) {
        std::cout << " Variables taken from the configuration\n";
        PrintFields(*config_vars, config_doc, " - ", "   | ");
    }
    std::cout << " Result\n";
    std::cout << " - Artifacts\n";
    auto artifacts_doc = rdesc.find("artifacts_doc");
    if (artifacts_doc != rdesc.end()) {
        PrintDoc(*artifacts_doc, "   | ");
    }
    std::cout << " - Runfiles\n";
    auto runfiles_doc = rdesc.find("runfiles_doc");
    if (runfiles_doc != rdesc.end()) {
        PrintDoc(*runfiles_doc, "   | ");
    }
    auto provides_doc = rdesc.find("provides_doc");
    if (provides_doc != rdesc.end()) {
        std::cout << " - Documented providers\n";
        for (auto& el : provides_doc->items()) {
            std::cout << "   - " << el.key() << "\n";
            PrintDoc(el.value(), "     | ");
        }
    }

    std::cout << std::endl;
    return kExitSuccess;
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
    try {
        auto arguments = ParseCommandLineArguments(argc, argv);

        SetupLogging(arguments.common);
#ifndef BOOTSTRAP_BUILD_TOOL
        SetupHashGenerator();
        SetupLocalExecution(arguments.endpoint, arguments.build);
#endif

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

#ifndef BOOTSTRAP_BUILD_TOOL
        GraphTraverser const traverser{{jobs,
                                        std::move(arguments.endpoint),
                                        std::move(arguments.build),
                                        std::move(stage_args),
                                        std::move(rebuild_args)},
                                       BaseProgressReporter::Reporter()};

        if (arguments.cmd == SubCommand::kInstallCas) {
            return FetchAndInstallArtifacts(traverser.ExecutionApi(),
                                            arguments.fetch)
                       ? kExitSuccess
                       : kExitFailure;
        }
#endif

        auto [main_repo, main_ws_root] =
            DetermineRoots(arguments.common, arguments.analysis);

#ifndef BOOTSTRAP_BUILD_TOOL
        if (arguments.cmd == SubCommand::kTraverse) {
            if (arguments.graph.git_cas) {
                if (Compatibility::IsCompatible()) {
                    Logger::Log(LogLevel::Error,
                                "Command line options {} and {} cannot be used "
                                "together.",
                                "--git-cas",
                                "--compatible");
                    std::exit(EXIT_FAILURE);
                }
                if (not RepositoryConfig::Instance().SetGitCAS(
                        *arguments.graph.git_cas)) {
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
            return DescribeTarget(main_repo,
                                  main_ws_root,
                                  arguments.common.jobs,
                                  arguments.analysis);
        }

        else {
#endif
            BuildMaps::Target::ResultTargetMap result_map{
                arguments.common.jobs};
            auto result = AnalyseTarget(&result_map,
                                        main_repo,
                                        main_ws_root,
                                        arguments.common.jobs,
                                        arguments.analysis);
            if (result) {
                if (arguments.analysis.graph_file) {
                    result_map.ToFile(*arguments.analysis.graph_file);
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
                        TaskSystem ts;
                        result_map.Clear(&ts);
                    }
                    return kExitSuccess;
                }
#ifndef BOOTSTRAP_BUILD_TOOL
                Logger::Log(LogLevel::Info,
                            "Analysed target {}",
                            result->id.ToString());
                ReportTaintedness(*result);
                auto const& [actions, blobs, trees] = result_map.ToResult();

                // Clean up result map, now that it is no longer needed
                {
                    TaskSystem ts;
                    result_map.Clear(&ts);
                }

                Logger::Log(
                    LogLevel::Info,
                    "{}ing {}.",
                    arguments.cmd == SubCommand::kRebuild ? "Rebuild" : "Build",
                    result->id.ToString());

                auto build_result = traverser.BuildAndStage(
                    artifacts, runfiles, actions, blobs, trees);
                if (build_result) {
                    // Repeat taintedness message to make the user aware that
                    // the artifacts are not for production use.
                    ReportTaintedness(*result);
                    if (build_result->second) {
                        Logger::Log(LogLevel::Warning,
                                    "Build result contains failed artifacts.");
                    }
                    return build_result->second ? kExitSuccessFailedArtifacts
                                                : kExitSuccess;
                }
            }
#endif
        }
    } catch (std::exception const& ex) {
        Logger::Log(
            LogLevel::Error, "Caught exception with message: {}", ex.what());
    }
    return kExitFailure;
}
