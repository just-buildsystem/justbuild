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

#ifndef INCLUDED_SRC_TEST_BUILDTOOL_EXECUTION_ENGINE_EXECUTOR_EXECUTOR_API_TEST_HPP
#define INCLUDED_SRC_TEST_BUILDTOOL_EXECUTION_ENGINE_EXECUTOR_EXECUTOR_API_TEST_HPP

#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "catch2/catch_test_macros.hpp"
#include "gsl/gsl"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_description.hpp"
#include "src/buildtool/common/remote/retry_config.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/common/statistics.hpp"
#include "src/buildtool/compatibility/compatibility.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/execution_api/remote/context.hpp"
#include "src/buildtool/execution_engine/dag/dag.hpp"
#include "src/buildtool/execution_engine/executor/context.hpp"
#include "src/buildtool/execution_engine/executor/executor.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/progress_reporting/progress.hpp"
#include "test/utils/executor/test_api_bundle.hpp"
#include "test/utils/remote_execution/test_remote_config.hpp"

using ApiFactory = std::function<IExecutionApi::Ptr()>;

static inline void SetupConfig(RepositoryConfig* repo_config) {
    auto info = RepositoryConfig::RepositoryInfo{FileRoot{
        std::filesystem::path{"test/buildtool/execution_engine/executor"}}};
    repo_config->Reset();
    repo_config->SetInfo("", std::move(info));
}

static inline void RunBlobUpload(RepositoryConfig* repo_config,
                                 ApiFactory const& factory) {
    SetupConfig(repo_config);
    auto api = factory();
    HashFunction const hash_function{Compatibility::IsCompatible()
                                         ? HashFunction::Type::PlainSHA256
                                         : HashFunction::Type::GitSHA1};

    std::string const blob = "test";
    CHECK(api->Upload(ArtifactBlobContainer{{ArtifactBlob{
        ArtifactDigest{hash_function.HashBlobData(blob).HexString(),
                       blob.size(),
                       /*is_tree=*/false},
        blob,
        /*is_exec=*/false}}}));
}

[[nodiscard]] static inline auto GetTestDir() -> std::filesystem::path {
    auto* tmp_dir = std::getenv("TEST_TMPDIR");
    if (tmp_dir != nullptr) {
        return tmp_dir;
    }
    return FileSystemManager::GetCurrentDirectory() /
           "test/buildtool/execution_engine/executor";
}

template <class Executor>
[[nodiscard]] static inline auto AddAndProcessTree(DependencyGraph* g,
                                                   Executor* runner,
                                                   Tree const& tree_desc)
    -> std::optional<Artifact::ObjectInfo> {
    REQUIRE(g->AddAction(tree_desc.Action()));

    // obtain tree action and tree artifact
    auto const* tree_action = g->ActionNodeWithId(tree_desc.Id());
    REQUIRE_FALSE(tree_action == nullptr);
    auto const* tree_artifact = g->ArtifactNodeWithId(tree_desc.Output().Id());
    REQUIRE_FALSE(tree_artifact == nullptr);

    // "run" tree action to produce tree artifact
    REQUIRE(runner->Process(tree_action));

    // read computed tree artifact info (digest + object type)
    return tree_artifact->Content().Info();
}

