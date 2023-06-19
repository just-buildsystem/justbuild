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

#include <string>
#include <unordered_map>
#include <vector>

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/common/artifact_factory.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/execution_engine/executor/executor.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"

/// \brief Mockup API test config.
struct TestApiConfig {
    struct TestArtifactConfig {
        bool uploads{};
        bool available{};
    };

    struct TestExecutionConfig {
        bool failed{};
        std::vector<std::string> outputs{};
    };

    struct TestResponseConfig {
        bool cached{};
        int exit_code{};
    };

    std::unordered_map<std::string, TestArtifactConfig> artifacts{};
    TestExecutionConfig execution;
    TestResponseConfig response;
};

// forward declarations
class TestApi;
class TestAction;
class TestResponse;

/// \brief Mockup Response, stores only config and action result
class TestResponse : public IExecutionResponse {
    friend class TestAction;

  public:
    [[nodiscard]] auto Status() const noexcept -> StatusCode final {
        return StatusCode::Success;
    }
    [[nodiscard]] auto ExitCode() const noexcept -> int final {
        return config_.response.exit_code;
    }
    [[nodiscard]] auto IsCached() const noexcept -> bool final {
        return config_.response.cached;
    }
    [[nodiscard]] auto HasStdErr() const noexcept -> bool final { return true; }
    [[nodiscard]] auto HasStdOut() const noexcept -> bool final { return true; }
    [[nodiscard]] auto StdErr() noexcept -> std::string final { return {}; }
    [[nodiscard]] auto StdOut() noexcept -> std::string final { return {}; }
    [[nodiscard]] auto ActionDigest() const noexcept -> std::string final {
        return {};
    }
    [[nodiscard]] auto Artifacts() noexcept -> ArtifactInfos final {
        ArtifactInfos artifacts{};
        artifacts.reserve(config_.execution.outputs.size());

        // collect files and store them
        for (auto const& path : config_.execution.outputs) {
            try {
                artifacts.emplace(
                    path,
                    Artifact::ObjectInfo{
                        .digest = ArtifactDigest{path, 0, /*is_tree=*/false},
                        .type = ObjectType::File});
            } catch (...) {
                return {};
            }
        }

        return artifacts;
    }
    [[nodiscard]] auto ArtifactsWithDirSymlinks() noexcept
        -> std::pair<ArtifactInfos, DirSymlinks> final {
        return {};
    }

  private:
    TestApiConfig config_{};
    explicit TestResponse(TestApiConfig config) noexcept
        : config_{std::move(config)} {}
};

/// \brief Mockup Action, stores only config
class TestAction : public IExecutionAction {
    friend class TestApi;

  public:
    auto Execute(Logger const* /*unused*/) noexcept
        -> IExecutionResponse::Ptr final {
        if (config_.execution.failed) {
            return nullptr;
        }
        return IExecutionResponse::Ptr{new TestResponse{config_}};
    }
    void SetCacheFlag(CacheFlag /*unused*/) noexcept final {}
    void SetTimeout(std::chrono::milliseconds /*unused*/) noexcept final {}

  private:
    TestApiConfig config_{};
    explicit TestAction(TestApiConfig config) noexcept
        : config_{std::move(config)} {}
};

/// \brief Mockup Api, use config to create action and handle artifact upload
class TestApi : public IExecutionApi {
  public:
    explicit TestApi(TestApiConfig config) noexcept
        : config_{std::move(config)} {}

    auto CreateAction(
        ArtifactDigest const& /*unused*/,
        std::vector<std::string> const& /*unused*/,
        std::vector<std::string> const& /*unused*/,
        std::vector<std::string> const& /*unused*/,
        std::map<std::string, std::string> const& /*unused*/,
        std::map<std::string, std::string> const& /*unused*/) noexcept
        -> IExecutionAction::Ptr final {
        return IExecutionAction::Ptr{new TestAction(config_)};
    }
    auto RetrieveToPaths(std::vector<Artifact::ObjectInfo> const& /*unused*/,
                         std::vector<std::filesystem::path> const& /*unused*/,
                         IExecutionApi* /* unused */) noexcept -> bool final {
        return false;  // not needed by Executor
    }
    auto RetrieveToFds(std::vector<Artifact::ObjectInfo> const& /*unused*/,
                       std::vector<int> const& /*unused*/,
                       bool /*unused*/) noexcept -> bool final {
        return false;  // not needed by Executor
    }
    auto RetrieveToCas(std::vector<Artifact::ObjectInfo> const& unused,
                       gsl::not_null<IExecutionApi*> const& /*unused*/) noexcept
        -> bool final {
        // Note that a false-positive "free-nonheap-object" warning is thrown by
        // gcc 12.2 with GNU libstdc++, if the caller passes a temporary vector
        // that is not used by this function. Therefore, we explicitly use this
        // vector here to suppress this warning. The actual value returned is
        // irrelevant for testing though.
        return unused.empty();  // not needed by Executor
    }
    auto Upload(BlobContainer const& blobs, bool /*unused*/) noexcept
        -> bool final {
        return std::all_of(
            blobs.begin(), blobs.end(), [this](auto const& blob) {
                return config_.artifacts[blob.data]
                           .uploads  // for local artifacts
                       or config_.artifacts[blob.digest.hash()]
                              .uploads;  // for known and action artifacts
            });
    }
    auto UploadTree(
        std::vector<
            DependencyGraph::NamedArtifactNodePtr> const& /*unused*/) noexcept
        -> std::optional<ArtifactDigest> final {
        return ArtifactDigest{};  // not needed by Executor
    }
    [[nodiscard]] auto IsAvailable(ArtifactDigest const& digest) const noexcept
        -> bool final {
        try {
            return config_.artifacts.at(digest.hash()).available;
        } catch (std::exception const& /* unused */) {
            return false;
        }
    }
    [[nodiscard]] auto IsAvailable(std::vector<ArtifactDigest> const& digests)
        const noexcept -> std::vector<ArtifactDigest> final {
        std::vector<ArtifactDigest> result;
        try {
            for (auto const& digest : digests) {
                if (not config_.artifacts.at(digest.hash()).available) {
                    result.push_back(digest);
                }
            }
        } catch (std::exception const& /* unused */) {
            return result;
        }
        return result;
    }

