#include <cstdlib>
#include <string>

#include "catch2/catch.hpp"
#include "src/buildtool/common/artifact_factory.hpp"
#include "src/buildtool/execution_api/common/execution_action.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/execution_api/common/execution_response.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"

using ApiFactory = std::function<IExecutionApi::Ptr()>;
using ExecProps = std::map<std::string, std::string>;

[[nodiscard]] static inline auto GetTestDir(std::string const& test_name)
    -> std::filesystem::path {
    auto* tmp_dir = std::getenv("TEST_TMPDIR");
    if (tmp_dir != nullptr) {
        return std::filesystem::path{tmp_dir} / test_name;
    }
    return FileSystemManager::GetCurrentDirectory() /
           "test/buildtool/execution_api" / test_name;
}

[[nodiscard]] static inline auto TestNoInputNoOutput(
    ApiFactory const& api_factory,
    ExecProps const& props,
    bool is_hermetic = false) {
    std::string test_content("test");

    auto api = api_factory();

    auto action = api->CreateAction(
        *api->UploadTree({}), {"echo", "-n", test_content}, {}, {}, {}, props);

    SECTION("Cache execution result in action cache") {
        action->SetCacheFlag(IExecutionAction::CacheFlag::CacheOutput);

        // run execution
        auto response = action->Execute();
        REQUIRE(response);

        // verify result
        CHECK(response->HasStdOut());
        CHECK(response->StdOut() == test_content);

        if (is_hermetic) {
            CHECK(not response->IsCached());

            SECTION("Rerun execution to verify caching") {
                // run execution
                auto response = action->Execute();
                REQUIRE(response);

                // verify result
                CHECK(response->HasStdOut());
                CHECK(response->StdOut() == test_content);
                CHECK(response->IsCached());
            }
        }
    }

    SECTION("Do not cache execution result in action cache") {
        action->SetCacheFlag(IExecutionAction::CacheFlag::DoNotCacheOutput);

        // run execution
        auto response = action->Execute();
        REQUIRE(response);

        // verify result
        CHECK(response->HasStdOut());
        CHECK(response->StdOut() == test_content);
        CHECK(not response->IsCached());

        SECTION("Rerun execution to verify caching") {
            // run execution
            auto response = action->Execute();
            REQUIRE(response);

            // verify result
            CHECK(response->HasStdOut());
            CHECK(response->StdOut() == test_content);
            CHECK(not response->IsCached());
        }
    }
}

[[nodiscard]] static inline auto TestNoInputCreateOutput(
    ApiFactory const& api_factory,
    ExecProps const& props,
    bool is_hermetic = false) {
    std::string test_content("test");
    auto test_digest = ArtifactDigest::Create(test_content);

    std::string output_path{"output_file"};

    auto api = api_factory();

    auto action = api->CreateAction(
        *api->UploadTree({}),
        {"/bin/sh",
         "-c",
         "set -e\necho -n " + test_content + " > " + output_path},
        {output_path},
        {},
        {},
        props);

    SECTION("Cache execution result in action cache") {
        action->SetCacheFlag(IExecutionAction::CacheFlag::CacheOutput);

        // run execution
        auto response = action->Execute();
        REQUIRE(response);

        // verify result
        auto artifacts = response->Artifacts();
        REQUIRE(artifacts.contains(output_path));
        CHECK(artifacts.at(output_path).digest == test_digest);

        if (is_hermetic) {
            CHECK(not response->IsCached());

            SECTION("Rerun execution to verify caching") {
                // run execution
                auto response = action->Execute();
                REQUIRE(response);

                // verify result
                auto artifacts = response->Artifacts();
                REQUIRE(artifacts.contains(output_path));
                CHECK(artifacts.at(output_path).digest == test_digest);
                CHECK(response->IsCached());
            }
        }
    }

    SECTION("Do not cache execution result in action cache") {
        action->SetCacheFlag(IExecutionAction::CacheFlag::DoNotCacheOutput);

        // run execution
        auto response = action->Execute();
        REQUIRE(response);

        // verify result
        auto artifacts = response->Artifacts();
        REQUIRE(artifacts.contains(output_path));
        CHECK(artifacts.at(output_path).digest == test_digest);
        CHECK(not response->IsCached());

        SECTION("Rerun execution to verify caching") {
            // run execution
            auto response = action->Execute();
            REQUIRE(response);

            // verify result
            auto artifacts = response->Artifacts();
            REQUIRE(artifacts.contains(output_path));
            CHECK(artifacts.at(output_path).digest == test_digest);
            CHECK(not response->IsCached());
        }
    }
}