static inline void RunHelloWorldCompilation(
    RepositoryConfig* repo_config,
    gsl::not_null<Statistics*> const& stats,
    gsl::not_null<Progress*> const& progress,
    ApiFactory const& factory,
    gsl::not_null<Auth const*> const& auth,

    bool is_hermetic = true,
    int expected_queued = 0,
    int expected_cached = 0) {
    using path = std::filesystem::path;
    SetupConfig(repo_config);
    auto const main_cpp_desc =
        ArtifactDescription::CreateLocal(path{"data/hello_world/main.cpp"}, "");
    auto const& main_cpp_id = main_cpp_desc.Id();
    std::string const make_hello_id = "make_hello";
    auto* env_path = std::getenv("PATH");
    std::map<std::string, std::string> env{};
    if (env_path != nullptr) {
        env.emplace("PATH", env_path);
    }
    else {
        env.emplace("PATH", "/bin:/usr/bin");
    }
    auto const make_hello_desc = ActionDescription{
        {"out/hello_world"},
        {},
        Action{make_hello_id,
               {"c++", "src/main.cpp", "-o", "out/hello_world"},
               env},
        {{"src/main.cpp", main_cpp_desc}}};
    auto const exec_desc =
        ArtifactDescription::CreateAction(make_hello_id, "out/hello_world");
    auto const& exec_id = exec_desc.Id();

    DependencyGraph g;
    CHECK(g.AddAction(make_hello_desc));
    CHECK(g.ArtifactNodeWithId(exec_id)->HasBuilderAction());

    auto remote_config = TestRemoteConfig::ReadFromEnvironment();
    REQUIRE(remote_config);

    RetryConfig retry_config{};  // default retry config
    RemoteContext const remote_context{.auth = auth,
                                       .retry_config = &retry_config,
                                       .exec_config = &*remote_config};

    HashFunction const hash_function{Compatibility::IsCompatible()
                                         ? HashFunction::Type::PlainSHA256
                                         : HashFunction::Type::GitSHA1};

    auto api = factory();
    auto const apis = CreateTestApiBundle(hash_function, api);

    ExecutionContext const exec_context{.repo_config = repo_config,
                                        .apis = &apis,
                                        .remote_context = &remote_context,
                                        .statistics = stats,
                                        .progress = progress};
    Executor runner{&exec_context};

    // upload local artifacts
    auto const* main_cpp_node = g.ArtifactNodeWithId(main_cpp_id);
    CHECK(main_cpp_node != nullptr);
    CHECK(runner.Process(main_cpp_node));

    // process action
    CHECK(runner.Process(g.ArtifactNodeWithId(exec_id)->BuilderActionNode()));
    if (is_hermetic) {
        CHECK(stats->ActionsQueuedCounter() == expected_queued);
        CHECK(stats->ActionsCachedCounter() == expected_cached);
    }

    auto tmpdir = GetTestDir();

    // retrieve ALL artifacts
    REQUIRE(FileSystemManager::CreateDirectory(tmpdir));
    for (auto const& artifact_id : g.ArtifactIdentifiers()) {
        REQUIRE(g.ArtifactNodeWithId(artifact_id)->Content().Info());
        CHECK(api->RetrieveToPaths(
            {*g.ArtifactNodeWithId(artifact_id)->Content().Info()},
            {(tmpdir / "output").string()}));
        CHECK(FileSystemManager::IsFile(tmpdir / "output"));
        REQUIRE(FileSystemManager::RemoveFile(tmpdir / "output"));
    }
}