  private:
    TestApiConfig config_{};
};

static void SetupConfig(std::filesystem::path const& ws) {
    auto info = RepositoryConfig::RepositoryInfo{FileRoot{ws}};
    RepositoryConfig::Instance().Reset();
    RepositoryConfig::Instance().SetInfo("", std::move(info));
}

[[nodiscard]] static auto CreateTest(gsl::not_null<DependencyGraph*> const& g,
                                     std::filesystem::path const& ws)
    -> TestApiConfig {
    using path = std::filesystem::path;
    SetupConfig(ws);

    auto const local_cpp_desc = ArtifactDescription{path{"local.cpp"}, ""};
    auto const known_cpp_desc = ArtifactDescription{
        ArtifactDigest{"known.cpp", 0, /*is_tree=*/false}, ObjectType::File};

    auto const test_action_desc = ActionDescription{
        {"output1.exe", "output2.exe"},
        {},
        Action{"test_action", {"cmd", "line"}, {}},
        {{"local.cpp", local_cpp_desc}, {"known.cpp", known_cpp_desc}}};

    CHECK(g->AddAction(test_action_desc));
    CHECK(FileSystemManager::WriteFile("local.cpp", ws / "local.cpp"));

    TestApiConfig config{};

    config.artifacts["local.cpp"].uploads = true;
    config.artifacts["known.cpp"].available = true;
    config.artifacts["output1.exe"].available = true;
    config.artifacts["output2.exe"].available = true;

    config.execution.failed = false;
    config.execution.outputs = {"output1.exe", "output2.exe"};

    config.response.cached = true;
    config.response.exit_code = 0;

    return config;
}

TEST_CASE("Executor: Process artifact", "[executor]") {
    std::filesystem::path workspace_path{
        "test/buildtool/execution_engine/executor"};
    DependencyGraph g;
    auto config = CreateTest(&g, workspace_path);

    auto const local_cpp_desc =
        ArtifactFactory::DescribeLocalArtifact("local.cpp", "");
    auto const local_cpp_id = ArtifactFactory::Identifier(local_cpp_desc);

    auto const known_cpp_desc = ArtifactFactory::DescribeKnownArtifact(
        "known.cpp", 0, ObjectType::File);
    auto const known_cpp_id = ArtifactFactory::Identifier(known_cpp_desc);

    SECTION("Processing succeeds for valid config") {
        auto api = TestApi::Ptr{new TestApi{config}};
        Executor runner{api.get(), api.get(), {}};

        CHECK(runner.Process(g.ArtifactNodeWithId(local_cpp_id)));
        CHECK(runner.Process(g.ArtifactNodeWithId(known_cpp_id)));
    }

    SECTION("Processing fails if uploading local artifact failed") {
        config.artifacts["local.cpp"].uploads = false;

        auto api = TestApi::Ptr{new TestApi{config}};
        Executor runner{api.get(), api.get(), {}};

        CHECK(not runner.Process(g.ArtifactNodeWithId(local_cpp_id)));
        CHECK(runner.Process(g.ArtifactNodeWithId(known_cpp_id)));
    }

    SECTION("Processing fails if known artifact is not available") {
        config.artifacts["known.cpp"].available = false;

        auto api = TestApi::Ptr{new TestApi{config}};
        Executor runner{api.get(), api.get(), {}};

        CHECK(runner.Process(g.ArtifactNodeWithId(local_cpp_id)));
        CHECK(not runner.Process(g.ArtifactNodeWithId(known_cpp_id)));
    }
}