[[nodiscard]] static inline auto TestOneInputCopiedToOutput(
    ApiFactory const& api_factory,
    ExecProps const& props,
    bool is_hermetic = false) {
    std::string test_content("test");
    auto test_digest = ArtifactDigest::Create(test_content);

    auto input_artifact_opt =
        ArtifactFactory::FromDescription(ArtifactFactory::DescribeKnownArtifact(
            test_digest.hash(), test_digest.size(), ObjectType::File));
    CHECK(input_artifact_opt.has_value());
    auto input_artifact =
        DependencyGraph::ArtifactNode{std::move(*input_artifact_opt)};

    std::string input_path{"dir/subdir/input"};
    std::string output_path{"output_file"};

    auto api = api_factory();
    CHECK(api->Upload(BlobContainer{{BazelBlob{test_digest, test_content}}},
                      false));

    auto action =
        api->CreateAction(*api->UploadTree({{input_path, &input_artifact}}),
                          {"cp", input_path, output_path},
                          {output_path},
                          {},
                          {},
                          props);

    SECTION("Cache execution result in action cache") {
        action->SetCacheFlag(IExecutionAction::CacheFlag::CacheOutput);

        // run execution
        auto response = action->Execute();
        REQUIRE(response);

        // verify result
        auto artifacts = response->Artifacts();
        REQUIRE(artifacts.contains(output_path));
        CHECK(artifacts.at(output_path).digest == test_digest);

        if (is_hermetic) {
            CHECK(not response->IsCached());

            SECTION("Rerun execution to verify caching") {
                // run execution
                auto response = action->Execute();
                REQUIRE(response);

                // verify result
                auto artifacts = response->Artifacts();
                REQUIRE(artifacts.contains(output_path));
                CHECK(artifacts.at(output_path).digest == test_digest);
                CHECK(response->IsCached());
            }
        }
    }

    SECTION("Do not cache execution result in action cache") {
        action->SetCacheFlag(IExecutionAction::CacheFlag::DoNotCacheOutput);

        // run execution
        auto response = action->Execute();
        REQUIRE(response);

        // verify result
        auto artifacts = response->Artifacts();
        REQUIRE(artifacts.contains(output_path));
        CHECK(artifacts.at(output_path).digest == test_digest);
        CHECK(not response->IsCached());

        SECTION("Rerun execution to verify caching") {
            // run execution
            auto response = action->Execute();
            REQUIRE(response);

            // verify result
            auto artifacts = response->Artifacts();
            REQUIRE(artifacts.contains(output_path));
            CHECK(artifacts.at(output_path).digest == test_digest);
            CHECK(not response->IsCached());
        }
    }
}