static inline void RunGreeterCompilation(
    RepositoryConfig* repo_config,
    gsl::not_null<Statistics*> const& stats,
    gsl::not_null<Progress*> const& progress,
    ApiFactory const& factory,
    gsl::not_null<Auth const*> const& auth,
    std::string const& greetcpp,
    bool is_hermetic = true,
    int expected_queued = 0,
    int expected_cached = 0) {
    using path = std::filesystem::path;
    SetupConfig(repo_config);
    auto const greet_hpp_desc =
        ArtifactDescription::CreateLocal(path{"data/greeter/greet.hpp"}, "");
    auto const& greet_hpp_id = greet_hpp_desc.Id();
    auto const greet_cpp_desc =
        ArtifactDescription::CreateLocal(path{"data/greeter"} / greetcpp, "");
    auto const& greet_cpp_id = greet_cpp_desc.Id();

    std::string const compile_greet_id = "compile_greet";
    auto* env_path = std::getenv("PATH");
    std::map<std::string, std::string> env{};
    if (env_path != nullptr) {
        env.emplace("PATH", env_path);
    }
    else {
        env.emplace("PATH", "/bin:/usr/bin");
    }
    auto const compile_greet_desc =
        ActionDescription{{"out/greet.o"},
                          {},
                          Action{compile_greet_id,
                                 {"c++",
                                  "-c",
                                  "src/greet.cpp",
                                  "-I",
                                  "include",
                                  "-o",
                                  "out/greet.o"},
                                 env},
                          {{"include/greet.hpp", greet_hpp_desc},
                           {"src/greet.cpp", greet_cpp_desc}}};

    auto const greet_o_desc =
        ArtifactDescription::CreateAction(compile_greet_id, "out/greet.o");
    auto const& greet_o_id = greet_o_desc.Id();

    std::string const make_lib_id = "make_lib";
    auto const make_lib_desc = ActionDescription{
        {"out/libgreet.a"},
        {},
        Action{make_lib_id, {"ar", "rcs", "out/libgreet.a", "greet.o"}, env},
        {{"greet.o", greet_o_desc}}};

    auto const main_cpp_desc =
        ArtifactDescription::CreateLocal(path{"data/greeter/main.cpp"}, "");
    auto const& main_cpp_id = main_cpp_desc.Id();

    auto const libgreet_desc =
        ArtifactDescription::CreateAction(make_lib_id, "out/libgreet.a");
    auto const& libgreet_id = libgreet_desc.Id();

    std::string const make_exe_id = "make_exe";
    auto const make_exe_desc =
        ActionDescription{{"out/greeter"},
                          {},
                          Action{make_exe_id,
                                 {"c++",
                                  "src/main.cpp",
                                  "-I",
                                  "include",
                                  "-L",
                                  "lib",
                                  "-lgreet",
                                  "-o",
                                  "out/greeter"},
                                 env},
                          {{"src/main.cpp", main_cpp_desc},
                           {"include/greet.hpp", greet_hpp_desc},
                           {"lib/libgreet.a", libgreet_desc}}};

    auto const exec_id =
        ArtifactDescription::CreateAction(make_exe_id, "out/greeter").Id();

    DependencyGraph g;
    CHECK(g.Add({compile_greet_desc, make_lib_desc, make_exe_desc}));

    auto remote_config = TestRemoteConfig::ReadFromEnvironment();
    REQUIRE(remote_config);

    RetryConfig retry_config{};  // default retry config
    RemoteContext const remote_context{.auth = auth,
                                       .retry_config = &retry_config,
                                       .exec_config = &*remote_config};

    HashFunction const hash_function{Compatibility::IsCompatible()
                                         ? HashFunction::Type::PlainSHA256
                                         : HashFunction::Type::GitSHA1};

    auto api = factory();
    auto const apis = CreateTestApiBundle(hash_function, api);

    ExecutionContext const exec_context{.repo_config = repo_config,
                                        .apis = &apis,
                                        .remote_context = &remote_context,
                                        .statistics = stats,
                                        .progress = progress};
    Executor runner{&exec_context};

    // upload local artifacts
    for (auto const& id : {greet_hpp_id, greet_cpp_id, main_cpp_id}) {
        auto const* node = g.ArtifactNodeWithId(id);
        CHECK(node != nullptr);
        CHECK(runner.Process(node));
    }

    // process actions
    CHECK(
        runner.Process(g.ArtifactNodeWithId(greet_o_id)->BuilderActionNode()));
    CHECK(
        runner.Process(g.ArtifactNodeWithId(libgreet_id)->BuilderActionNode()));
    CHECK(runner.Process(g.ArtifactNodeWithId(exec_id)->BuilderActionNode()));
    if (is_hermetic) {
        CHECK(stats->ActionsQueuedCounter() == expected_queued);
        CHECK(stats->ActionsCachedCounter() == expected_cached);
    }

    auto tmpdir = GetTestDir();

    // retrieve ALL artifacts
    REQUIRE(FileSystemManager::CreateDirectory(tmpdir));
    for (auto const& artifact_id : g.ArtifactIdentifiers()) {
        REQUIRE(g.ArtifactNodeWithId(artifact_id)->Content().Info());
        CHECK(api->RetrieveToPaths(
            {*g.ArtifactNodeWithId(artifact_id)->Content().Info()},
            {(tmpdir / "output").string()}));
        CHECK(FileSystemManager::IsFile(tmpdir / "output"));
        REQUIRE(FileSystemManager::RemoveFile(tmpdir / "output"));
    }
}

[[maybe_unused]] static void TestBlobUpload(RepositoryConfig* repo_config,
                                            ApiFactory const& factory) {
    SetupConfig(repo_config);
    // NOLINTNEXTLINE
    RunBlobUpload(repo_config, factory);
}

[[maybe_unused]] static void TestHelloWorldCompilation(
    RepositoryConfig* repo_config,
    gsl::not_null<Statistics*> const& stats,
    gsl::not_null<Progress*> const& progress,
    ApiFactory const& factory,
    gsl::not_null<Auth const*> const& auth,
    bool is_hermetic = true) {
    SetupConfig(repo_config);
    // expecting 1 action queued, 0 results from cache
    // NOLINTNEXTLINE
    RunHelloWorldCompilation(
        repo_config, stats, progress, factory, auth, is_hermetic, 1, 0);

    SECTION("Running same compilation again") {
        // expecting 2 actions queued, 1 result from cache
        // NOLINTNEXTLINE
        RunHelloWorldCompilation(
            repo_config, stats, progress, factory, auth, is_hermetic, 2, 1);
    }
}

