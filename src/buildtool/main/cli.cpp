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

#include "src/buildtool/main/cli.hpp"

#include "gsl/gsl"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/main/exit_codes.hpp"

namespace {

/// \brief Setup arguments for sub command "just describe".
auto SetupDescribeCommandArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<CommandLineArguments*> const& clargs) {
    SetupCommonArguments(app, &clargs->common);
    SetupCacheArguments(app, &clargs->endpoint);
    SetupAnalysisArguments(app, &clargs->analysis, false);
    SetupLogArguments(app, &clargs->log);
    SetupServeEndpointArguments(app, &clargs->serve);
    SetupCommonAuthArguments(app, &clargs->auth);
    SetupClientAuthArguments(app, &clargs->cauth);
    SetupExecutionEndpointArguments(app, &clargs->endpoint);
    SetupCompatibilityArguments(app);
    SetupDescribeArguments(app, &clargs->describe);
    SetupRetryArguments(app, &clargs->retry);
}

/// \brief Setup arguments for sub command "just analyse".
auto SetupAnalyseCommandArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<CommandLineArguments*> const& clargs) {
    SetupCommonArguments(app, &clargs->common);
    SetupLogArguments(app, &clargs->log);
    SetupAnalysisArguments(app, &clargs->analysis);
    SetupCacheArguments(app, &clargs->endpoint);
    SetupExecutionEndpointArguments(app, &clargs->endpoint);
    SetupExecutionPropertiesArguments(app, &clargs->endpoint);
    SetupServeEndpointArguments(app, &clargs->serve);
    SetupCommonAuthArguments(app, &clargs->auth);
    SetupClientAuthArguments(app, &clargs->cauth);
    SetupDiagnosticArguments(app, &clargs->diagnose);
    SetupCompatibilityArguments(app);
    SetupRetryArguments(app, &clargs->retry);
}

/// \brief Setup arguments for sub command "just build".
auto SetupBuildCommandArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<CommandLineArguments*> const& clargs) {
    SetupCommonArguments(app, &clargs->common);
    SetupLogArguments(app, &clargs->log);
    SetupAnalysisArguments(app, &clargs->analysis);
    SetupCacheArguments(app, &clargs->endpoint);
    SetupExecutionEndpointArguments(app, &clargs->endpoint);
    SetupExecutionPropertiesArguments(app, &clargs->endpoint);
    SetupServeEndpointArguments(app, &clargs->serve);
    SetupCommonAuthArguments(app, &clargs->auth);
    SetupClientAuthArguments(app, &clargs->cauth);
    SetupCommonBuildArguments(app, &clargs->build);
    SetupBuildArguments(app, &clargs->build);
    SetupTCArguments(app, &clargs->tc);
    SetupCompatibilityArguments(app);
    SetupRetryArguments(app, &clargs->retry);
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
    SetupCacheArguments(app, &clargs->endpoint);
    SetupExecutionEndpointArguments(app, &clargs->endpoint);
    SetupCommonAuthArguments(app, &clargs->auth);
    SetupClientAuthArguments(app, &clargs->cauth);
    SetupFetchArguments(app, &clargs->fetch);
    SetupLogArguments(app, &clargs->log);
    SetupRetryArguments(app, &clargs->retry);
}

/// \brief Setup arguments for sub command "just install-cas".
auto SetupAddToCasCommandArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<CommandLineArguments*> const& clargs) {
    SetupCompatibilityArguments(app);
    SetupCacheArguments(app, &clargs->endpoint);
    SetupExecutionEndpointArguments(app, &clargs->endpoint);
    SetupCommonAuthArguments(app, &clargs->auth);
    SetupClientAuthArguments(app, &clargs->cauth);
    SetupLogArguments(app, &clargs->log);
    SetupRetryArguments(app, &clargs->retry);
    SetupToAddArguments(app, &clargs->to_add);
}

