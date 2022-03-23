#ifndef INCLUDED_SRC_TEST_BUILDTOOL_GRAPH_GRAVERSER_GRAPH_TRAVERSER_TEST_HPP
#define INCLUDED_SRC_TEST_BUILDTOOL_GRAPH_GRAVERSER_GRAPH_TRAVERSER_TEST_HPP

#include <filesystem>
#include <sstream>
#include <utility>

#include "catch2/catch.hpp"
#include "src/buildtool/common/statistics.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/jsonfs.hpp"
#include "src/buildtool/graph_traverser/graph_traverser.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/json.hpp"
#include "test/utils/test_env.hpp"

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
    explicit TestProject(std::string const& example_name,
                         bool run_local = false)
        : example_name_{example_name},
          root_dir_{kWorkspacePrefix / example_name_},
          run_local_{run_local} {
        SetupConfig();
    }

    explicit TestProject(std::string&& example_name, bool run_local = false)
        : example_name_{std::move(example_name)},
          root_dir_{kWorkspacePrefix / example_name_},
          run_local_{run_local} {
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
    bool run_local_{};

    void SetupConfig() {
        auto info = RepositoryConfig::RepositoryInfo{FileRoot{root_dir_}};
        RepositoryConfig::Instance().Reset();
        RepositoryConfig::Instance().SetInfo("", std::move(info));
    }

    auto GenerateFromEntryPoints(nlohmann::json const& entry_points)
        -> CommandLineArguments {
        static int id{};

        GraphTraverser::CommandLineArguments gtargs{0, {}, {}, {}, {}};

        CommandLineArguments clargs{gtargs};
        clargs.artifacts = entry_points;
        clargs.graph_description = root_dir_ / "graph_description";
        clargs.gtargs.jobs = std::max(1U, std::thread::hardware_concurrency());
        clargs.gtargs.stage = StageArguments{
            kOutputDirPrefix / (example_name_ + std::to_string(id++))};
        if (not run_local_) {
            clargs.gtargs.endpoint.remote_execution_address =
                ReadRemoteAddressFromEnv();
            clargs.gtargs.build.platform_properties =
                ReadPlatformPropertiesFromEnv();
            if (not clargs.gtargs.endpoint.remote_execution_address) {
                Logger::Log(LogLevel::Error,
                            "Missing env var 'REMOTE_EXECUTION_ADDRESS' for "
                            "non-local graph_traverser tests.");
                std::exit(EXIT_FAILURE);
            }
        }
        return clargs;
    }
};

}  // namespace

[[maybe_unused]] static void TestHelloWorldCopyMessage(
    bool run_local,
    bool is_hermetic = true) {
    TestProject p("hello_world_copy_message", run_local);

    auto const clargs = p.CmdLineArgs();
    GraphTraverser const gt{clargs.gtargs};
    auto const output_paths =
        gt.BuildAndStage(clargs.graph_description, clargs.artifacts);

    REQUIRE(output_paths);
    REQUIRE(output_paths->first.size() == 1);
    CHECK(FileSystemManager::IsFile(output_paths->first.at(0)));
    auto const contents =
        FileSystemManager::ReadFile(output_paths->first.at(0));
    CHECK(contents.has_value());
    CHECK(contents == "Hello, World!\n");

    if (is_hermetic) {
        CHECK(Statistics::Instance().ActionsQueuedCounter() == 2);
        CHECK(Statistics::Instance().ActionsCachedCounter() == 0);
    }

    SECTION("Executable is retrieved as executable") {
        auto const clargs_exec = p.CmdLineArgs("_entry_points_get_executable");
        GraphTraverser const gt_get_exec{clargs_exec.gtargs};
        auto const exec_output_paths = gt_get_exec.BuildAndStage(
            clargs_exec.graph_description, clargs_exec.artifacts);

        REQUIRE(exec_output_paths);
        REQUIRE(exec_output_paths->first.size() == 1);
        auto const exec_path = exec_output_paths->first.at(0);
        CHECK(FileSystemManager::IsFile(exec_path));
        CHECK(FileSystemManager::IsExecutable(exec_path));
        CHECK(FileSystemManager::Type(exec_path) == ObjectType::Executable);

        if (is_hermetic) {
            CHECK(Statistics::Instance().ActionsQueuedCounter() ==
                  3);  // One more action queued
            CHECK(Statistics::Instance().ActionsCachedCounter() ==
                  1);  // But that action was cached
        }
    }
}

[[maybe_unused]] static void TestCopyLocalFile(bool run_local,
                                               bool is_hermetic = true) {
    TestProject p("copy_local_file", run_local);

    auto const clargs = p.CmdLineArgs();
    GraphTraverser const gt{clargs.gtargs};
    auto const output_paths =
        gt.BuildAndStage(clargs.graph_description, clargs.artifacts);

    REQUIRE(output_paths);
    REQUIRE(output_paths->first.size() == 1);
    CHECK(FileSystemManager::IsFile(output_paths->first.at(0)));

    if (is_hermetic) {
        CHECK(Statistics::Instance().ActionsQueuedCounter() == 0);
        CHECK(Statistics::Instance().ActionsCachedCounter() == 0);
    }
}