[[maybe_unused]] static void TestGreeterCompilation(
    RepositoryConfig* repo_config,
    gsl::not_null<Statistics*> const& stats,
    gsl::not_null<Progress*> const& progress,
    ApiFactory const& factory,
    gsl::not_null<Auth const*> const& auth,
    bool is_hermetic = true) {
    SetupConfig(repo_config);
    // expecting 3 action queued, 0 results from cache
    // NOLINTNEXTLINE
    RunGreeterCompilation(repo_config,
                          stats,
                          progress,
                          factory,
                          auth,
                          "greet.cpp",
                          is_hermetic,
                          3,
                          0);

    SECTION("Running same compilation again") {
        // expecting 6 actions queued, 3 results from cache
        RunGreeterCompilation(repo_config,
                              stats,
                              progress,
                              factory,
                              auth,
                              "greet.cpp",
                              is_hermetic,
                              6,  // NOLINT
                              3);
    }

    SECTION("Running modified compilation") {
        // expecting 6 actions queued, 2 results from cache
        RunGreeterCompilation(repo_config,
                              stats,
                              progress,
                              factory,
                              auth,
                              "greet_mod.cpp",
                              is_hermetic,
                              6,  // NOLINT
                              2);
    }
}

static inline void TestUploadAndDownloadTrees(
    RepositoryConfig* repo_config,
    gsl::not_null<Statistics*> const& stats,
    gsl::not_null<Progress*> const& progress,
    ApiFactory const& factory,
    gsl::not_null<Auth const*> const& auth,
    bool /*is_hermetic*/ = true,
    int /*expected_queued*/ = 0,
    int /*expected_cached*/ = 0) {
    SetupConfig(repo_config);
    auto tmpdir = GetTestDir();
    auto* env_path = std::getenv("PATH");
    std::map<std::string, std::string> env{};
    if (env_path != nullptr) {
        env.emplace("PATH", env_path);
    }
    else {
        env.emplace("PATH", "/bin:/usr/bin");
    }

    HashFunction const hash_function{Compatibility::IsCompatible()
                                         ? HashFunction::Type::PlainSHA256
                                         : HashFunction::Type::GitSHA1};

    auto foo = std::string{"foo"};
    auto bar = std::string{"bar"};
    auto foo_digest =
        ArtifactDigest{hash_function.HashBlobData(foo).HexString(),
                       foo.size(),
                       /*is_tree=*/false};
    auto bar_digest =
        ArtifactDigest{hash_function.HashBlobData(bar).HexString(),
                       bar.size(),
                       /*is_tree=*/false};

    // upload blobs
    auto api = factory();
    REQUIRE(api->Upload(ArtifactBlobContainer{
        {ArtifactBlob{foo_digest, foo, /*is_exec=*/false},
         ArtifactBlob{bar_digest, bar, /*is_exec=*/false}}}));

    // define known artifacts
    auto foo_desc =
        ArtifactDescription::CreateKnown(foo_digest, ObjectType::File);
    auto bar_desc =
        ArtifactDescription::CreateKnown(bar_digest, ObjectType::Symlink);

    DependencyGraph g{};
    auto foo_id = g.AddArtifact(foo_desc);
    auto bar_id = g.AddArtifact(bar_desc);

    auto remote_config = TestRemoteConfig::ReadFromEnvironment();
    REQUIRE(remote_config);

    RetryConfig retry_config{};  // default retry config
    RemoteContext const remote_context{.auth = auth,
                                       .retry_config = &retry_config,
                                       .exec_config = &*remote_config};

    auto const apis = CreateTestApiBundle(hash_function, api);

    ExecutionContext const exec_context{.repo_config = repo_config,
                                        .apis = &apis,
                                        .remote_context = &remote_context,
                                        .statistics = stats,
                                        .progress = progress};
    Executor runner{&exec_context};

    REQUIRE(runner.Process(g.ArtifactNodeWithId(foo_id)));
    REQUIRE(runner.Process(g.ArtifactNodeWithId(bar_id)));

    SECTION("Simple tree") {
        auto tree_desc = Tree{{{"a", foo_desc}, {"b", bar_desc}}};
        auto tree_info = AddAndProcessTree(&g, &runner, tree_desc);
        REQUIRE(tree_info);
        CHECK(IsTreeObject(tree_info->type));

        tmpdir /= "simple";
        CHECK(api->RetrieveToPaths({*tree_info}, {tmpdir.string()}));
        CHECK(FileSystemManager::IsDirectory(tmpdir));
        CHECK(FileSystemManager::IsFile(tmpdir / "a"));
        CHECK(FileSystemManager::IsNonUpwardsSymlink(tmpdir / "b"));
        CHECK(*FileSystemManager::ReadFile(tmpdir / "a") == "foo");
        CHECK(*FileSystemManager::ReadSymlink(tmpdir / "b") == "bar");
        REQUIRE(FileSystemManager::RemoveDirectory(tmpdir, true));
    }

    SECTION("Subdir in tree path") {
        auto tree_desc = Tree{{{"a", foo_desc}, {"b/a", bar_desc}}};
        auto tree_info = AddAndProcessTree(&g, &runner, tree_desc);
        REQUIRE(tree_info);
        CHECK(IsTreeObject(tree_info->type));

        tmpdir /= "subdir";
        CHECK(api->RetrieveToPaths({*tree_info}, {tmpdir.string()}));
        CHECK(FileSystemManager::IsDirectory(tmpdir));
        CHECK(FileSystemManager::IsDirectory(tmpdir / "b"));
        CHECK(FileSystemManager::IsFile(tmpdir / "a"));
        CHECK(FileSystemManager::IsNonUpwardsSymlink(tmpdir / "b" / "a"));
        CHECK(*FileSystemManager::ReadFile(tmpdir / "a") == "foo");
        CHECK(*FileSystemManager::ReadSymlink(tmpdir / "b" / "a") == "bar");
        REQUIRE(FileSystemManager::RemoveDirectory(tmpdir, true));
    }

    SECTION("Nested trees") {
        auto tree_desc_nested = Tree{{{"a", bar_desc}}};
        auto tree_desc_parent =
            Tree{{{"a", foo_desc}, {"b", tree_desc_nested.Output()}}};

        REQUIRE(AddAndProcessTree(&g, &runner, tree_desc_nested));
        auto tree_info = AddAndProcessTree(&g, &runner, tree_desc_parent);
        REQUIRE(tree_info);
        CHECK(IsTreeObject(tree_info->type));

        tmpdir /= "nested";
        CHECK(api->RetrieveToPaths({*tree_info}, {tmpdir.string()}));
        CHECK(FileSystemManager::IsDirectory(tmpdir));
        CHECK(FileSystemManager::IsDirectory(tmpdir / "b"));
        CHECK(FileSystemManager::IsFile(tmpdir / "a"));
        CHECK(FileSystemManager::IsNonUpwardsSymlink(tmpdir / "b" / "a"));
        CHECK(*FileSystemManager::ReadFile(tmpdir / "a") == "foo");
        CHECK(*FileSystemManager::ReadSymlink(tmpdir / "b" / "a") == "bar");
        REQUIRE(FileSystemManager::RemoveDirectory(tmpdir, true));
    }

    SECTION("Dot-path tree as action input") {
        auto tree_desc = Tree{{{"a", foo_desc}, {"b/a", bar_desc}}};
        auto action_inputs =
            ActionDescription::inputs_t{{".", tree_desc.Output()}};
        ActionDescription action_desc{{"a", "b/a"},
                                      {},
                                      Action{"action_id", {"echo"}, env},
                                      action_inputs};

        REQUIRE(AddAndProcessTree(&g, &runner, tree_desc));
        REQUIRE(g.Add({action_desc}));
        auto const* action_node = g.ActionNodeWithId("action_id");
        REQUIRE(runner.Process(action_node));

        tmpdir /= "dotpath";
        std::vector<Artifact::ObjectInfo> infos{};
        std::vector<std::filesystem::path> paths{};
        for (auto const& [path, node] : action_node->OutputFiles()) {
            REQUIRE(node->Content().Info());
            paths.emplace_back(tmpdir / path);
            infos.emplace_back(*node->Content().Info());
        }

        CHECK(api->RetrieveToPaths(infos, paths));
        CHECK(FileSystemManager::IsDirectory(tmpdir));
        CHECK(FileSystemManager::IsDirectory(tmpdir / "b"));
        CHECK(FileSystemManager::IsFile(tmpdir / "a"));
        CHECK(FileSystemManager::IsNonUpwardsSymlink(tmpdir / "b" / "a"));
        CHECK(*FileSystemManager::ReadFile(tmpdir / "a") == "foo");
        CHECK(*FileSystemManager::ReadSymlink(tmpdir / "b" / "a") == "bar");
        REQUIRE(FileSystemManager::RemoveDirectory(tmpdir, true));
    }

    SECTION("Dot-path non-tree as action input") {
        auto action_inputs = ActionDescription::inputs_t{{".", foo_desc}};
        ActionDescription action_desc{
            {"foo"}, {}, Action{"action_id", {"echo"}, {}}, action_inputs};

        REQUIRE(g.Add({action_desc}));
        auto const* action_node = g.ActionNodeWithId("action_id");
        REQUIRE_FALSE(runner.Process(action_node));
    }
}

