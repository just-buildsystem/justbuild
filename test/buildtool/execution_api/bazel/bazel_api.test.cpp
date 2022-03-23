#include <cstdlib>
#include <string>

#include "catch2/catch.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_api.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "test/buildtool/execution_api/common/api_test.hpp"
#include "test/utils/test_env.hpp"

namespace {

auto const kApiFactory = []() {
    static auto const& server = RemoteExecutionConfig::Instance();
    return IExecutionApi::Ptr{
        new BazelApi{"remote-execution", server.Host(), server.Port(), {}}};
};

}  // namespace

TEST_CASE("BazelAPI: No input, no output", "[execution_api]") {
    TestNoInputNoOutput(kApiFactory, ReadPlatformPropertiesFromEnv());
}

TEST_CASE("BazelAPI: No input, create output", "[execution_api]") {
    TestNoInputCreateOutput(kApiFactory, ReadPlatformPropertiesFromEnv());
}

TEST_CASE("BazelAPI: One input copied to output", "[execution_api]") {
    TestOneInputCopiedToOutput(kApiFactory, ReadPlatformPropertiesFromEnv());
}

TEST_CASE("BazelAPI: Non-zero exit code, create output", "[execution_api]") {
    TestNonZeroExitCodeCreateOutput(kApiFactory,
                                    ReadPlatformPropertiesFromEnv());
}

TEST_CASE("BazelAPI: Retrieve two identical trees to path", "[execution_api]") {
    TestRetrieveTwoIdenticalTreesToPath(
        kApiFactory, ReadPlatformPropertiesFromEnv(), "remote");
}

TEST_CASE("BazelAPI: Create directory prior to execution", "[execution_api]") {
    TestCreateDirPriorToExecution(kApiFactory, ReadPlatformPropertiesFromEnv());
}