[[maybe_unused]] static void TestSequencePrinterBuildLibraryOnly(
    bool run_local,
    bool is_hermetic = true) {
    TestProject p("sequence_printer_build_library_only", run_local);

    auto const clargs = p.CmdLineArgs();
    GraphTraverser const gt{clargs.gtargs};
    auto const output_paths =
        gt.BuildAndStage(clargs.graph_description, clargs.artifacts);

    REQUIRE(output_paths);
    REQUIRE(output_paths->first.size() == 1);
    CHECK(FileSystemManager::IsFile(output_paths->first.at(0)));

    auto const clargs_full_build = p.CmdLineArgs("_entry_points_full_build");
    GraphTraverser const gt_full_build{clargs_full_build.gtargs};
    auto const full_build_output_paths = gt_full_build.BuildAndStage(
        clargs_full_build.graph_description, clargs_full_build.artifacts);

    REQUIRE(full_build_output_paths);
    REQUIRE(full_build_output_paths->first.size() == 1);
    CHECK(FileSystemManager::IsFile(full_build_output_paths->first.at(0)));

    if (is_hermetic) {
        CHECK(Statistics::Instance().ActionsQueuedCounter() == 8);
        CHECK(Statistics::Instance().ActionsCachedCounter() == 3);
    }
    else {
        CHECK(Statistics::Instance().ActionsCachedCounter() > 0);
    }
}

[[maybe_unused]] static void TestHelloWorldWithKnownSource(
    bool run_local,
    bool is_hermetic = true) {
    TestProject full_hello_world("hello_world_copy_message", run_local);

    auto const clargs_update_cpp =
        full_hello_world.CmdLineArgs("_entry_points_upload_source");
    GraphTraverser const gt_upload{clargs_update_cpp.gtargs};
    auto const cpp_output_path = gt_upload.BuildAndStage(
        clargs_update_cpp.graph_description, clargs_update_cpp.artifacts);

    REQUIRE(cpp_output_path);
    REQUIRE(cpp_output_path->first.size() == 1);

    CHECK(FileSystemManager::IsFile(cpp_output_path->first.at(0)));

    if (is_hermetic) {
        CHECK(Statistics::Instance().ActionsQueuedCounter() == 0);
        CHECK(Statistics::Instance().ActionsCachedCounter() == 0);
    }

    TestProject hello_world_known_cpp("hello_world_known_source", run_local);

    auto const clargs = hello_world_known_cpp.CmdLineArgs();
    GraphTraverser const gt{clargs.gtargs};
    auto const output_paths =
        gt.BuildAndStage(clargs.graph_description, clargs.artifacts);

    REQUIRE(output_paths);
    REQUIRE(output_paths->first.size() == 1);
    CHECK(FileSystemManager::IsFile(output_paths->first.at(0)));

    if (is_hermetic) {
        CHECK(Statistics::Instance().ActionsQueuedCounter() == 2);
        CHECK(Statistics::Instance().ActionsCachedCounter() == 0);
    }
    else {
        CHECK(Statistics::Instance().ActionsQueuedCounter() >= 2);
    }
}

static void TestBlobsUploadedAndUsed(bool run_local, bool is_hermetic = true) {
    TestProject p("use_uploaded_blobs", run_local);
    auto const clargs = p.CmdLineArgs();

    GraphTraverser gt{clargs.gtargs};
    auto const output_paths =
        gt.BuildAndStage(clargs.graph_description, clargs.artifacts);

    REQUIRE(output_paths);
    REQUIRE(output_paths->first.size() == 1);
    CHECK(FileSystemManager::IsFile(output_paths->first.at(0)));

    auto const contents =
        FileSystemManager::ReadFile(output_paths->first.at(0));
    CHECK(contents.has_value());
    CHECK(contents == "this is a test to check if blobs are uploaded");

    if (is_hermetic) {
        CHECK(Statistics::Instance().ActionsQueuedCounter() == 1);
        CHECK(Statistics::Instance().ActionsCachedCounter() == 0);
    }
    else {
        CHECK(Statistics::Instance().ActionsQueuedCounter() >= 1);
    }
}

static void TestEnvironmentVariablesSetAndUsed(bool run_local,
                                               bool is_hermetic = true) {
    TestProject p("use_env_variables", run_local);
    auto const clargs = p.CmdLineArgs();

    GraphTraverser gt{clargs.gtargs};
    auto const output_paths =
        gt.BuildAndStage(clargs.graph_description, clargs.artifacts);

    REQUIRE(output_paths);
    REQUIRE(output_paths->first.size() == 1);
    CHECK(FileSystemManager::IsFile(output_paths->first.at(0)));

    auto const contents =
        FileSystemManager::ReadFile(output_paths->first.at(0));
    CHECK(contents.has_value());
    CHECK(contents == "content from environment variable");

    if (is_hermetic) {
        CHECK(Statistics::Instance().ActionsQueuedCounter() == 1);
        CHECK(Statistics::Instance().ActionsCachedCounter() == 0);
    }
    else {
        CHECK(Statistics::Instance().ActionsQueuedCounter() >= 1);
    }
}

