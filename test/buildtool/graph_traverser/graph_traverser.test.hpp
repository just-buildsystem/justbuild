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

#ifndef INCLUDED_SRC_TEST_BUILDTOOL_GRAPH_GRAVERSER_GRAPH_TRAVERSER_TEST_HPP
#define INCLUDED_SRC_TEST_BUILDTOOL_GRAPH_GRAVERSER_GRAPH_TRAVERSER_TEST_HPP

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "catch2/catch_test_macros.hpp"
#include "gsl/gsl"
#include "nlohmann/json.hpp"
#include "src/buildtool/auth/authentication.hpp"
#include "src/buildtool/common/remote/retry_config.hpp"
#include "src/buildtool/common/statistics.hpp"
#include "src/buildtool/execution_api/common/api_bundle.hpp"
#include "src/buildtool/execution_api/local/config.hpp"
#include "src/buildtool/execution_api/local/context.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/execution_api/remote/context.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/jsonfs.hpp"
#include "src/buildtool/graph_traverser/graph_traverser.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/progress_reporting/progress.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/utils/cpp/json.hpp"

// NOLINTNEXTLINE(google-build-namespaces)
namespace {

class TestProject {
  public:
    struct CommandLineArguments {
        GraphTraverser::CommandLineArguments gtargs;
        nlohmann::json artifacts{};
        std::filesystem::path graph_description{};

        explicit CommandLineArguments(
            GraphTraverser::CommandLineArguments gtargs)
            : gtargs{std::move(std::move(gtargs))} {}
    };

    // NOLINTNEXTLINE(modernize-pass-by-value)
    explicit TestProject(std::string const& example_name)
        : example_name_{example_name},
          root_dir_{kWorkspacePrefix / example_name_} {
        SetupConfig();
    }

    explicit TestProject(std::string&& example_name)
        : example_name_{std::move(example_name)},
          root_dir_{kWorkspacePrefix / example_name_} {
        SetupConfig();
    }

    /// \brief Get command line arguments parsing entry points file in
    /// data/<example-name>/<entry_points_filename>, where
    /// <entry_points_filename> takes "_entry_points" as default value
    auto CmdLineArgs(std::string const& entry_points_filename =
                         kDefaultEntryPointsFileName) -> CommandLineArguments {
        auto const entry_points_file = root_dir_ / entry_points_filename;
        if (not FileSystemManager::IsFile(entry_points_file)) {
            Logger::Log(
                LogLevel::Error,
                "file with entry points for graph_traverser tests can not be "
                "found in path {}",
                entry_points_file.string());
            std::exit(EXIT_FAILURE);
        }
        auto const entry_points_json = Json::ReadFile(entry_points_file);
        if (not entry_points_json.has_value()) {
            Logger::Log(LogLevel::Error,
                        "can not read {} for graph_traverser tests",
                        entry_points_file.string());
            std::exit(EXIT_FAILURE);
        }
        return GenerateFromEntryPoints(*entry_points_json);
    }

    auto GetRepoConfig() -> RepositoryConfig* { return &repo_config_; }

  private:
    static inline std::filesystem::path const kOutputDirPrefix =
        FileSystemManager::GetCurrentDirectory() / "./tmp-";
    static inline std::filesystem::path const kWorkspacePrefix =
        FileSystemManager::GetCurrentDirectory() /
        "test/buildtool/graph_traverser/data/";
    static inline std::string const kDefaultEntryPointsFileName =
        "_entry_points";
    std::string example_name_{};
    std::filesystem::path root_dir_{};
    RepositoryConfig repo_config_{};

    void SetupConfig() {
        auto info = RepositoryConfig::RepositoryInfo{FileRoot{root_dir_}};
        repo_config_.SetInfo("", std::move(info));
    }

