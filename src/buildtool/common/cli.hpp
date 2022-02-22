#ifndef INCLUDED_SRC_BUILDTOOL_COMMON_CLI_HPP
#define INCLUDED_SRC_BUILDTOOL_COMMON_CLI_HPP

#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "CLI/CLI.hpp"
#include "fmt/core.h"
#include "nlohmann/json.hpp"
#include "gsl-lite/gsl-lite.hpp"
#include "src/buildtool/logging/log_level.hpp"

constexpr auto kDefaultLogLevel = LogLevel::Info;

/// \brief Arguments common to all commands.
struct CommonArguments {
    std::optional<std::filesystem::path> workspace_root{};
    std::optional<std::filesystem::path> repository_config{};
    std::optional<std::string> main{};
    std::optional<std::filesystem::path> log_file{};
    std::size_t jobs{std::max(1U, std::thread::hardware_concurrency())};
    LogLevel log_limit{kDefaultLogLevel};
};

/// \brief Arguments required for analysing targets.
struct AnalysisArguments {
    std::filesystem::path config_file{};
    std::optional<nlohmann::json> target{};
    std::optional<std::string> target_file_name{};
    std::optional<std::string> rule_file_name{};
    std::optional<std::string> expression_file_name{};
    std::optional<std::filesystem::path> target_root{};
    std::optional<std::filesystem::path> rule_root{};
    std::optional<std::filesystem::path> expression_root{};
    std::optional<std::filesystem::path> graph_file{};
    std::optional<std::filesystem::path> artifacts_to_build_file{};
};

/// \brief Arguments required for running diagnostics.
struct DiagnosticArguments {
    std::optional<std::string> dump_actions{std::nullopt};
    std::optional<std::string> dump_blobs{std::nullopt};
    std::optional<std::string> dump_trees{std::nullopt};
    std::optional<std::string> dump_targets{std::nullopt};
    std::optional<std::string> dump_anonymous{std::nullopt};
    std::optional<std::string> dump_nodes{std::nullopt};
};

/// \brief Arguments required for specifying cache/build endpoint.
struct EndpointArguments {
    std::optional<std::filesystem::path> local_root{};
    std::optional<std::string> remote_execution_address;
};

/// \brief Arguments required for building.
struct BuildArguments {
    std::optional<std::vector<std::string>> local_launcher{std::nullopt};
    std::map<std::string, std::string> platform_properties;
    std::size_t build_jobs{};
    std::optional<std::string> dump_artifacts{std::nullopt};
    std::optional<std::string> print_to_stdout{std::nullopt};
    bool persistent_build_dir{false};
    bool show_runfiles{false};
};

/// \brief Arguments required for staging.
struct StageArguments {
    std::filesystem::path output_dir{};
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
};

/// \brief Arguments required for running from graph file.
struct GraphArguments {
    nlohmann::json artifacts{};
    std::filesystem::path graph_file{};
    std::optional<std::filesystem::path> git_cas{};
};

static inline auto SetupCommonArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<CommonArguments*> const& clargs) {
    app->add_option("-C,--repository_config",
                    clargs->repository_config,
                    "Path to configuration file for multi-repository builds.")
        ->type_name("PATH");
    app->add_option(
           "--main", clargs->main, "The repository to take the target from.")
        ->type_name("NAME");
    app->add_option_function<std::string>(
           "-w,--workspace_root",
           [clargs](auto const& workspace_root_raw) {
               clargs->workspace_root = std::filesystem::canonical(
                   std::filesystem::absolute(workspace_root_raw));
           },
           "Path of the workspace's root directory.")
        ->type_name("PATH");
    app->add_option("-j,--jobs",
                    clargs->jobs,
                    "Number of jobs to run (Default: Number of cores).")
        ->type_name("NUM");
    app->add_option(
           "-f,--log-file", clargs->log_file, "Path to local log file.")
        ->type_name("PATH");
    app->add_option_function<std::underlying_type_t<LogLevel>>(
           "--log-limit",
           [clargs](auto const& limit) {
               clargs->log_limit = ToLogLevel(limit);
           },
           fmt::format("Log limit in interval [{},{}] (Default: {}).",
                       kFirstLogLevel,
                       kLastLogLevel,
                       kDefaultLogLevel))
        ->type_name("NUM");
}

static inline auto SetupAnalysisArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<AnalysisArguments*> const& clargs,
    bool with_graph = true) {
    app->add_option(
           "-c,--config", clargs->config_file, "Path to configuration file.")
        ->type_name("PATH");
    app->add_option_function<std::vector<std::string>>(
           "target",
           [clargs](auto const& target_raw) {
               if (target_raw.size() > 1) {
                   clargs->target =
                       nlohmann::json{target_raw[0], target_raw[1]};
               }
               else {
                   clargs->target = nlohmann::json{target_raw[0]}[0];
               }
           },
           "Module and target name to build.\n"
           "Assumes current module if module name is omitted.")
        ->expected(2);
    app->add_option("--target_root",
                    clargs->target_root,
                    "Path of the target files' root directory.\n"
                    "Default: Same as --workspace_root")
        ->type_name("PATH");
    app->add_option("--rule_root",
                    clargs->rule_root,
                    "Path of the rule files' root directory.\n"
                    "Default: Same as --target_root")
        ->type_name("PATH");
    app->add_option("--expression_root",
                    clargs->expression_root,
                    "Path of the expression files' root directory.\n"
                    "Default: Same as --rule_root")
        ->type_name("PATH");
    app->add_option("--target_file_name",
                    clargs->target_file_name,
                    "Name of the targets file.");
    app->add_option(
        "--rule_file_name", clargs->rule_file_name, "Name of the rules file.");
    app->add_option("--expression_file_name",
                    clargs->expression_file_name,
                    "Name of the expressions file.");
    if (with_graph) {
        app->add_option(
               "--dump_graph",
               clargs->graph_file,
               "File path for writing the action graph description to.")
            ->type_name("PATH");
        app->add_option("--dump_artifacts_to_build",
                        clargs->artifacts_to_build_file,
                        "File path for writing the artifacts to build to.")
            ->type_name("PATH");
    }
}