[[nodiscard]] static inline auto TestNonZeroExitCodeCreateOutput(
    ApiFactory const& api_factory,
    ExecProps const& props) {
    std::string test_content("test");
    auto test_digest = ArtifactDigest::Create(test_content);

    std::string output_path{"output_file"};

    auto api = api_factory();

    auto action = api->CreateAction(*api->UploadTree({}),
                                    {"/bin/sh",
                                     "-c",
                                     "set -e\necho -n " + test_content + " > " +
                                         output_path + "\nexit 1\n"},
                                    {output_path},
                                    {},
                                    {},
                                    props);

    SECTION("Cache execution result in action cache") {
        action->SetCacheFlag(IExecutionAction::CacheFlag::CacheOutput);

        // run execution
        auto response = action->Execute();
        REQUIRE(response);

        // verify result
        CHECK(response->ExitCode() == 1);
        auto artifacts = response->Artifacts();
        REQUIRE(artifacts.contains(output_path));
        CHECK(artifacts.at(output_path).digest == test_digest);

        CHECK(not response->IsCached());

        SECTION("Rerun execution to verify that non-zero actions are rerun") {
            // run execution
            auto response = action->Execute();
            REQUIRE(response);

            // verify result
            CHECK(response->ExitCode() == 1);
            auto artifacts = response->Artifacts();
            REQUIRE(artifacts.contains(output_path));
            CHECK(artifacts.at(output_path).digest == test_digest);
            CHECK(not response->IsCached());
        }
    }

    SECTION("Do not cache execution result in action cache") {
        action->SetCacheFlag(IExecutionAction::CacheFlag::DoNotCacheOutput);

        // run execution
        auto response = action->Execute();
        REQUIRE(response);

        // verify result
        CHECK(response->ExitCode() == 1);
        auto artifacts = response->Artifacts();
        REQUIRE(artifacts.contains(output_path));
        CHECK(artifacts.at(output_path).digest == test_digest);
        CHECK(not response->IsCached());

        SECTION("Rerun execution to verify non-zero actions are not cached") {
            // run execution
            auto response = action->Execute();
            REQUIRE(response);

            // verify result
            CHECK(response->ExitCode() == 1);
            auto artifacts = response->Artifacts();
            REQUIRE(artifacts.contains(output_path));
            CHECK(artifacts.at(output_path).digest == test_digest);
            CHECK(not response->IsCached());
        }
    }
}

[[nodiscard]] static inline auto TestRetrieveTwoIdenticalTreesToPath(
    ApiFactory const& api_factory,
    ExecProps const& props,
    std::string const& test_name,
    bool is_hermetic = false) {
    auto api = api_factory();

    auto foo_path = std::filesystem::path{"foo"} / "baz";
    auto bar_path = std::filesystem::path{"bar"} / "baz";

    auto make_cmd = [&](std::string const& out_dir) {
        return fmt::format(
            "set -e\nmkdir -p {0}/{1} {0}/{2}\n"
            "echo -n baz > {0}/{3}\necho -n baz > {0}/{4}",
            out_dir,
            foo_path.parent_path().string(),
            bar_path.parent_path().string(),
            foo_path.string(),
            bar_path.string());
    };

    auto action = api->CreateAction(*api->UploadTree({}),
                                    {"/bin/sh", "-c", make_cmd("root")},
                                    {},
                                    {"root"},
                                    {},
                                    props);

    action->SetCacheFlag(IExecutionAction::CacheFlag::CacheOutput);

    // run execution
    auto response = action->Execute();
    REQUIRE(response);

    // verify result
    CHECK(response->ExitCode() == 0);

    if (is_hermetic) {
        CHECK_FALSE(response->IsCached());
    }

    auto artifacts = response->Artifacts();
    REQUIRE_FALSE(artifacts.empty());

    auto info = artifacts.begin()->second;

    SECTION("retrieve via same API object") {
        auto out_path = GetTestDir(test_name) / "out1";
        CHECK(api->RetrieveToPaths({info}, {out_path}));
        CHECK(FileSystemManager::IsFile(out_path / foo_path));
        CHECK(FileSystemManager::IsFile(out_path / bar_path));
        CHECK(FileSystemManager::ReadFile(out_path / foo_path) ==
              FileSystemManager::ReadFile(out_path / bar_path));
    }

    SECTION("retrive from new API object but same endpoint") {
        auto second_api = api_factory();
        auto out_path = GetTestDir(test_name) / "out2";
        CHECK(second_api->RetrieveToPaths({info}, {out_path}));
        CHECK(FileSystemManager::IsFile(out_path / foo_path));
        CHECK(FileSystemManager::IsFile(out_path / bar_path));
        CHECK(FileSystemManager::ReadFile(out_path / foo_path) ==
              FileSystemManager::ReadFile(out_path / bar_path));
    }
}