static void TestTreesUsed(bool run_local, bool is_hermetic = true) {
    TestProject p("use_trees", run_local);
    auto const clargs = p.CmdLineArgs();

    GraphTraverser gt{clargs.gtargs};
    auto const output_paths =
        gt.BuildAndStage(clargs.graph_description, clargs.artifacts);

    REQUIRE(output_paths);
    REQUIRE(output_paths->first.size() == 1);
    CHECK(FileSystemManager::IsFile(output_paths->first.at(0)));

    auto const contents =
        FileSystemManager::ReadFile(output_paths->first.at(0));
    CHECK(contents.has_value());
    CHECK(contents == "this is a test to check if blobs are uploaded");

    if (is_hermetic) {
        CHECK(Statistics::Instance().ActionsQueuedCounter() == 2);
        CHECK(Statistics::Instance().ActionsCachedCounter() == 0);
    }
    else {
        CHECK(Statistics::Instance().ActionsQueuedCounter() >= 2);
    }
}

static void TestNestedTreesUsed(bool run_local, bool is_hermetic = true) {
    TestProject p("use_nested_trees", run_local);
    auto const clargs = p.CmdLineArgs();

    GraphTraverser gt{clargs.gtargs};
    auto const output_paths =
        gt.BuildAndStage(clargs.graph_description, clargs.artifacts);

    REQUIRE(output_paths);
    REQUIRE(output_paths->first.size() == 1);
    CHECK(FileSystemManager::IsFile(output_paths->first.at(0)));

    auto const contents =
        FileSystemManager::ReadFile(output_paths->first.at(0));
    CHECK(contents.has_value());
    CHECK(contents == "this is a test to check if blobs are uploaded");

    if (is_hermetic) {
        CHECK(Statistics::Instance().ActionsQueuedCounter() == 1);
        CHECK(Statistics::Instance().ActionsCachedCounter() == 0);
    }
    else {
        CHECK(Statistics::Instance().ActionsQueuedCounter() >= 1);
    }
}

static void TestFlakyHelloWorldDetected(bool run_local,
                                        bool /*is_hermetic*/ = true) {
    TestProject p("flaky_hello_world", run_local);

    {
        auto clargs = p.CmdLineArgs("_entry_points_ctimes");
        GraphTraverser const gt{clargs.gtargs};
        auto const output_paths =
            gt.BuildAndStage(clargs.graph_description, clargs.artifacts);

        REQUIRE(output_paths);
        REQUIRE(output_paths->first.size() == 1);
    }

    using namespace std::chrono_literals;
    std::this_thread::sleep_for(1s);

    // make_exe[flaky]->make_output[miss]
    auto clargs_output = p.CmdLineArgs();
    clargs_output.gtargs.rebuild = RebuildArguments{};
    GraphTraverser const gt_output{clargs_output.gtargs};
    REQUIRE(gt_output.BuildAndStage(clargs_output.graph_description,
                                    clargs_output.artifacts));
    CHECK(Statistics::Instance().ActionsFlakyCounter() == 1);
    CHECK(Statistics::Instance().RebuiltActionComparedCounter() == 1);
    CHECK(Statistics::Instance().RebuiltActionMissingCounter() == 1);
    Statistics::Instance().Reset();

    // make_exe[flaky]->make_output[miss]->strip_time [miss]
    auto clargs_stripped = p.CmdLineArgs("_entry_points_stripped");
    clargs_stripped.gtargs.rebuild = RebuildArguments{};
    GraphTraverser const gt_stripped{clargs_stripped.gtargs};
    REQUIRE(gt_stripped.BuildAndStage(clargs_stripped.graph_description,
                                      clargs_stripped.artifacts));
    CHECK(Statistics::Instance().ActionsFlakyCounter() == 1);
    CHECK(Statistics::Instance().RebuiltActionComparedCounter() == 1);
    CHECK(Statistics::Instance().RebuiltActionMissingCounter() == 2);
    Statistics::Instance().Reset();

    // make_exe[flaky]->make_output[miss]->strip_time[miss]->list_ctimes [flaky]
    auto clargs_ctimes = p.CmdLineArgs("_entry_points_ctimes");
    clargs_ctimes.gtargs.rebuild = RebuildArguments{};
    GraphTraverser const gt_ctimes{clargs_ctimes.gtargs};
    REQUIRE(gt_ctimes.BuildAndStage(clargs_ctimes.graph_description,
                                    clargs_ctimes.artifacts));
    CHECK(Statistics::Instance().ActionsFlakyCounter() == 2);
    CHECK(Statistics::Instance().RebuiltActionComparedCounter() == 2);
    CHECK(Statistics::Instance().RebuiltActionMissingCounter() == 2);
}

#endif  // INCLUDED_SRC_TEST_BUILDTOOL_GRAPH_GRAVERSER_GRAPH_TRAVERSER_TEST_HPP