static inline auto SetupDiagnosticArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<DiagnosticArguments*> const& clargs) {
    app->add_option("--dump_actions",
                    clargs->dump_actions,
                    "Dump actions to file (use - for stdout).")
        ->type_name("PATH");
    app->add_option("--dump_trees",
                    clargs->dump_trees,
                    "Dump trees to file (use - for stdout).")
        ->type_name("PATH");
    app->add_option("--dump_blobs",
                    clargs->dump_blobs,
                    "Dump blobs to file (use - for stdout).")
        ->type_name("PATH");
    app->add_option("--dump_targets",
                    clargs->dump_targets,
                    "Dump targets to file (use - for stdout).")
        ->type_name("PATH");
    app->add_option("--dump_anonymous",
                    clargs->dump_anonymous,
                    "Dump anonymous targets to file (use - for stdout).")
        ->type_name("PATH");
    app->add_option("--dump_nodes",
                    clargs->dump_nodes,
                    "Dump nodes of target to file (use - for stdout).")
        ->type_name("PATH");
}

static inline auto SetupEndpointArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<EndpointArguments*> const& clargs) {
    app->add_option_function<std::string>(
           "--local_build_root",
           [clargs](auto const& build_root_raw) {
               clargs->local_root = std::filesystem::weakly_canonical(
                   std::filesystem::absolute(build_root_raw));
           },
           "Root for local CAS, cache, and build directories.")
        ->type_name("PATH");

    app->add_option("-r,--remote_execution_address",
                    clargs->remote_execution_address,
                    "Address of the remote execution service.")
        ->type_name("NAME:PORT");
}

static inline auto SetupBuildArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<BuildArguments*> const& clargs) {
    app->add_flag("-p,--persistent",
                  clargs->persistent_build_dir,
                  "Do not clean build directory after execution.");

    app->add_option_function<std::string>(
           "-L,--local_launcher",
           [clargs](auto const& launcher_raw) {
               clargs->local_launcher =
                   nlohmann::json::parse(launcher_raw)
                       .template get<std::vector<std::string>>();
           },
           "JSON array with the list of strings representing the launcher to "
           "prepend actions' commands before being executed locally.")
        ->type_name("JSON")
        ->default_val(nlohmann::json{"env", "--"}.dump());

    app->add_option_function<std::string>(
           "--remote_execution_property",
           [clargs](auto const& property) {
               std::istringstream pss(property);
               std::string key;
               std::string val;
               if (not std::getline(pss, key, ':') or
                   not std::getline(pss, val, ':')) {
                   throw CLI::ConversionError{property,
                                              "--remote_execution_property"};
               }
               clargs->platform_properties.emplace(std::move(key),
                                                   std::move(val));
           },
           "Property for remote execution as key-value pair.")
        ->type_name("KEY:VAL")
        ->allow_extra_args(false)
        ->expected(1, 1);

    app->add_option(
           "-J,--build_jobs",
           clargs->build_jobs,
           "Number of jobs to run during build phase (Default: same as jobs).")
        ->type_name("NUM");
    app->add_option("--dump_artifacts",
                    clargs->dump_artifacts,
                    "Dump artifacts to file (use - for stdout).")
        ->type_name("PATH");

    app->add_flag("-s,--show_runfiles",
                  clargs->show_runfiles,
                  "Do not omit runfiles in build report.");

    app->add_option("-P,--print_to_stdout",
                    clargs->print_to_stdout,
                    "After building, print the specified artifact to stdout.")
        ->type_name("LOGICAL_PATH");
}

static inline auto SetupStageArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<StageArguments*> const& clargs) {
    app->add_option_function<std::string>(
           "-o,--output_dir",
           [clargs](auto const& output_dir_raw) {
               clargs->output_dir = std::filesystem::weakly_canonical(
                   std::filesystem::absolute(output_dir_raw));
           },
           "Path of the directory where outputs will be copied.")
        ->type_name("PATH")
        ->required();
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
           "--dump_flaky", clargs->dump_flaky, "Dump flaky actions to file.")
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
           "-o,--output_path",
           [clargs](auto const& output_path_raw) {
               clargs->output_path = std::filesystem::weakly_canonical(
                   std::filesystem::absolute(output_path_raw));
           },
           "Install path for the artifact. (omit to dump to stdout)")
        ->type_name("PATH");
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

    app->add_option("-g,--graph_file",
                    clargs->graph_file,
                    "Path of the file containing the description of the "
                    "actions.")
        ->required();

    app->add_option("--git_cas",
                    clargs->git_cas,
                    "Path to a Git repository, containing blobs of potentially "
                    "missing KNOWN artifacts.");
}

#endif  // INCLUDED_SRC_BUILDTOOL_COMMON_CLI_HPP