[[nodiscard]] static inline auto TestRetrieveMixedBlobsAndTrees(
    ApiFactory const& api_factory,
    ExecProps const& props,
    std::string const& test_name,
    bool is_hermetic = false) {
    auto api = api_factory();

    auto foo_path = std::filesystem::path{"foo"};
    auto bar_path = std::filesystem::path{"subdir"} / "bar";

    auto cmd = fmt::format("set -e\nmkdir -p {}\ntouch {} {}",
                           bar_path.parent_path().string(),
                           bar_path.string(),
                           foo_path.string());

    auto action = api->CreateAction(*api->UploadTree({}),
                                    {"/bin/sh", "-c", cmd},
                                    {foo_path.string()},
                                    {bar_path.parent_path().string()},
                                    {},
                                    props);

    action->SetCacheFlag(IExecutionAction::CacheFlag::CacheOutput);

    // run execution
    auto response = action->Execute();
    REQUIRE(response);

    // verify result
    CHECK(response->ExitCode() == 0);

    if (is_hermetic) {
        CHECK_FALSE(response->IsCached());
    }

    auto artifacts = response->Artifacts();
    REQUIRE_FALSE(artifacts.empty());

    std::vector<std::filesystem::path> paths{};
    std::vector<Artifact::ObjectInfo> infos{};

    SECTION("retrieve via same API object") {
        auto out_path = GetTestDir(test_name) / "out1";
        std::for_each(artifacts.begin(),
                      artifacts.end(),
                      [&out_path, &paths, &infos](auto const& entry) {
                          paths.emplace_back(out_path / entry.first);
                          infos.emplace_back(entry.second);
                      });
        CHECK(api->RetrieveToPaths(infos, paths));
        CHECK(FileSystemManager::IsFile(out_path / foo_path));
        CHECK(FileSystemManager::IsFile(out_path / bar_path));
    }

    SECTION("retrieve from new API object but same endpoint") {
        auto second_api = api_factory();
        auto out_path = GetTestDir(test_name) / "out2";
        std::for_each(artifacts.begin(),
                      artifacts.end(),
                      [&out_path, &paths, &infos](auto const& entry) {
                          paths.emplace_back(out_path / entry.first);
                          infos.emplace_back(entry.second);
                      });
        CHECK(second_api->RetrieveToPaths(infos, paths));
        CHECK(FileSystemManager::IsFile(out_path / foo_path));
        CHECK(FileSystemManager::IsFile(out_path / bar_path));
    }
}

[[nodiscard]] static inline auto TestCreateDirPriorToExecution(
    ApiFactory const& api_factory,
    ExecProps const& props,
    bool is_hermetic = false) {
    auto api = api_factory();

    auto output_path = std::filesystem::path{"foo/bar/baz"};

    auto action = api->CreateAction(
        *api->UploadTree({}),
        {"/bin/sh",
         "-c",
         fmt::format("set -e\n [ -d {} ]", output_path.string())},
        {},
        {output_path},
        {},
        props);

    SECTION("Cache execution result in action cache") {
        action->SetCacheFlag(IExecutionAction::CacheFlag::CacheOutput);

        // run execution
        auto response = action->Execute();
        REQUIRE(response);

        // verify result
        auto artifacts = response->Artifacts();
        REQUIRE(artifacts.contains(output_path));
        CHECK(IsTreeObject(artifacts.at(output_path).type));

        if (is_hermetic) {
            CHECK(not response->IsCached());

            SECTION("Rerun execution to verify caching") {
                // run execution
                auto response = action->Execute();
                REQUIRE(response);

                // verify result
                auto artifacts = response->Artifacts();
                REQUIRE(artifacts.contains(output_path));
                CHECK(IsTreeObject(artifacts.at(output_path).type));
                CHECK(response->IsCached());
            }
        }
    }

    SECTION("Do not cache execution result in action cache") {
        action->SetCacheFlag(IExecutionAction::CacheFlag::DoNotCacheOutput);

        // run execution
        auto response = action->Execute();
        REQUIRE(response);

        // verify result
        auto artifacts = response->Artifacts();
        REQUIRE(artifacts.contains(output_path));
        CHECK(IsTreeObject(artifacts.at(output_path).type));
        CHECK(not response->IsCached());

        SECTION("Rerun execution to verify caching") {
            // run execution
            auto response = action->Execute();
            REQUIRE(response);

            // verify result
            auto artifacts = response->Artifacts();
            REQUIRE(artifacts.contains(output_path));
            CHECK(IsTreeObject(artifacts.at(output_path).type));
            CHECK(not response->IsCached());
        }
    }
}