static inline void TestRetrieveOutputDirectories(
    RepositoryConfig* repo_config,
    gsl::not_null<Statistics*> const& stats,
    gsl::not_null<Progress*> const& progress,
    ApiFactory const& factory,
    gsl::not_null<Auth const*> const& auth,
    bool /*is_hermetic*/ = true,
    int /*expected_queued*/ = 0,
    int /*expected_cached*/ = 0) {
    SetupConfig(repo_config);
    auto tmpdir = GetTestDir();

    HashFunction const hash_function{Compatibility::IsCompatible()
                                         ? HashFunction::Type::PlainSHA256
                                         : HashFunction::Type::GitSHA1};

    auto const make_tree_id = std::string{"make_tree"};
    auto const* make_tree_cmd =
        "mkdir -p baz/baz/\n"
        "touch foo\n"
        "ln -s dummy bar\n"
        "touch baz/foo\n"
        "ln -s dummy baz/bar\n"
        "touch baz/baz/foo\n"
        "ln -s dummy baz/baz/bar";

    auto create_action = [&make_tree_id, make_tree_cmd](
                             std::vector<std::string>&& out_files,
                             std::vector<std::string>&& out_dirs) {
        auto* env_path = std::getenv("PATH");
        std::map<std::string, std::string> env{};
        if (env_path != nullptr) {
            env.emplace("PATH", env_path);
        }
        else {
            env.emplace("PATH", "/bin:/usr/bin");
        }
        return ActionDescription{
            std::move(out_files),
            std::move(out_dirs),
            Action{make_tree_id, {"sh", "-c", make_tree_cmd}, env},
            {}};
    };

    auto remote_config = TestRemoteConfig::ReadFromEnvironment();
    REQUIRE(remote_config);

    RetryConfig retry_config{};  // default retry config
    RemoteContext const remote_context{.auth = auth,
                                       .retry_config = &retry_config,
                                       .exec_config = &*remote_config};

    SECTION("entire action output as directory") {
        auto const make_tree_desc = create_action({}, {""});
        auto const root_desc =
            ArtifactDescription::CreateAction(make_tree_id, "");

        DependencyGraph g{};
        REQUIRE(g.AddAction(make_tree_desc));

        auto const* action = g.ActionNodeWithId(make_tree_id);
        REQUIRE_FALSE(action == nullptr);
        auto const* root = g.ArtifactNodeWithId(root_desc.Id());
        REQUIRE_FALSE(root == nullptr);

        // run action
        auto api = factory();
        auto const apis = CreateTestApiBundle(hash_function, api);

        ExecutionContext const exec_context{.repo_config = repo_config,
                                            .apis = &apis,
                                            .remote_context = &remote_context,
                                            .statistics = stats,
                                            .progress = progress};
        Executor runner{&exec_context};
        REQUIRE(runner.Process(action));

        // read output
        auto root_info = root->Content().Info();
        REQUIRE(root_info);
        CHECK(IsTreeObject(root_info->type));

        // retrieve ALL artifacts
        auto tmpdir = GetTestDir() / "entire_output";
        REQUIRE(FileSystemManager::CreateDirectory(tmpdir));

        REQUIRE(api->RetrieveToPaths({*root_info}, {tmpdir}));
        CHECK(FileSystemManager::IsFile(tmpdir / "foo"));
        CHECK(FileSystemManager::IsNonUpwardsSymlink(tmpdir / "bar"));
        CHECK(FileSystemManager::IsDirectory(tmpdir / "baz"));
        CHECK(FileSystemManager::IsFile(tmpdir / "baz" / "foo"));
        CHECK(FileSystemManager::IsNonUpwardsSymlink(tmpdir / "baz" / "bar"));
        CHECK(FileSystemManager::IsDirectory(tmpdir / "baz" / "baz"));
        CHECK(FileSystemManager::IsFile(tmpdir / "baz" / "baz" / "foo"));
        CHECK(FileSystemManager::IsNonUpwardsSymlink(tmpdir / "baz" / "baz" /
                                                     "bar"));
    }

    SECTION("disjoint files and directories") {
        auto const make_tree_desc = create_action({"foo", "bar"}, {"baz"});
        auto const foo_desc =
            ArtifactDescription::CreateAction(make_tree_id, "foo");
        auto const bar_desc =
            ArtifactDescription::CreateAction(make_tree_id, "bar");
        auto const baz_desc =
            ArtifactDescription::CreateAction(make_tree_id, "baz");

        DependencyGraph g{};
        REQUIRE(g.AddAction(make_tree_desc));

        auto const* action = g.ActionNodeWithId(make_tree_id);
        REQUIRE_FALSE(action == nullptr);
        auto const* foo = g.ArtifactNodeWithId(foo_desc.Id());
        REQUIRE_FALSE(foo == nullptr);
        auto const* bar = g.ArtifactNodeWithId(bar_desc.Id());
        REQUIRE_FALSE(bar == nullptr);
        auto const* baz = g.ArtifactNodeWithId(baz_desc.Id());
        REQUIRE_FALSE(baz == nullptr);

        // run action
        auto api = factory();
        auto const apis = CreateTestApiBundle(hash_function, api);

        ExecutionContext const exec_context{.repo_config = repo_config,
                                            .apis = &apis,
                                            .remote_context = &remote_context,
                                            .statistics = stats,
                                            .progress = progress};
        Executor runner{&exec_context};
        REQUIRE(runner.Process(action));

        // read output
        auto foo_info = foo->Content().Info();
        REQUIRE(foo_info);
        CHECK(IsFileObject(foo_info->type));

        auto bar_info = bar->Content().Info();
        REQUIRE(bar_info);
        CHECK(IsSymlinkObject(bar_info->type));

        auto baz_info = baz->Content().Info();
        REQUIRE(baz_info);
        CHECK(IsTreeObject(baz_info->type));

        // retrieve ALL artifacts
        auto tmpdir = GetTestDir() / "disjoint";
        REQUIRE(FileSystemManager::CreateDirectory(tmpdir));

        REQUIRE(api->RetrieveToPaths({*foo_info}, {tmpdir / "foo"}));
        CHECK(FileSystemManager::IsFile(tmpdir / "foo"));

        REQUIRE(api->RetrieveToPaths({*bar_info}, {tmpdir / "bar"}));
        CHECK(FileSystemManager::IsNonUpwardsSymlink(tmpdir / "bar"));

        REQUIRE(api->RetrieveToPaths({*baz_info}, {tmpdir / "baz"}));
        CHECK(FileSystemManager::IsDirectory(tmpdir / "baz"));
        CHECK(FileSystemManager::IsFile(tmpdir / "baz" / "foo"));
        CHECK(FileSystemManager::IsNonUpwardsSymlink(tmpdir / "baz" / "bar"));
        CHECK(FileSystemManager::IsDirectory(tmpdir / "baz" / "baz"));
        CHECK(FileSystemManager::IsFile(tmpdir / "baz" / "baz" / "foo"));
        CHECK(FileSystemManager::IsNonUpwardsSymlink(tmpdir / "baz" / "baz" /
                                                     "bar"));
    }

    SECTION("nested files and directories") {
        auto const make_tree_desc =
            create_action({"foo", "baz/bar"}, {"", "baz/baz"});
        auto const root_desc =
            ArtifactDescription::CreateAction(make_tree_id, "");
        auto const foo_desc =
            ArtifactDescription::CreateAction(make_tree_id, "foo");
        auto const bar_desc =
            ArtifactDescription::CreateAction(make_tree_id, "baz/bar");
        auto const baz_desc =
            ArtifactDescription::CreateAction(make_tree_id, "baz/baz");

        DependencyGraph g{};
        REQUIRE(g.AddAction(make_tree_desc));

        auto const* action = g.ActionNodeWithId(make_tree_id);
        REQUIRE_FALSE(action == nullptr);
        auto const* root = g.ArtifactNodeWithId(root_desc.Id());
        REQUIRE_FALSE(root == nullptr);
        auto const* foo = g.ArtifactNodeWithId(foo_desc.Id());
        REQUIRE_FALSE(foo == nullptr);
        auto const* bar = g.ArtifactNodeWithId(bar_desc.Id());
        REQUIRE_FALSE(bar == nullptr);
        auto const* baz = g.ArtifactNodeWithId(baz_desc.Id());
        REQUIRE_FALSE(baz == nullptr);

        // run action
        auto api = factory();
        auto const apis = CreateTestApiBundle(hash_function, api);

        ExecutionContext const exec_context{.repo_config = repo_config,
                                            .apis = &apis,
                                            .remote_context = &remote_context,
                                            .statistics = stats,
                                            .progress = progress};
        Executor runner{&exec_context};
        REQUIRE(runner.Process(action));

        // read output
        auto root_info = root->Content().Info();
        REQUIRE(root_info);
        CHECK(IsTreeObject(root_info->type));

        auto foo_info = foo->Content().Info();
        REQUIRE(foo_info);
        CHECK(IsFileObject(foo_info->type));

        auto bar_info = bar->Content().Info();
        REQUIRE(bar_info);
        CHECK(IsSymlinkObject(bar_info->type));

        auto baz_info = baz->Content().Info();
        REQUIRE(baz_info);
        CHECK(IsTreeObject(baz_info->type));

        // retrieve ALL artifacts
        auto tmpdir = GetTestDir() / "baz";
        REQUIRE(FileSystemManager::CreateDirectory(tmpdir));

        REQUIRE(api->RetrieveToPaths({*root_info}, {tmpdir / "root"}));
        CHECK(FileSystemManager::IsDirectory(tmpdir / "root"));
        CHECK(FileSystemManager::IsFile(tmpdir / "root" / "foo"));
        CHECK(FileSystemManager::IsNonUpwardsSymlink(tmpdir / "root" / "bar"));
        CHECK(FileSystemManager::IsDirectory(tmpdir / "root" / "baz"));
        CHECK(FileSystemManager::IsFile(tmpdir / "root" / "baz" / "foo"));
        CHECK(FileSystemManager::IsNonUpwardsSymlink(tmpdir / "root" / "baz" /
                                                     "bar"));
        CHECK(FileSystemManager::IsDirectory(tmpdir / "root" / "baz" / "baz"));
        CHECK(
            FileSystemManager::IsFile(tmpdir / "root" / "baz" / "baz" / "foo"));
        CHECK(FileSystemManager::IsNonUpwardsSymlink(tmpdir / "root" / "baz" /
                                                     "baz" / "bar"));

        REQUIRE(api->RetrieveToPaths({*foo_info}, {tmpdir / "foo"}));
        CHECK(FileSystemManager::IsFile(tmpdir / "foo"));

        REQUIRE(api->RetrieveToPaths({*bar_info}, {tmpdir / "bar"}));
        CHECK(FileSystemManager::IsNonUpwardsSymlink(tmpdir / "bar"));

        REQUIRE(api->RetrieveToPaths({*baz_info}, {tmpdir / "baz"}));
        CHECK(FileSystemManager::IsDirectory(tmpdir / "baz"));
        CHECK(FileSystemManager::IsFile(tmpdir / "baz" / "foo"));
        CHECK(FileSystemManager::IsNonUpwardsSymlink(tmpdir / "baz" / "bar"));
    }

    SECTION("non-existing outputs") {
        SECTION("non-existing file") {
            auto const make_tree_desc = create_action({"fool"}, {});
            auto const fool_desc =
                ArtifactDescription::CreateAction(make_tree_id, "fool");

            DependencyGraph g{};
            REQUIRE(g.AddAction(make_tree_desc));

            auto const* action = g.ActionNodeWithId(make_tree_id);
            REQUIRE_FALSE(action == nullptr);
            auto const* fool = g.ArtifactNodeWithId(fool_desc.Id());
            REQUIRE_FALSE(fool == nullptr);

            // run action
            auto api = factory();
            auto const apis = CreateTestApiBundle(hash_function, api);

            ExecutionContext const exec_context{
                .repo_config = repo_config,
                .apis = &apis,
                .remote_context = &remote_context,
                .statistics = stats,
                .progress = progress};
            Executor runner{&exec_context};
            CHECK_FALSE(runner.Process(action));
        }

        SECTION("non-existing directory") {
            auto const make_tree_desc = create_action({"bazel"}, {});
            auto const bazel_desc =
                ArtifactDescription::CreateAction(make_tree_id, "bazel");

            DependencyGraph g{};
            REQUIRE(g.AddAction(make_tree_desc));

            auto const* action = g.ActionNodeWithId(make_tree_id);
            REQUIRE_FALSE(action == nullptr);
            auto const* bazel = g.ArtifactNodeWithId(bazel_desc.Id());
            REQUIRE_FALSE(bazel == nullptr);

            // run action
            auto api = factory();
            auto const apis = CreateTestApiBundle(hash_function, api);

            ExecutionContext const exec_context{
                .repo_config = repo_config,
                .apis = &apis,
                .remote_context = &remote_context,
                .statistics = stats,
                .progress = progress};
            Executor runner{&exec_context};
            CHECK_FALSE(runner.Process(action));
        }
    }
}

#endif  // INCLUDED_SRC_TEST_BUILDTOOL_EXECUTION_ENGINE_EXECUTOR_EXECUTOR_API_TEST_HPP
