#include "catch2/catch.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_api.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/execution_engine/executor/executor.hpp"
#include "test/buildtool/execution_engine/executor/executor_api.test.hpp"

TEST_CASE("Executor<BazelApi>: Upload blob", "[executor]") {
    ExecutionConfiguration config;
    auto const& info = RemoteExecutionConfig::Instance();

    TestBlobUpload([&] {
        return BazelApi::Ptr{
            new BazelApi{"remote-execution", info.Host(), info.Port(), config}};
    });
}

TEST_CASE("Executor<BazelApi>: Compile hello world", "[executor]") {
    ExecutionConfiguration config;
    config.skip_cache_lookup = false;

    auto const& info = RemoteExecutionConfig::Instance();

    TestHelloWorldCompilation(
        [&] {
            return BazelApi::Ptr{new BazelApi{
                "remote-execution", info.Host(), info.Port(), config}};
        },
        false /* not hermetic */);
}

TEST_CASE("Executor<BazelApi>: Compile greeter", "[executor]") {
    ExecutionConfiguration config;
    config.skip_cache_lookup = false;

    auto const& info = RemoteExecutionConfig::Instance();

    TestGreeterCompilation(
        [&] {
            return BazelApi::Ptr{new BazelApi{
                "remote-execution", info.Host(), info.Port(), config}};
        },
        false /* not hermetic */);
}

TEST_CASE("Executor<BazelApi>: Upload and download trees", "[executor]") {
    ExecutionConfiguration config;
    config.skip_cache_lookup = false;

    auto const& info = RemoteExecutionConfig::Instance();

    TestUploadAndDownloadTrees(
        [&] {
            return BazelApi::Ptr{new BazelApi{
                "remote-execution", info.Host(), info.Port(), config}};
        },
        false /* not hermetic */);
}

TEST_CASE("Executor<BazelApi>: Retrieve output directories", "[executor]") {
    ExecutionConfiguration config;
    config.skip_cache_lookup = false;

    auto const& info = RemoteExecutionConfig::Instance();

    TestRetrieveOutputDirectories(
        [&] {
            return BazelApi::Ptr{new BazelApi{
                "remote-execution", info.Host(), info.Port(), config}};
        },
        false /* not hermetic */);
}