    auto GenerateFromEntryPoints(nlohmann::json const& entry_points)
        -> CommandLineArguments {
        static int id{};

        GraphTraverser::CommandLineArguments gtargs{0, {}, {}, {}};

        CommandLineArguments clargs{gtargs};
        clargs.artifacts = entry_points;
        auto const comp_graph = root_dir_ / "graph_description_compatible";
        if (Compatibility::IsCompatible() and
            FileSystemManager::Exists(comp_graph)) {
            clargs.graph_description = comp_graph;
        }
        else {
            clargs.graph_description = root_dir_ / "graph_description";
        }
        clargs.gtargs.jobs = std::max(1U, std::thread::hardware_concurrency());
        clargs.gtargs.stage = StageArguments{
            kOutputDirPrefix / (example_name_ + std::to_string(id++))};
        return clargs;
    }
};

[[nodiscard]] inline auto CreateLocalExecConfig() noexcept
    -> LocalExecutionConfig {
    std::vector<std::string> launcher{"env"};
    auto* env_path = std::getenv("PATH");
    if (env_path != nullptr) {
        launcher.emplace_back(std::string{"PATH="} + std::string{env_path});
    }
    else {
        launcher.emplace_back("PATH=/bin:/usr/bin");
    }
    LocalExecutionConfig::Builder builder;
    if (auto config = builder.SetLauncher(std::move(launcher)).Build()) {
        return *std::move(config);
    }
    Logger::Log(LogLevel::Error, "Failure setting the local launcher.");
    std::exit(EXIT_FAILURE);
}

}  // namespace

[[maybe_unused]] static void TestHelloWorldCopyMessage(
    StorageConfig const& storage_config,
    Storage const& storage,
    gsl::not_null<Auth const*> const& auth,
    gsl::not_null<RemoteExecutionConfig const*> const& remote_config,
    bool is_hermetic = true) {
    TestProject p("hello_world_copy_message");

    auto const local_exec_config = CreateLocalExecConfig();
    auto const clargs = p.CmdLineArgs();

    Statistics stats{};
    Progress progress{};

    // pack the local context instances to be passed to ApiBundle
    LocalContext const local_context{.exec_config = &local_exec_config,
                                     .storage_config = &storage_config,
                                     .storage = &storage};

    RetryConfig retry_config{};  // default retry config

    // pack the remote context instances to be passed to ApiBundle
    RemoteContext const remote_context{.auth = auth,
                                       .retry_config = &retry_config,
                                       .exec_config = remote_config};

    ApiBundle const apis{&local_context, &remote_context, p.GetRepoConfig()};

    GraphTraverser const gt{clargs.gtargs,
                            p.GetRepoConfig(),
                            remote_config->platform_properties,
                            remote_config->dispatch,
                            &stats,
                            &progress,
                            &apis,
                            [](auto done, auto cv) {}};
    auto const result =
        gt.BuildAndStage(clargs.graph_description, clargs.artifacts);

    REQUIRE(result);
    REQUIRE(result->output_paths.size() == 1);
    CHECK(FileSystemManager::IsFile(result->output_paths.at(0)));
    auto const contents =
        FileSystemManager::ReadFile(result->output_paths.at(0));
    CHECK(contents.has_value());
    CHECK(contents == "Hello, World!\n");

    if (is_hermetic) {
        CHECK(stats.ActionsQueuedCounter() == 2);
        CHECK(stats.ActionsCachedCounter() == 0);
    }

    SECTION("Executable is retrieved as executable") {
        auto const clargs_exec = p.CmdLineArgs("_entry_points_get_executable");
        GraphTraverser const gt_get_exec{clargs_exec.gtargs,
                                         p.GetRepoConfig(),
                                         remote_config->platform_properties,
                                         remote_config->dispatch,
                                         &stats,
                                         &progress,
                                         &apis,
                                         [](auto done, auto cv) {}};
        auto const exec_result = gt_get_exec.BuildAndStage(
            clargs_exec.graph_description, clargs_exec.artifacts);

        REQUIRE(exec_result);
        REQUIRE(exec_result->output_paths.size() == 1);
        auto const exec_path = exec_result->output_paths.at(0);
        CHECK(FileSystemManager::IsFile(exec_path));
        CHECK(FileSystemManager::IsExecutable(exec_path));
        CHECK(FileSystemManager::Type(exec_path) == ObjectType::Executable);

        if (is_hermetic) {
            CHECK(stats.ActionsQueuedCounter() == 3);  // One more action queued
            CHECK(stats.ActionsCachedCounter() ==
                  1);  // But that action was cached
        }
    }
}

