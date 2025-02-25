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

#include "src/buildtool/execution_engine/executor/executor.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "catch2/catch_test_macros.hpp"
#include "gsl/gsl"
#include "src/buildtool/auth/authentication.hpp"
#include "src/buildtool/common/action.hpp"
#include "src/buildtool/common/action_description.hpp"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_blob.hpp"
#include "src/buildtool/common/artifact_description.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/identifier.hpp"
#include "src/buildtool/common/remote/remote_common.hpp"
#include "src/buildtool/common/remote/retry_config.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/common/statistics.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/common/execution_action.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/execution_api/common/execution_response.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/execution_api/remote/context.hpp"
#include "src/buildtool/execution_engine/dag/dag.hpp"
#include "src/buildtool/execution_engine/executor/context.hpp"
#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/progress_reporting/progress.hpp"
#include "src/utils/cpp/expected.hpp"
#include "test/utils/executor/test_api_bundle.hpp"
#include "test/utils/hermeticity/test_hash_function_type.hpp"

/// \brief Mockup API test config.
struct TestApiConfig {
    struct TestArtifactConfig {
        bool uploads{};
        bool available{};
    };

    struct TestExecutionConfig {
        bool failed{};
        std::vector<std::string> outputs;
    };

    struct TestResponseConfig {
        bool cached{};
        int exit_code{};
    };

    std::unordered_map<std::string, TestArtifactConfig> artifacts;
    TestExecutionConfig execution;
    TestResponseConfig response;
};

static auto NamedDigest(std::string const& str) -> ArtifactDigest {
    HashFunction const hash_function{TestHashType::ReadFromEnvironment()};
    return ArtifactDigestFactory::HashDataAs<ObjectType::File>(hash_function,
                                                               str);
}

/// \brief Mockup Response, stores only config and action result
class TestResponse : public IExecutionResponse {
  public:
    explicit TestResponse(TestApiConfig config) noexcept
        : config_{std::move(config)} {}

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
    [[nodiscard]] auto ActionDigest() const noexcept
        -> std::string const& final {
        static const std::string kEmptyHash;
        return kEmptyHash;
    }
    [[nodiscard]] auto Artifacts() noexcept
        -> expected<gsl::not_null<ArtifactInfos const*>, std::string> final {
        if (not populated_) {
            Populate();
        }
        return gsl::not_null<ArtifactInfos const*>(&artifacts_);
    }
    [[nodiscard]] auto DirectorySymlinks() noexcept
        -> expected<gsl::not_null<DirSymlinks const*>, std::string> final {
        static const DirSymlinks kEmptySymlinks{};
        return gsl::not_null<DirSymlinks const*>(&kEmptySymlinks);
    }

  private:
    TestApiConfig config_{};
    ArtifactInfos artifacts_;
    bool populated_ = false;

    void Populate() noexcept {
        if (populated_) {
            return;
        }
        populated_ = true;

        ArtifactInfos artifacts{};
        artifacts.reserve(config_.execution.outputs.size());

        // collect files and store them
        for (auto const& path : config_.execution.outputs) {
            try {
                artifacts.emplace(
                    path,
                    Artifact::ObjectInfo{.digest = NamedDigest(path),
                                         .type = ObjectType::File});
            } catch (...) {
                return;
            }
        }
        artifacts_ = std::move(artifacts);
    }
};

/// \brief Mockup Action, stores only config
class TestAction : public IExecutionAction {
  public:
    explicit TestAction(TestApiConfig config) noexcept
        : config_{std::move(config)} {}

    auto Execute(Logger const* /*unused*/) noexcept
        -> IExecutionResponse::Ptr final {
        if (config_.execution.failed) {
            return nullptr;
        }
        return std::make_unique<TestResponse>(config_);
    }
    void SetCacheFlag(CacheFlag /*unused*/) noexcept final {}
    void SetTimeout(std::chrono::milliseconds /*unused*/) noexcept final {}

  private:
    TestApiConfig config_{};
};

/// \brief Mockup Api, use config to create action and handle artifact upload
class TestApi : public IExecutionApi {
  public:
    explicit TestApi(TestApiConfig config,
                     HashFunction::Type hash_type) noexcept
        : config_{std::move(config)}, hash_type_{hash_type} {}

