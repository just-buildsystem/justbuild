#include <cstdlib>
#include <string>

#include "catch2/catch.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_api.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "test/buildtool/execution_api/common/api_test.hpp"
#include "test/utils/test_env.hpp"

namespace {

auto const kApiFactory = []() {
    static auto const& server = RemoteExecutionConfig::RemoteAddress();
    return IExecutionApi::Ptr{
        new BazelApi{"remote-execution", server->host, server->port, {}}};
};

}  // namespace

TEST_CASE("BazelAPI: No input, no output", "[execution_api]") {
    TestNoInputNoOutput(kApiFactory,
                        RemoteExecutionConfig::PlatformProperties());
}

TEST_CASE("BazelAPI: No input, create output", "[execution_api]") {
    TestNoInputCreateOutput(kApiFactory,
                            RemoteExecutionConfig::PlatformProperties());
}

TEST_CASE("BazelAPI: One input copied to output", "[execution_api]") {
    TestOneInputCopiedToOutput(kApiFactory,
                               RemoteExecutionConfig::PlatformProperties());
}

TEST_CASE("BazelAPI: Non-zero exit code, create output", "[execution_api]") {
    TestNonZeroExitCodeCreateOutput(
        kApiFactory, RemoteExecutionConfig::PlatformProperties());
}

TEST_CASE("BazelAPI: Retrieve two identical trees to path", "[execution_api]") {
    TestRetrieveTwoIdenticalTreesToPath(
        kApiFactory, RemoteExecutionConfig::PlatformProperties(), "two_trees");
}

TEST_CASE("BazelAPI: Retrieve mixed blobs and trees", "[execution_api]") {
    TestRetrieveMixedBlobsAndTrees(kApiFactory,
                                   RemoteExecutionConfig::PlatformProperties(),
                                   "blobs_and_trees");
}

TEST_CASE("BazelAPI: Create directory prior to execution", "[execution_api]") {
    TestCreateDirPriorToExecution(kApiFactory,
                                  RemoteExecutionConfig::PlatformProperties());
}