[[maybe_unused]] static void TestCopyLocalFile(
    StorageConfig const& storage_config,
    Storage const& storage,
    gsl::not_null<Auth const*> const& auth,
    gsl::not_null<RemoteExecutionConfig const*> const& remote_config,
    bool is_hermetic = true) {
    TestProject p("copy_local_file");

    auto const local_exec_config = CreateLocalExecConfig();
    auto const clargs = p.CmdLineArgs();

    Statistics stats{};
    Progress progress{};

    // pack the local context instances to be passed to ApiBundle
    LocalContext const local_context{.exec_config = &local_exec_config,
                                     .storage_config = &storage_config,
                                     .storage = &storage};

    RetryConfig retry_config{};  // default retry config

    // pack the remote context instances to be passed to ApiBundle
    RemoteContext const remote_context{.auth = auth,
                                       .retry_config = &retry_config,
                                       .exec_config = remote_config};

    ApiBundle const apis{&local_context, &remote_context, p.GetRepoConfig()};

    GraphTraverser const gt{clargs.gtargs,
                            p.GetRepoConfig(),
                            remote_config->platform_properties,
                            remote_config->dispatch,
                            &stats,
                            &progress,
                            &apis,
                            [](auto done, auto cv) {}};
    auto const result =
        gt.BuildAndStage(clargs.graph_description, clargs.artifacts);

    REQUIRE(result);
    REQUIRE(result->output_paths.size() == 1);
    CHECK(FileSystemManager::IsFile(result->output_paths.at(0)));

    if (is_hermetic) {
        CHECK(stats.ActionsQueuedCounter() == 0);
        CHECK(stats.ActionsCachedCounter() == 0);
    }
}

[[maybe_unused]] static void TestSequencePrinterBuildLibraryOnly(
    StorageConfig const& storage_config,
    Storage const& storage,
    gsl::not_null<Auth const*> const& auth,
    gsl::not_null<RemoteExecutionConfig const*> const& remote_config,
    bool is_hermetic = true) {
    TestProject p("sequence_printer_build_library_only");

    auto const local_exec_config = CreateLocalExecConfig();
    auto const clargs = p.CmdLineArgs();

    Statistics stats{};
    Progress progress{};

    // pack the local context instances to be passed to ApiBundle
    LocalContext const local_context{.exec_config = &local_exec_config,
                                     .storage_config = &storage_config,
                                     .storage = &storage};

    RetryConfig retry_config{};  // default retry config

    // pack the remote context instances to be passed to ApiBundle
    RemoteContext const remote_context{.auth = auth,
                                       .retry_config = &retry_config,
                                       .exec_config = remote_config};

    ApiBundle const apis{&local_context, &remote_context, p.GetRepoConfig()};

    GraphTraverser const gt{clargs.gtargs,
                            p.GetRepoConfig(),
                            remote_config->platform_properties,
                            remote_config->dispatch,
                            &stats,
                            &progress,
                            &apis,
                            [](auto done, auto cv) {}};
    auto const result =
        gt.BuildAndStage(clargs.graph_description, clargs.artifacts);

    REQUIRE(result);
    REQUIRE(result->output_paths.size() == 1);
    CHECK(FileSystemManager::IsFile(result->output_paths.at(0)));

    auto const clargs_full_build = p.CmdLineArgs("_entry_points_full_build");
    GraphTraverser const gt_full_build{clargs_full_build.gtargs,
                                       p.GetRepoConfig(),
                                       remote_config->platform_properties,
                                       remote_config->dispatch,
                                       &stats,
                                       &progress,
                                       &apis,
                                       [](auto done, auto cv) {}};
    auto const full_build_result = gt_full_build.BuildAndStage(
        clargs_full_build.graph_description, clargs_full_build.artifacts);

    REQUIRE(full_build_result);
    REQUIRE(full_build_result->output_paths.size() == 1);
    CHECK(FileSystemManager::IsFile(full_build_result->output_paths.at(0)));

    if (is_hermetic) {
        CHECK(stats.ActionsQueuedCounter() == 8);
        CHECK(stats.ActionsCachedCounter() == 3);
    }
    else {
        CHECK(stats.ActionsCachedCounter() > 0);
    }
}