/// \brief Setup arguments for sub command "just traverse".
auto SetupTraverseCommandArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<CommandLineArguments*> const& clargs) {
    SetupCommonArguments(app, &clargs->common);
    SetupLogArguments(app, &clargs->log);
    SetupCacheArguments(app, &clargs->endpoint);
    SetupExecutionEndpointArguments(app, &clargs->endpoint);
    SetupExecutionPropertiesArguments(app, &clargs->endpoint);
    SetupCommonAuthArguments(app, &clargs->auth);
    SetupClientAuthArguments(app, &clargs->cauth);
    SetupGraphArguments(app, &clargs->graph);  // instead of analysis
    SetupCommonBuildArguments(app, &clargs->build);
    SetupBuildArguments(app, &clargs->build);
    SetupStageArguments(app, &clargs->stage);
    SetupCompatibilityArguments(app);
}

/// \brief Setup arguments for sub command "just gc".
auto SetupGcCommandArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<CommandLineArguments*> const& clargs) {
    SetupLogArguments(app, &clargs->log);
    SetupCacheArguments(app, &clargs->endpoint);
    SetupGcArguments(app, &clargs->gc);
}

/// \brief Setup arguments for sub command "just execute".
auto SetupExecutionServiceCommandArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<CommandLineArguments*> const& clargs) {
    SetupCompatibilityArguments(app);
    SetupCommonBuildArguments(app, &clargs->build);
    SetupCacheArguments(app, &clargs->endpoint);
    SetupServiceArguments(app, &clargs->service);
    SetupLogArguments(app, &clargs->log);
    SetupCommonAuthArguments(app, &clargs->auth);
    SetupServerAuthArguments(app, &clargs->sauth);
}

/// \brief Setup arguments for sub command "just serve".
auto SetupServeServiceCommandArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<CommandLineArguments*> const& clargs) {
    // all other arguments will be read from config file
    SetupServeArguments(app, &clargs->serve);
}

}  // namespace

auto ParseCommandLineArguments(int argc, char const* const* argv)
    -> CommandLineArguments {
    CLI::App app("just, a generic build tool");
    app.option_defaults()->take_last();

    auto* cmd_version = app.add_subcommand(
        "version", "Print version information in JSON format.");
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
    auto* cmd_add_to_cas = app.add_subcommand(
        "add-to-cas", "Add a local file or directory to CAS.");
    auto* cmd_gc =
        app.add_subcommand("gc", "Trigger garbage collection of local cache.");
    auto* cmd_execution = app.add_subcommand(
        "execute", "Start single node execution service on this machine.");
    auto* cmd_serve =
        app.add_subcommand("serve", "Provide target dependencies for a build.");
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
    SetupAddToCasCommandArguments(cmd_add_to_cas, &clargs);
    SetupTraverseCommandArguments(cmd_traverse, &clargs);
    SetupGcCommandArguments(cmd_gc, &clargs);
    SetupExecutionServiceCommandArguments(cmd_execution, &clargs);
    SetupServeServiceCommandArguments(cmd_serve, &clargs);
    try {
        app.parse(argc, argv);
    } catch (CLI::Error& e) {
        std::exit(app.exit(e));
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error, "Command line parse error: {}", ex.what());
        std::exit(kExitFailure);
    }

    if (*cmd_version) {
        clargs.cmd = SubCommand::kVersion;
    }
    else if (*cmd_describe) {
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
    else if (*cmd_add_to_cas) {
        clargs.cmd = SubCommand::kAddToCas;
    }
    else if (*cmd_traverse) {
        clargs.cmd = SubCommand::kTraverse;
    }
    else if (*cmd_gc) {
        clargs.cmd = SubCommand::kGc;
    }
    else if (*cmd_execution) {
        clargs.cmd = SubCommand::kExecute;
    }
    else if (*cmd_serve) {
        clargs.cmd = SubCommand::kServe;
    }

    return clargs;
}
