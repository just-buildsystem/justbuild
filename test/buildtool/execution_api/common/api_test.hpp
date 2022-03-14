#include <cstdlib>
#include <string>

#include "catch2/catch.hpp"
#include "src/buildtool/common/artifact_factory.hpp"
#include "src/buildtool/execution_api/common/execution_action.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/execution_api/common/execution_response.hpp"

using ApiFactory = std::function<IExecutionApi::Ptr()>;
using ExecProps = std::map<std::string, std::string>;

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