[[maybe_unused]] static void TestHelloWorldWithKnownSource(
    StorageConfig const& storage_config,
    Storage const& storage,
    gsl::not_null<Auth const*> const& auth,
    gsl::not_null<RemoteExecutionConfig const*> const& remote_config,
    bool is_hermetic = true) {
    TestProject full_hello_world("hello_world_copy_message");

    auto const local_exec_config = CreateLocalExecConfig();
    auto const clargs_update_cpp =
        full_hello_world.CmdLineArgs("_entry_points_upload_source");

    Statistics stats{};
    Progress progress{};

    // pack the local context instances to be passed to ApiBundle
    LocalContext const local_context{.exec_config = &local_exec_config,
                                     .storage_config = &storage_config,
                                     .storage = &storage};

    RetryConfig retry_config{};  // default retry config

    // pack the remote context instances to be passed to ApiBundle
    RemoteContext const remote_context{.auth = auth,
                                       .retry_config = &retry_config,
                                       .exec_config = remote_config};

    ApiBundle const apis{
        &local_context, &remote_context, full_hello_world.GetRepoConfig()};

    GraphTraverser const gt_upload{clargs_update_cpp.gtargs,
                                   full_hello_world.GetRepoConfig(),
                                   remote_config->platform_properties,
                                   remote_config->dispatch,
                                   &stats,
                                   &progress,
                                   &apis,
                                   [](auto done, auto cv) {}};
    auto const cpp_result = gt_upload.BuildAndStage(
        clargs_update_cpp.graph_description, clargs_update_cpp.artifacts);

    REQUIRE(cpp_result);
    REQUIRE(cpp_result->output_paths.size() == 1);

    CHECK(FileSystemManager::IsFile(cpp_result->output_paths.at(0)));

    if (is_hermetic) {
        CHECK(stats.ActionsQueuedCounter() == 0);
        CHECK(stats.ActionsCachedCounter() == 0);
    }
    TestProject hello_world_known_cpp("hello_world_known_source");

    auto const clargs = hello_world_known_cpp.CmdLineArgs();
    GraphTraverser const gt{clargs.gtargs,
                            full_hello_world.GetRepoConfig(),
                            remote_config->platform_properties,
                            remote_config->dispatch,
                            &stats,
                            &progress,
                            &apis,
                            [](auto done, auto cv) {}};
    auto const result =
        gt.BuildAndStage(clargs.graph_description, clargs.artifacts);

    REQUIRE(result);
    REQUIRE(result->output_paths.size() == 1);
    CHECK(FileSystemManager::IsFile(result->output_paths.at(0)));

    if (is_hermetic) {
        CHECK(stats.ActionsQueuedCounter() == 2);
        CHECK(stats.ActionsCachedCounter() == 0);
    }
    else {
        CHECK(stats.ActionsQueuedCounter() >= 2);
    }
}