TEST_CASE("Executor: Process action", "[executor]") {
    std::filesystem::path workspace_path{
        "test/buildtool/execution_engine/executor"};

    DependencyGraph g;
    auto config = CreateTest(&g, workspace_path);

    auto const local_cpp_desc =
        ArtifactFactory::DescribeLocalArtifact("local.cpp", "");
    auto const local_cpp_id = ArtifactFactory::Identifier(local_cpp_desc);

    auto const known_cpp_desc = ArtifactFactory::DescribeKnownArtifact(
        "known.cpp", 0, ObjectType::File);
    auto const known_cpp_id = ArtifactFactory::Identifier(known_cpp_desc);

    ActionIdentifier action_id{"test_action"};
    auto const output1_desc =
        ArtifactFactory::DescribeActionArtifact(action_id, "output1.exe");
    auto const output1_id = ArtifactFactory::Identifier(output1_desc);

    auto const output2_desc =
        ArtifactFactory::DescribeActionArtifact(action_id, "output2.exe");
    auto const output2_id = ArtifactFactory::Identifier(output2_desc);

    SECTION("Processing succeeds for valid config") {
        auto api = TestApi::Ptr{new TestApi{config}};
        Executor runner{api.get(), api.get(), {}};

        CHECK(runner.Process(g.ArtifactNodeWithId(local_cpp_id)));
        CHECK(runner.Process(g.ArtifactNodeWithId(known_cpp_id)));
        CHECK(runner.Process(g.ActionNodeWithId(action_id)));
        CHECK(runner.Process(g.ArtifactNodeWithId(output1_id)));
        CHECK(runner.Process(g.ArtifactNodeWithId(output2_id)));
    }

    SECTION("Processing succeeds even if result was is not cached") {
        config.response.cached = false;

        auto api = TestApi::Ptr{new TestApi{config}};
        Executor runner{api.get(), api.get(), {}};

        CHECK(runner.Process(g.ArtifactNodeWithId(local_cpp_id)));
        CHECK(runner.Process(g.ArtifactNodeWithId(known_cpp_id)));
        CHECK(runner.Process(g.ActionNodeWithId(action_id)));
        CHECK(runner.Process(g.ArtifactNodeWithId(output1_id)));
        CHECK(runner.Process(g.ArtifactNodeWithId(output2_id)));
    }

    SECTION("Processing succeeds even if output is not available in CAS") {
        config.artifacts["output2.exe"].available = false;

        auto api = TestApi::Ptr{new TestApi{config}};
        Executor runner{api.get(), api.get(), {}};

        CHECK(runner.Process(g.ArtifactNodeWithId(local_cpp_id)));
        CHECK(runner.Process(g.ArtifactNodeWithId(known_cpp_id)));
        CHECK(runner.Process(g.ActionNodeWithId(action_id)));

        // Note: Both output digests should be created via SaveDigests(),
        // but processing output2.exe fails as it is not available in CAS.
        CHECK(runner.Process(g.ArtifactNodeWithId(output1_id)));
        CHECK(not runner.Process(g.ArtifactNodeWithId(output2_id)));
    }

    SECTION("Processing fails if execution failed") {
        config.execution.failed = true;

        auto api = TestApi::Ptr{new TestApi{config}};
        Executor runner{api.get(), api.get(), {}};

        CHECK(runner.Process(g.ArtifactNodeWithId(local_cpp_id)));
        CHECK(runner.Process(g.ArtifactNodeWithId(known_cpp_id)));
        CHECK(not runner.Process(g.ActionNodeWithId(action_id)));
        CHECK(not runner.Process(g.ArtifactNodeWithId(output1_id)));
        CHECK(not runner.Process(g.ArtifactNodeWithId(output2_id)));
    }

    SECTION("Processing fails if exit code is non-zero") {
        config.response.exit_code = 1;

        auto api = TestApi::Ptr{new TestApi{config}};
        Executor runner{api.get(), api.get(), {}};

        CHECK(runner.Process(g.ArtifactNodeWithId(local_cpp_id)));
        CHECK(runner.Process(g.ArtifactNodeWithId(known_cpp_id)));
        CHECK(not runner.Process(g.ActionNodeWithId(action_id)));

        // Note: Both output digests should be missing as SaveDigests() for
        // both is only called if processing action succeeds.
        CHECK(not runner.Process(g.ArtifactNodeWithId(output1_id)));
        CHECK(not runner.Process(g.ArtifactNodeWithId(output2_id)));
    }

    SECTION("Processing fails if any output is missing") {
        config.execution.outputs = {"output1.exe" /*, "output2.exe"*/};

        auto api = TestApi::Ptr{new TestApi{config}};
        Executor runner{api.get(), api.get(), {}};

        CHECK(runner.Process(g.ArtifactNodeWithId(local_cpp_id)));
        CHECK(runner.Process(g.ArtifactNodeWithId(known_cpp_id)));
        CHECK(not runner.Process(g.ActionNodeWithId(action_id)));

        // Note: Both output digests should be missing as SaveDigests() for
        // both is only called if processing action succeeds.
        CHECK(not runner.Process(g.ArtifactNodeWithId(output1_id)));
        CHECK(not runner.Process(g.ArtifactNodeWithId(output2_id)));
    }
}