    [[nodiscard]] auto CreateAction(
        ArtifactDigest const& /*unused*/,
        std::vector<std::string> const& /*unused*/,
        std::string const& /*unused*/,
        std::vector<std::string> const& /*unused*/,
        std::vector<std::string> const& /*unused*/,
        std::map<std::string, std::string> const& /*unused*/,
        std::map<std::string, std::string> const& /*unused*/) const noexcept
        -> IExecutionAction::Ptr final {
        return std::make_unique<TestAction>(config_);
    }
    [[nodiscard]] auto RetrieveToPaths(
        std::vector<Artifact::ObjectInfo> const& /*unused*/,
        std::vector<std::filesystem::path> const& /*unused*/,
        IExecutionApi const* /* unused */) const noexcept -> bool final {
        return false;  // not needed by Executor
    }
    [[nodiscard]] auto RetrieveToFds(
        std::vector<Artifact::ObjectInfo> const& /*unused*/,
        std::vector<int> const& /*unused*/,
        bool /*unused*/,
        IExecutionApi const* /*unused*/) const noexcept -> bool final {
        return false;  // not needed by Executor
    }
    [[nodiscard]] auto RetrieveToCas(
        std::vector<Artifact::ObjectInfo> const& unused,
        IExecutionApi const& /*unused*/) const noexcept -> bool final {
        // Note that a false-positive "free-nonheap-object" warning is thrown by
        // gcc 12.2 with GNU libstdc++, if the caller passes a temporary vector
        // that is not used by this function. Therefore, we explicitly use this
        // vector here to suppress this warning. The actual value returned is
        // irrelevant for testing though.
        return unused.empty();  // not needed by Executor
    }
    [[nodiscard]] auto RetrieveToMemory(
        Artifact::ObjectInfo const& /*artifact_info*/) const noexcept
        -> std::optional<std::string> override {
        return std::nullopt;  // not needed by Executor
    }
    [[nodiscard]] auto Upload(std::unordered_set<ArtifactBlob>&& blobs,
                              bool /*unused*/) const noexcept -> bool final {
        return std::all_of(
            blobs.begin(), blobs.end(), [this](auto const& blob) {
                // for local artifacts
                auto const content = blob.ReadContent();
                if (content == nullptr) {
                    return false;
                }
                auto it1 = config_.artifacts.find(*content);
                if (it1 != config_.artifacts.end() and it1->second.uploads) {
                    return true;
                }
                // for known and action artifacts
                auto it2 = config_.artifacts.find(blob.GetDigest().hash());
                return it2 != config_.artifacts.end() and it2->second.uploads;
            });
    }
    [[nodiscard]] auto UploadTree(
        std::vector<DependencyGraph::NamedArtifactNodePtr> const& /*unused*/)
        const noexcept -> std::optional<ArtifactDigest> final {
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
    [[nodiscard]] auto GetMissingDigests(
        std::unordered_set<ArtifactDigest> const& digests) const noexcept
        -> std::unordered_set<ArtifactDigest> final {
        std::unordered_set<ArtifactDigest> result;
        try {
            for (auto const& digest : digests) {
                if (not config_.artifacts.at(digest.hash()).available) {
                    result.emplace(digest);
                }
            }
        } catch (std::exception const& /* unused */) {
            return result;
        }
        return result;
    }

    [[nodiscard]] auto GetHashType() const noexcept
        -> HashFunction::Type final {
        return hash_type_;
    }

  private:
    TestApiConfig config_{};
    HashFunction::Type hash_type_;
};

[[nodiscard]] auto SetupConfig(std::filesystem::path const& ws)
    -> RepositoryConfig {
    auto info = RepositoryConfig::RepositoryInfo{FileRoot{ws}};
    RepositoryConfig repo_config{};
    repo_config.SetInfo("", std::move(info));
    return repo_config;
}

[[nodiscard]] static auto CreateTest(gsl::not_null<DependencyGraph*> const& g,
                                     std::filesystem::path const& ws)
    -> std::pair<TestApiConfig, RepositoryConfig> {
    using path = std::filesystem::path;

    auto const local_cpp_desc =
        ArtifactDescription::CreateLocal(path{"local.cpp"}, "");
    auto const known_digest = NamedDigest("known.cpp");
    auto const known_cpp_desc =
        ArtifactDescription::CreateKnown(known_digest, ObjectType::File);

    auto const test_action_desc = ActionDescription{
        {"output1.exe", "output2.exe"},
        {},
        Action{"test_action", {"cmd", "line"}, {}},
        {{"local.cpp", local_cpp_desc}, {known_digest.hash(), known_cpp_desc}}};

    CHECK(g->AddAction(test_action_desc));
    CHECK(FileSystemManager::WriteFile("local.cpp", ws / "local.cpp"));

    TestApiConfig config{};

    config.artifacts[NamedDigest("local.cpp").hash()].uploads = true;
    config.artifacts[NamedDigest("known.cpp").hash()].available = true;
    config.artifacts[NamedDigest("output1.exe").hash()].available = true;
    config.artifacts[NamedDigest("output2.exe").hash()].available = true;

    config.execution.failed = false;
    config.execution.outputs = {"output1.exe", "output2.exe"};

    config.response.cached = true;
    config.response.exit_code = 0;

    return std::make_pair(config, SetupConfig(ws));
}

TEST_CASE("Executor: Process artifact", "[executor]") {
    std::filesystem::path workspace_path{
        "test/buildtool/execution_engine/executor"};
    DependencyGraph g;
    auto [config, repo_config] = CreateTest(&g, workspace_path);

    HashFunction const hash_function{TestHashType::ReadFromEnvironment()};

    auto const local_cpp_id =
        ArtifactDescription::CreateLocal("local.cpp", "").Id();

    auto const known_cpp_id = ArtifactDescription::CreateKnown(
                                  NamedDigest("known.cpp"), ObjectType::File)
                                  .Id();

    Auth auth{};
    RetryConfig retry_config{};             // default retry config
    RemoteExecutionConfig remote_config{};  // default remote config
    RemoteContext const remote_context{.auth = &auth,
                                       .retry_config = &retry_config,
                                       .exec_config = &remote_config};

    SECTION("Processing succeeds for valid config") {
        auto api = std::make_shared<TestApi>(config, hash_function.GetType());
        Statistics stats{};
        Progress progress{};
        auto const apis = CreateTestApiBundle(api);
        ExecutionContext const exec_context{.repo_config = &repo_config,
                                            .apis = &apis,
                                            .remote_context = &remote_context,
                                            .statistics = &stats,
                                            .progress = &progress};
        Executor runner{&exec_context};

        CHECK(runner.Process(g.ArtifactNodeWithId(local_cpp_id)));
        CHECK(runner.Process(g.ArtifactNodeWithId(known_cpp_id)));
    }

    SECTION("Processing fails if uploading local artifact failed") {
        config.artifacts[NamedDigest("local.cpp").hash()].uploads = false;

        auto api = std::make_shared<TestApi>(config, hash_function.GetType());
        Statistics stats{};
        Progress progress{};
        auto const apis = CreateTestApiBundle(api);
        ExecutionContext const exec_context{.repo_config = &repo_config,
                                            .apis = &apis,
                                            .remote_context = &remote_context,
                                            .statistics = &stats,
                                            .progress = &progress};
        Executor runner{&exec_context};

        CHECK(not runner.Process(g.ArtifactNodeWithId(local_cpp_id)));
        CHECK(runner.Process(g.ArtifactNodeWithId(known_cpp_id)));
    }

    SECTION("Processing fails if known artifact is not available") {
        config.artifacts[NamedDigest("known.cpp").hash()].available = false;

        auto api = std::make_shared<TestApi>(config, hash_function.GetType());
        Statistics stats{};
        Progress progress{};
        auto const apis = CreateTestApiBundle(api);
        ExecutionContext const exec_context{.repo_config = &repo_config,
                                            .apis = &apis,
                                            .remote_context = &remote_context,
                                            .statistics = &stats,
                                            .progress = &progress};
        Executor runner{&exec_context};

        CHECK(runner.Process(g.ArtifactNodeWithId(local_cpp_id)));
        CHECK(not runner.Process(g.ArtifactNodeWithId(known_cpp_id)));
    }
}

TEST_CASE("Executor: Process action", "[executor]") {
    std::filesystem::path workspace_path{
        "test/buildtool/execution_engine/executor"};

    DependencyGraph g;
    auto [config, repo_config] = CreateTest(&g, workspace_path);

    HashFunction const hash_function{TestHashType::ReadFromEnvironment()};

    auto const local_cpp_id =
        ArtifactDescription::CreateLocal("local.cpp", "").Id();

    auto const known_cpp_id = ArtifactDescription::CreateKnown(
                                  NamedDigest("known.cpp"), ObjectType::File)
                                  .Id();

    ActionIdentifier const action_id{"test_action"};
    auto const output1_id =
        ArtifactDescription::CreateAction(action_id, "output1.exe").Id();

    auto const output2_id =
        ArtifactDescription::CreateAction(action_id, "output2.exe").Id();

    Auth auth{};
    RetryConfig retry_config{};             // default retry config
    RemoteExecutionConfig remote_config{};  // default remote config
    RemoteContext const remote_context{.auth = &auth,
                                       .retry_config = &retry_config,
                                       .exec_config = &remote_config};

    SECTION("Processing succeeds for valid config") {
        auto api = std::make_shared<TestApi>(config, hash_function.GetType());
        Statistics stats{};
        Progress progress{};
        auto const apis = CreateTestApiBundle(api);
        ExecutionContext const exec_context{.repo_config = &repo_config,
                                            .apis = &apis,
                                            .remote_context = &remote_context,
                                            .statistics = &stats,
                                            .progress = &progress};
        Executor runner{&exec_context};

        CHECK(runner.Process(g.ArtifactNodeWithId(local_cpp_id)));
        CHECK(runner.Process(g.ArtifactNodeWithId(known_cpp_id)));
        CHECK(runner.Process(g.ActionNodeWithId(action_id)));
        CHECK(runner.Process(g.ArtifactNodeWithId(output1_id)));
        CHECK(runner.Process(g.ArtifactNodeWithId(output2_id)));
    }

    SECTION("Processing succeeds even if result was is not cached") {
        config.response.cached = false;

        auto api = std::make_shared<TestApi>(config, hash_function.GetType());
        Statistics stats{};
        Progress progress{};
        auto const apis = CreateTestApiBundle(api);
        ExecutionContext const exec_context{.repo_config = &repo_config,
                                            .apis = &apis,
                                            .remote_context = &remote_context,
                                            .statistics = &stats,
                                            .progress = &progress};
        Executor runner{&exec_context};

        CHECK(runner.Process(g.ArtifactNodeWithId(local_cpp_id)));
        CHECK(runner.Process(g.ArtifactNodeWithId(known_cpp_id)));
        CHECK(runner.Process(g.ActionNodeWithId(action_id)));
        CHECK(runner.Process(g.ArtifactNodeWithId(output1_id)));
        CHECK(runner.Process(g.ArtifactNodeWithId(output2_id)));
    }

    SECTION("Processing succeeds even if output is not available in CAS") {
        config.artifacts[NamedDigest("output2.exe").hash()].available = false;

        auto api = std::make_shared<TestApi>(config, hash_function.GetType());
        Statistics stats{};
        Progress progress{};
        auto const apis = CreateTestApiBundle(api);
        ExecutionContext const exec_context{.repo_config = &repo_config,
                                            .apis = &apis,
                                            .remote_context = &remote_context,
                                            .statistics = &stats,
                                            .progress = &progress};
        Executor runner{&exec_context};

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

        auto api = std::make_shared<TestApi>(config, hash_function.GetType());
        Statistics stats{};
        Progress progress{};
        auto const apis = CreateTestApiBundle(api);
        ExecutionContext const exec_context{.repo_config = &repo_config,
                                            .apis = &apis,
                                            .remote_context = &remote_context,
                                            .statistics = &stats,
                                            .progress = &progress};
        Executor runner{&exec_context};

        CHECK(runner.Process(g.ArtifactNodeWithId(local_cpp_id)));
        CHECK(runner.Process(g.ArtifactNodeWithId(known_cpp_id)));
        CHECK(not runner.Process(g.ActionNodeWithId(action_id)));
        CHECK(not runner.Process(g.ArtifactNodeWithId(output1_id)));
        CHECK(not runner.Process(g.ArtifactNodeWithId(output2_id)));
    }

    SECTION("Processing fails if exit code is non-zero") {
        config.response.exit_code = 1;

        auto api = std::make_shared<TestApi>(config, hash_function.GetType());
        Statistics stats{};
        Progress progress{};
        auto const apis = CreateTestApiBundle(api);
        ExecutionContext const exec_context{.repo_config = &repo_config,
                                            .apis = &apis,
                                            .remote_context = &remote_context,
                                            .statistics = &stats,
                                            .progress = &progress};
        Executor runner{&exec_context};

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

        auto api = std::make_shared<TestApi>(config, hash_function.GetType());
        Statistics stats{};
        Progress progress{};
        auto const apis = CreateTestApiBundle(api);
        ExecutionContext const exec_context{.repo_config = &repo_config,
                                            .apis = &apis,
                                            .remote_context = &remote_context,
                                            .statistics = &stats,
                                            .progress = &progress};
        Executor runner{&exec_context};

        CHECK(runner.Process(g.ArtifactNodeWithId(local_cpp_id)));
        CHECK(runner.Process(g.ArtifactNodeWithId(known_cpp_id)));
        CHECK(not runner.Process(g.ActionNodeWithId(action_id)));

        // Note: Both output digests should be missing as SaveDigests() for
        // both is only called if processing action succeeds.
        CHECK(not runner.Process(g.ArtifactNodeWithId(output1_id)));
        CHECK(not runner.Process(g.ArtifactNodeWithId(output2_id)));
    }
}