static void TestBlobsUploadedAndUsed(
    StorageConfig const& storage_config,
    Storage const& storage,
    gsl::not_null<Auth const*> const& auth,
    gsl::not_null<RemoteExecutionConfig const*> const& remote_config,
    bool is_hermetic = true) {
    TestProject p("use_uploaded_blobs");
    auto const clargs = p.CmdLineArgs();

    Statistics stats{};
    Progress progress{};

    auto const local_exec_config = CreateLocalExecConfig();

    // pack the local context instances to be passed to ApiBundle
    LocalContext const local_context{.exec_config = &local_exec_config,
                                     .storage_config = &storage_config,
                                     .storage = &storage};

    RetryConfig retry_config{};  // default retry config

    // pack the remote context instances to be passed to ApiBundle
    RemoteContext const remote_context{.auth = auth,
                                       .retry_config = &retry_config,
                                       .exec_config = remote_config};

    ApiBundle const apis{&local_context, &remote_context, p.GetRepoConfig()};

    GraphTraverser gt{clargs.gtargs,
                      p.GetRepoConfig(),
                      remote_config->platform_properties,
                      remote_config->dispatch,
                      &stats,
                      &progress,
                      &apis,
                      [](auto done, auto cv) {}};
    auto const result =
        gt.BuildAndStage(clargs.graph_description, clargs.artifacts);

    REQUIRE(result);
    REQUIRE(result->output_paths.size() == 1);
    CHECK(FileSystemManager::IsFile(result->output_paths.at(0)));

    auto const contents =
        FileSystemManager::ReadFile(result->output_paths.at(0));
    CHECK(contents.has_value());
    CHECK(contents == "this is a test to check if blobs are uploaded");

    if (is_hermetic) {
        CHECK(stats.ActionsQueuedCounter() == 1);
        CHECK(stats.ActionsCachedCounter() == 0);
    }
    else {
        CHECK(stats.ActionsQueuedCounter() >= 1);
    }
}

static void TestEnvironmentVariablesSetAndUsed(
    StorageConfig const& storage_config,
    Storage const& storage,
    gsl::not_null<Auth const*> const& auth,
    gsl::not_null<RemoteExecutionConfig const*> const& remote_config,
    bool is_hermetic = true) {
    TestProject p("use_env_variables");
    auto const clargs = p.CmdLineArgs();

    Statistics stats{};
    Progress progress{};

    auto const local_exec_config = CreateLocalExecConfig();

    // pack the local context instances to be passed to ApiBundle
    LocalContext const local_context{.exec_config = &local_exec_config,
                                     .storage_config = &storage_config,
                                     .storage = &storage};

    RetryConfig retry_config{};  // default retry config

    // pack the remote context instances to be passed to ApiBundle
    RemoteContext const remote_context{.auth = auth,
                                       .retry_config = &retry_config,
                                       .exec_config = remote_config};

    ApiBundle const apis{&local_context, &remote_context, p.GetRepoConfig()};

    GraphTraverser gt{clargs.gtargs,
                      p.GetRepoConfig(),
                      remote_config->platform_properties,
                      remote_config->dispatch,
                      &stats,
                      &progress,
                      &apis,
                      [](auto done, auto cv) {}};
    auto const result =
        gt.BuildAndStage(clargs.graph_description, clargs.artifacts);

    REQUIRE(result);
    REQUIRE(result->output_paths.size() == 1);
    CHECK(FileSystemManager::IsFile(result->output_paths.at(0)));

    auto const contents =
        FileSystemManager::ReadFile(result->output_paths.at(0));
    CHECK(contents.has_value());
    CHECK(contents == "content from environment variable");

    if (is_hermetic) {
        CHECK(stats.ActionsQueuedCounter() == 1);
        CHECK(stats.ActionsCachedCounter() == 0);
    }
    else {
        CHECK(stats.ActionsQueuedCounter() >= 1);
    }
}

