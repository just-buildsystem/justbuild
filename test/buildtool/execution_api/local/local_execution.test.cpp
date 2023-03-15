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

#include <chrono>
#include <string>
#include <vector>

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/common/artifact_factory.hpp"
#include "src/buildtool/execution_api/local/local_api.hpp"
#include "test/utils/hermeticity/local.hpp"

namespace {

[[nodiscard]] auto GetTestDir() -> std::filesystem::path {
    auto* tmp_dir = std::getenv("TEST_TMPDIR");
    if (tmp_dir != nullptr) {
        return tmp_dir;
    }
    return FileSystemManager::GetCurrentDirectory() /
           "test/buildtool/execution_api/local";
}

}  // namespace

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalExecution: No input, no output",
                 "[execution_api]") {
    auto api = LocalApi{};

    std::string test_content("test");
    std::vector<std::string> const cmdline = {"echo", "-n", test_content};
    auto action =
        api.CreateAction(*api.UploadTree({}), cmdline, {}, {}, {}, {});
    REQUIRE(action);

    SECTION("Cache execution result in action cache") {
        // run execution
        action->SetCacheFlag(IExecutionAction::CacheFlag::CacheOutput);
        auto output = action->Execute(nullptr);
        REQUIRE(output);

        // verify result
        CHECK_FALSE(output->IsCached());
        CHECK(output->StdOut() == test_content);

        output = action->Execute(nullptr);
        REQUIRE(output);
        CHECK(output->IsCached());
    }

    SECTION("Do not cache execution result in action cache") {
        // run execution
        action->SetCacheFlag(IExecutionAction::CacheFlag::DoNotCacheOutput);
        auto output = action->Execute(nullptr);
        REQUIRE(output);

        // verify result
        CHECK_FALSE(output->IsCached());
        CHECK(output->StdOut() == test_content);

        // ensure result IS STILL NOT in cache
        output = action->Execute(nullptr);
        REQUIRE(output);
        CHECK_FALSE(output->IsCached());
    }
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalExecution: No input, no output, env variables used",
                 "[execution_api]") {
    auto api = LocalApi{};

    std::string test_content("test from env var");
    std::vector<std::string> const cmdline = {
        "/bin/sh", "-c", "set -e\necho -n ${MYCONTENT}"};
    auto action = api.CreateAction(*api.UploadTree({}),
                                   cmdline,
                                   {},
                                   {},
                                   {{"MYCONTENT", test_content}},
                                   {});
    REQUIRE(action);

    SECTION("Cache execution result in action cache") {
        // run execution
        action->SetCacheFlag(IExecutionAction::CacheFlag::CacheOutput);
        auto output = action->Execute(nullptr);
        REQUIRE(output);

        // verify result
        CHECK_FALSE(output->IsCached());
        CHECK(output->StdOut() == test_content);

        // ensure result IS in cache
        output = action->Execute(nullptr);
        REQUIRE(output);
        CHECK(output->IsCached());
    }

    SECTION("Do not cache execution result in action cache") {
        // run execution
        action->SetCacheFlag(IExecutionAction::CacheFlag::DoNotCacheOutput);
        auto output = action->Execute(nullptr);
        REQUIRE(output);

        // verify result
        CHECK_FALSE(output->IsCached());
        CHECK(output->StdOut() == test_content);

        // ensure result IS STILL NOT in cache
        output = action->Execute(nullptr);
        REQUIRE(output);
        CHECK_FALSE(output->IsCached());
    }
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalExecution: No input, create output",
                 "[execution_api]") {
    auto api = LocalApi{};

    std::string test_content("test");
    auto test_digest = ArtifactDigest::Create<ObjectType::File>(test_content);

    std::string output_path{"output_file"};
    std::vector<std::string> const cmdline = {
        "/bin/sh",
        "-c",
        "set -e\necho -n " + test_content + " > " + output_path};

    auto action = api.CreateAction(
        *api.UploadTree({}), cmdline, {output_path}, {}, {}, {});
    REQUIRE(action);

    SECTION("Cache execution result in action cache") {
        // run execution
        action->SetCacheFlag(IExecutionAction::CacheFlag::CacheOutput);
        auto output = action->Execute(nullptr);
        REQUIRE(output);

        // verify result
        CHECK_FALSE(output->IsCached());
        auto artifacts = output->Artifacts();
        REQUIRE(artifacts.contains(output_path));
        CHECK(artifacts.at(output_path).digest == test_digest);

        // ensure result IS in cache
        output = action->Execute(nullptr);
        REQUIRE(output);
        CHECK(output->IsCached());
    }

    SECTION("Do not cache execution result in action cache") {
        // run execution
        action->SetCacheFlag(IExecutionAction::CacheFlag::DoNotCacheOutput);
        auto output = action->Execute(nullptr);
        REQUIRE(output);

        // verify result
        CHECK_FALSE(output->IsCached());
        auto artifacts = output->Artifacts();
        REQUIRE(artifacts.contains(output_path));
        CHECK(artifacts.at(output_path).digest == test_digest);

        // ensure result IS STILL NOT in cache
        output = action->Execute(nullptr);
        REQUIRE(output);
        CHECK_FALSE(output->IsCached());
    }
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalExecution: One input copied to output",
                 "[execution_api]") {
    auto api = LocalApi{};

    std::string test_content("test");
    auto test_digest = ArtifactDigest::Create<ObjectType::File>(test_content);
    REQUIRE(api.Upload(BlobContainer{{BazelBlob{
                           test_digest, test_content, /*is_exec=*/false}}},
                       false));

    std::string input_path{"dir/subdir/input"};
    std::string output_path{"output_file"};

    std::vector<std::string> const cmdline = {"cp", input_path, output_path};

    auto local_artifact_opt =
        ArtifactFactory::FromDescription(ArtifactFactory::DescribeKnownArtifact(
            test_digest.hash(), test_digest.size(), ObjectType::File));
    REQUIRE(local_artifact_opt);
    auto local_artifact =
        DependencyGraph::ArtifactNode{std::move(*local_artifact_opt)};

    auto action =
        api.CreateAction(*api.UploadTree({{input_path, &local_artifact}}),
                         cmdline,
                         {output_path},
                         {},
                         {},
                         {});
    REQUIRE(action);

    SECTION("Cache execution result in action cache") {
        // run execution
        action->SetCacheFlag(IExecutionAction::CacheFlag::CacheOutput);
        auto output = action->Execute(nullptr);
        REQUIRE(output);

        // verify result
        CHECK_FALSE(output->IsCached());
        auto artifacts = output->Artifacts();
        REQUIRE(artifacts.contains(output_path));
        CHECK(artifacts.at(output_path).digest == test_digest);

        // ensure result IS in cache
        output = action->Execute(nullptr);
        REQUIRE(output);
        CHECK(output->IsCached());
    }

    SECTION("Do not cache execution result in action cache") {
        // run execution
        action->SetCacheFlag(IExecutionAction::CacheFlag::DoNotCacheOutput);
        auto output = action->Execute(nullptr);
        REQUIRE(output);

        // verify result
        CHECK_FALSE(output->IsCached());
        auto artifacts = output->Artifacts();
        REQUIRE(artifacts.contains(output_path));
        CHECK(artifacts.at(output_path).digest == test_digest);

        // ensure result IS STILL NOT in cache
        output = action->Execute(nullptr);
        REQUIRE(output);
        CHECK_FALSE(output->IsCached());
    }
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalExecution: Cache failed action's result",
                 "[execution_api]") {
    auto api = LocalApi{};

    auto flag = GetTestDir() / "flag";
    std::vector<std::string> const cmdline = {
        "sh", "-c", fmt::format("[ -f '{}' ]", flag.string())};

    auto action =
        api.CreateAction(*api.UploadTree({}), cmdline, {}, {}, {}, {});
    REQUIRE(action);

    action->SetCacheFlag(IExecutionAction::CacheFlag::CacheOutput);

    // run failed action
    auto failed = action->Execute(nullptr);
    REQUIRE(failed);
    CHECK_FALSE(failed->IsCached());
    CHECK(failed->ExitCode() != 0);

    REQUIRE(FileSystemManager::CreateFile(flag));

    // run success action (should rerun and overwrite)
    auto success = action->Execute(nullptr);
    REQUIRE(success);
    CHECK_FALSE(success->IsCached());
    CHECK(success->ExitCode() == 0);

    // rerun success action (should be served from cache)
    auto cached = action->Execute(nullptr);
    REQUIRE(cached);
    CHECK(cached->IsCached());
    CHECK(cached->ExitCode() == 0);

    CHECK(FileSystemManager::RemoveFile(flag));
}