static void TestTreesUsed(
    StorageConfig const& storage_config,
    Storage const& storage,
    gsl::not_null<Auth const*> const& auth,
    gsl::not_null<RemoteExecutionConfig const*> const& remote_config,
    bool is_hermetic = true) {
    TestProject p("use_trees");
    auto const clargs = p.CmdLineArgs();

    Statistics stats{};
    Progress progress{};

    auto const local_exec_config = CreateLocalExecConfig();

    // pack the local context instances to be passed to ApiBundle
    LocalContext const local_context{.exec_config = &local_exec_config,
                                     .storage_config = &storage_config,
                                     .storage = &storage};

    RetryConfig retry_config{};  // default retry config

    // pack the remote context instances to be passed to ApiBundle
    RemoteContext const remote_context{.auth = auth,
                                       .retry_config = &retry_config,
                                       .exec_config = remote_config};

    ApiBundle const apis{&local_context, &remote_context, p.GetRepoConfig()};

    GraphTraverser gt{clargs.gtargs,
                      p.GetRepoConfig(),
                      remote_config->platform_properties,
                      remote_config->dispatch,
                      &stats,
                      &progress,
                      &apis,
                      [](auto done, auto cv) {}};
    auto const result =
        gt.BuildAndStage(clargs.graph_description, clargs.artifacts);

    REQUIRE(result);
    REQUIRE(result->output_paths.size() == 1);
    CHECK(FileSystemManager::IsFile(result->output_paths.at(0)));

    auto const contents =
        FileSystemManager::ReadFile(result->output_paths.at(0));
    CHECK(contents.has_value());
    CHECK(contents == "this is a test to check if blobs are uploaded");

    if (is_hermetic) {
        CHECK(stats.ActionsQueuedCounter() == 2);
        CHECK(stats.ActionsCachedCounter() == 0);
    }
    else {
        CHECK(stats.ActionsQueuedCounter() >= 2);
    }
}

static void TestNestedTreesUsed(
    StorageConfig const& storage_config,
    Storage const& storage,
    gsl::not_null<Auth const*> const& auth,
    gsl::not_null<RemoteExecutionConfig const*> const& remote_config,
    bool is_hermetic = true) {
    TestProject p("use_nested_trees");
    auto const clargs = p.CmdLineArgs();

    Statistics stats{};
    Progress progress{};

    auto const local_exec_config = CreateLocalExecConfig();

    // pack the local context instances to be passed to ApiBundle
    LocalContext const local_context{.exec_config = &local_exec_config,
                                     .storage_config = &storage_config,
                                     .storage = &storage};

    RetryConfig retry_config{};  // default retry config

    // pack the remote context instances to be passed to ApiBundle
    RemoteContext const remote_context{.auth = auth,
                                       .retry_config = &retry_config,
                                       .exec_config = remote_config};

    ApiBundle const apis{&local_context, &remote_context, p.GetRepoConfig()};

    GraphTraverser gt{clargs.gtargs,
                      p.GetRepoConfig(),
                      remote_config->platform_properties,
                      remote_config->dispatch,
                      &stats,
                      &progress,
                      &apis,
                      [](auto done, auto cv) {}};
    auto const result =
        gt.BuildAndStage(clargs.graph_description, clargs.artifacts);

    REQUIRE(result);
    REQUIRE(result->output_paths.size() == 1);
    CHECK(FileSystemManager::IsFile(result->output_paths.at(0)));

    auto const contents =
        FileSystemManager::ReadFile(result->output_paths.at(0));
    CHECK(contents.has_value());
    CHECK(contents == "this is a test to check if blobs are uploaded");

    if (is_hermetic) {
        CHECK(stats.ActionsQueuedCounter() == 1);
        CHECK(stats.ActionsCachedCounter() == 0);
    }
    else {
        CHECK(stats.ActionsQueuedCounter() >= 1);
    }
}

static void TestFlakyHelloWorldDetected(
    StorageConfig const& storage_config,
    Storage const& storage,
    gsl::not_null<Auth const*> const& auth,
    gsl::not_null<RemoteExecutionConfig const*> const& remote_config,
    bool /*is_hermetic*/ = true) {
    TestProject p("flaky_hello_world");

    Statistics stats{};
    Progress progress{};

    auto const local_exec_config = CreateLocalExecConfig();

    // pack the local context instances to be passed to ApiBundle
    LocalContext const local_context{.exec_config = &local_exec_config,
                                     .storage_config = &storage_config,
                                     .storage = &storage};

    RetryConfig retry_config{};  // default retry config

    // pack the remote context instances to be passed to ApiBundle
    RemoteContext const remote_context{.auth = auth,
                                       .retry_config = &retry_config,
                                       .exec_config = remote_config};

    ApiBundle const apis{&local_context, &remote_context, p.GetRepoConfig()};

    {
        auto clargs = p.CmdLineArgs("_entry_points_ctimes");
        GraphTraverser const gt{clargs.gtargs,
                                p.GetRepoConfig(),
                                remote_config->platform_properties,
                                remote_config->dispatch,
                                &stats,
                                &progress,
                                &apis,
                                [](auto done, auto cv) {}};
        auto const result =
            gt.BuildAndStage(clargs.graph_description, clargs.artifacts);

        REQUIRE(result);
        REQUIRE(result->output_paths.size() == 1);
    }

    using namespace std::chrono_literals;
    std::this_thread::sleep_for(1s);

    // make_exe[flaky]->make_output[miss]
    auto clargs_output = p.CmdLineArgs();
    clargs_output.gtargs.rebuild = RebuildArguments{};
    GraphTraverser const gt_output{clargs_output.gtargs,
                                   p.GetRepoConfig(),
                                   remote_config->platform_properties,
                                   remote_config->dispatch,
                                   &stats,
                                   &progress,
                                   &apis,
                                   [](auto done, auto cv) {}};
    REQUIRE(gt_output.BuildAndStage(clargs_output.graph_description,
                                    clargs_output.artifacts));
    CHECK(stats.ActionsFlakyCounter() == 1);
    CHECK(stats.RebuiltActionComparedCounter() == 1);
    CHECK(stats.RebuiltActionMissingCounter() == 1);
    stats.Reset();

    // make_exe[flaky]->make_output[miss]->strip_time [miss]
    auto clargs_stripped = p.CmdLineArgs("_entry_points_stripped");
    clargs_stripped.gtargs.rebuild = RebuildArguments{};
    GraphTraverser const gt_stripped{clargs_stripped.gtargs,
                                     p.GetRepoConfig(),
                                     remote_config->platform_properties,
                                     remote_config->dispatch,
                                     &stats,
                                     &progress,
                                     &apis,
                                     [](auto done, auto cv) {}};
    REQUIRE(gt_stripped.BuildAndStage(clargs_stripped.graph_description,
                                      clargs_stripped.artifacts));
    CHECK(stats.ActionsFlakyCounter() == 1);
    CHECK(stats.RebuiltActionComparedCounter() == 1);
    CHECK(stats.RebuiltActionMissingCounter() == 2);
    stats.Reset();

    // make_exe[flaky]->make_output[miss]->strip_time[miss]->list_ctimes [flaky]
    auto clargs_ctimes = p.CmdLineArgs("_entry_points_ctimes");
    clargs_ctimes.gtargs.rebuild = RebuildArguments{};
    GraphTraverser const gt_ctimes{clargs_ctimes.gtargs,
                                   p.GetRepoConfig(),
                                   remote_config->platform_properties,
                                   remote_config->dispatch,
                                   &stats,
                                   &progress,
                                   &apis,
                                   [](auto done, auto cv) {}};
    REQUIRE(gt_ctimes.BuildAndStage(clargs_ctimes.graph_description,
                                    clargs_ctimes.artifacts));
    CHECK(stats.ActionsFlakyCounter() == 2);
    CHECK(stats.RebuiltActionComparedCounter() == 2);
    CHECK(stats.RebuiltActionMissingCounter() == 2);
}

#endif  // INCLUDED_SRC_TEST_BUILDTOOL_GRAPH_GRAVERSER_GRAPH_TRAVERSER_TEST_HPP
